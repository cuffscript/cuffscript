#!/usr/bin/env python3
"""Rewrites CuffScript 3.x source into 4.0.0 syntax.

    python3 tools/migrate_3_to_4.py FILE_OR_DIR...          # print what would change (a unified diff)
    python3 tools/migrate_3_to_4.py --write FILE_OR_DIR...  # rewrite the files in place
    python3 tools/migrate_3_to_4.py --check FILE_OR_DIR...  # exit 1 if any file still needs migrating

It reads `.cuff` files, and the ```cuff code blocks of `.md` files. What it rewrites:

  set [returnable] [async] [pure] func   ->  [async] [pure] func
  change NAME to global                  ->  global NAME
  replace NAME[i] to value               ->  change NAME[i] to value
  find P from T [g]                      ->  find(T, p"P") / find_all(T, p"P")
  match T from P                         ->  match(T, p"P")
  replace P in T to R [g]                ->  replace(T, p"P", R) / replace_all(T, p"P", R)
  split T by P                           ->  split(T, p"P")
  count P in T                           ->  count(T, p"P")
  text is "pattern" / IS "pattern"       ->  text matches p"pattern" / text matches p"pattern"i
  text is not "pattern"                  ->  !text matches p"pattern"
  a && b, a || b                         ->  a and b, a or b
  a variable that is now a reserved word ->  the same name with a trailing underscore (reported)
  add X to NAME[i], remove X from NAME[i] ->  left as written, and reported: 3.x applied it to NAME and then read
                                              [i] as a list on its own; 4.0.0 applies it to the element NAME[i]

A pattern that is not a string literal is wrapped in pattern(...). The tool works on tokens, not on a full
parse, so it leaves alone anything it cannot read with confidence and lists it in its report ("manual:").
Run the result through `cuffc` and your tests; the report names every place that needs a second look.
"""

import argparse
import difflib
import pathlib
import re
import sys

NEW_RESERVED = {"each", "skip", "fail", "class", "extends", "with", "private", "self", "super", "isa", "matches"}
CONSTRUCTS = {"find", "match", "replace", "split", "count"}
TYPE_WORDS = {"number", "str", "list", "map", "boolean"}
ARITH = {"+", "-", "*", "/", "%", "**"}
# Characters that mean something in a pattern. A string literal without them is the same text as a pattern.
PATTERN_SPECIAL = set("[]()<>{}+*?~|\\")
# Words that can start an expression, apart from identifiers: what follows `find` etc. when it is a construct.
EXPR_START_WORDS = {"true", "false", "empty", "await", "match", "find", "replace", "split", "count"} | TYPE_WORDS

OLD_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "'": "'", "\\": "\\"}


class Tok:
    __slots__ = ("kind", "text", "lead")

    def __init__(self, kind, text, lead):
        self.kind = kind  # id num str fstr op nl comment
        self.text = text
        self.lead = lead  # the spaces and tabs before it

    def __repr__(self):
        return f"Tok({self.kind},{self.text!r})"


def tokenize(src):
    toks = []
    i = 0
    n = len(src)
    lead = ""
    while i < n:
        c = src[i]
        if c in " \t":
            j = i
            while j < n and src[j] in " \t":
                j += 1
            lead += src[i:j]
            i = j
            continue
        if c == "\r" or c == "\n":
            j = i + 1
            if c == "\r" and j < n and src[j] == "\n":
                j += 1
            toks.append(Tok("nl", src[i:j], lead))
            lead = ""
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            word = src[i:j]
            if word == "f" and j < n and src[j] == '"':
                end = scan_string(src, j)
                toks.append(Tok("fstr", src[i:end], lead))
                lead = ""
                i = end
                continue
            if word == "note" and j < n and src[j] == ":":
                end = scan_comment(src, i)
                toks.append(Tok("comment", src[i:end], lead))
                lead = ""
                i = end
                continue
            toks.append(Tok("id", word, lead))
            lead = ""
            i = j
            continue
        if c.isdigit():
            j = i
            while j < n and src[j].isdigit():
                j += 1
            if j + 1 < n and src[j] == "." and src[j + 1].isdigit():
                j += 1
                while j < n and src[j].isdigit():
                    j += 1
            toks.append(Tok("num", src[i:j], lead))
            lead = ""
            i = j
            continue
        if c == '"' or c == "'":
            end = scan_string(src, i)
            toks.append(Tok("str", src[i:end], lead))
            lead = ""
            i = end
            continue
        two = src[i:i + 2]
        if two in ("**", ">=", "<=", "&&", "||"):
            toks.append(Tok("op", two, lead))
            lead = ""
            i += 2
            continue
        toks.append(Tok("op", c, lead))
        lead = ""
        i += 1
    if lead:
        toks.append(Tok("comment", "", lead))
    return toks


def scan_string(src, i):
    """End offset of the string literal that starts at src[i] (a quote). An unterminated one runs to the line end."""
    quote = src[i]
    j = i + 1
    n = len(src)
    while j < n:
        c = src[j]
        if c == "\\" and j + 1 < n:
            j += 2
            continue
        if c == quote:
            return j + 1
        if c == "\n":
            return j
        j += 1
    return n


def scan_comment(src, i):
    """End offset of the comment that starts at src[i] ('note:')."""
    j = i + 5
    n = len(src)
    k = j
    while k < n and src[k] in " \t":
        k += 1
    if k >= n or src[k] in "\r\n":
        m = re.compile(r"endnote(?![A-Za-z0-9_])").search(src, k)
        return m.end() if m else n
    while j < n and src[j] not in "\r\n":
        j += 1
    return j


def decode_string(literal):
    """The value of a string literal as the 3.x tokenizer reads it."""
    body = literal[1:-1] if len(literal) >= 2 and literal[-1] == literal[0] else literal[1:]
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            esc = body[i + 1]
            out.append(OLD_ESCAPES.get(esc, "\\" + esc))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


class Report:
    def __init__(self):
        self.manual = []
        self.renamed = []
        self.current = 0  # sources that already use 4.0.0 syntax

    def merge(self, other):
        self.manual += other.manual
        self.renamed += other.renamed
        self.current += other.current


class Migrator:
    def __init__(self, src, report, name="<source>", line_base=0, in_fstring=False):
        self.src = src
        self.toks = tokenize(src)
        self.report = report
        self.name = name
        self.line_base = line_base
        self.in_f = in_fstring
        self.changed = False
        self.bang = self.find_negations()

    # ---- helpers on the token list -------------------------------------------------------------------------------

    def line_of(self, idx):
        count = 1
        for t in self.toks[:idx]:
            count += t.text.count("\n") if t.kind in ("nl", "comment", "str", "fstr") else 0
        return self.line_base + count

    def sk(self, i, nl=False):
        """Index of the next significant token at or after i (comments are skipped, and newlines when `nl`)."""
        toks = self.toks
        while i < len(toks) and (toks[i].kind == "comment" or (nl and toks[i].kind == "nl")):
            i += 1
        return i

    def is_op(self, i, text):
        return i < len(self.toks) and self.toks[i].kind == "op" and self.toks[i].text == text

    def is_id(self, i, text=None):
        if i >= len(self.toks) or self.toks[i].kind != "id":
            return False
        return text is None or self.toks[i].text == text

    # ---- extent parsers: the end index (exclusive) of an expression, or None -----------------------------------------
    # They follow the 3.x grammar: logical > not > comparison > additive > multiplicative > unary > power > postfix.

    def x_expr(self, i, nl=False):
        i = self.x_not(i, nl)
        while i is not None:
            j = self.sk(i, nl)
            if self.is_id(j, "and") or self.is_id(j, "or") or self.is_op(j, "&&") or self.is_op(j, "||"):
                i = self.x_not(j + 1, nl)
            else:
                return i
        return None

    def x_not(self, i, nl):
        j = self.sk(i, nl)
        if self.is_op(j, "!"):
            return self.x_not(j + 1, nl)
        return self.x_comparison(i, nl)

    def x_comparison(self, i, nl):
        i = self.x_additive(i, nl)
        while i is not None:
            j = self.sk(i, nl)
            if self.is_id(j, "is") or self.is_id(j, "IS") or any(self.is_op(j, o) for o in (">=", "<=", ">", "<")):
                k = j + 1
                if (self.is_id(j, "is") or self.is_id(j, "IS")) and self.is_id(self.sk(k, nl), "not"):
                    k = self.sk(k, nl) + 1
                i = self.x_additive(k, nl)
            else:
                return i
        return None

    def x_additive(self, i, nl=False):
        i = self.x_mult(i, nl)
        while i is not None:
            j = self.sk(i, nl)
            if self.is_op(j, "+") or self.is_op(j, "-"):
                i = self.x_mult(j + 1, nl)
            else:
                return i
        return None

    def x_mult(self, i, nl):
        i = self.x_unary(i, nl)
        while i is not None:
            j = self.sk(i, nl)
            if any(self.is_op(j, o) for o in ("*", "/", "%")):
                i = self.x_unary(j + 1, nl)
            else:
                return i
        return None

    def x_unary(self, i, nl):
        j = self.sk(i, nl)
        if self.is_op(j, "-"):
            return self.x_unary(j + 1, nl)
        i = self.x_postfix(i, nl)
        if i is None:
            return None
        j = self.sk(i, nl)
        if self.is_op(j, "**"):
            return self.x_unary(j + 1, nl)
        return i

    def x_postfix(self, i, nl):
        i = self.x_primary(i, nl)
        while i is not None:
            j = self.sk(i, nl)
            if self.is_op(j, "["):
                k = self.x_expr(j + 1, True)
                if k is None:
                    return None
                k = self.sk(k, True)
                if self.is_op(k, "~"):
                    k = self.x_expr(k + 1, True)
                    if k is None:
                        return None
                    k = self.sk(k, True)
                if not self.is_op(k, "]"):
                    return None
                i = k + 1
            elif self.is_op(j, "("):
                k = self.sk(j + 1, True)
                if not self.is_op(k, ")"):
                    k = self.x_expr(k, True)
                    while k is not None:
                        k = self.sk(k, True)
                        if self.is_op(k, ","):
                            k = self.x_expr(self.sk(k + 1, True), True)
                        else:
                            break
                    if k is None:
                        return None
                    k = self.sk(k, True)
                if not self.is_op(k, ")"):
                    return None
                i = k + 1
            else:
                return i
        return None

    def x_primary(self, i, nl):
        j = self.sk(i, nl)
        if j >= len(self.toks):
            return None
        t = self.toks[j]
        if t.kind in ("num", "str", "fstr"):
            return j + 1
        if t.kind == "id":
            if t.text == "await":
                return self.x_expr(j + 1, nl)
            if t.text in CONSTRUCTS:
                found = self.construct(j, nl=nl)
                if found is not None:
                    return found["end"]
            return j + 1
        if t.kind == "op" and t.text == "(":
            k = self.x_expr(j + 1, True)
            if k is None:
                return None
            k = self.sk(k, True)
            return k + 1 if self.is_op(k, ")") else None
        if t.kind == "op" and t.text == "[":
            k = self.sk(j + 1, True)
            if self.is_op(k, "]"):
                return k + 1
            while True:
                k = self.x_expr(k, True)
                if k is None:
                    return None
                k = self.sk(k, True)
                if self.is_op(k, ","):
                    k = self.sk(k + 1, True)
                    if self.is_op(k, "]"):
                        return k + 1
                    continue
                return k + 1 if self.is_op(k, "]") else None
        if t.kind == "op" and t.text == "{":
            k = self.sk(j + 1, True)
            if self.is_op(k, "}"):
                return k + 1
            while True:
                k = self.x_expr(k, True)
                if k is None:
                    return None
                k = self.sk(k, True)
                if not self.is_op(k, ":"):
                    return None
                k = self.x_expr(k + 1, True)
                if k is None:
                    return None
                k = self.sk(k, True)
                if self.is_op(k, ","):
                    k = self.sk(k + 1, True)
                    if self.is_op(k, "}"):
                        return k + 1
                    continue
                return k + 1 if self.is_op(k, "}") else None
        return None

    # ---- the text constructs ---------------------------------------------------------------------------------------

    def starts_expression(self, j):
        """Is the token at j one that can begin an expression (and is not a bracket)? As in the 3.x parser."""
        if j >= len(self.toks):
            return False
        t = self.toks[j]
        if t.kind in ("num", "str", "fstr"):
            return True
        if t.kind == "id":
            return t.text not in ("to", "from", "in", "by", "do", "end", "and", "or", "is", "IS", "not", "else")
        return t.kind == "op" and t.text in ("-", "!", "{")

    def flags_at(self, j):
        """(flags, next index) for an optional `g`/`i`/`m` word written after a text construct."""
        if j < len(self.toks) and self.toks[j].kind == "id" and re.fullmatch(r"[gim]+", self.toks[j].text):
            return self.toks[j].text, j + 1
        return "", j

    def construct(self, i, nl=False):
        """If the word at i starts a 3.x text construct, describe it: its end, operands and flags."""
        word = self.toks[i].text
        nxt = i + 1
        if nxt >= len(self.toks) or not self.starts_expression(nxt):
            return None
        # `set match m to ...`: a type before a name is not a construct (it never has `from` after one operand).
        if word == "find":
            return self.parse_find(i)
        if word == "match":
            return self.parse_match(i)
        if word == "replace":
            return self.parse_replace(i)
        if word == "split":
            return self.parse_split(i)
        if word == "count":
            return self.parse_count(i)
        return None

    def pattern_operand(self, j, stop_words):
        """A pattern argument at j: a lone string literal, or any expression up to one of `stop_words`."""
        if self.toks[j].kind == "str":
            return ("literal", j, j + 1)
        end = self.x_expr(j, False)
        if end is None:
            return None
        return ("dynamic", j, end)

    def parse_find(self, i):
        p = self.pattern_operand(i + 1, ("from",))
        if p is None or not self.is_id(self.sk(p[2]), "from") or self.sk(p[2]) != p[2]:
            return None
        k = p[2] + 1
        end = self.x_additive(k)
        if end is None:
            return None
        flags, after = self.flags_at(end)
        return {"kind": "find", "pattern": p, "target": (k, end), "flags": flags, "end": after, "start": i}

    def parse_match(self, i):
        end = self.x_additive(i + 1)
        if end is None or not self.is_id(end, "from"):
            return None
        p = self.pattern_operand(end + 1, ())
        if p is None:
            return None
        flags, after = self.flags_at(p[2])
        return {"kind": "match", "pattern": p, "target": (i + 1, end), "flags": flags, "end": after, "start": i}

    def parse_replace(self, i):
        p = self.pattern_operand(i + 1, ("in",))
        if p is None or not self.is_id(p[2], "in"):
            return None
        tend = self.x_additive(p[2] + 1)
        if tend is None or not self.is_id(tend, "to"):
            return None
        rend = self.x_additive(tend + 1)
        if rend is None:
            return None
        flags, after = self.flags_at(rend)
        return {"kind": "replace", "pattern": p, "target": (p[2] + 1, tend), "replacement": (tend + 1, rend),
                "flags": flags, "end": after, "start": i}

    def parse_split(self, i):
        tend = self.x_additive(i + 1)
        if tend is None or not self.is_id(tend, "by"):
            return None
        p = self.pattern_operand(tend + 1, ())
        if p is None:
            return None
        return {"kind": "split", "pattern": p, "target": (i + 1, tend), "flags": "", "end": p[2], "start": i}

    def parse_count(self, i):
        p = self.pattern_operand(i + 1, ("in",))
        if p is None or not self.is_id(p[2], "in"):
            return None
        tend = self.x_additive(p[2] + 1)
        if tend is None:
            return None
        flags, after = self.flags_at(tend)
        return {"kind": "count", "pattern": p, "target": (p[2] + 1, tend), "flags": flags, "end": after, "start": i}

    # ---- pattern literals ------------------------------------------------------------------------------------------

    def pattern_text(self, literal_tok, flags):
        """A 4.0 expression for the pattern that string literal `literal_tok` stood for, with flags i/m."""
        keep = "".join(f for f in "im" if f in flags)
        value = decode_string(literal_tok.text)
        plain = not any(ch in value for ch in "\n\r\t")
        if not self.in_f and plain:
            return 'p"' + value.replace('"', '\\"') + '"' + keep
        return self.pattern_call(literal_tok.text, keep)

    def pattern_call(self, expr_text, keep):
        if self.in_f:
            # Inside an f-string expression a double quote would end the f-string: use a single-quoted str.
            expr_text = self.as_single_quoted(expr_text)
        return "pattern(" + expr_text + (', "' + keep + '"' if keep and not self.in_f else
                                         (", '" + keep + "'" if keep else "")) + ")"

    @staticmethod
    def as_single_quoted(text):
        if text.startswith('"') and text.endswith('"') and len(text) >= 2:
            body = decode_string(text)
            escaped = body.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")
            return "'" + escaped + "'"
        return text

    def operand_text(self, a, b):
        return self.render(a, b).strip()

    def pattern_expr(self, p, flags):
        kind, a, b = p
        if kind == "literal":
            return self.pattern_text(self.toks[a], flags)
        inner = self.operand_text(a, b)
        keep = "".join(f for f in "im" if f in flags)
        if self.in_f:
            return "pattern(" + inner + (", '" + keep + "'" if keep else "") + ")"
        return "pattern(" + inner + (', "' + keep + '"' if keep else "") + ")"

    def render_construct(self, c):
        flags = c["flags"]
        pat = self.pattern_expr(c["pattern"], flags)
        target = self.operand_text(*c["target"])
        kind = c["kind"]
        if kind == "find":
            name = "find_all" if "g" in flags else "find"
            return f"{name}({target}, {pat})"
        if kind == "match":
            return f"match({target}, {pat})"
        if kind == "replace":
            name = "replace_all" if "g" in flags else "replace"
            return f"{name}({target}, {pat}, {self.operand_text(*c['replacement'])})"
        if kind == "split":
            return f"split({target}, {pat})"
        return f"count({target}, {pat})"

    # ---- `text is "pattern"` ---------------------------------------------------------------------------------------

    def left_operand_start(self, idx):
        """Index of the first token of the additive expression that ends just before token idx, or None."""
        toks = self.toks
        j = idx - 1
        while True:
            while j >= 0 and toks[j].kind == "comment":
                j -= 1
            if j < 0:
                return None
            t = toks[j]
            # one term: leaf, then postfix brackets to its left
            while t.kind == "op" and t.text in (")", "]"):
                close = t.text
                opener = "(" if close == ")" else "["
                depth = 0
                k = j
                while k >= 0:
                    if toks[k].kind == "op" and toks[k].text == close:
                        depth += 1
                    elif toks[k].kind == "op" and toks[k].text == opener:
                        depth -= 1
                        if depth == 0:
                            break
                    k -= 1
                if k < 0:
                    return None
                j = k - 1
                if j < 0:
                    return k
                t = toks[j]
                if not (t.kind in ("id", "num", "str", "fstr") or (t.kind == "op" and t.text in (")", "]"))):
                    # a bare group or list literal starts here
                    j = k
                    t = toks[j]
                    break
            else:
                if t.kind not in ("id", "num", "str", "fstr") and not (t.kind == "op" and t.text in ("(", "[")):
                    return None
            start = j
            prev = j - 1
            while prev >= 0 and toks[prev].kind == "comment":
                prev -= 1
            if prev >= 0 and toks[prev].kind == "op" and toks[prev].text in ARITH:
                j = prev - 1
                continue
            return start

    def is_conversion(self, i):
        """For an `is`/`IS` token at i followed by a lone string literal: (negated, literal index, end) or None."""
        j = self.sk(i + 1)
        negated = False
        if self.is_id(j, "not"):
            negated = True
            j = self.sk(j + 1)
        if j >= len(self.toks) or self.toks[j].kind != "str":
            return None
        after = self.sk(j + 1)
        if after < len(self.toks) and self.toks[after].kind == "op" and self.toks[after].text in ARITH | {"[", "(", "**"}:
            return None
        return negated, j, j + 1

    # ---- rendering -------------------------------------------------------------------------------------------------

    def render(self, a, b):
        toks = self.toks
        out = []
        pending = None  # the lead of a dropped token, handed to the next one that is written
        i = a

        bang_at = None

        def emit(tok_lead, text):
            nonlocal pending, bang_at
            if pending is not None:
                tok_lead = pending
                pending = None
            if bang_at is not None:
                text = "!" + text
                bang_at = None
            out.append(tok_lead + text)

        def drop(tok):
            nonlocal pending
            if pending is None:
                pending = tok.lead

        while i < b:
            t = toks[i]
            if i in self.bang:
                bang_at = i
            if t.kind == "fstr":
                emit(t.lead, self.migrate_fstring(t.text))
                i += 1
                continue
            if t.kind != "id" and not (t.kind == "op" and t.text in ("&&", "||")):
                emit(t.lead, t.text)
                i += 1
                continue
            if t.kind == "op":
                self.changed = True
                emit(t.lead, "and" if t.text == "&&" else "or")
                i += 1
                continue

            word = t.text
            prev = self.prev_sig(i)

            # set [returnable] [async] [pure] func
            if word == "set":
                j, mods = self.sk(i + 1), []
                while self.is_id(j) and self.toks[j].text in ("returnable", "async", "pure"):
                    mods.append(j)
                    j = self.sk(j + 1)
                if self.is_id(j, "func"):
                    self.changed = True
                    drop(t)
                    for m in mods:
                        if toks[m].text == "returnable":
                            # remove it, keeping the whitespace layout of the line
                            pass
                    i += 1
                    # the modifiers are written as they were, except `returnable`
                    while i < j:
                        if toks[i].kind == "id" and toks[i].text == "returnable":
                            drop(toks[i])
                        else:
                            emit(toks[i].lead, toks[i].text)
                        i += 1
                    continue

            # add X to NAME[i]: the target is a different thing in 4.0.0, so it is reported, not rewritten
            if word in ("add", "remove") and self.at_statement_start(i):
                self.check_collection_target(i, "to" if word == "add" else "from")

            # change NAME to global
            if word == "change":
                j = self.sk(i + 1)
                k = self.sk(j + 1) if j < len(toks) else j
                m = self.sk(k + 1) if k < len(toks) else k
                if (self.is_id(j) and self.is_id(k, "to") and self.is_id(m, "global")
                        and self.ends_statement(m + 1)):
                    self.changed = True
                    emit(t.lead, "global")
                    emit(toks[j].lead, toks[j].text)
                    i = m + 1
                    continue

            # replace NAME[i] to value (a statement)
            if word == "replace" and self.at_statement_start(i):
                j = self.sk(i + 1)
                if self.is_id(j) and self.is_op(j + 1, "["):
                    self.changed = True
                    emit(t.lead, "change")
                    i += 1
                    continue

            # text constructs
            if word in CONSTRUCTS and not (prev and prev.kind == "id" and prev.text in ("set", "constant", "func")):
                c = self.construct(i)
                if c is not None and c["end"] <= b:
                    self.changed = True
                    emit(t.lead, self.render_construct(c))
                    i = c["end"]
                    continue

            # `is` / `IS` against a string literal
            if word in ("is", "IS"):
                conv = self.is_conversion(i)
                if conv is not None:
                    negated, lit, end = conv
                    if not self.needs_pattern(word, toks[lit]):
                        # stays an equality test, written as it was
                        emit(t.lead, "is" + (" not" if negated else ""))
                        i = lit
                        continue
                    self.changed = True
                    lit_text = self.pattern_text(toks[lit], "i" if word == "IS" else "")
                    if negated and self.left_operand_start(i) is None:
                        self.report.manual.append(
                            f"{self.name}:{self.line_of(i)}: `is not` against a pattern: write `!x matches {lit_text}`")
                    emit(t.lead, "matches")
                    emit(toks[lit].lead or " ", lit_text)
                    i = lit + 1
                    continue

            # identifiers that are reserved words now
            if word in NEW_RESERVED and not (prev and prev.kind == "op" and prev.text == "."):
                self.changed = True
                self.report.renamed.append(f"{self.name}:{self.line_of(i)}: `{word}` is a reserved word in 4.0.0 -> `{word}_`")
                emit(t.lead, word + "_")
                i += 1
                continue

            emit(t.lead, word)
            i += 1
        return "".join(out)

    def needs_pattern(self, is_word, literal_tok):
        """Does `x is "literal"` of 3.x need `matches` (a pattern or a case-insensitive test) in 4.0.0?"""
        value = decode_string(literal_tok.text)
        return is_word == "IS" or any(ch in PATTERN_SPECIAL for ch in value)

    def find_negations(self):
        """Token indexes that get a `!` in front: the left operand of every `is not "pattern"` that becomes `matches`."""
        marks = set()
        for i, t in enumerate(self.toks):
            if t.kind == "id" and t.text in ("is", "IS"):
                conv = self.is_conversion(i)
                if conv and conv[0] and self.needs_pattern(t.text, self.toks[conv[1]]):
                    start = self.left_operand_start(i)
                    if start is not None:
                        marks.add(start)
        return marks

    def check_collection_target(self, i, joiner):
        """Reports `add X to NAME[...]` / `remove X from NAME[...]`, whose meaning changed with the target rules."""
        depth = 0
        j = i + 1
        while j < len(self.toks) and self.toks[j].kind != "nl":
            t = self.toks[j]
            if t.kind == "op" and t.text in "([{":
                depth += 1
            elif t.kind == "op" and t.text in ")]}":
                depth -= 1
            elif depth == 0 and t.kind == "id" and t.text == joiner:
                k = self.sk(j + 1)
                if self.is_id(k) and self.is_op(k + 1, "["):
                    self.report.manual.append(
                        f"{self.name}:{self.line_of(i)}: `{self.toks[i].text} ... {joiner} {self.toks[k].text}[...]` "
                        f"now changes the element, not {self.toks[k].text}: check what was meant")
                return
            j += 1

    def is_current_syntax(self):
        """Does the source already use something that exists only in 4.0.0 syntax? 3.x has no `.` outside numbers and
        strings, no `p"..."`, no `loop each`, no `func` without `set`, and no two operands side by side. Such a
        source is left alone: its `class`, `self` and `each` are keywords now, not names to rename."""
        toks = self.toks
        operand_end = lambda t: t.kind in ("id", "num", "str", "fstr") or (t.kind == "op" and t.text in (")", "]"))
        for i, t in enumerate(toks):
            if t.kind == "op" and t.text == "." and not self.in_use_statement(i):
                return True
            if t.kind != "id":
                continue
            nxt = self.sk(i + 1)
            at_start = self.at_statement_start(i)
            if t.text == "p" and i + 1 < len(toks) and toks[i + 1].kind == "str" and toks[i + 1].lead == "":
                return True
            if t.text == "loop" and self.is_id(nxt, "each"):
                return True
            if t.text == "func":
                k = i - 1
                while k >= 0 and toks[k].kind == "id" and toks[k].text in ("async", "pure", "returnable"):
                    k -= 1
                if not (k >= 0 and toks[k].kind == "id" and toks[k].text == "set"):
                    return True
            if t.text == "class" and at_start and self.is_id(nxt) and self.is_id(self.sk(nxt + 1)):
                return True
            if t.text == "global" and at_start and self.is_id(nxt):
                return True
            if t.text == "fail" and at_start and nxt < len(toks) and toks[nxt].kind in ("str", "fstr", "num", "id"):
                return True
            if t.text == "skip" and at_start and (nxt >= len(toks) or toks[nxt].kind == "nl"):
                return True
            if t.text == "with" and at_start and nxt < len(toks) and toks[nxt].kind != "op":
                j = nxt
                while j < len(toks) and toks[j].kind != "nl":
                    j += 1
                if self.is_op(j - 1, ":") and self.is_id(j - 2, "do"):
                    return True
            if t.text in ("isa", "matches") and i > 0 and operand_end(toks[self.prev_index(i)]) and nxt < len(toks) and toks[nxt].kind != "nl":
                return True
        return False

    def in_use_statement(self, i):
        """Is token i on a line that starts with `use`? A module path such as ./lib has dots."""
        j = i
        while j > 0 and self.toks[j - 1].kind != "nl":
            j -= 1
        return self.is_id(j, "use")

    def prev_index(self, i):
        j = i - 1
        while j > 0 and self.toks[j].kind == "comment":
            j -= 1
        return max(j, 0)

    def prev_sig(self, i):
        j = i - 1
        while j >= 0 and self.toks[j].kind == "comment":
            j -= 1
        return self.toks[j] if j >= 0 else None

    def at_statement_start(self, i):
        p = self.prev_sig(i)
        if p is None or p.kind == "nl":
            return True
        return p.kind == "op" and p.text == ":"

    def ends_statement(self, j):
        j = self.sk(j)
        return j >= len(self.toks) or self.toks[j].kind == "nl" or self.is_id(j, "end")

    # ---- f-strings ---------------------------------------------------------------------------------------------------

    def migrate_fstring(self, literal):
        """Migrates the expressions written between braces in an f-string."""
        out = []
        i = 0
        n = len(literal)
        while i < n:
            c = literal[i]
            if c == "\\" and i + 1 < n:
                out.append(literal[i:i + 2])
                i += 2
                continue
            if c == "{":
                depth, j, quote = 1, i + 1, None
                while j < n and depth:
                    ch = literal[j]
                    if quote:
                        if ch == "\\":
                            j += 1
                        elif ch == quote:
                            quote = None
                    elif ch in "'\"":
                        quote = ch
                    elif ch == "{":
                        depth += 1
                    elif ch == "}":
                        depth -= 1
                    j += 1
                if depth == 0:
                    inner = literal[i + 1:j - 1]
                    sub = Migrator(inner, self.report, self.name, self.line_base, in_fstring=True)
                    new = sub.render(0, len(sub.toks))
                    if sub.changed:
                        self.changed = True
                    out.append("{" + new + "}")
                    i = j
                    continue
            out.append(c)
            i += 1
        return "".join(out)

    def run(self):
        return self.render(0, len(self.toks))


# ---- files -----------------------------------------------------------------------------------------------------------

FENCE = re.compile(r"^(?P<indent>[ \t]*)(?P<ticks>`{3,})(?P<info>[^\n]*)$")


def migrate_source(src, name, report, line_base=0):
    m = Migrator(src, report, name, line_base)
    if m.is_current_syntax():
        report.current += 1
        return src
    return m.run()


def migrate_markdown(text, name, report):
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = FENCE.match(line)
        if not m:
            out.append(line)
            i += 1
            continue
        ticks = m.group("ticks")
        info = m.group("info").strip()
        j = i + 1
        while j < len(lines) and not (lines[j].strip().startswith(ticks) and lines[j].strip().strip("`") == ""):
            j += 1
        body = lines[i + 1:j]
        out.append(line)
        if info.split(" ")[0] == "cuff":
            migrated = migrate_source("\n".join(body), name, report, line_base=i + 1)
            out.extend(migrated.split("\n"))
        else:
            out.extend(body)
        if j < len(lines):
            out.append(lines[j])
        i = j + 1
    return "\n".join(out)


def collect(paths):
    files = []
    for p in paths:
        path = pathlib.Path(p)
        if not path.exists():
            sys.exit(f"migrate_3_to_4: no such file or directory: {p}")
        if path.is_dir():
            files += sorted(x for x in path.rglob("*") if x.suffix in (".cuff", ".md") and ".git" not in x.parts)
        else:
            files.append(path)
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--write", action="store_true", help="rewrite the files in place")
    ap.add_argument("--check", action="store_true", help="exit 1 if any file would change")
    ap.add_argument("--quiet", action="store_true", help="do not print the diff")
    args = ap.parse_args()

    report = Report()
    changed_files = 0
    for path in collect(args.paths):
        raw = path.read_bytes().decode("utf-8")
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n") if crlf else raw
        if path.suffix == ".md":
            new = migrate_markdown(text, str(path), report)
        elif path.suffix == ".cuff":
            new = migrate_source(text, str(path), report)
        else:
            continue
        if new == text:
            continue
        changed_files += 1
        if args.write:
            path.write_bytes((new.replace("\n", "\r\n") if crlf else new).encode("utf-8"))
        elif not args.check and not args.quiet:
            sys.stdout.writelines(difflib.unified_diff(text.splitlines(True), new.splitlines(True),
                                                       str(path), str(path) + " (4.0.0)"))
    for line in report.renamed:
        print("renamed:", line, file=sys.stderr)
    for line in report.manual:
        print("manual:", line, file=sys.stderr)
    if report.current:
        print(f"{report.current} source(s) already use 4.0.0 syntax and were left alone", file=sys.stderr)
    print(f"{changed_files} file(s) {'rewritten' if args.write else 'need migrating'}", file=sys.stderr)
    if args.check and changed_files:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
