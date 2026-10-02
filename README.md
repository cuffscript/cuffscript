<p align="center">
  <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript" width="360" />
</p>

---

<h1 align="center">The CuffScript programming language</h1>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/License-Apache%202.0-red?style=flat" alt="Apache 2.0 license" />
</p>

CuffScript is a scripting language that is as easy as a game, as light as a feather, and as fast as a flash. This repository includes its language engine, command-line runner, built-in libraries, and runnable examples.

The engine can be used through the command-line interpreter or embedded in a C++ application.

## Quick Start

### Install Requirements

Before building CuffScript, make sure you have:

- A C++17 compatible compiler
- `make` available on your system
- On Windows, MinGW or a similar toolchain that supports `mingw32-make`

If you are using Linux or macOS, the standard system `make` should work. If you are on Windows, install MinGW and ensure it is on your `PATH` before running the build commands below.

### Build

On Linux or macOS:

```sh
make
```

On Windows with MinGW:

```powershell
mingw32-make -f Makefile.win
```

The executable is `cuffc` on Linux/macOS and `cuffc.exe` on Windows.

### Run a Script

```sh
./cuffc examples/01_hello.cuff
```

On Windows:

```powershell
.\cuffc.exe examples\01_hello.cuff
```

To read a script from standard input, run `cuffc` without a file. Use `--help` to see available command-line options.

## A Small Example

```cuff
set str name to "World"

set returnable func greet(person) do:
    return f"Hello, {person}!"
end

print(greet(name))
```

## What It Includes

- Variables, constants, functions, conditionals, and loops
- Lists, ordered maps, indexing, and slicing
- A built-in pattern-matching language
- Async functions, modules, and recoverable runtime errors
- Built-in libraries for math, strings, collections, time, JSON, and more
- A C++ API for embedding the engine, plus a WebAssembly build target

## Documentation

- [Language specification](docs/SPEC.md)
- [Pattern syntax](docs/REGEX.md)
- [Security policy](SECURITY.md)
- [Runnable examples](examples/README.md)
- [Contributing](CONTRIBUTING.md)
- [WebAssembly package](npm/README.md)

## Tests

```sh
make
bash tests/run.sh
```

On Windows, use `mingw32-make -f Makefile.win` followed by `powershell -File tests\run.ps1`.

## License

CuffScript is licensed under the [Apache License 2.0](LICENSE).
