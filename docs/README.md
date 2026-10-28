# CuffScript documentation

The documentation is organised by what you are trying to do. If you are new, start with
[Getting started](guide/getting-started.md).

## Guide: learn and do

| Page | Covers |
| :--- | :--- |
| [Installation](guide/installation.md) | Building `cuffc` and `cuffsh` on Linux, macOS and Windows |
| [Getting started](guide/getting-started.md) | Running a script, your first program, the shell |
| [A tour of the language](guide/language-tour.md) | Every part of the language, with examples that run |
| [Cookbook](guide/cookbook.md) | Short recipes for common jobs |
| [Text and patterns](guide/text-and-patterns.md) | Finding and changing text by shape |
| [Errors and debugging](guide/errors-and-debugging.md) | Reading an error report, `or_else`, the common errors, finding a bug |
| [Modules](guide/modules.md) | Splitting a program into files, and the sandbox |
| [Embedding the engine](guide/embedding.md) | Using CuffScript from a C++ program, and in the browser |
| [FAQ](guide/faq.md) | The questions beginners ask first |

## Reference: look it up

| Page | Covers |
| :--- | :--- |
| [Language reference](reference/language.md) | The language, defined |
| [Grammar](reference/grammar.md) | What the parser accepts: statements, expressions, tokens and patterns |
| [Pattern syntax](reference/patterns.md) | The pattern language |
| [Libraries](reference/dlc/README.md) | One page per `use DLC:...` library |
| [Built-in functions](reference/builtins.md), [function index](reference/functions.md) | What is always available, and every function in one list |
| [`cuffc`](reference/cuffc.md), [`cuffsh`](reference/cuffsh.md) | The script runner and the interactive shell |
| [C++ API](reference/cpp-api.md) | `CuffEngine`, `Session`, options and error reports |
| [Error catalogue](reference/errors.md) | Every error code, with an example and a fix |
| [Limits](reference/limits.md) | Every limit and the error it raises |
| [Known limitations](reference/known-limitations.md) | What the language does not do, and what surprises people |
| [Glossary](reference/glossary.md) | The words these pages use |

## Internals: how it works

| Page | Covers |
| :--- | :--- |
| [Architecture](internals/architecture.md) | The pipeline, the folders and the front ends |
| [Source map](internals/source-map.md) | Which file does what |
| [Source locations and error reports](internals/source-locations.md) | How an error finds its line |
| [The error model](internals/error-model.md) | What an error is, and who catches it |
| [Resource limits](internals/resource-limits.md) | Stack, recursion and memory safety |
| [The shell's architecture](internals/shell-architecture.md) | How `cuffsh` is built |
| [Design notes](internals/design-notes.md) | The history of individual decisions |

## Contributing

| Page | Covers |
| :--- | :--- |
| [Contributing](../CONTRIBUTING.md) | Reporting bugs, proposing features, pull requests |
| [Building](contributing/building.md) | Targets, options, compilers, platforms |
| [Testing](contributing/testing.md) | The suites, adding a test, sanitizers |
| [Continuous integration](contributing/ci.md) | What each workflow checks and how to reproduce it |
| [Extending the engine](contributing/extending.md) | Adding a function, a statement, an error, a token |
| [Documentation](contributing/documentation.md) | How these pages are kept true |
| [Code style](contributing/code-style.md) | Conventions |
| [Releasing](contributing/releasing.md) | Versioning, the changelog and publishing |

## The project

[Changelog](CHANGELOG.md) · [Security policy](../SECURITY.md) · [Getting help](../SUPPORT.md) · [Code of conduct](../CODE_OF_CONDUCT.md) · [License](../LICENSE) · [Runnable examples](../examples/README.md)
