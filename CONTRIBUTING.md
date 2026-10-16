# Contributing to CuffScript

Thank you for contributing to CuffScript.

---

## Before You Start

- Check the [issue tracker](https://github.com/cuffscript/cuffscript/issues) to see if the same topic has already been reported.
- Before contributing code, please read the relevant documentation to understand the language design.

---

## Reporting Issues

If you find a bug, please open an issue and include the following:

- A minimal CuffScript code snippet that reproduces the problem
- The actual output and the expected output
- Environment details such as OS and compiler version

If you would like to propose a new feature, please open an issue first before writing any code.
Pull requests for large features submitted without prior discussion may be closed.

---

## Pull Requests

1. Fork the repository and create a working branch.
2. Make your changes, with a test that fails without them (see [Testing](docs/contributing/testing.md)).
3. Run the checks below.
4. Submit the PR and reference the related issue number in the description.

Before you submit:

```sh
make && bash tests/run.sh                                   # the engine and language suite
./cli/build.sh && python3 -m pip install -r cli/tests/requirements.txt && cli/tests/run_tests.sh   # if you touched cli/
python3 tools/docs.py check                                  # the docs are still true
```

If your change affects what a user can see (the language, a library, an option, an error), update the page that describes it. Limits, error codes, library function lists and command-line help are generated: run `python3 tools/docs.py generate`. [Documentation](docs/contributing/documentation.md) says which page covers what.

---

## Where Things Are

| To work on | Start with |
| :--- | :--- |
| The language or the engine | [Architecture](docs/internals/architecture.md), then [Extending the engine](docs/contributing/extending.md) |
| The interactive shell | [The shell's architecture](docs/internals/shell-architecture.md) |
| Tests | [Testing](docs/contributing/testing.md) |
| Docs | [Documentation](docs/contributing/documentation.md) |
| A release | [Releasing](docs/contributing/releasing.md) |

---

## Code Style

The short version: C++17, four spaces for indentation, no `using namespace std;` in headers, warning-free with `-Wall -Wextra -Werror` on GCC and Clang. The full conventions are in [Code style](docs/contributing/code-style.md).

---

## Building

```bash
make
./cuffc path/to/program.cuff
```

Every target and option is in [Building](docs/contributing/building.md).
