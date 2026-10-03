# cuffsh — CuffScript Interactive Shell

An interactive REPL that sits next to `cuffc` (the script runner). Nothing
under `engine/` (or anywhere else in the project) is modified — it uses the
same public API `cuffc` does (`cuff::CuffEngine::run`, `cuff::Interpreter`).
Drop this `cli/` folder into the project root as-is and build it.

```
>> print(1 + 1)
2
>> set returnable func sq(n) do:
..     return n * n
.. end
>> print(sq(6))
36
```

**Enter runs what you've typed. Shift+Enter adds a new line instead.** There
is no "this looks unfinished, let me guess and wait" behavior: whatever you
submit is parsed and run exactly once, and any error is the engine's own
message, printed the same way `cuffc` prints it (`ERROR: [E....] ...`).

## Build

The project's own `Makefile` / `Makefile.win` are untouched; build with the
script in this folder.

**Windows** (mingw-w64 g++):  `cli\build.bat`  →  `cli\cuffsh.exe`
(statically linked, so no mingw runtime DLLs to ship)

**Linux / macOS**:  `./cli/build.sh`  →  `cli/cuffsh`
(macOS: Xcode Command Line Tools are enough — it uses the system `clang++`)

Neither script touches `main.cpp` or `Makefile*`; they compile `cli/main.cpp`
against `../engine`. `-Werror` is not used in them (so a newer compiler's new
warnings can't break your build); the checks below do use it.

## Using it

```
cuffsh [options] [file.cuff]
```

`file.cuff` runs in the session before the prompt appears (after the logo).
`--root`, `--max-steps`, `--timeout`, `--no-network`, `--allow-private-network`
set the same engine options they do for `cuffc`. `--no-color`, `--no-banner`,
`--version`, `-h` are shell options.

### Editing

| Key | Does |
|---|---|
| Enter | run the whole entry |
| Shift+Enter, Alt+Enter | insert a newline (multi-line code) |
| ← → , Home/End, Delete, Backspace | edit anywhere in the entry |
| Ctrl+A / E / K / U / W / L | line start / end / kill-to-end / kill-to-start / delete word / clear screen |
| Ctrl+← →, Alt+B / F | move by word |
| ↑ ↓ | move between lines of a multi-line entry; at the first/last line, step through previous entries |
| Tab | inserts 4 spaces |
| Ctrl+C | cancel the entry |
| Ctrl+D (Ctrl+Z on Windows) | leave, when the prompt is empty |

Pasting multi-line code gives you **one** entry to review (bracketed paste on
Linux/macOS; on Windows, an Enter that already has more typing queued behind
it is treated as a newline). Long lines that wrap, and wide characters
(Korean/CJK/emoji), are redrawn correctly. Control characters and bidi
override characters are dropped from typed and pasted text.

Session commands (leading colon — never valid CuffScript, so no ambiguity):
`:help`, `:load <path>`, `:reset`, `:clear`, `:version`, `:exit` / `:quit`.
Variables and functions persist between entries (and functions may be
redefined).

If stdin is not a terminal (`echo ... | cuffsh`, `cuffsh < file`) there is no
prompt: the whole input runs as one script, exactly like `cuffc` reading from
stdin. If the terminal can't do cursor movement (`TERM=dumb`, very old Windows
consoles), a plain line prompt is used instead (no Shift+Enter there).

### Does Shift+Enter work in my terminal?

- **Windows**: yes, always — the console API reports the Shift state of Enter
  directly, whatever app hosts the console. (Git Bash/mintty gives Windows
  programs no console at all; cuffsh says so and exits — run it from cmd,
  PowerShell, Windows Terminal, or as `winpty cuffsh.exe`.)
- **Linux/macOS**: only in terminals that speak the Kitty keyboard protocol
  (Kitty, WezTerm, foot, Ghostty, recent iTerm2, ...). cuffsh asks the
  terminal first and only turns the protocol on if it answers, so nothing is
  sent to terminals that don't understand it. In plain xterm, GNOME Terminal,
  macOS Terminal.app, or inside tmux, Shift+Enter is indistinguishable from
  Enter — **use Alt+Enter** there (macOS Terminal: enable "Use Option as Meta
  key"). That's a limit of those terminals, not something a program can fix.

## Ctrl+C and runaway code

The engine can't be asked to stop a computation once it has started. So:

- **At the prompt**, Ctrl+C cancels what you've typed.
- **While code is running**, Ctrl+C **exits the shell** (code 130) — there is
  no safe way to stop just the computation.

`--timeout` (default 15000 ms) keeps that rare: a runaway loop usually hits the
engine's own limit (`E6002`) and fails just that entry, session intact. Note
the engine checks the clock only every ~1000 loop iterations/calls, and time
spent waiting inside `input()` counts — for programs that wait on the keyboard
for a long time, use `--timeout 0` (the engine's own "unlimited" default).

The terminal is restored on every way out, including being killed (SIGTERM /
terminal closed).

## Tests

```
cd cli/tests && pip install pyte wcwidth     # a terminal emulator, to check what is really on screen
./run_tests.sh [path/to/cuffsh] [--fuzz N]
```

Runs ~90 checks on a real pty — wrapping, wide characters, cursor editing, a
randomized comparison of the screen against an independent layout model,
paste, key protocols, terminal-restore on exit/kill, `input()`, piped use,
CLI flags — plus a fuzzer that feeds random/malformed terminal input. The
engine's own tests are separate (`tests/run.sh`). Also verified: builds clean
with `-Wall -Wextra -Werror` on gcc, clang+libc++ (the macOS toolchain) and
mingw-w64; AddressSanitizer/UBSan runs with the fuzzer; and every shipped
example/test script produces byte-identical output under `cuffc` and `cuffsh`.

The Windows-only code (raw console key events) can't be run on Linux; the
Windows `.exe` was run under Wine for piped/argument/startup-file behavior,
but interactive Windows behavior should still get a look on a real machine.
