import sys, os, time, signal, termios, tempfile
from harness import *

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cuffsh"))
results = []

def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "   <-- " + str(detail)))

def new(cols=80, rows=24, args=("--no-banner",), env=None):
    s = Session([BIN, *args], cols=cols, rows=rows, env_extra=env)
    s.pump(0.3)
    return s

CTRL = lambda ch: bytes([ord(ch) - 64])

# 1 basic
s = new(); s.send("print(1+2)\r", 0.4)
check("basic run prints 3", s.nonblank() == [">> print(1+2)", "3", ">>"], s.nonblank()); s.send(":exit\r", .3); s.finish(.2)

# 2 long wrapped line: type, backspace, verify exactly one entry drawn
s = new(cols=30, rows=12)
s.send('print("' + "x"*43 + '")', 0.3)
exp = ['>> print("' + "x"*20, "x"*23 + '")']
check("wrap: typed long line draws once", s.nonblank() == exp, s.nonblank())
s.send(b"\x7f"*5, 0.3)
exp2 = ['>> print("' + "x"*20, "x"*20]
check("wrap: backspace across wrapped line redraws cleanly", s.nonblank() == exp2, s.nonblank())
s.send(b"\x7f"*33, 0.3)
check("wrap: backspace back across the wrap boundary", s.nonblank() == ['>> print("' + "x"*7], s.nonblank())
s.finish(.2)

# 3 exactly-full row (deferred wrap) then more typing
s = new(cols=20, rows=10)
s.send("a"*17, 0.3)   # prompt(3)+17 = 20 = exactly full
check("full row: exactly full line stays one row", s.nonblank() == [">> " + "a"*17], s.nonblank())
s.send("b", 0.3)
check("full row: next char wraps", s.nonblank() == [">> " + "a"*17, "b"], s.nonblank())
s.send(b"\x7f", .3)
check("full row: backspace back to full row", s.nonblank() == [">> " + "a"*17], s.nonblank())
s.finish(.2)

# 4 Korean wide chars wrapping (cols=20 => 17 cols after prompt => 8 wide chars per row + 1 col spare)
s = new(cols=20, rows=10)
s.send(("가나다라마바사아자차").encode(), .3)
t = s.nonblank()
check("wide: 10 hangul over 20 cols wraps whole characters", t == [">> 가나다라마바사아", "자차"], t)
s.send(b"\x7f"*3, .3)
t = s.nonblank()
check("wide: backspace removes whole hangul chars + clean redraw", t == [">> 가나다라마바사"], t)
s.finish(.2)

# 5 Left / Right / insert / cursor position
s = new(); s.send("ac", .2); s.send(LEFT, .2); s.send("b", .2)
check("edit: insert in the middle", s.nonblank() == [">> abc"] and s.cursor() == (0, 5), (s.nonblank(), s.cursor()))
s.send(b"\x1b[H", .2); check("edit: Home", s.cursor() == (0, 3), s.cursor())
s.send(b"\x1b[F", .2); check("edit: End", s.cursor() == (0, 6), s.cursor())
s.send(b"\x1b[H" + b"\x1b[3~", .3); check("edit: Delete removes char under cursor", s.nonblank() == [">> bc"], s.nonblank())
s.send(CTRL('E') + CTRL('U'), .3); check("edit: Ctrl+U kills to line start", s.nonblank() == [">>"], s.nonblank())
s.send("foo bar baz", .2); s.send(CTRL('W'), .3); check("edit: Ctrl+W deletes previous word", s.nonblank() == [">> foo bar"], s.nonblank())
s.send(CTRL('A') + CTRL('K'), .3); check("edit: Ctrl+K kills to end", s.nonblank() == [">>"], s.nonblank())
s.finish(.2)

# 6 multi-line editing with Up/Down + Shift+Enter
s = new()
s.send("aaa" + SHIFT_ENTER.decode() + "bbb" + SHIFT_ENTER.decode() + "ccc", .4)
check("multi: three lines drawn with prompts", s.nonblank() == [">> aaa", ".. bbb", ".. ccc"], s.nonblank())
s.send(UP, .2); check("multi: Up moves to previous line", s.cursor() == (1, 6), s.cursor())
s.send(UP, .2); check("multi: Up again to first line", s.cursor() == (0, 6), s.cursor())
s.send("X", .3); check("multi: typing inserts at the cursor", s.nonblank() == [">> aaaX", ".. bbb", ".. ccc"], s.nonblank())
s.send(DOWN + DOWN, .3); check("multi: Down moves back down", s.cursor()[0] == 2, s.cursor())
s.finish(.2)

# 7 submit with the cursor on an upper line: output must land BELOW the entry
s = new()
s.send('if 1 is 1 do:' + SHIFT_ENTER.decode() + '    print("hi")' + SHIFT_ENTER.decode() + 'end', .4)
s.send(UP + UP, .3); s.send(b"\r", .5)
t = s.nonblank()
check("submit from upper line: output below entry, entry intact", t[:3] == [">> if 1 is 1 do:", "..     print(\"hi\")", ".. end"] and t[3] == "hi", t)
s.send(":exit\r", .3); s.finish(.2)

# 8 Kitty / modifyOtherKeys key reports
s = new(); s.send("abc", .2); s.send(b"\x1b[99;5u", .3)
check("kitty: Ctrl+C cancels the entry", s.nonblank() == [">> abc", ">>"], s.nonblank())
s.send(b"\x1b[100;5u", .5); check("kitty: Ctrl+D on empty prompt exits", s.rc == 0, s.rc); s.finish(.2)
s = new(); s.send("abc", .2); s.send(b"\x1b[27;5;99~", .3)
check("modifyOtherKeys: Ctrl+C cancels the entry", s.nonblank() == [">> abc", ">>"], s.nonblank()); s.finish(.2)
s = new(); s.send("a", .2); s.send(b"\x1b[27;2;13~", .2); s.send("b", .3)
check("modifyOtherKeys: Shift+Enter adds a line", s.nonblank() == [">> a", ".. b"], s.nonblank()); s.finish(.2)
s = new(); s.send("a", .2); s.send(b"\x1b\r", .2); s.send("b", .3)
check("Alt+Enter adds a line", s.nonblank() == [">> a", ".. b"], s.nonblank()); s.finish(.2)
s = new(); s.send("a", .2); s.send(b"\x1b[13;2:1u", .2); s.send("b", .3)
check("kitty: Shift+Enter with event-type subparam", s.nonblank() == [">> a", ".. b"], s.nonblank()); s.finish(.2)

# 9 Tab
s = new(); s.send("a\tb", .3); check("Tab inserts 4 spaces", s.nonblank() == [">> a    b"], s.nonblank()); s.finish(.2)

# 10 bracketed paste
s = new()
s.send(b"\x1b[200~print(1)\nprint(2)\x1b[201~", .5)
check("paste: multi-line paste is NOT run line by line", s.nonblank() == [">> print(1)", ".. print(2)"], s.nonblank())
s.send(b"\r", .5); check("paste: Enter runs the whole pasted block", s.nonblank()[-3:] == ["1", "2", ">>"], s.nonblank()); s.finish(.2)
s = new(); s.send(b"\x1b[200~a\r\nb\rc\x1b[201~", .5)
check("paste: CRLF and CR become single newlines", s.nonblank() == [">> a", ".. b", ".. c"], s.nonblank()); s.finish(.2)
s = new(); s.send(b"\x1b[200~ab\x07\x1b[31mcd\x1b[201~", .5)
check("paste: control bytes / escape sequences are stripped", s.nonblank() == [">> abcd"], s.nonblank()); s.finish(.2)
s = new(); s.send(b"\x1b[200~a\xe2\x80\xaeb\xc2\x85c\x1b[201~", .5)
check("paste: bidi-override / C1 control chars are dropped", s.nonblank() == [">> abc"], s.nonblank()); s.finish(.2)
big = "x" * 20000
s = new(cols=100, rows=30); t0 = time.time(); s.send(b"\x1b[200~" + big.encode() + b"\x1b[201~", 1.0)
check("paste: 20 KB paste is fast (<5s) and complete", time.time() - t0 < 5 and sum(len(l) for l in s.text()) >= 2000, time.time() - t0); s.finish(.2)
s = new(cols=100, rows=30); t0 = time.time(); s.send(big.encode(), 2.0)   # same, but WITHOUT bracketed paste
check("typed-ahead 20 KB burst is not quadratic-slow", time.time() - t0 < 6, time.time() - t0); s.finish(.2)

# 11 history
s = new()
s.send("print(111)\r", .4); s.send("print(222)\r", .4)
s.send(UP, .2); check("history: Up recalls last", s.nonblank()[-1] == ">> print(222)", s.nonblank())
s.send(UP, .2); check("history: Up again recalls older", s.nonblank()[-1] == ">> print(111)", s.nonblank())
s.send(DOWN + DOWN, .2); check("history: Down back to empty draft", s.nonblank()[-1] == ">>", s.nonblank())
s.send("print(\"a\")" + SHIFT_ENTER.decode() + "print(\"b\")\r", .5)
s.send(UP, .3); t = s.nonblank(); check("history: multi-line entry recalled whole", t[-2:] == [">> print(\"a\")", ".. print(\"b\")"], t)
s.send(":exit\r", .3); s.finish(.2)

# 12 Ctrl+L
s = new(); s.send("print(1)\r", .3); s.send("abc", .2); s.send(CTRL('L'), .4)
check("Ctrl+L clears screen and redraws entry", s.nonblank() == [">> abc"], s.nonblank()); s.finish(.2)

# 13 blank Enter
s = new(); s.send("\r\r", .3); check("blank Enter just gives a fresh prompt", s.nonblank() == [">>", ">>", ">>"], s.nonblank()); s.finish(.2)

# 14 errors printed exactly like cuffc
s = new(); s.send("print(1/0)\r", .4)
t = s.nonblank(); check("runtime error uses engine format with ERROR: prefix", t[1].startswith("ERROR: [E4006] Runtime Error at line 1"), t)
s.send("if 1 is 1 do:\r", .4); t = s.nonblank()
check("incomplete input -> engine's own message, no auto-continue", any(l.startswith("ERROR: [E2001] Syntax Error") and "expected 'end'" in l for l in t), t)
s.send(":exit\r", .3); s.finish(.2)

# 15 Ctrl+C at prompt vs during execution
s = new(); s.send("abc", .2); s.send(b"\x03", .3)
check("Ctrl+C at prompt cancels entry only", s.rc is None and s.nonblank() == [">> abc", ">>"], s.nonblank()); s.finish(.2)
s = new(args=("--no-banner", "--timeout", "0"))
s.send("set number c to 0" + SHIFT_ENTER.decode() + "loop while true do:" + SHIFT_ENTER.decode() + "change c to c + 1" + SHIFT_ENTER.decode() + "end\r", .6)
s.send(b"\x03", .6)
check("Ctrl+C during execution exits with 130", s.rc == 130, s.rc); s.finish(.2)

# 16 engine timeout
s = new(args=("--no-banner", "--timeout", "300"))
s.send("set number c to 0" + SHIFT_ENTER.decode() + "loop while true do:" + SHIFT_ENTER.decode() + "change c to c + 1" + SHIFT_ENTER.decode() + "end\r", 1.5)
t = s.nonblank(); check("timeout stops runaway loop, session survives", any("E6002" in l for l in t) and t[-1] == ">>", t)
s.send('print("alive")\r', .4); check("session usable after timeout", "alive" in s.nonblank(), s.nonblank()); s.send(":exit\r", .3); s.finish(.2)

# 17 terminal is restored when killed while reading
s = new(); s.pump(.3)
raw_flags = termios.tcgetattr(s.fd)[3]
check("(sanity) terminal is raw while reading", not (raw_flags & termios.ICANON), raw_flags)
os.kill(s.pid, signal.SIGTERM); s.pump(.6)
flags = termios.tcgetattr(s.fd)[3]
check("SIGTERM restores echo/canonical mode", (flags & termios.ICANON) and (flags & termios.ECHO), flags)
check("SIGTERM turns the extra key modes back off", b"\x1b[?2004l" in s.raw, s.raw[-60:]); s.finish(.2)

# 18 TERM=dumb -> plain line mode, no escape garbage
s = new(env={"TERM": "dumb"}); s.send("print(5)\r", .4)
check("TERM=dumb: usable, no cursor-control escapes", s.nonblank()[:2] == [">> print(5)", "5"] and b"\x1b[" not in s.raw, (s.nonblank(), s.raw[:80]))
s.send(":exit\r", .3); s.finish(.2)

# 19 stdout redirected, stdin+stderr are the terminal
out_fd, out = tempfile.mkstemp()
os.close(out_fd)
s = Session(["sh", "-c", f"exec {BIN} --no-banner > {out}"]); s.pump(.4)
s.send("print(42)\r", .5)
check("stdout redirected: prompt+echo still visible (on stderr)", ">> print(42)" in s.nonblank(), s.nonblank())
s.send(":exit\r", .3); s.finish(.3)
check("stdout redirected: program output went to the file", open(out).read().strip() == "42", open(out).read())
os.unlink(out)

# 20 banner first, then startup file output
f_fd, f = tempfile.mkstemp(suffix=".cuff")
with os.fdopen(f_fd, "w") as fh:
    fh.write('print("from file")\n')
s = Session([BIN, f]); s.pump(.6)
t = s.nonblank(); i_logo = next((i for i, l in enumerate(t) if "___" in l), -1); i_out = t.index("from file") if "from file" in t else -1
check("banner printed before the startup file's output", 0 <= i_logo < i_out, t); s.send(":exit\r", .3); s.finish(.2)

# 21 :load with quoted path, and meta-commands
s = new(); s.send(f':load "{f}"\r', .5); check(":load accepts a quoted path", "from file" in s.nonblank(), s.nonblank())
s.send(":bogus\r", .3); check("unknown command message", any(l.startswith("Error: Unknown command") for l in s.nonblank()), s.nonblank()); s.send(":exit\r", .3); s.finish(.2)
os.unlink(f)

# 22 Korean typing + backspace + submit
s = new(); s.send('print("안녕")'.encode(), .3); s.send(b"\x7f"*3, .3); s.send('하")'.encode(), .3); s.send(b"\r", .4)
check("hangul typing/backspace/run", s.nonblank()[:2] == ['>> print("안하")', "안하"], s.nonblank()); s.send(":exit\r", .3); s.finish(.2)

# 23 junk keys are swallowed
s = new(); s.send(b"\x1b[15~\x1b[1;5P\x1bOP\x1b[Z", .3); s.send("ok", .3)
check("function keys / unknown sequences never leak into the buffer", s.nonblank() == [">> ok"], s.nonblank()); s.finish(.2)

# 24 lone ESC then a key doesn't swallow the key
s = new(); s.send(b"\x1b", .3); s.send("z", .3); check("lone Escape is ignored, next key works", s.nonblank() == [">> z"], s.nonblank()); s.finish(.2)

# 25 resize-width differences: very narrow terminal
s = new(cols=8, rows=12); s.send("abcdefghij", .3); check("narrow terminal (8 cols) wraps without garbage", s.nonblank() == [">> abcde", "fghij"], s.nonblank()); s.finish(.2)


# 26 entries taller than the terminal (regression: every redraw used to leave a stale copy behind)
def tall_session(rows=10, cols=40, n=30):
    s = Session([BIN, "--no-banner"], cols=cols, rows=rows, history=True); s.pump(0.35)
    code = "\n".join(f"print({i})" for i in range(1, n + 1))
    s.send(b"\x1b[200~" + code.encode() + b"\x1b[201~", 0.6)
    return s

def dup_count(s, n=30):
    tr = s.transcript()
    return max((sum(1 for l in tr if l.endswith(f"print({i})")) for i in range(1, n + 1)), default=0)

s = tall_session()
t = s.nonblank()
check("tall: paste shows a viewport that fits the window, ending at the cursor", len(t) <= 10 and t[-2].endswith("print(30)") and t[-1].startswith("[rows"), t)
for key in (UP, UP, LEFT, RIGHT, DOWN, b"\x1b[H", b"\x1b[F"):
    s.send(key, .2)
check("tall: moving the cursor never duplicates code on screen or in scrollback", dup_count(s) <= 1, [l for l in s.transcript() if "print(30)" in l])
for _ in range(29): s.send(UP, .08)
t = s.nonblank()
check("tall: scrolling to the top shows the first lines, once", t[0] == ">> print(1)" and dup_count(s) <= 1 and s.cursor()[0] == 0, (t[:3], s.cursor()))
check("tall: status line says which rows are shown", t[-1].startswith("[rows 1-8 of 30"), t[-1])
s.send(b"\r", 1.0)
tr = s.transcript()
full = [l for l in tr if l.startswith((">> print(", ".. print("))]
check("tall: Enter prints the WHOLE entry once, then runs it", len(full) == 30 and all(any(l.endswith(f"print({i})") for l in full) for i in range(1, 31)) and tr.count("30") >= 1, (len(full), tr[-6:]))
check("tall: output of all 30 lines appears", all(str(i) in tr for i in range(1, 31)), tr[-12:])
s.send(":exit\r", .4); s.finish(.2)

s = tall_session()
s.send(b"\x03", .4)
t = s.nonblank()
check("tall: Ctrl+C erases the scrolling view and leaves a clean prompt", t[-1] == ">>" and not any("print(" in l for l in t[-2:]), t)
s.send("print(99)\r", .5); check("tall: usable after cancelling", "99" in s.nonblank(), s.nonblank()); s.send(":exit\r", .3); s.finish(.2)

s = tall_session(rows=8, cols=20, n=25)   # narrow AND short: wrapped rows count toward the height
for key in (UP, UP, UP, LEFT, DOWN):
    s.send(key, .15)
check("tall+wrapped: no duplicated code", dup_count(s, 25) <= 1, s.transcript()[-10:])
s.send(b"\x7f"*40, .5)
check("tall: backspacing a tall entry back down to fit redraws cleanly", dup_count(s, 25) <= 1 and s.nonblank()[-1] != "" , s.nonblank()); s.finish(.2)

bad = [n for n, ok in results if not ok]
print(f"\n{len(results)-len(bad)}/{len(results)} passed")
sys.exit(1 if bad else 0)
