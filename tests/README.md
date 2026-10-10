# Tests

```bash
make
bash tests/run.sh
```

On Windows, without WSL/Git Bash:

```powershell
mingw32-make -f Makefile.win
powershell -File tests\run.ps1
```

`run.ps1` mirrors `run.sh` (same section headers, same pass/fail summary line, same exit
code convention) and works on Windows PowerShell 5.1 as well as PowerShell 7+ on any OS.
It decodes output as UTF-8, normalizes line endings, closes stdin for each test, and links
the unit tests with `-lws2_32` and an 8 MiB stack on Windows, as `Makefile.win` does. It can be
started from any directory, takes `CUFFC`, `CXX` and `UNIT_FLAGS` from the environment as
`run.sh` does, and lists the failed tests again at the end of its output.

Runs, in order: `tests/unit/` (standalone C++ unit tests, compiled and run directly —
the regex engine's own test suite, which can exercise it without going through the whole
language pipeline, plus engine-level checks such as the parser/stack limits and the DLC
network/filesystem guards), `tests/cases/` (exact output diff), `tests/errors/` (must fail
with a specific error code), `examples/` (exact output diff where a `.expected` exists,
otherwise just checks exit 0 — used for the one example with genuinely random output), and
`examples/error_cases/` (must fail with a specific error code).

## Running under sanitizers (Linux/macOS)

`run.sh` takes the binary and the unit-test compile flags from the environment, so the whole
suite can run under AddressSanitizer + UndefinedBehaviorSanitizer:

```bash
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
g++ -std=c++17 -O1 -g $SAN -o cuffc-asan main.cpp
ASAN_OPTIONS=detect_leaks=0 CUFFC=./cuffc-asan UNIT_FLAGS="-O1 -g $SAN" bash tests/run.sh
```

Leak detection is switched off on purpose: a list or map that contains itself is reference
counted and never freed (see `docs/IMPLEMENTATION_NOTES.md` item 11), and a few tests build
exactly that. `run.sh` also falls back to `gtimeout` (or no timeout) where the `timeout`
command is missing, as on a stock macOS. MinGW has no sanitizers, so this is not available
through `run.ps1`.

## Testing a reduced build

`make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK` builds the engine without the socket
client (see `docs/EXTENDING.md`, section 11). Run the suite against it with
`CUFFC=./cuffc bash tests/run.sh`: everything passes except the four
`tests/errors/network_*.cuff` cases, which test the client that was left out.

## Adding a C++ unit test

Drop a `.cpp` file with a `main()` in `tests/unit/`. It's compiled with `-I.` from the repo
root, so include engine headers by path (`#include "engine/regex/RegexEngine.h"`). Exit
non-zero to signal failure; the runner prints your last line of output either way.

## Adding a success case

Drop a `.cuff` file in `tests/cases/`, then generate its golden output:

```bash
./cuffc tests/cases/my_case.cuff > tests/cases/my_case.expected
```

Read the output once before committing it — this locks in whatever the interpreter did as
"correct". Don't add a case whose output is non-deterministic (random numbers, timestamps).

## Adding an error case

Drop a `.cuff` file in `tests/errors/` that's expected to fail, then record which error
code it must produce:

```bash
./cuffc tests/errors/my_error.cuff   # note the [E####] in the output
echo "E####" > tests/errors/my_error.expected_code
```

`.expected_code` is optional — without one, the runner only checks that the script fails
(exit code != 0).
