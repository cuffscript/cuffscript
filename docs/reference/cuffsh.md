# `cuffsh`: the interactive shell

`cuffsh` is a read-evaluate-print loop for CuffScript. Variables and functions persist from one entry to the
next. Build it with `cli/build.sh` (Linux and macOS) or `cli\build.bat` (Windows); see
[Building](../contributing/building.md).

```text
>> print(1 + 1)
2
>> set returnable func sq(n) do:
..     return n * n
.. end
>> print(sq(6))
36
```

**Enter runs what you have typed. Shift+Enter or Alt+Enter adds a new line instead.** The shell does not
guess that something "looks unfinished": whatever you submit is parsed and run exactly once, and any error is
the engine's own report, in the same form `cuffc` prints.

## Options

<!-- BEGIN GENERATED: help-cuffsh -->
```text
CuffScript Interactive Shell <version>
Enter runs what you've typed; Shift+Enter (or Alt+Enter) inserts a newline
(for if/loop/func and other multi-line code) instead of running it yet.

Usage: cuffsh [options] [file.cuff]
  file.cuff              run this script in the session before the prompt
                         appears
  --root <dir>           allow 'use ... from' to load modules anywhere under <dir>
  --max-steps <n>        stop after <n> loop iterations + function calls (0 = unlimited)
  --timeout <ms>         stop after <ms> milliseconds of run time (0 = unlimited)
  --no-network           switch DLC:network off (calls into it raise E5-005)
  --allow-private-network
                         let DLC:network reach loopback/private/link-local addresses
  --no-filesystem        switch DLC:filesystem off (calls into it raise E5-005)
  --no-color             disable colored output
  --no-banner            skip the startup banner
  -h, --help             show this help and exit
  --version              show version information and exit

Defaults: modules may be loaded from the current directory, and each entry stops
after 15000 ms. The engine cannot cancel a running entry, so --timeout is the
main safety net for a runaway loop; with --timeout 0, Ctrl+C during execution
closes the whole shell instead (see :help).
```
<!-- END GENERATED: help-cuffsh -->

`file.cuff` runs in the session before the prompt appears. The engine options are the ones `cuffc` takes
(see [`cuffc`](cuffc.md#options)); `--no-color`, `--no-banner`, `--version` and `-h` are the shell's own.

## Editing

| Key | Does |
| :--- | :--- |
| Enter | Run the whole entry. |
| Shift+Enter, Alt+Enter | Insert a newline (multi-line code). |
| Left, Right, Home, End, Delete, Backspace | Edit anywhere in the entry. |
| Ctrl+A / E / K / U / W / L | Line start / end / kill to end / kill to start / delete word / clear screen. |
| Ctrl+B / F, Ctrl+H | Left / right / backspace, as in a Unix shell. |
| Ctrl+P / N | Up / down, as in a Unix shell. |
| Ctrl+Left, Ctrl+Right, Alt+B, Alt+F | Move by word. |
| Up, Down | Move between lines of a multi-line entry; at the first or last line, step through earlier entries. The shell remembers the most recent entries of the session, and an entry identical to the one before it is stored once. |
| Tab | Insert four spaces. |
| Ctrl+C | Cancel the entry. |
| Ctrl+D (Ctrl+Z on Windows) | Leave, when the prompt is empty. |

If an entry is taller than the terminal window (a long paste), the screen shows a scrolling view around the
cursor with a `[rows A-B of N]` status line, and the whole entry is printed once when you press Enter.

Pasting multi-line code gives you **one** entry to review (bracketed paste on Linux and macOS; on Windows an
Enter that already has more typing queued behind it counts as a newline). Long lines that wrap, and wide
characters (Korean, CJK, emoji), are redrawn correctly. Control characters and bidirectional-override
characters are dropped from typed and pasted text.

## Session commands

A line that starts with a colon is a command; it can never be valid CuffScript, so there is no ambiguity.

| Command | Does |
| :--- | :--- |
| `:help`, `:h`, `:?` | List the commands and keys. |
| `:load <path>` | Run a file in the current session. The path may be quoted. Without a path the shell answers `usage: :load <path>`. |
| `:reset` | Forget everything the session declared; the shell answers `Session reset.` |
| `:clear`, `:cls` | Clear the screen. |
| `:version` | Show the version. |
| `:exit`, `:quit`, `:q` | Leave. |

Any other word after the colon is reported as `Unknown command` and the session is untouched.

Functions may be redefined. Errors in a function that was declared in an earlier entry are shown against
the entry that declared it, not against the one that called it.

## When input is not a terminal

If standard input is not a terminal (`echo ... | cuffsh`, `cuffsh < file`) there is no prompt: the whole input
runs as one script, exactly as `cuffc` reads it from standard input, and an error is printed as `ERROR: ...`
on standard error. The exit status is the one `cuffc` gives: 0 when the script ran, 1 when it failed. An interactive session
always ends with 0. If the terminal cannot do cursor movement (`TERM=dumb`, very old Windows consoles), a plain
line prompt is used instead, without Shift+Enter.

## Does Shift+Enter work in my terminal?

- **Windows:** yes. The console reports the Shift state of Enter directly. Git Bash (mintty) gives Windows
  programs no console at all; `cuffsh` says so and exits. Run it from cmd, PowerShell or Windows Terminal, or as
  `winpty cuffsh.exe`.
- **Linux and macOS:** only in terminals that speak the Kitty keyboard protocol (Kitty, WezTerm, foot, Ghostty,
  recent iTerm2, ...). The shell asks the terminal first and turns the protocol on only if it answers. In plain
  xterm, GNOME Terminal, macOS Terminal.app, or inside tmux, Shift+Enter cannot be told from Enter: **use
  Alt+Enter** there (in Terminal.app, enable "Use Option as Meta key").

## Ctrl+C and runaway code

The engine cannot be asked to stop a computation once it has started. So:

- **At the prompt**, Ctrl+C cancels what you have typed.
- **While code is running**, Ctrl+C **exits the shell** (status 130): there is no safe way to stop just the
  computation.

`--timeout` keeps that rare. A runaway loop usually hits the engine's own limit (`E6-002`) and fails just that
entry, with the session intact. The clock is checked every few hundred loop iterations or calls, and time spent
waiting inside `input()` counts; for programs that wait on the keyboard for a long time, use `--timeout 0`.

The terminal is restored on every way out, including being killed (`SIGTERM`, or the terminal closing).
