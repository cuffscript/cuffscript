# Releasing

## Before you tag

1. Run everything: `bash tests/run.sh`, `cli/tests/run_tests.sh`, and `python3 tools/docs.py check --require-binaries`. On
   Windows, `tests\run.ps1` and `cli\tests\run_tests.ps1`.
2. Build with the strict flags on each compiler family you support (see [Building](building.md#checking-a-build-the-way-ci-does)).
3. Set the version. It is written in two places, which the documentation check requires to agree with each other and with the top
   entry of the changelog:
   - `cli/Version.h` (`kVersion`), shown by `cuffsh --version` and its banner;
   - `npm/package.json` (`version`).
4. Regenerate the docs if anything they are generated from changed: `python3 tools/docs.py generate`.

## Versioning

A version is `MAJOR.MINOR.PATCH`, written with a leading `v` in tags and in the changelog (`v1.2.3`). The [changelog](../CHANGELOG.md) shows how the numbers are used:

| Part | Used for |
| :--- | :--- |
| Patch | Fixes: corrections to what a feature does, and to what the documentation says it does |
| Minor | New features, libraries and options, together with fixes; a change to the language that a program can notice is called out |
| Major | A larger break, such as renaming a keyword or a library function |

This is not Semantic Versioning to the letter. Instead, every change that can make a program that worked behave differently is labelled
**Behavior change** in the release notes, whatever the number, so that someone upgrading can find them by searching for those words. The number is
chosen from the list of changes when the release is made.

The version is set in `cli/Version.h` and `npm/package.json` and must agree with the top entry of the changelog; the documentation check enforces it.

## The changelog

`docs/CHANGELOG.md` is written when a GitHub release is **published**: the workflow `update-changelog.yaml` takes the release's
title and body, puts them at the top of the file under `## <tag> - <date>`, and opens a pull request. Write the release body in
the style of the existing entries:

```text
## Description

- **Fixed: a short, bold statement of the problem.** One or two sentences on what was wrong and what happens now.
- **Added: ...** / **Changed: ...** for the rest. Name behaviour changes plainly ("**Behavior change:** ...").
```

Describe what a user will see, not which files changed. Group related changes in one bullet. If you add the entry by hand
instead of through the release, do not also publish a release with the same body, or the workflow will add it a second time.

## npm

The npm package holds build output only. After the version is set:

```sh
make wasm          # needs the Emscripten SDK; writes npm/dist/
cd npm && npm publish
```

## After

Check that the changelog pull request is merged and that `python3 tools/docs.py check` passes on `main`.
