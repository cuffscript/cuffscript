# Building

CuffScript is header-only C++17 with no third-party dependencies, so building is one compiler invocation. The `Makefile`
wraps it.

## Requirements

- A C++17 compiler: GCC, Clang (with libstdc++ or libc++, as on macOS), or MinGW-w64 on Windows. The code is built with
  `-Wall -Wextra -Werror` on all three, so a warning is a failure.
- `make` (`mingw32-make` on Windows).
- Optional: Python 3.8 or later for the documentation tool and the shell tests; the Emscripten SDK for the WebAssembly build.

## Targets

| Command | Builds |
| :--- | :--- |
| `make` | `cuffc`, the script runner, with `-O3 -flto -DNDEBUG`. |
| `make clean` | Removes it. |
| `make wasm` | The WebAssembly module, into `npm/dist/`. Needs Emscripten. |
| `make wasm-clean` | Removes the module. |
| `mingw32-make -f Makefile.win` | `cuffc.exe` on Windows. It links the Windows socket library and asks for a larger stack than the Windows default, because the parser and the compiler follow nested source recursively. |
| `./cli/build.sh` | `cli/cuffsh`, the interactive shell (Linux, macOS). |
| `cli\build.bat` | `cli\cuffsh.exe` (Windows), statically linked so there are no runtime DLLs to ship. |

`make` rebuilds when any header under `engine/` changes. The shell scripts do not use `-Werror`, so that a newer compiler's
new warnings cannot break a user's build; the checks below do.

## Build options

`EXTRA_CXXFLAGS` adds flags to every target, including `make wasm` and `Makefile.win`:

```sh
make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK
```

| Flag | Effect |
| :--- | :--- |
| `-DCUFF_DISABLE_NETWORK` | Leaves out the socket client and its platform headers. `use DLC:network` still loads; its functions report that the feature is unavailable. The `tests/errors/libraries/network_*` cases test the client and are expected to fail in such a build. |
| `-DCUFF_EXCLUDE_DLC_<NAME>` | Leaves a library out; see below. `EXCLUDE_DLC` writes these for you. |

### Leaving libraries out

`EXCLUDE_DLC` names libraries that the binary should not have:

```sh
make -B EXCLUDE_DLC="network hardware"
```

A library left out is unknown to that binary, exactly as a name that never existed. `use DLC:network` is `E5-004`, and its hint
lists the libraries that are there; `x.sqrt()` without `DLC:math` is "number has no method 'sqrt'"; the command-line flags that only
the library reads (`--no-network` and `--allow-private-network`, `--no-filesystem`, `--hardware`) are unknown options and are not
in `--help`. Its code is not in the binary. `DLC:convert` cannot be left out: its functions are also built-ins of every program.
A name that is not one of the libraries that can be left out stops the build, with the list of those that can; that list is
`engine/dlc/libraries.txt`, the one every build reads. Use `make -B` after changing the list, because the binary does not depend on it.

The documentation, the IDE and `tools/docs.py` describe the full build: a library left out of one binary is still part of the
language. For C++, `Engine::Options` keeps every field in every build, so a host compiles against either; a field of a library
that is not there is not read.

| Build | How |
| :--- | :--- |
| `cuffc` | `make -B EXCLUDE_DLC="network hardware"` (also `make wasm`) |
| `cuffc.exe` | `mingw32-make -f Makefile.win -B EXCLUDE_DLC="network hardware"` |
| `cuffsh` | `EXCLUDE_DLC="network hardware" ./cli/build.sh`; on Windows `set EXCLUDE_DLC=network hardware` and then `cli\build.bat` |
| Android | `android/build.sh EXCLUDE_DLC="network hardware"`, or `-DCUFF_EXCLUDE_DLC="network;hardware"` for CMake |
| A host's own build | `-DCUFF_EXCLUDE_DLC_NETWORK -DCUFF_EXCLUDE_DLC_HARDWARE` on its compiler's command line |

What each library adds to `cuffc` on Linux, measured as the code and data that leaving it out takes away (GCC 13, `-O3 -flto`,
stripped; the file itself shrinks in whole 4 KB pages):

| Library | Code and data | Share of `cuffc` |
| :--- | ---: | ---: |
| `math` | 42.6 KB | 3.6% |
| `string` | 17.4 KB | 1.5% |
| `time` | 3.1 KB | 0.3% |
| `datetime` | 21.6 KB | 1.8% |
| `random` | 8.5 KB | 0.7% |
| `list` | 27.4 KB | 2.3% |
| `map` | 8.6 KB | 0.7% |
| `json` | 29.2 KB | 2.5% |
| `network` | 35.5 KB | 3.0% |
| `filesystem` | 37.8 KB | 3.2% |
| `csv` | 18.8 KB | 1.6% |
| `hash` | 43.1 KB | 3.7% |
| `path` | 15.2 KB | 1.3% |
| `hardware` | 35.6 KB | 3.0% |

Leaving all fourteen out takes 354 KB of code and data, 30% of the binary (860,704 bytes instead of 1,225,248). That is more than
the fourteen numbers add up to (344 KB), because a helper that two libraries share goes only when both do.

To test such a build, give the runner the same names: see [Testing](testing.md#a-reduced-build).

## Checking a build the way CI does

Before sending a change, build with the same strictness on the compilers you have:

```sh
g++     -std=c++17 -Wall -Wextra -Werror -O2 -o /tmp/cuffc main.cpp
clang++ -std=c++17 -Wall -Wextra -Werror -O2 -o /tmp/cuffc main.cpp
clang++ -std=c++17 -stdlib=libc++ -Wall -Wextra -Werror -fsyntax-only main.cpp   # the macOS standard library
```

Header-only code that compiles with libstdc++ can still miss an `#include` that libc++ needs, which is the usual way a build
that passes on Linux fails on macOS. For Windows without a Windows machine, a MinGW-w64 cross compiler
(`x86_64-w64-mingw32-g++`) catches errors in the `#ifdef _WIN32` code, and the result can be run under Wine.

Sanitizer builds are described in [Testing](testing.md#sanitizers).

## Line endings

`.gitattributes` fixes the ones that matter: shell scripts, makefiles and test fixtures (`*.cuff`, `*.expected`, `*.expected_code`)
are checked out with LF on every platform, and `.bat` files with CRLF. Without it a Windows checkout would turn a bash script into
"bad interpreter: bash\r" and change the contents of a test program that contains a multi-line string. If a script fails with a
stray `\r` in its error message, the file was checked out without these rules.

## Platforms

| Platform | Toolchain verified | Notes |
| :--- | :--- | :--- |
| Linux | GCC, Clang | |
| macOS | Apple Clang, libc++ | The Command Line Tools are enough. |
| Windows | MinGW-w64 (GCC) | Source is read as bytes, so CRLF files and non-ASCII paths behave the same as elsewhere. |
| WebAssembly | Emscripten | Has its own default stack budget; see [Resource limits](../internals/resource-limits.md). |
| Android | NDK r23 or newer (Clang, bionic) | `android/`: `cuffc` and `cuffsh` for arm64-v8a, armeabi-v7a, x86_64 and x86; see `android/README.md`. CI builds it on Linux. |
