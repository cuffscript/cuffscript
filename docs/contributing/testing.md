# Testing

There are three test suites, one tool test and one documentation check. All of them run in CI; `.github/workflows/` says which on what.

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
| Cases | `tests/cases/<area>/` | `name.cuff` and `name.expected`; the output must match exactly. |
| Errors | `tests/errors/<area>/` | `name.cuff` that must fail; `name.expected_code` (optional) holds the error code it must fail with. |
| Examples | `examples/` | Output must match `name.expected` where there is one; otherwise it only has to run. |
| Example errors | `examples/error_cases/` | Must fail with the code in `name.expected_code`. |

Environment variables: `CUFFC` (the binary to test), `CXX` (the compiler for the unit tests) and `UNIT_FLAGS` (their
optimisation and extra flags). `run.sh` falls back to `gtimeout`, or to no timeout, where `timeout` is missing, as on a
stock macOS. `run.ps1` works on Windows PowerShell 5.1 and on PowerShell 7 on any operating system, decodes output as UTF-8,
normalises line endings and links the unit tests the way `Makefile.win` does.

### Adding a test

- **A unit test:** drop a `.cpp` with a `main()` into `tests/unit/`. It is compiled with `-I.` from the root, so include
  engine headers by path (`#include "engine/CuffEngine.h"`).
- **A success case:** pick the area the test is about (the list is in [`tests/README.md`](../../tests/README.md#areas)) and add
  `tests/cases/<area>/my_case.cuff`, then record the output after reading it once:
  `./cuffc tests/cases/<area>/my_case.cuff > tests/cases/<area>/my_case.expected`. Do not add a case whose output varies (random
  numbers, timestamps). A file a case loads, such as a module, goes in a `fixtures/` folder beside it; the runners never run
  that folder on its own.
- **An error case:** add `tests/errors/<area>/my_error.cuff`, run it, and write the code it prints into
  `tests/errors/<area>/my_error.expected_code` (for example `E4-006`). Options for the run go in `my_error.args`.

### What the tests leave out

`python3 tests/coverage.py` asks the engine what it contains and fails when

- an error code the engine can raise is the expected code of no test (a unit test or an expected output that names the code
  counts too);
- a built-in or library function is called by no test or example, or a built-in method is never called as `.name(`;
- a production of the [grammar](../reference/grammar.md) is in no area of the map in `tests/README.md`, or the map names an
  area that has no tests.

A gap that cannot be closed (a code no script can reach) is written in `tests/coverage_exceptions.txt` with the reason, and the
checker then shows it as accepted. Add a new error code, function or grammar production together with its test, and run the
checker before sending a change. `--list` also prints the first tests that cover each item.

### What the unit tests are for

| File | Protects |
| :--- | :--- |
| `regex_test.cpp` | The pattern engine on its own, without the language around it. |
| `syntax_test.cpp` | The grammar, production by production: what the parser accepts, what it rejects, and the hint it gives for a spelling it recognises but does not accept. The programs are parsed, never run. |
| `classes_test.cpp` | The object model: that each interpreter owns its classes, what a `Session` keeps between entries, the order in which an object is made, how `with` looks a name up, private access, and how errors become objects. |
| `builtins_test.cpp` | The built-in functions that touch the process's streams or shared state: `input()` and `DLC:random`. |
| `error_snippet_test.cpp` | The error report: caret placement, which line an error is on, which text it is drawn from (a module, an earlier entry of a `Session`, for every way a function can fail), and that a reported line and column always agree with the offset. |
| `phenomena_test.cpp` | What has been printed when an error ends a run: a syntax error stops everything, a runtime error keeps the earlier lines, a waiting `async` call is dropped. The rest of [Phenomena](../guide/phenomena.md) is in `tests/cases/phenomena/`. |
| `limits_test.cpp` | The limits and how they relate: long chains, the call depth, what does and does not use native stack, the stack plan, how the stack that is left is measured and kept (per thread, and anew when the stack limit changes), a small real thread stack, and the limits on classes, members and inheritance depth. |
| `dlc_guards_test.cpp` | The network and filesystem guards: which addresses a request may reach, how a URL is read and where a redirect leads, a request made while the process has more than 1024 files open, and what the filesystem functions may touch. |
| `locale_test.cpp` | That a script means the same whatever locale the host process has set: numbers read and written with `.` under a locale that writes `,`, and letter case under one in which the upper case of `i` is not `I`. Each half runs where the machine has such a locale and says when it was skipped. |
| `threads_test.cpp` | Several threads, each with its own `Session`, parsing and running at the same time, including programs that declare classes and make objects. It exercises the two locked structures the sessions share: the identifier table and the generator behind `DLC:random`. Built with `-fsanitize=thread` it reports a data race in either. |

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

Leak detection is off on purpose: a list, map or object that refers to itself is reference counted and never freed (see
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

## The machine's other routes

The bytecode machine reaches the same result by more than one route, and an ordinary program takes mostly one of them. Two
build switches turn the other one on everywhere, so that the whole suite runs through it:

| Build | What it changes | Why it is tested |
| :--- | :--- | :--- |
| `-DCUFF_DYNAMIC_NAMES` | The compiler proves nothing about names: every variable is looked up at run time and its declared type is read from the register's meta. | In a normal build that route is only taken by a name the compiler cannot settle, such as a variable that one branch of an `if` declares. A mistake in it would otherwise show only in unusual programs. |
| `-DCUFF_VM_SWITCH` | The machine jumps between instructions through a `switch` and not through a table of labels. | It is the form a compiler other than GCC and Clang gets. |

```sh
g++ -std=c++17 -O2 -DNDEBUG -DCUFF_DYNAMIC_NAMES -o cuffc-dynamic main.cpp
CUFFC=./cuffc-dynamic UNIT_FLAGS="-O2 -DCUFF_DYNAMIC_NAMES" bash tests/run.sh
g++ -std=c++17 -O2 -DNDEBUG -DCUFF_VM_SWITCH -o cuffc-switch main.cpp
CUFFC=./cuffc-switch UNIT_FLAGS="-O2 -DCUFF_VM_SWITCH" bash tests/run.sh
```

`tests/fuzz.py` compares two builds on random programs: each program is run with both, and the output, the error report and the
exit status must be the same. The programs are made to do what people rarely write on purpose: read a name that only one branch
declared, declare a name twice with two types in two blocks, assign through `with`, link globals, load a module into a block,
raise and catch at random, call with the wrong arguments.

```sh
python3 tests/fuzz.py ./cuffc ./cuffc-dynamic                       # 2000 programs
python3 tests/fuzz.py ./cuffc ./cuffc-asan --count 500 --seed 9000  # a release build against a sanitizer build
```

A program on which the builds differ is written to `fuzz-failures/seed_<n>.cuff`, and `--seed <n> --count 1` makes it again. Run
the comparison after any change to `engine/bytecode/Compiler.h` or to `Interpreter::runLoop`, against the build from before the
change as well: two builds that should behave the same are the cheapest test there is of a change that is meant to change nothing.
The `Sanitizers` workflow runs both builds and both comparisons.

## A reduced build

`make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK` then `CUFFC=./cuffc bash tests/run.sh`: everything passes except the
`tests/errors/libraries/network_*.cuff` cases, which test the client that was left out.

## When CI fails and it passes for you

- Open the first failing step. The shell workflow prints the Python, `pyte`, `wcwidth`, compiler and `make` versions before it
  builds, which is the first thing to compare with your machine.
- A shell test that fails only on CI is usually timing. Re-run locally with `CUFFSH_TEST_SCALE=3`; if that passes, the runner is
  slow, and the workflow's own scale should be raised.
- A build that fails only on macOS is usually a missing `#include` that libc++ needs; see [Building](building.md#checking-a-build-the-way-ci-does).
- A failure that only appears on Windows is worth reproducing with a MinGW cross compiler under Wine before guessing.
