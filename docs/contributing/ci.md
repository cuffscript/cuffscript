# Continuous integration

Six GitHub Actions workflows live in `.github/workflows/`. Five of them check a change before it is merged, and one writes the changelog after a release. This page says what each one does and
how to reproduce a failure on your own machine.

| Workflow | File | Runs on | What it checks |
| :--- | :--- | :--- | :--- |
| Build and Test | `build-and-test.yaml` | Every pull request and every push to `main` | Builds `cuffc` and runs the engine and language suite on Linux and macOS; builds with MinGW-w64 and runs it on Windows |
| REPL Build and Test | `repl-test.yaml` | Changes under `cli/`, `engine/`, `main.cpp`, `Makefile`, `Makefile.win` | Builds `cuffsh` and runs the shell suites |
| Documentation Check | `docs-check.yaml` | Every pull request and every push to `main` | `python3 tools/docs.py check --require-binaries` |
| Sanitizers | `sanitizers.yaml` | Changes under `engine/`, `tests/`, `examples/`, `main.cpp`, `Makefile` | The engine suite under AddressSanitizer and UndefinedBehaviorSanitizer |
| PR Conflict Check | `PR-conflict-check.yaml` | A pull request that is opened, updated, reopened or reviewed | Fails when the pull request has merge conflicts, unless it has been approved |
| Update CHANGELOG on Release | `update-changelog.yaml` | A GitHub release being published | Opens a pull request that adds the release notes to the changelog |

All of them take their permissions from the workflow file: the checks only read the repository, and the changelog workflow may write to a branch and open a pull request.

## Build and Test

- **Linux and macOS** (`ubuntu-latest`, `macos-latest`, not stopped by each other's failure): `make`, then `bash tests/run.sh`. macOS first installs GNU coreutils with Homebrew, because the test runner uses `timeout`, which a stock macOS does not have.
- **Windows** (`windows-latest`): installs MSYS2 with the MinGW-w64 toolchain, puts it on `PATH`, builds with `mingw32-make -f Makefile.win`, runs `tests/run.ps1`, and builds the shell with `cli\build.bat`.

To reproduce locally: `make && bash tests/run.sh` (Windows: `mingw32-make -f Makefile.win` and `powershell -File tests\run.ps1`). [Testing](testing.md) lists every section of the suite.

## REPL Build and Test

The shell has its own suites, which need a pseudo-terminal, so they are split by platform:

- **Linux and macOS**: builds `cuffc` and `cuffsh`, prints the versions of Python, `pyte`, `wcwidth`, the compiler and `make`, then runs `cli/tests/run_tests.sh` (the interactive suites, the fuzzer and the comparison with `cuffc`).
- **Windows**: builds both programs and runs `cli/tests/run_tests.ps1 -Suite native`, the native checks (piped scripts, error text identical to `cuffc`'s, UTF-8 paths, the flags, the limits). The interactive suites assert Unix terminal behaviour and are not run there.

A shell test that fails only on a CI runner is usually timing; see [When CI fails and it passes for you](testing.md#when-ci-fails-and-it-passes-for-you).

## Documentation Check

Builds `cuffc` and `cuffsh` (the examples in the documentation are run, and the command-line help is embedded) and runs `python3 tools/docs.py check --require-binaries`, which fails when a generated page is stale, a link or heading anchor is broken, a library function is
undocumented, a limit's value is written outside its generated page, the versions disagree, or an example's output differs from what the page shows. To fix it locally, run `python3 tools/docs.py generate` and then the check; see [Documentation](documentation.md).

## Sanitizers

Builds `cuffc` with AddressSanitizer and UndefinedBehaviorSanitizer and runs the whole engine suite, unit tests included, under them. Leak detection is off on purpose (a list or map that contains itself is never freed; see the
[design notes](../internals/design-notes.md)). The command to run it by hand is in [Testing](testing.md#sanitizers). The workflow only runs when something it could affect changed.

## PR Conflict Check

Waits for GitHub to work out whether the pull request can be merged, and fails the check when it has conflicts, so a conflicted pull request is visible in the list of checks. A pull request that has an approving review passes even with conflicts, because the maintainer who
merges it is expected to resolve them.

## Update CHANGELOG on Release

When a release is published, the workflow takes the release's tag, date, title and body, puts them at the top of `docs/CHANGELOG.md` under `## <tag> - <date>`, and opens a pull request for it. The values from the release event reach the
script as environment variables, never as part of the script text, because a tag name can contain characters that a shell would act on. See [Releasing](releasing.md).

## Making a failure reproducible

1. Open the first failing step, not the last: later steps often fail because of the first.
2. Compare the tool versions the job printed with yours. The compiler, `make`, Python and the `pyte` version are the usual differences.
3. Run the same command from the repository root on a clean checkout, after `make clean` where a stale binary could matter.
4. For a platform you do not have, say so in the pull request rather than guessing: another contributor, or the next CI run, can usually reproduce it.
