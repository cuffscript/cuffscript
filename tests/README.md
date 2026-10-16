# `tests/`

The engine and language test suite.

| Directory | Contents |
| :--- | :--- |
| `unit/` | C++ unit tests, one file each, with a `main()` |
| `cases/` | Scripts with their exact expected output (`name.cuff`, `name.expected`) |
| `errors/` | Scripts that must fail with a given error code (`name.cuff`, `name.expected_code`) |

```sh
bash tests/run.sh                      # Linux, macOS
powershell -File tests\run.ps1         # Windows
```

How the suites work, how to add a test, the environment variables, sanitizers and what CI runs are in
[Testing](../docs/contributing/testing.md). The shell has its own tests in `cli/tests/`.
