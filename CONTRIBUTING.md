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
2. Make your changes.
3. Verify that the build succeeds with `make`.
4. Submit the PR and reference the related issue number in the description.

If your change affects the language grammar or token system, please also update the relevant specification documents (`docs/SPEC.md` or `docs/REGEX.md`).

---

## Code Style

- Use the C++17 standard.
- Use 4 spaces for indentation.
- Do not use `using namespace std;` in header files.

---

## Building

```bash
make
./cuffc path/to/program.cuff
```
