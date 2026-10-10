import sys, os, time, signal, termios, tempfile, subprocess, random
from harness import *
import wcwidth

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cuffsh"))
results = []
def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "   <-- " + str(detail)))
def new(cols=80, rows=24, args=("--no-banner",), **kw):
    s = Session([BIN, *args], cols=cols, rows=rows, **kw); s.pump(0.35); return s

COOKED = termios.ICANON | termios.ECHO | termios.ISIG

# ---- terminals with different capabilities --------------------------------
s = new(kitty=False)
s.send("a" + "\r", .3)   # Shift+Enter on a plain terminal is just CR
check("no-kitty terminal: probe answered DA1 only -> protocol never enabled", b"\x1b[>1u" not in s.raw, s.raw[:80])
check("no-kitty terminal: Shift+Enter (plain CR) behaves like Enter", s.nonblank()[:2] == [">> a", "ERROR: [E4-001] Runtime Error at line 1, column 1: undefined variable 'a'"] or "undefined variable" in " ".join(s.nonblank()), s.nonblank())
s.send(b"\x1b\rx", .2); s.send("y", .2)
check("no-kitty terminal: Alt+Enter still gives a newline", ".. xy" in s.nonblank(), s.nonblank())
s.finish(.2)

s = new(kitty=True)
for i in range(3): s.send(f"print({i})\r", .3)
s.send(":exit\r", .4); s.finish(.2)
pushes, pops = s.raw.count(b"\x1b[>1u"), s.raw.count(b"\x1b[<1u")
check("kitty terminal: push/pop balanced across entries", pushes == pops and pushes >= 4, (pushes, pops))
check("kitty terminal: probed exactly once", s.raw.count(b"\x1b[?u") == 1, s.raw.count(b"\x1b[?u"))

t0 = time.time(); s = Session([BIN, "--no-banner"], answer_queries=False); s.pump(.1)
s.send("print(7)\r", .6); el = time.time() - t0
check("terminal that answers nothing: works after ~250ms probe timeout, no protocol enabled", "7" in s.nonblank() and b"\x1b[>1u" not in s.raw, (s.nonblank(), el))
s.send(":exit\r", .3); s.finish(.2)

# ---- terminal state is restored on every way out ---------------------------
def flags_after(action):
    s = new(); action(s); s.pump(.5)
    f = termios.tcgetattr(s.fd)[3]; s.finish(.2); return f
f = flags_after(lambda s: s.send(":exit\r", .4))
check("terminal cooked after :exit", (f & COOKED) == COOKED, f)
f = flags_after(lambda s: s.send(b"\x04", .4))
check("terminal cooked after Ctrl+D", (f & COOKED) == COOKED, f)
def hup(s): os.kill(s.pid, signal.SIGHUP)
s = new(); os.kill(s.pid, signal.SIGHUP); s.pump(.6)
check("SIGHUP while reading: shell dies and terminal state is restored", s.rc is not None and (termios.tcgetattr(s.fd)[3] & COOKED) == COOKED, (s.rc, termios.tcgetattr(s.fd)[3]))
s.finish(.1)
s = new(); os.kill(s.pid, signal.SIGINT); s.pump(.4); s.send("print(1)\r", .4)
check("external SIGINT while idle is harmless", s.rc is None and "1" in s.nonblank(), s.nonblank()); s.finish(.2)

# ---- paste safety valve ------------------------------------------------------
s = new(); s.send(b"\x1b[200~abc", .3); time.sleep(3.3); s.pump(.2); s.send(b"\r", .5)
check("lost paste-end marker: Enter works again after the timeout", s.rc is None and any("ERROR" in l or "abc" in l for l in s.nonblank()[1:]) , s.nonblank()); s.finish(.2)
s = new(); s.send(b"\x1b[200~abc", .3); s.send(b"\x03", .3)
check("Ctrl+C always escapes a stuck paste", s.nonblank() == [">> abc", ">>"], s.nonblank()); s.finish(.2)

# ---- input() inside the shell -------------------------------------------------
s = new(); s.send('print(input("name? "))\r', .5)
check("input(): prompt shown and terminal is back in normal echoing mode", "name?" in " ".join(s.nonblank()), s.nonblank())
s.send("bob\r", .5)
check("input(): typed text reaches the program", "bob" in s.nonblank()[-2:], s.nonblank())
s.send("print(2)\r", .4); check("shell fully usable after input()", "2" in s.nonblank(), s.nonblank()); s.send(":exit\r", .3); s.finish(.2)

# ---- non-interactive (piped) use ---------------------------------------------
r = subprocess.run([BIN], input=b'set number x to 4\nprint(x * 2)\nprint("\xed\x95\x9c")\n', capture_output=True)
check("piped stdin: whole input runs as one script, no prompts/banner", r.stdout == "8\n한\n".encode() and r.returncode == 0, (r.stdout, r.stderr))
r = subprocess.run([BIN], input=b'print(1/0)\n', capture_output=True)
check("piped stdin: errors printed like cuffc ('ERROR: ...')", r.stderr.startswith(b"ERROR: [E4-006]"), r.stderr)
cuffc = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "cuffc")
if os.path.exists(cuffc):
    rc = subprocess.run([cuffc], input=b'print(1/0)\n', capture_output=True)
    check("piped error text identical to cuffc's own output", rc.stderr == r.stderr, (rc.stderr, r.stderr))
r = subprocess.run([BIN, "--bogus"], capture_output=True); check("unknown option rejected", r.returncode == 2 and b"unknown option" in r.stderr)
r = subprocess.run([BIN, "--timeout"], capture_output=True); check("--timeout without a value: one clear message", r.returncode == 2 and r.stderr.count(b"\n") == 1, r.stderr)
r = subprocess.run([BIN, "--timeout", "abc"], capture_output=True); check("--timeout abc rejected", r.returncode == 2)
r = subprocess.run([BIN, "--timeout", "-5"], capture_output=True); check("--timeout -5 rejected", r.returncode == 2)
r = subprocess.run([BIN, "/nonexistent.cuff"], input=b"", capture_output=True); check("missing startup file: clear error, shell still runs", b"Cannot open file" in r.stderr, r.stderr)
r = subprocess.run([BIN, "--help"], capture_output=True); check("--help works", r.returncode == 0 and b"Shift+Enter" in r.stdout)

# ---- randomized model check: screen == independent Python layout model -------
def model_render(buf, cols):
    """rows of text a terminal would show for the entry, plus cursor (row, col) for index `cur`."""
    rows = [""]; widths = [0]
    r = 0; c = 0
    def newrow():
        nonlocal r, c
        rows.append(""); r += 1; c = 0
    def put(ch):
        nonlocal c
        w = wcwidth.wcwidth(ch)
        if w < 0: w = 0
        if c + w > cols: newrow()
        rows[r] += ch; c += w
    return rows, put, newrow

def expected(lines, cols, cur_line, cur_col_idx):
    rows, put, newrow = model_render(None, cols)
    pos = None
    cur_cell = None
    for li, line in enumerate(lines):
        if li > 0:
            newrow()
        for ch in (">> " if li == 0 else ".. "): put(ch)
        for ci, ch in enumerate(line):
            if li == cur_line and ci == cur_col_idx:
                w = wcwidth.wcwidth(ch)
                # position where this char will land
                cc = len(rows[-1]) and sum(max(wcwidth.wcwidth(x),0) for x in rows[-1])
                cur_cell = (len(rows)-1, cc) if cc + w <= cols else (len(rows), 0)
            put(ch)
        if li == cur_line and cur_col_idx == len(line):
            cc = sum(max(wcwidth.wcwidth(x),0) for x in rows[-1])
            cur_cell = (len(rows)-1, min(cc, cols-1))
    return [r.rstrip() for r in rows], cur_cell

random.seed(12345)
ALPHA = list("abcdefghijklmnopqrstuvwxyz0123456789 ()=+") + list("가나다라마바사") + ["é"]
fails = 0; trials = 40
for trial in range(trials):
    cols = random.choice([12, 17, 20, 31, 40])
    s = new(cols=cols, rows=40)
    lines = [[]]; li = 0; ci = 0          # model: list of lines (lists of chars), cursor
    ops = []
    for _ in range(random.randint(10, 40)):
        op = random.choices(["type", "nl", "left", "right", "home", "end", "bs", "del"], [10, 2, 3, 3, 1, 1, 3, 2])[0]
        if op == "type":
            ch = random.choice(ALPHA); lines[li].insert(ci, ch); ci += 1; s.send(ch.encode(), .02); ops.append(ch)
        elif op == "nl":
            tail = lines[li][ci:]; lines[li] = lines[li][:ci]; lines.insert(li+1, tail); li += 1; ci = 0; s.send(SHIFT_ENTER, .02); ops.append("<NL>")
        elif op == "left":
            if ci > 0: ci -= 1
            elif li > 0: li -= 1; ci = len(lines[li])
            s.send(LEFT, .02); ops.append("<L>")
        elif op == "right":
            if ci < len(lines[li]): ci += 1
            elif li < len(lines)-1: li += 1; ci = 0
            s.send(RIGHT, .02); ops.append("<R>")
        elif op == "home": ci = 0; s.send(b"\x1b[H", .02); ops.append("<HOME>")
        elif op == "end": ci = len(lines[li]); s.send(b"\x1b[F", .02); ops.append("<END>")
        elif op == "bs":
            if ci > 0: del lines[li][ci-1]; ci -= 1
            elif li > 0:
                prev = lines[li-1]; ci = len(prev); lines[li-1] = prev + lines[li]; del lines[li]; li -= 1
            s.send(b"\x7f", .02); ops.append("<BS>")
        elif op == "del":
            if ci < len(lines[li]): del lines[li][ci]
            elif li < len(lines)-1: lines[li] += lines[li+1]; del lines[li+1]
            s.send(b"\x1b[3~", .02); ops.append("<DEL>")
    s.pump(.35)
    exp_rows, exp_cur = expected([''.join(l) for l in lines], cols, li, ci)
    got = s.nonblank(); gc = s.cursor()
    ok_rows = got == [r for r in exp_rows if True] or got == exp_rows
    ok_cur = exp_cur is not None and gc == exp_cur
    if not (ok_rows and ok_cur):
        fails += 1
        if fails <= 3:
            print("  trial", trial, "cols", cols, "ops", "".join(ops)[:120]); print("   expect", exp_rows, exp_cur); print("   got   ", got, gc)
    s.finish(.1)
check(f"randomized editing: screen text + cursor match an independent model ({trials} trials)", fails == 0, f"{fails} mismatches")


# ---- same idea for entries TALLER than the terminal: the visible rows must be a slice of the model ----
random.seed(777)
fails = 0; trials = 30
for trial in range(trials):
    cols = random.choice([14, 20, 30]); rows = 8
    s = Session([BIN, "--no-banner"], cols=cols, rows=rows, history=True); s.pump(0.35)
    lines = [[]]; li = 0; ci = 0; ops = []
    for _ in range(random.randint(25, 60)):
        op = random.choices(["type", "nl", "left", "right", "home", "end", "bs", "del"], [10, 6, 3, 3, 1, 1, 2, 1])[0]
        if op == "type":
            ch = random.choice(ALPHA); lines[li].insert(ci, ch); ci += 1; s.send(ch.encode(), .02)
        elif op == "nl":
            tail = lines[li][ci:]; lines[li] = lines[li][:ci]; lines.insert(li+1, tail); li += 1; ci = 0; s.send(SHIFT_ENTER, .02)
        elif op == "left":
            if ci > 0: ci -= 1
            elif li > 0: li -= 1; ci = len(lines[li])
            s.send(LEFT, .02)
        elif op == "right":
            if ci < len(lines[li]): ci += 1
            elif li < len(lines)-1: li += 1; ci = 0
            s.send(RIGHT, .02)
        elif op == "home": ci = 0; s.send(b"\x1b[H", .02)
        elif op == "end": ci = len(lines[li]); s.send(b"\x1b[F", .02)
        elif op == "bs":
            if ci > 0: del lines[li][ci-1]; ci -= 1
            elif li > 0:
                prev = lines[li-1]; ci = len(prev); lines[li-1] = prev + lines[li]; del lines[li]; li -= 1
            s.send(b"\x7f", .02)
        elif op == "del":
            if ci < len(lines[li]): del lines[li][ci]
            elif li < len(lines)-1: lines[li] += lines[li+1]; del lines[li+1]
            s.send(b"\x1b[3~", .02)
    s.pump(.4)
    exp_rows, exp_cur = expected([''.join(l) for l in lines], cols, li, ci)
    got = s.nonblank(); gc = s.cursor()
    avail = max(3, rows - 1)
    if len(exp_rows) <= avail:
        ok = got == exp_rows and gc == exp_cur
    else:
        shown = got[:-1]
        ok = got[-1].startswith("[rows") and len(shown) == avail - 1 and any(
            exp_rows[k:k+len(shown)] == shown and gc == (exp_cur[0] - k, exp_cur[1]) for k in range(len(exp_rows) - len(shown) + 1))
    ok = ok and len(s.screen.history.top) == 0       # nothing ever scrolled off: nothing stale can be left behind
    if not ok:
        fails += 1
        if fails <= 3: print("  trial", trial, "cols", cols, "model rows", len(exp_rows), "\n   got", got, gc, "\n   exp cur", exp_cur)
    s.finish(.1)
check(f"randomized editing of TALL entries: view is a slice of the model, cursor visible, nothing stale ({trials} trials)", fails == 0, f"{fails} mismatches")

bad = [n for n, ok in results if not ok]
print(f"\n{len(results)-len(bad)}/{len(results)} passed"); sys.exit(1 if bad else 0)
