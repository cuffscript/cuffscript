# CuffScript documentation

The documentation is organised by what you are trying to do. If you are new, start with
[Getting started](guide/getting-started.md).

## Guide: learn and do

| Page | Covers |
| :--- | :--- |
| [Getting started](guide/getting-started.md) | Building, running a script, the shell |
| [A tour of the language](guide/language-tour.md) | Every part of the language, with examples that run |
| [Errors and debugging](guide/errors-and-debugging.md) | Reading an error report, `or_else`, the common errors, finding a bug |
| [Modules](guide/modules.md) | Splitting a program into files, and the sandbox |
| [Text and patterns](guide/text-and-patterns.md) | Finding and changing text by shape |
| [Embedding the engine](guide/embedding.md) | Using CuffScript from a C++ program |

## Reference: look it up

| Page | Covers |
| :--- | :--- |
| [Language reference](reference/language.md) | The language, defined |
| [Pattern syntax](reference/patterns.md) | The pattern language |
| [Libraries](reference/dlc/README.md) | One page per `use DLC:...` library |
| [Built-in functions](reference/builtins.md), [function index](reference/functions.md) | What is always available, and every function in one list |
| [`cuffc`](reference/cuffc.md), [`cuffsh`](reference/cuffsh.md) | The script runner and the interactive shell |
| [Error catalogue](reference/errors.md) | Every error code |
| [Limits](reference/limits.md) | Every limit and the error it raises |

## Internals: how it works

| Page | Covers |
| :--- | :--- |
| [Architecture](internals/architecture.md) | The pipeline, the folders and the front ends |
| [Source locations and error reports](internals/source-locations.md) | How an error finds its line |
| [The error model](internals/error-model.md) | What an error is, and who catches it |
| [Resource limits](internals/resource-limits.md) | Stack, recursion and memory safety |
| [The shell's architecture](internals/shell-architecture.md) | How `cuffsh` is built |
| [Design notes](internals/design-notes.md) | The history of individual decisions |

## Contributing

| Page | Covers |
| :--- | :--- |
| [Building](contributing/building.md) | Targets, options, compilers, platforms |
| [Testing](contributing/testing.md) | The suites, adding a test, sanitizers |
| [Extending the engine](contributing/extending.md) | Adding a function, a statement, an error, a token |
| [Documentation](contributing/documentation.md) | How these pages are kept true |
| [Code style](contributing/code-style.md) | Conventions |
| [Releasing](contributing/releasing.md) | Versioning and the changelog |

Also: the [changelog](CHANGELOG.md), the [security policy](../SECURITY.md), the [runnable examples](../examples/README.md).
