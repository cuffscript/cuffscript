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
| `mingw32-make -f Makefile.win` | `cuffc.exe` on Windows. It links the Windows socket library and asks for a larger stack, because the evaluator is recursive. |
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
| `-DCUFF_DISABLE_NETWORK` | Leaves out the socket client and its platform headers. `use DLC:network` still loads; its functions report that the feature is unavailable. The `tests/errors/network_*` cases test the client and are expected to fail in such a build. |

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
