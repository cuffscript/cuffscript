<p align="center">
  <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript" width="360" />
</p>

<h1 align="center">The CuffScript programming language</h1>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/License-Apache%202.0-red?style=flat" alt="Apache 2.0 license" />
</p>

CuffScript is a lightweight scripting language designed for readable, natural-language-like syntax and simple general-purpose scripting. This repository includes its language engine, a script runner (`cuffc`), an interactive shell (`cuffsh`), built-in libraries, and runnable examples.

The engine can be used through the command-line interpreter, embedded in a C++ application, or run in a browser through WebAssembly.

```cuff
set str name to "World"

set returnable func greet(person) do:
    return f"Hello, {person}!"
end

print(greet(name))
```

```output
Hello, World!
```

## Quick Start

### Install Requirements

Before building CuffScript, make sure you have:

- A C++17 compatible compiler (GCC, Clang, or MinGW-w64 on Windows)
- `make` available on your system (`mingw32-make` on Windows)

If you are using Linux or macOS, the standard system `make` should work. If you are on Windows, install MinGW-w64 and ensure it is on your `PATH` before running the build commands below. [Installation](docs/guide/installation.md) has the details for each platform.

### Build

On Linux or macOS:

```sh
make
```

On Windows with MinGW:

```powershell
mingw32-make -f Makefile.win
```

The executable is `cuffc` on Linux/macOS and `cuffc.exe` on Windows. The interactive shell is built separately with `./cli/build.sh` (`cli\build.bat` on Windows).

### Run a Script

```sh
./cuffc examples/01_hello.cuff
```

On Windows:

```powershell
.\cuffc.exe examples\01_hello.cuff
```

To read a script from standard input, run `cuffc` without a file. Use `--help` to see available command-line options.

## A Longer Example

```cuff
use DLC:list

set list scores to [72, 95, 88, 61]
set number total to 0

loop repeat i to 1 ~ length(scores) do:
    if scores[i] >= 70 do:
        change total to total + 1
    end
end

print(f"{total} of {length(scores)} passed")

set str code to "AB-1234"
if code is "[up]2-[num]4" do:
    print("the code has the right shape")
end
```

```output
3 of 4 passed
the code has the right shape
```

## What It Includes

- Variables, constants, functions, conditionals, and loops
- Lists, ordered maps, indexing, and slicing
- A built-in pattern-matching language with word-style tokens (`[num]`, `[email]`) instead of cryptic symbols
- Async functions, modules, and recoverable runtime errors (`or_else`)
- Ten built-in libraries: math, strings, lists, maps, conversion, time, random numbers, JSON, files and a small HTTP client
- Error reports with the source line, a caret under the position, and a hint, and a [catalogue](docs/reference/errors.md) of every error code
- Limits on nesting, recursion, size and matching work, so a script can fail without crashing its host
- A C++ API for embedding the engine, plus a WebAssembly build target

## Documentation

Everything is under [`docs/`](docs/README.md), organised by what you are trying to do:

| If you want to... | Read |
| :--- | :--- |
| Build it and run your first script | [Installation](docs/guide/installation.md), [Getting started](docs/guide/getting-started.md) |
| Learn the language | [A tour of the language](docs/guide/language-tour.md), [the cookbook](docs/guide/cookbook.md), [the FAQ](docs/guide/faq.md) |
| Look something up | [Language reference](docs/reference/language.md), [grammar](docs/reference/grammar.md), [libraries](docs/reference/dlc/README.md), [patterns](docs/reference/patterns.md), [errors](docs/reference/errors.md), [limits](docs/reference/limits.md) |
| Use the shell or the runner | [`cuffsh`](docs/reference/cuffsh.md), [`cuffc`](docs/reference/cuffc.md) |
| Embed the engine in a C++ program | [Embedding the engine](docs/guide/embedding.md), [C++ API](docs/reference/cpp-api.md), [security policy](SECURITY.md) |
| Understand how it works | [Architecture](docs/internals/architecture.md), [source map](docs/internals/source-map.md) |
| Contribute | [Contributing](CONTRIBUTING.md) |

Runnable programs are in [`examples/`](examples/README.md), and the WebAssembly package is described in [`npm/README.md`](npm/README.md).

## Repository layout

| Path | Contents |
| :--- | :--- |
| `engine/` | The language engine (header-only C++17) |
| `main.cpp`, `cli/` | `cuffc`, the script runner, and `cuffsh`, the interactive shell |
| `wasm/`, `npm/` | The WebAssembly binding and the npm package |
| `examples/` | Runnable programs, error cases and a C++ embedding example |
| `tests/` | The engine and language test suite |
| `tools/` | The documentation generator and checker |
| `docs/` | The documentation, including the [changelog](docs/CHANGELOG.md) |

## Getting help and contributing

Questions, bug reports and proposals are welcome: see [Getting help](SUPPORT.md) and [Contributing](CONTRIBUTING.md). Security problems are reported privately; see the [Security Policy](SECURITY.md). Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Tests

```sh
make
bash tests/run.sh
```

On Windows, use `mingw32-make -f Makefile.win` followed by `powershell -File tests\run.ps1`. The shell, the documentation and the sanitizer builds have their own checks; see [Testing](docs/contributing/testing.md).

## License

CuffScript is licensed under the [Apache License 2.0](LICENSE).
