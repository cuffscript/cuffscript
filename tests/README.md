# `tests/`

The engine and language test suite. It is run by `tests/run.sh` (Linux, macOS) and `tests/run.ps1` (Windows, and PowerShell 7 anywhere), which print the same sections and the same summary.

| Directory | Contents | A test is |
| :--- | :--- | :--- |
| `unit/` | C++ unit tests | A `.cpp` file with a `main()`. It is compiled and run; a non-zero exit fails it, and the last line of its output is shown. |
| `cases/` | Programs with their exact output | `name.cuff` and `name.expected`; the output must match exactly. |
| `errors/` | Programs that must fail | `name.cuff` and, optionally, `name.expected_code` with the error code it must fail with. A file `name.args` holds command-line options for the run (for example `--no-network`). |

The runners also run the programs in `examples/`, with their `.expected` output, and the programs in `examples/error_cases/`, with the codes in their `.expected_code` files.

```sh
make                                   # build cuffc first
bash tests/run.sh                      # Linux, macOS
powershell -File tests\run.ps1         # Windows
```

A passing run ends with a summary line such as `146 passed, 0 failed`. Set `CUFFC` to test another binary (a sanitizer build, for example) and `UNIT_FLAGS` to change how the unit tests are compiled.

How the suites work, how to add a test, what each unit test protects, sanitizers and what CI runs are in [Testing](../docs/contributing/testing.md). The interactive shell has its own tests in `cli/tests/`.
