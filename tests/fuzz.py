#!/usr/bin/env python3
"""Runs random programs with two builds of cuffc and reports every program on which they disagree.

    python3 tests/fuzz.py ./cuffc ./cuffc-dyn                 2000 programs, seeds 1 to 2000
    python3 tests/fuzz.py ./cuffc ./cuffc-asan --count 500 --seed 9000 --jobs 4

The engine reaches the same result by more than one route: a name is used through its register where the compiler can
prove what it stands for and looked up at run time where it cannot, the machine jumps through a table of labels or
through a switch, and a release build and a sanitizer build run the same code with different layouts. Two builds that
differ only in the route (see "The machine's other routes" in docs/contributing/testing.md) must print the same
output, fail with the same report and exit with the same status on every program. The examples and the test cases
are written by people, who write sensible programs; these are not. They declare a name in one branch and read it
after the other, declare it twice with two types in two blocks, assign through `with`, bridge globals, import a
module into a block, raise and catch at random, and call with the wrong number and types of arguments.

A program that differs is written to the --keep directory (default: fuzz-failures/) as seed_<n>.cuff, with the two
modules it may load beside it in lib/. The seed alone reproduces it: --seed <n> --count 1.

Only the Python standard library is used.
"""
import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import tempfile

VARS = ['a', 'b', 'c', 'd', 'g', 'h', 'k', 'm']
CONSTS = ['KA', 'KB', 'KC']
TYPES = ['number', 'number', 'number', 'str', 'boolean', 'list', 'map', 'any', 'any', 'Box']


class Fn:
    def __init__(self, kind, cls=None):
        self.kind = kind          # top, func, method
        self.cls = cls
        self.declared = []        # what the parser holds as declared: block structured
        self.seen = {}            # every name declared so far in this code -> type
        self.loops = 0
        self.withs = 0
        self.pure = False
        self.is_async = False
        self.bridged = []


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.out = []
        self.ind = 0
        self.n = 0
        self.globals = {}
        self.funcs = []           # (name, [(type, name, default?)], pure, async)
        self.budget = 0

    # ---- helpers
    def line(self, s):
        self.out.append('    ' * self.ind + s)

    def uid(self, p):
        self.n += 1
        return f'{p}{self.n}'

    def chance(self, p):
        return self.r.random() < p

    def pick(self, xs):
        return self.r.choice(xs)

    def names_of(self, fn, t=None, loose=True):
        def ok(ty):
            return t is None or ty == t or (loose and ty == 'any')
        sure = [n for n in fn.declared if ok(fn.seen.get(n, 'any'))]
        if fn.kind != 'top':
            sure += [n for n in fn.bridged if ok(self.globals.get(n, 'any'))]
        if sure and self.chance(0.88):
            return sure
        out = [n for n, ty in fn.seen.items() if ok(ty)]
        if fn.kind != 'top':
            out += [n for n, ty in self.globals.items() if ok(ty)]
        return out or sure

    # ---- expressions
    def num(self, fn, d=0):
        r = self.r
        c = r.random()
        if d > 2 or c < 0.25:
            return str(r.choice([0, 1, 1, 2, 2, 3, 4, 5, 7, 10, 2.5]))
        if c < 0.45:
            ns = self.names_of(fn, 'number')
            if ns:
                return self.pick(ns)
            return '3'
        if c < 0.65:
            op = r.choice(['+', '+', '-', '*', '*', '/', '%', '**'])
            a, b = self.num(fn, d + 1), self.num(fn, d + 1)
            if op == '**':
                b = str(r.choice([0, 1, 2, 3]))
            return f'({a} {op} {b})' if self.chance(0.7) else f'{a} {op} {b}'
        if c < 0.69:
            return f'-{self.num(fn, d + 1)}'
        if c < 0.75:
            ls = self.names_of(fn, 'list', False)
            if ls:
                return f'{self.pick(ls)}[{self.num(fn, d + 2)}]'
            return '4'
        if c < 0.79:
            ms = self.names_of(fn, 'map', False)
            if ms:
                return f'{self.pick(ms)}["{r.choice(["x", "y", "z"])}"]'
            return '6'
        if c < 0.86:
            return self.field(fn, d, r.choice(['n', 'n', 'CAP', 'extra', 'hidden']))
        if c < 0.94:
            return self.call(fn, d)
        if c < 0.97:
            return f'to_number({self.any(fn, d + 1)})'
        return f'length({self.pick([self.lst, self.txt, self.mp])(fn, d + 1)})'

    def field(self, fn, d, name):
        c = self.r.random()
        if fn.kind == 'method' and c < 0.5:
            return f'self.{name}'
        if fn.withs and c < 0.75:
            return name if self.chance(0.6) else f'.{name}'
        return f'{self.box(fn, d + 1)}.{name}'

    def place_field(self, fn, name):
        c = self.r.random()
        if fn.kind == 'method' and c < 0.5:
            return f'self.{name}'
        if fn.withs and c < 0.75:
            return name if self.chance(0.6) else f'.{name}'
        ns = self.names_of(fn, 'Box', False)
        return f'{self.pick(ns)}.{name}' if ns else None

    def plain(self, fn, d):
        """An expression that may stand inside an f-string: no quotes and no braces."""
        for _ in range(4):
            e = self.any(fn, d)
            if '"' not in e and '{' not in e:
                return e
        return self.pick(list(fn.seen) or ['1'])

    def args(self, fn, params, d):
        out = []
        for (t, _, dflt) in params:
            if dflt and self.chance(0.4):
                break
            out.append(self.of(fn, t if self.chance(0.975) else self.pick(TYPES), d + 1))
        if self.chance(0.03):
            out.append('1')
        return ', '.join(out)

    def call(self, fn, d):
        c = self.r.random()
        if c < 0.55 and self.funcs:
            name, params, pure, is_async = self.pick(self.funcs)
            pre = 'await ' if is_async and self.chance(0.85) else ''
            return f'{pre}{name}({self.args(fn, params, d)})'
        if c < 0.62:
            return f'rec({self.r.choice([0, 1, 3, 5])})'
        m = self.r.choice([('bump', [('number', 'by', True)]), ('peek', []), ('total', [('number', 'x', False)]),
                           ('inner', []), ('more', [])])
        if fn.kind == 'method' and self.chance(0.4):
            recv = 'super' if fn.cls == 'Kid' and self.chance(0.25) else 'self'
            return f'{recv}.{m[0]}({self.args(fn, m[1], d)})'
        if fn.withs and self.chance(0.5):
            return f'{m[0]}({self.args(fn, m[1], d)})'
        return f'{self.box(fn, d + 1)}.{m[0]}({self.args(fn, m[1], d)})'

    def txt(self, fn, d=0):
        r = self.r
        c = r.random()
        if d > 2 or c < 0.3:
            return '"' + r.choice(['s', 'ab', '', 'x y', 'héllo', 'k']) + '"'
        if c < 0.45:
            ns = self.names_of(fn, 'str')
            return self.pick(ns) if ns else '"t"'
        if c < 0.6:
            tail = self.plain(fn, d + 2)
            return 'f"' + r.choice(['v=', '', '<']) + '{' + self.plain(fn, d + 1) + '}' + r.choice(['', '>', ' {' + tail + '}']) + '"'
        if c < 0.72:
            return f'({self.txt(fn, d + 1)} + {self.txt(fn, d + 1)})'
        if c < 0.8:
            return f'to_str({self.any(fn, d + 1)})'
        if c < 0.86:
            return f'type_of({self.any(fn, d + 1)})'
        if c < 0.92:
            return self.field(fn, d, 's')
        if c < 0.96:
            return f'{self.txt(fn, d + 1)}[{self.num(fn, d + 2)}]'
        return f'{self.txt(fn, d + 1)}[1 ~ {self.num(fn, d + 2)}]'

    def boo(self, fn, d=0):
        r = self.r
        c = r.random()
        if d > 2 or c < 0.15:
            return r.choice(['true', 'false'])
        if c < 0.25:
            ns = self.names_of(fn, 'boolean')
            return self.pick(ns) if ns else 'true'
        if c < 0.55:
            return f'{self.num(fn, d + 1)} {r.choice(["<", "<=", ">", ">=", "is", "is not"])} {self.num(fn, d + 1)}'
        if c < 0.65:
            return f'{self.any(fn, d + 1)} {r.choice(["is", "is not"])} {self.any(fn, d + 1)}'
        if c < 0.72:
            return f'!{self.boo(fn, d + 1)}'
        if c < 0.86:
            op = r.choice(['and', 'or'])
            a, b = self.boo(fn, d + 1), self.boo(fn, d + 1)
            return f'({a} {op} {b})' if self.chance(0.5) else f'{a} {op} {b}'
        if c < 0.92:
            return f'{self.any(fn, d + 1)} isa {r.choice(["Box", "Kid", "Error", "Oops"])}'
        if c < 0.96:
            return f'{self.any(fn, d + 1)} is empty'
        return f'{self.txt(fn, d + 1)} < {self.txt(fn, d + 1)}'

    def lst(self, fn, d=0):
        r = self.r
        c = r.random()
        if d > 2 or c < 0.4:
            return '[' + ', '.join(self.any(fn, d + 2) if self.chance(0.3) else self.num(fn, d + 2) for _ in range(r.randint(0, 4))) + ']'
        if c < 0.65:
            ns = self.names_of(fn, 'list')
            return self.pick(ns) if ns else '[1, 2]'
        if c < 0.75:
            return f'({self.lst(fn, d + 1)} + {self.lst(fn, d + 1)})'
        if c < 0.82:
            return f'{self.lst(fn, d + 1)}[1 ~ {self.num(fn, d + 2)}]'
        if c < 0.9:
            return f'copy({self.lst(fn, d + 1)})'
        return self.field(fn, d, 'items')

    def mp(self, fn, d=0):
        r = self.r
        c = r.random()
        if d > 2 or c < 0.5:
            return '{' + ', '.join(f'"{k}": {self.any(fn, d + 2) if self.chance(0.3) else self.num(fn, d + 2)}'
                                   for k in r.sample(['x', 'y', 'z'], r.randint(0, 3))) + '}'
        if c < 0.85:
            ns = self.names_of(fn, 'map')
            return self.pick(ns) if ns else '{"x": 1}'
        return f'copy({self.mp(fn, d + 1)})'

    def box(self, fn, d=0):
        r = self.r
        c = r.random()
        ns = self.names_of(fn, 'Box', False)
        if ns and c < 0.6:
            return self.pick(ns)
        if fn.kind == 'method' and c < 0.75:
            return 'self'
        if d > 2 or c < 0.9:
            return r.choice(['Box()', 'Box(2)', 'Kid()', 'Kid(4)']) if d > 1 else f'{r.choice(["Box", "Kid"])}({self.args(fn, [("number", "n", True)], d)})'
        return self.field(fn, d, 'link')

    def any(self, fn, d=0):
        c = self.r.random()
        if c < 0.08:
            return 'empty'
        if c < 0.2:
            ns = self.names_of(fn) if self.chance(0.97) else VARS
            if ns:
                return self.pick(ns)
        return self.pick([self.num, self.num, self.num, self.txt, self.boo, self.lst, self.mp, self.box])(fn, d)

    def of(self, fn, t, d=0):
        return {'number': self.num, 'str': self.txt, 'boolean': self.boo, 'list': self.lst, 'map': self.mp, 'any': self.any,
                'Box': self.box, 'Kid': self.box}.get(t, self.any)(fn, d)

    # ---- statements
    def prelude(self, fn):
        for t in self.r.sample(['number', 'list', 'map', 'Box', 'str', 'any'], self.r.randint(2, 5)):
            name = self.fresh(fn)
            if not name:
                break
            self.line(f'{t} {name} = {self.of(fn, t, 2)}')
            fn.declared.append(name)
            fn.seen[name] = t
            if fn.kind == 'top':
                self.globals[name] = t

    def body(self, fn, depth, lo=1, hi=3, first=None, prelude=False):
        mark = len(fn.declared)
        self.ind += 1
        if first:
            self.line(first)
        if prelude:
            self.prelude(fn)
        for _ in range(self.r.randint(lo, hi)):
            self.stmt(fn, depth + 1)
        self.ind -= 1
        del fn.declared[mark:]

    def handler(self, fn, depth, opener_on_last_line):
        mark = len(fn.declared)
        head = 'or_else'
        if self.chance(0.6):
            name = self.uid('e') if self.chance(0.8) else self.fresh(fn)
            if name:
                ty = self.r.choice(['Error', 'Error', 'Error', 'any', 'object', 'Oops'])
                head += f' as {ty} {name}'
                fn.declared.append(name)
                fn.seen[name] = 'any'
        self.out[-1] += f' {head} do:'
        self.ind += 1
        if ' as ' in head and self.chance(0.7):
            nm = head.split()[-1]
            self.line(f'print("caught", {nm}.code, {nm}.message, {nm}.line)')
        else:
            self.line('print("caught")')
        for _ in range(self.r.randint(0, 2)):
            self.stmt(fn, depth + 1)
        self.ind -= 1
        self.line('end')
        del fn.declared[mark:]

    def fresh(self, fn, consts=False):
        pool = [v for v in (CONSTS if consts else VARS) if v not in fn.declared]
        return self.pick(pool) if pool else None

    def target(self, fn):
        """A place and the type it should take."""
        r = self.r
        c = r.random()
        if c < 0.55:
            ns = [(n, fn.seen.get(n, self.globals.get(n, 'any'))) for n in self.names_of(fn)]
            if ns:
                n, t = self.pick(ns)
                if t == 'list' and self.chance(0.4):
                    return f'{n}[{self.num(fn, 2)}]', 'any'
                if t == 'map' and self.chance(0.5):
                    return f'{n}["{r.choice(["x", "y", "w"])}"]', 'any'
                if t == 'Box' and self.chance(0.6):
                    f, ft = r.choice([('n', 'number'), ('s', 'str'), ('items', 'list'), ('link', 'any'), ('CAP', 'number'), ('hidden', 'number')])
                    if ft == 'list' and self.chance(0.5):
                        return f'{n}.{f}[{self.num(fn, 2)}]', 'any'
                    return f'{n}.{f}', ft
                return n, t
        f, ft = r.choice([('n', 'number'), ('n', 'number'), ('s', 'str'), ('items', 'list'), ('link', 'any'), ('CAP', 'number'), ('extra', 'number')])
        if fn.kind == 'method' and c < 0.8:
            return f'self.{f}', ft
        if fn.withs and c < 0.9:
            return (f if self.chance(0.6) else f'.{f}'), ft
        if c < 0.97:
            pf = self.place_field(fn, f)
            if pf:
                return pf, ft
            ns = self.names_of(fn)
            if ns:
                n = self.pick(ns)
                return n, fn.seen.get(n, self.globals.get(n, 'any'))
        n = self.pick(VARS + CONSTS)
        return n, fn.seen.get(n, 'number')

    def stmt(self, fn, depth):
        self.budget -= 1
        r = self.r
        kinds = [('decl', 20), ('assign', 14), ('print', 12), ('if', 8), ('repeat', 5), ('while', 3), ('each', 4),
                 ('with', 4), ('call', 5), ('coll', 5), ('ret', 2 if fn.kind != 'top' else 0.2), ('stopskip', 3 if fn.loops else 0.15),
                 ('global', 4 if fn.kind != 'top' else 0.1), ('fail', 1.5), ('use', 0.8), ('pass', 1)]
        if depth > 3 or self.budget < 0:
            kinds = [k for k in kinds if k[0] in ('decl', 'assign', 'print', 'call', 'coll', 'stopskip', 'global')]
        kind = r.choices([k for k, _ in kinds], [w for _, w in kinds])[0]
        simple = True
        if kind == 'decl':
            const = self.chance(0.1)
            name = self.fresh(fn, const)
            if const and self.chance(0.05):
                name = self.fresh(fn)
            if not name:
                return self.line(f'print({self.any(fn)})')
            t = self.pick(TYPES)
            e = self.of(fn, t if self.chance(0.975) else self.pick(TYPES))
            self.line(f'{"constant " if const else ""}{t} {name} = {e}')
            fn.declared.append(name)
            fn.seen[name] = t
            if fn.kind == 'top':
                self.globals[name] = t
        elif kind == 'assign':
            place, t = self.target(fn)
            self.line(f'{place} = {self.of(fn, t if self.chance(0.965) else self.pick(TYPES))}')
        elif kind == 'print':
            self.line('print(' + ', '.join(self.any(fn) for _ in range(r.randint(1, 3))) + ')')
        elif kind == 'call':
            self.line(self.call(fn, 0))
        elif kind == 'coll':
            c = r.random()
            ls = self.names_of(fn, 'list')
            tgt = self.pick(ls) if ls else None
            if self.chance(0.25) or not tgt:
                tgt = self.place_field(fn, 'items') or tgt
            if not tgt:
                return self.line(f'print({self.any(fn)})')
            if c < 0.55:
                self.line(f'add {self.any(fn, 1)} to {tgt}')
            elif c < 0.8:
                self.line(f'remove {self.num(fn, 2)} from {tgt}')
            elif c < 0.9:
                self.line(f'remove at {self.num(fn, 2)} from {tgt}')
            else:
                ms = self.names_of(fn, 'map') or [tgt]
                self.line(f'remove "{r.choice(["x", "y"])}" from {self.pick(ms)}')
        elif kind == 'ret':
            self.line('return' if self.chance(0.2) else f'return {self.any(fn)}')
        elif kind == 'stopskip':
            self.line(r.choice(['stop', 'skip']))
        elif kind == 'global':
            ns = list(self.globals) or VARS
            chosen = r.sample(ns, min(len(ns), r.randint(1, 2)))
            self.line('global ' + ', '.join(chosen))
            if fn.kind != 'top':
                fn.bridged += [n for n in chosen if n not in fn.bridged]
        elif kind == 'fail':
            self.line('fail ' + r.choice(['"boom"', 'Error("bad")', 'Oops("oops")', self.any(fn)]))
        elif kind == 'use':
            self.line('use ' + r.choice(['moda', 'modb', 'moda']) + ' from ./lib')
            for n, t in (('ma', 'number'), ('mlist', 'list'), ('a', 'str'), ('MK', 'number'), ('mb', 'number')):
                fn.seen.setdefault(n, t)
            return
        elif kind == 'pass':
            self.line('note: nothing here')
            return
        else:
            simple = False
            if kind == 'if':
                self.line(f'if {self.boo(fn)} do:')
                self.body(fn, depth)
                while self.chance(0.3):
                    self.line(f'else if {self.boo(fn)} do:')
                    self.body(fn, depth)
                if self.chance(0.45):
                    self.line('else do:')
                    self.body(fn, depth)
                self.line('end')
            elif kind == 'repeat':
                mark = len(fn.declared)
                name = self.fresh(fn) or self.uid('i')
                t = r.choice(['number', 'number', 'number', 'any', 'str'] if self.chance(0.1) else ['number', 'any'])
                lo, hi = r.choice([('1', '3'), ('3', '1'), ('0', '0'), ('1', self.num(fn, 2)), (self.num(fn, 2), '2'), ('1', '2.5')] if self.chance(0.3) else [('1', '3'), ('2', '4'), ('3', '1')])
                self.line(f'loop repeat {t} {name} = {lo} ~ {hi} do:')
                fn.declared.append(name)
                fn.seen[name] = t
                fn.loops += 1
                self.body(fn, depth)
                fn.loops -= 1
                self.line('end')
                del fn.declared[mark:]
            elif kind == 'while':
                w = self.uid('w')
                if w in fn.declared:
                    return
                self.line(f'number {w} = 0')
                fn.declared.append(w)
                fn.seen[w] = 'number'
                if fn.kind == 'top':
                    self.globals[w] = 'number'
                cond = f'{w} < {r.randint(1, 4)}'
                if self.chance(0.3):
                    cond += f' {r.choice(["and", "or"])} {self.boo(fn, 1)}' if self.chance(0.5) else ''
                self.line(f'loop while {cond} do:')
                fn.loops += 1
                self.body(fn, depth, first=f'{w} = {w} + 1')
                fn.loops -= 1
                self.line('end')
            elif kind == 'each':
                mark = len(fn.declared)
                name = self.fresh(fn) or self.uid('v')
                src = r.choice(['list', 'list', 'list', 'map', 'str'])
                t = r.choice(['any', 'any', 'number', 'str', 'list', 'Box']) if src == 'list' else r.choice(['any', 'str'])
                e = self.of(fn, src) if self.chance(0.95) else self.num(fn)
                self.line(f'loop each {t} {name} in {e} do:')
                fn.declared.append(name)
                fn.seen[name] = t
                fn.loops += 1
                self.body(fn, depth)
                fn.loops -= 1
                self.line('end')
                del fn.declared[mark:]
            elif kind == 'with':
                e = self.box(fn) if self.chance(0.95) else self.any(fn)
                self.line(f'with {e} do:')
                fn.withs += 1
                self.body(fn, depth)
                fn.withs -= 1
                self.line('end')
        p = 0.22 if simple else 0.12
        if fn.kind == 'top' and depth == 0:
            p = 0.5
        if self.chance(p):
            self.handler(fn, depth, True)

    # ---- program
    def function(self, i):
        fn = Fn('func')
        fn.pure = self.chance(0.12)
        fn.is_async = self.chance(0.1)
        params = []
        dflt = False
        for _ in range(self.r.randint(0, 3)):
            name = self.fresh(fn)
            t = self.pick(TYPES)
            dflt = dflt or self.chance(0.3)
            params.append((t, name, dflt))
            fn.declared.append(name)
            fn.seen[name] = t
        mods = ('pure ' if fn.pure else '') + ('async ' if fn.is_async else '')
        ps = ', '.join(f'{t} {n}' + (f' = {self.of(Fn("func"), t, 2)}' if d else '') for t, n, d in params)
        name = f'f{i}'
        self.line(f'{mods}func {name}({ps}) do:')
        self.body(fn, 0, 2, 6, prelude=self.chance(0.85))
        self.ind += 1
        if self.chance(0.8):
            self.line(f'return {self.num(fn)}')
        self.ind -= 1
        self.line('end')
        self.funcs.append((name, params, fn.pure, fn.is_async))

    def method(self, cls, name, params, lo=1, hi=3, first=None, last=None, mods=''):
        fn = Fn('method', cls)
        for t, n, _ in params:
            fn.declared.append(n)
            fn.seen[n] = t
        ps = ', '.join(f'{t} {n}' + (f' = {d}' if d else '') for t, n, d in params)
        self.line(f'{mods}func {name}({ps}) do:')
        self.body(fn, 1, lo, hi, first)
        if last:
            self.ind += 1
            self.line(last)
            self.ind -= 1
        self.line('end')

    def classes(self):
        r = self.r
        self.line('class Box do:')
        self.ind += 1
        self.line(f'number n = {r.choice(["0", "1 + 1", "3"])}')
        self.line('str s = "box"')
        self.line(f'list items = {r.choice(["[]", "[1, 2]", "[1] + [2]"])}')
        self.line(f'any link = {r.choice(["empty", "empty", "[0]", "{}"])}')
        self.line('constant number CAP = 3')
        self.line('private number hidden = 7')
        self.method('Box', 'init', [('number', 'n', '1')], 0, 2, first='self.n = n')
        self.method('Box', 'bump', [('number', 'by', '1')], 0, 1, first='self.n = self.n + by', last='return self.n')
        self.method('Box', 'peek', [], 0, 1, last='return self.hidden + self.n')
        self.method('Box', 'total', [('number', 'x', '')], 0, 2, last='return x + self.n')
        self.method('Box', 'inner', [], 0, 1, last='return 1', mods='private ')
        if self.chance(0.7):
            self.method('Box', 'to_str', [], 0, 0, last='return f"Box({self.n})"')
        self.ind -= 1
        self.line('end')
        self.line('class Kid extends Box do:')
        self.ind += 1
        self.line('number extra = 5')
        if self.chance(0.7):
            self.method('Kid', 'init', [('number', 'n', '2')], 0, 1, first='super.init(n + 1)', last='self.extra = n')
        self.method('Kid', 'bump', [('number', 'by', '1')], 0, 1, last='return super.bump(by) + self.extra')
        self.method('Kid', 'more', [], 1, 3, last='return self.extra')
        self.ind -= 1
        self.line('end')
        self.line('class Oops extends Error do:')
        self.line('    number level = 1')
        self.line('end')
        self.line('func rec(number n) do:')
        self.line('    if n <= 0 do:')
        self.line('        return 0')
        self.line('    end')
        self.line('    return n + rec(n - 1)')
        self.line('end')

    def program(self):
        r = self.r
        if self.chance(0.85):
            self.line('use DLC:list')
        self.funcs = []
        nf = r.randint(1, 4)
        early = r.randint(0, nf)
        # Classes and functions are known to the whole file, so where they are written does not matter to callers.
        top = Fn('top')
        self.budget = r.randint(15, 60)
        self.classes()
        for i in range(early):
            self.function(i)
        n = r.randint(4, 22)
        if self.chance(0.85):
            self.prelude(top)
        for s in range(n):
            self.stmt(top, 0)
            top.seen = dict(self.globals, **{k: v for k, v in top.seen.items()})
            if early < nf and self.chance(0.15):
                self.function(early)
                early += 1
        while early < nf:
            self.function(early)
            early += 1
        return '\n'.join(self.out) + '\n'


MODULES = {
    'moda.cuff': """number ma = 10
list mlist = [1, 2]
constant number MK = 4
str a = "from module"
func mf(number x = 1) do:
    return x + 100
end
print("moda loaded")
""",
    'modb.cuff': """use moda from ./lib
number mb = ma + 1
any a = [7]
func mg() do:
    global mb
    mb = mb + 1
    return mb
end
print("modb loaded", mb)
""",
}


def write_modules(directory):
    os.makedirs(os.path.join(directory, 'lib'), exist_ok=True)
    for name, text in MODULES.items():
        with open(os.path.join(directory, 'lib', name), 'w', encoding='utf-8') as f:
            f.write(text)


def run(binary, path, max_steps, timeout):
    try:
        p = subprocess.run([binary, '--max-steps', str(max_steps), path], capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return None


def compare(seeds, a, b, args, keep):
    """Runs the programs of `seeds` in a directory of their own. Returns the seeds on which the builds differ, and
    the seeds that one of them did not finish in time, which says nothing either way."""
    work = tempfile.mkdtemp(prefix='cuff-fuzz-')
    differing, unfinished = [], []
    try:
        write_modules(work)
        path = os.path.join(work, 'case.cuff')
        for seed in seeds:
            text = Gen(seed).program()
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            first = run(a, path, args.max_steps, args.timeout)
            second = run(b, path, args.max_steps, args.timeout)
            if first is None or second is None:
                unfinished.append(seed)
            elif first != second:
                differing.append((seed, first, second))
                write_modules(keep)
                with open(os.path.join(keep, f'seed_{seed}.cuff'), 'w', encoding='utf-8') as f:
                    f.write(text)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return differing, unfinished


def main():
    ap = argparse.ArgumentParser(description='Compare two builds of cuffc on random programs.')
    ap.add_argument('a', help='one build of cuffc')
    ap.add_argument('b', help='another build of cuffc')
    ap.add_argument('--count', type=int, default=2000, help='how many programs (default 2000)')
    ap.add_argument('--seed', type=int, default=1, help='the first seed (default 1)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help='how many programs to run at once')
    ap.add_argument('--max-steps', type=int, default=200000, help='the step budget of each run (default 200000)')
    ap.add_argument('--timeout', type=int, default=120, help='seconds one run may take (default 120); a sanitizer build is slow')
    ap.add_argument('--keep', default='fuzz-failures', help='where to write the programs that differ')
    args = ap.parse_args()
    a, b = os.path.abspath(args.a), os.path.abspath(args.b)
    for binary in (a, b):
        if not os.path.isfile(binary):
            sys.exit(f'fuzz.py: {binary} does not exist')
    keep = os.path.abspath(args.keep)
    jobs = max(1, min(args.jobs, args.count))
    seeds = list(range(args.seed, args.seed + args.count))
    shares = [seeds[k::jobs] for k in range(jobs)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(lambda share: compare(share, a, b, args, keep), shares))
    differing = sorted((d for found, _ in results for d in found), key=lambda d: d[0])
    unfinished = sorted(seed for _, late in results for seed in late)
    for seed, first, second in differing[:5]:
        print(f'seed {seed}: the builds differ ({os.path.join(args.keep, f"seed_{seed}.cuff")})')
        for name, (status, out, err) in ((args.a, first), (args.b, second)):
            shown = (out + err).decode('utf-8', 'replace').rstrip().split('\n')[-4:]
            print(f'  {name}: exit {status}')
            for line in shown:
                print(f'    {line}')
    if len(differing) > 5:
        print(f'... and {len(differing) - 5} more: seeds {", ".join(str(d[0]) for d in differing[5:25])}')
    if unfinished:
        print(f'not compared, a run took more than {args.timeout} s: seeds {", ".join(str(seed) for seed in unfinished[:20])}')
    print(f'{args.count} programs, {len(differing)} differ')
    sys.exit(1 if differing else 0)


if __name__ == '__main__':
    main()
