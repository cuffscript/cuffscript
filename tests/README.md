# Tests

```bash
make
bash tests/run.sh
```

Runs, in order: `tests/unit/` (standalone C++ unit tests, compiled and run directly —
currently the regex engine's own test suite, which can exercise it without going through
the whole language pipeline), `tests/cases/` (exact output diff), `tests/errors/` (must fail
with a specific error code), `examples/` (exact output diff where a `.expected` exists,
otherwise just checks exit 0 — used for the one example with genuinely random output), and
`examples/error_cases/` (must fail with a specific error code).

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
