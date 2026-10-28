# Contributing to CuffScript

Thank you for contributing to CuffScript. This page tells you how to report a problem, propose a change, and get a pull request accepted. Everyone taking part is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

---

## Ways to contribute

- **Report a bug**: a program that behaves differently from what the [documentation](docs/README.md) says, or an error that points to the wrong place.
- **Ask a question**: if the documentation does not answer it, that is a gap worth knowing about. See [Support](SUPPORT.md).
- **Propose a feature**: the language is small on purpose, so a proposal should say what problem it solves and what it would cost.
- **Improve the documentation**: a wrong sentence, a missing example or an unclear explanation is as welcome a fix as code.
- **Fix something**: a bug, a missing test, a portability problem on a platform you use.

Security problems are not reported in public; follow the [Security Policy](SECURITY.md).

---

## Before you start

- Look through the [issue tracker](https://github.com/cuffscript/cuffscript/issues) to see whether the topic has already been reported or discussed.
- Read the page that describes the part you want to change. [Where things are](#where-things-are) points to it.
- For anything larger than a small fix, open an issue first and wait for a reply before writing code. Pull requests for large features that were not discussed may be closed.

---

## Reporting bugs

Open an issue with the **Bug report** form, and include:

- the smallest CuffScript program that shows the problem (cut lines until the problem disappears, then put the last one back);
- what you expected, and what happened instead, with the complete error report if there is one (the `ERROR: [E...]` block, line and caret included);
- your environment: operating system, compiler and its version, and the CuffScript version (`cuffsh --version`, or the commit you built);
- whether it still happens on the latest release or on `main`.

A report with a program someone else can paste and run is usually fixed first.

## Proposing features

Open an issue with the **Feature request** form. Explain the problem before the solution: what you tried to write, why it was awkward, and what you would like to be able to write. A change to the language
has to fit its direction (easy to read for beginners, small, predictable), so a proposal is judged on that as much as on how useful it is. The [design notes](docs/internals/design-notes.md) record decisions that were
already made, and why; reading them avoids repeating a discussion.

---

## Pull requests

1. Fork the repository and create a working branch from `main`.
2. Make your change, with a test that fails without it (see [Testing](docs/contributing/testing.md)).
3. Update the documentation the change affects (see [Documentation](docs/contributing/documentation.md)).
4. Run the checks below.
5. Open the pull request, fill in the template, and reference the issue it relates to.

One logical change per pull request makes it faster to review and easier to undo. A change that fixes a bug should say in its description how the bug was reproduced.

### Checks to run

```sh
make && bash tests/run.sh                    # the engine and language suite
python3 tools/docs.py check                   # the documentation is still true
```

If you touched `cli/` (the shell), also:

```sh
./cli/build.sh
python3 -m venv .venv && .venv/bin/pip install -r cli/tests/requirements.txt     # once
PYTHON=.venv/bin/python cli/tests/run_tests.sh
```

On Windows, use `tests\run.ps1` in place of `tests/run.sh`; see [Testing](docs/contributing/testing.md) for every runner and what it covers. The same checks run in continuous integration on every pull request (see [Continuous integration](docs/contributing/ci.md)), so a
change that passes them locally is expected to pass there.

If your change affects what a user can see (the language, a library, an option, an error), update the page that describes it. Limits, error codes, library function lists and command-line help are generated: run
`python3 tools/docs.py generate`.

### Describing the change

The pull request template asks for what changed, why, and how. The release notes are written from these descriptions, so say what a user will notice. Mark a change that makes an existing program behave differently
as a **behaviour change** in the description: those are listed separately in the [changelog](docs/CHANGELOG.md) and are what [versioning](docs/contributing/releasing.md#versioning) is based on.

### Review

A maintainer reviews every pull request. Expect questions about behaviour that is not covered by a test, and about anything that could break an existing program. Keep the discussion on the change; follow-up commits to the same branch update the pull request.

---

## Where things are

| To work on | Start with |
| :--- | :--- |
| The language or the engine | [Architecture](docs/internals/architecture.md), then [Extending the engine](docs/contributing/extending.md) |
| Which file does what | [Source map](docs/internals/source-map.md) |
| The interactive shell | [The shell's architecture](docs/internals/shell-architecture.md) |
| Tests | [Testing](docs/contributing/testing.md) |
| Docs | [Documentation](docs/contributing/documentation.md) |
| Continuous integration | [Continuous integration](docs/contributing/ci.md) |
| A release | [Releasing](docs/contributing/releasing.md) |
| Why something works the way it does | [Design notes](docs/internals/design-notes.md) |

---

## Code style

The short version: C++17, four spaces for indentation, no `using namespace std;` in headers, warning-free with `-Wall -Wextra -Werror` on GCC and Clang, and comments only where the code cannot say it itself. The full conventions are in
[Code style](docs/contributing/code-style.md).

---

## Building

```sh
make
./cuffc path/to/program.cuff
```

Every target and option, and what each platform needs, is in [Building](docs/contributing/building.md).

---

## License

CuffScript is licensed under the [Apache License 2.0](LICENSE). Unless you say otherwise, a contribution you submit for inclusion is under the same license (section 5 of the license).
