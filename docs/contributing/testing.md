# Testing

There are three test suites and one documentation check. All of them run in CI; `.github/workflows/` says which on what.

| Suite | What it covers | Run it with |
| :--- | :--- | :--- |
| Engine and language | `tests/`: unit tests, success cases, error cases, the examples | `bash tests/run.sh` |
| Shell | `cli/tests/`: the interactive shell, on a real pseudo-terminal | `cli/tests/run_tests.sh` |
| Documentation | Generated pages, links, the examples in the docs | `python3 tools/docs.py check` |

Build first: `make` for the first two, plus `cli/build.sh` for the shell tests.

## The engine and language suite

`bash tests/run.sh` (Windows: `powershell -File tests\run.ps1`; the two print the same sections and summary) runs, in order:

| Section | Directory | A test is |
| :--- | :--- | :--- |
| Unit | `tests/unit/*.cpp` | A C++ file with a `main()`, compiled and run. Exit non-zero to fail; the last line of output is shown. |
| Cases | `tests/cases/` | `name.cuff` and `name.expected`; the output must match exactly. |
| Errors | `tests/errors/` | `name.cuff` that must fail; `name.expected_code` (optional) holds the error code it must fail with. |
| Examples | `examples/` | Output must match `name.expected` where there is one; otherwise it only has to run. |
| Example errors | `examples/error_cases/` | Must fail with the code in `name.expected_code`. |

Environment variables: `CUFFC` (the binary to test), `CXX` (the compiler for the unit tests) and `UNIT_FLAGS` (their
optimisation and extra flags). `run.sh` falls back to `gtimeout`, or to no timeout, where `timeout` is missing, as on a
stock macOS. `run.ps1` works on Windows PowerShell 5.1 and on PowerShell 7 on any operating system, decodes output as UTF-8,
normalises line endings and links the unit tests the way `Makefile.win` does.

### Adding a test

- **A unit test:** drop a `.cpp` with a `main()` into `tests/unit/`. It is compiled with `-I.` from the root, so include
  engine headers by path (`#include "engine/CuffEngine.h"`).
- **A success case:** add `tests/cases/my_case.cuff`, then record the output after reading it once:
  `./cuffc tests/cases/my_case.cuff > tests/cases/my_case.expected`. Do not add a case whose output varies (random numbers,
  timestamps).
- **An error case:** add `tests/errors/my_error.cuff`, run it, and write the code it prints into
  `tests/errors/my_error.expected_code` (for example `E4-006`).

### What the unit tests are for

| File | Protects |
| :--- | :--- |
| `regex_test.cpp` | The pattern engine on its own, without the language around it. |
| `error_snippet_test.cpp` | The error report: caret placement, which line an error is on, which file it is drawn from, and that a reported line and column always agree with the offset. |
| `limits_test.cpp` | The limits and how they relate: long chains, the stack plan, a small real thread stack. |
| `dlc_guards_test.cpp` | The network and filesystem guards. |
| `threads_test.cpp` | Several threads, each with its own `Session`, parsing and running at the same time. It exercises the two locked structures the sessions share: the identifier table and the generator behind `DLC:random`. Built with `-fsanitize=thread` it reports a data race in either. |

## The shell suite

```sh
python3 -m venv .venv                                    # once; .venv/ is ignored by git
.venv/bin/pip install -r cli/tests/requirements.txt      # once
PYTHON=.venv/bin/python cli/tests/run_tests.sh [path/to/cuffsh] [--fuzz N]
```

A virtual environment is the portable way to install the two packages: Debian, Ubuntu and Homebrew mark their system Python as externally managed, and `pip install` into it is refused. With
the packages installed some other way, plain `cli/tests/run_tests.sh` is enough.

On Windows the same entry point is `powershell -File cli\tests\run_tests.ps1` (also PowerShell 7 on Linux and macOS). It
has two parts. The **native checks** need nothing but PowerShell and run everywhere: piped scripts, error text identical to
`cuffc`'s, Korean text and paths, the command-line flags, the resource limits, and a comparison of `cuffsh` with `cuffc` over every
script in the repository. The **interactive suites** below are the Python ones.

The interactive suites drive `cuffsh` on a real pseudo-terminal and feed what it prints into a terminal emulator (`pyte`), so they check what
a user would see on screen: wrapping, wide characters, cursor editing, paste, key protocols, terminal restore on exit and on
being killed, `input()`, piped use, and the command-line flags. A randomised test compares the screen with an independent model of
the layout, and a fuzzer sends random and malformed terminal input. The suite also runs every script in the repository through
both `cuffc` and `cuffsh` and requires identical text (this check is skipped when `cuffc` is not built, so build it first).

| Variable | Meaning |
| :--- | :--- |
| `PYTHON` | The interpreter to use (default `python3`). |
| `CUFFSH_TEST_SCALE` | Multiplies every wait, for a slow or heavily loaded machine. `run_tests.ps1 -TimeScale N` sets it. |

The harness (`cli/tests/harness.py`) is written for the differences between operating systems' pseudo-terminals: reading a pty
whose other end has closed raises an error on Linux and returns end-of-file on macOS, a write to such a pty fails on macOS but is
accepted on Linux, and a child is not yet reaped at the moment its pty closes. All three mean "the child is exiting", and the harness
waits for it and carries on; a test that checks an exit status must not look once and conclude the child is still running.

`cli/tests/requirements.txt` lists the Python packages (`-InstallDeps` installs them for `run_tests.ps1`). The interactive
suites need a Unix pseudo-terminal and assert Unix behaviour (the terminal modes `termios` reports, `SIGINT`, `SIGTERM` and
`SIGHUP`), so they run on Linux, macOS and WSL; `run_tests.ps1` reports them as not applicable on Windows, where the native
checks and the manual checklist it prints at the end cover the shell. CI runs the native checks on Windows
(`.github/workflows/repl-test.yaml`).

## Documentation checks

`python3 tools/docs.py check` verifies that the generated pages are current, that every library function is documented and
nothing documented is missing from the code, that every relative link and heading anchor resolves, that no limit's value is
written outside the generated page, that the version is the same everywhere, and that every runnable example in the docs
produces the output shown. See [Documentation](documentation.md).

## Sanitizers

Leak detection is off on purpose: a list or map that contains itself is reference counted and never freed (see
[design notes](../internals/design-notes.md)), and some tests build exactly that.

```sh
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
g++ -std=c++17 -O1 -g $SAN -o cuffc-asan main.cpp
ASAN_OPTIONS=detect_leaks=0 CUFFC=./cuffc-asan UNIT_FLAGS="-O1 -g $SAN" bash tests/run.sh
```

ThreadSanitizer is for the threads test:

```sh
g++ -std=c++17 -O1 -g -fsanitize=thread -pthread -I. tests/unit/threads_test.cpp -o /tmp/threads && /tmp/threads
```

The `Sanitizers` workflow runs the first of these on every change to the engine or the tests. MinGW has no sanitizers, so neither is
available through `run.ps1`.

## A reduced build

`make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK` then `CUFFC=./cuffc bash tests/run.sh`: everything passes except the
`tests/errors/network_*.cuff` cases, which test the client that was left out.

## When CI fails and it passes for you

- Open the first failing step. The shell workflow prints the Python, `pyte`, `wcwidth`, compiler and `make` versions before it
  builds, which is the first thing to compare with your machine.
- A shell test that fails only on CI is usually timing. Re-run locally with `CUFFSH_TEST_SCALE=3`; if that passes, the runner is
  slow, and the workflow's own scale should be raised.
- A build that fails only on macOS is usually a missing `#include` that libc++ needs; see [Building](building.md#checking-a-build-the-way-ci-does).
- A failure that only appears on Windows is worth reproducing with a MinGW cross compiler under Wine before guessing.
