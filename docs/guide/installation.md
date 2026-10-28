# Installation

CuffScript is built from source: the engine is header-only C++17 with no third-party dependencies, so building is one compiler invocation. This page gets you from a fresh machine to a working `cuffc` and `cuffsh`.
[Building](../contributing/building.md) has every target and option, and the checks contributors run.

## What you need

| You need | For |
| :--- | :--- |
| A C++17 compiler: GCC, Clang, or MinGW-w64 on Windows | Building everything |
| `make` (`mingw32-make` on Windows) | The `Makefile` |
| Python 3.8 or later (optional) | The documentation check and the shell tests |
| The Emscripten SDK (optional) | The WebAssembly build |

## Get the source

```sh
git clone https://github.com/cuffscript/cuffscript.git
cd cuffscript
```

A source archive of a release works as well; unpack it and change into the folder.

## Linux

Install a compiler and `make` with your package manager, for example:

```sh
sudo apt install build-essential        # Debian, Ubuntu
sudo dnf install gcc-c++ make           # Fedora
sudo pacman -S gcc make                 # Arch
```

Then build the script runner and the shell:

```sh
make                  # cuffc, the script runner
./cli/build.sh        # cli/cuffsh, the interactive shell
```

## macOS

The Command Line Tools provide Apple Clang and `make`:

```sh
xcode-select --install
make
./cli/build.sh
```

## Windows

Install [MSYS2](https://www.msys2.org), open the **MSYS2 MINGW64** shell, and install the toolchain:

```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make
```

Put `C:\msys64\mingw64\bin` (or wherever MSYS2 is installed) on your `PATH`, then build from PowerShell or cmd:

```powershell
mingw32-make -f Makefile.win       # cuffc.exe
cli\build.bat                      # cli\cuffsh.exe
```

The build links the Windows socket library and asks for a larger stack, because the evaluator is recursive, and the shell is linked statically, so there are no runtime DLLs to ship. Run `cuffsh.exe` from cmd, PowerShell or Windows Terminal. Git Bash (mintty) gives
Windows programs no console, and `cuffsh` says so and exits; `winpty cuffsh.exe` works there.

## Check that it works

```sh
./cuffc examples/01_hello.cuff
echo 'print(6 * 7)' | ./cuffc
./cli/cuffsh --version
```

The first prints a greeting, the second prints `42`, and the third prints the version of the shell. To run the whole test suite, see [Testing](../contributing/testing.md).

## Put it on your path

Copy or link the binaries somewhere your shell looks:

```sh
sudo cp cuffc cli/cuffsh /usr/local/bin/        # Linux, macOS
```

On Windows, put the folder that holds `cuffc.exe` and `cli\cuffsh.exe` on `PATH`. After that, `cuffc program.cuff` works from any folder. A program does not need anything installed next to it: the engine, the libraries and the
standard functions are inside the binary.

## Build options

`EXTRA_CXXFLAGS` adds compiler flags to every target. The only option that changes what is built is `-DCUFF_DISABLE_NETWORK`, which leaves the network client out of the binary entirely, for a build that must never be able to reach the network:

```sh
make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK
```

A binary built this way behaves as if every program ran with `--no-network`.

## Building for the browser

```sh
make wasm        # needs the Emscripten SDK; writes npm/dist/
```

The result is the module that the `cuffscript-wasm` npm package ships. See [Embedding the engine](embedding.md#webassembly-and-npm).

## When the build fails

- **`error: ... -Werror`**: the project builds with `-Wall -Wextra -Werror`, so a compiler that is much newer than the ones tested may reject a warning. Build with `make EXTRA_CXXFLAGS=-Wno-error` to see whether it is only a warning, and report it as a bug.
- **`g++: command not found`** or **`make: command not found`**: install the tools above. On Windows, check that the MinGW folder is on `PATH` in the shell you are using.
- **A slow build**: `make` optimises the whole program at once (`-O3 -flto`), so it can take a while on a small machine. Nothing is wrong; it is one large compilation unit.
- **"bad interpreter: /usr/bin/env bash\r"** when running a script: the file was checked out with Windows line endings; see [line endings](../contributing/building.md#line-endings).

## Where to go next

- [Getting started](getting-started.md): your first program.
- [A tour of the language](language-tour.md).
- [`cuffc`](../reference/cuffc.md) and [`cuffsh`](../reference/cuffsh.md), the two programs you just built.
