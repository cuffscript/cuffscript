# The shell's architecture

`cuffsh` is a front end (see [Architecture](architecture.md#front-ends)): it reads what you type, hands text to the engine and
prints what comes back. Everything about the terminal is in `cli/`; the engine is used only through `cuff::Session`.

## Files

| File | Owns |
| :--- | :--- |
| `main.cpp` | Command-line parsing (the engine flags through `EngineOptionsCli.h`, the shell's own here), choosing between the interactive and the piped mode, and exit codes. |
| `Repl.h` | The loop: show the prompt, take an entry, run `:` commands, hand everything else to the `Session`, print the outcome. It contains no parser, no interpreter and no error formatting. |
| `LineEditor.h` | The prompt editor: raw per-key input, the buffer, wrapping and redrawing, history, paste handling, and the keyboard protocols. |
| `Terminal.h` | Terminal detection, colour and escape-sequence setup, the terminal width, and the signal and Ctrl+C handling that governs what happens while code runs. |
| `Interrupt.h` | One flag, set while `Session::run` is executing, that tells the Ctrl+C handler whether a computation is in progress. |
| `Platform.h` | Everything that differs between operating systems: the command line as UTF-8, reading files and standard input, whether standard input is a console, and converting a UTF-8 path to the encoding the engine expects. |
| `Text.h` | Trimming and the sanitising of text that goes to the screen. |
| `Width.h` | Names the width helpers the editor uses; the tables themselves are the engine's (`engine/common/Width.h`), shared with the error reporter. |
| `Banner.h`, `Version.h` | The banner and the version (checked against the npm package by `tools/docs.py`). |

## The loop

```text
read an entry (LineEditor)  ->  starts with ':'?  ->  a session command (Repl)
                                   no
                                   v
          Session::run(entry)  ->  Outcome { success, error }
                                   v
                    print the error, if any, exactly as given
```

An entry is **exactly what was typed**. The shell does not decide that something "looks unfinished": it hands the text to the
engine, which reports its own error if the text is incomplete. That is why Shift+Enter exists, and why there is no
continuation-prompt logic to keep in step with the grammar.

Because the shell holds one `Session` for its whole life, a function declared in one entry can be called by the next, and an
error inside it is drawn from the entry that declared it ([source locations](source-locations.md#sourceref-which-text)).
`:reset` calls `Session::reset`. `:load` reads a file and runs it as one entry. Startup `file.cuff` is run the same way.

## Piped input

If standard input is not a terminal, the shell does not start the editor. It reads the whole input and runs it as a single entry,
the way `cuffc` reads a script from standard input, and prints a failure as `ERROR: ...` on standard error. A test compares the
two over every script in the repository and requires identical text. If the terminal exists but cannot move the cursor
(`TERM=dumb`), a plain line prompt is used.

## Keyboard input

The editor reads raw keys, which is the only way to see Shift+Enter.

- **Windows:** console input records carry the Shift state of Enter directly.
- **Linux and macOS:** the editor asks the terminal whether it speaks the Kitty keyboard protocol and enables it only if the
  terminal answers. Otherwise Shift+Enter is indistinguishable from Enter, and Alt+Enter is the newline key. Bracketed paste turns
  a multi-line paste into one entry.
- On a terminal in raw mode, the editor restores the original mode on every way out: normal exit, an error, SIGTERM and the
  terminal closing.

Typed and pasted text has control characters and bidirectional-override characters removed before it reaches the buffer, so that
pasting from a web page cannot hide what is about to run.

## Redrawing

The editor owns the lines it drew. After each key it redraws from the first changed row, using the width of each character (Hangul
and other wide characters take two columns, combining marks none) to know where the cursor is, including across a wrapped line.
An entry taller than the window is shown as a scrolling view around the cursor, with a status line.

## Ctrl+C

The engine cannot be told to stop a computation once it has started. At the prompt, Ctrl+C cancels the entry (the editor sees the
raw key). While `Session::run` is executing (`g_programRunning`), the handler in `Terminal.h` ends the process with status 130,
because that is the only safe way out. `--timeout` is what keeps this rare: the engine ends a runaway entry with `E6-002` and the
session survives.

## Windows details

The command line is read as UTF-16 and converted to UTF-8; paths are converted back for the engine with `platform::enginePath`.
Standard input is read as UTF-16 from the console and converted, so `input()` returns UTF-8. Under Git Bash (mintty) a Windows
program has no console, which the shell detects and reports. The executable is linked statically so it needs no runtime DLLs.

## Tests

`cli/tests/` drives the real executable on a pseudo-terminal and checks the emulated screen; see [Testing](../contributing/testing.md#the-shell-suite).
