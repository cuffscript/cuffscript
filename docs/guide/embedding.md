# Embedding the engine

The language engine is a set of C++17 headers under `engine/`. There is nothing to link: include the header you need and
compile with `-I<repository root>`. A complete, buildable host program lives in
[`examples/embedding`](../../examples/embedding/README.md); this page explains what it does and what to decide before you
ship one. Every type and function is listed in the [C++ API reference](../reference/cpp-api.md).

```cpp
#include "engine/CuffEngine.h"
#include <iostream>

int main()
{
    cuff::CuffEngine::Options options;
    options.timeoutMs = 2000;          // stop a runaway script after two seconds
    options.networkEnabled = false;    // this host does not want scripts reaching the network

    auto result = cuff::CuffEngine::execute("print(1 + 1)\n", ".", options);
    if (!result.success)
        std::cerr << "ERROR: " << result.error << "\n";
    return result.success ? 0 : 1;
}
```

```sh
g++ -std=c++17 -I/path/to/cuffscript -o host host.cpp
```

The headers compile without warnings under `-Wall -Wextra`. The engine uses exceptions internally, so build with exceptions enabled
(the default); it does not use RTTI-dependent features that need special flags.

## Input and output

`print` writes to `std::cout` and `input` reads from `std::cin`; the engine has no stream parameter of its own. To capture a script's
output or to supply its input, replace the stream buffers around the call, and put them back afterwards:

```cpp
std::ostringstream captured;
auto* previous = std::cout.rdbuf(captured.rdbuf());
auto result = cuff::CuffEngine::execute("print(6 * 7)\n", ".", options);
std::cout.rdbuf(previous);
// captured.str() is "42\n"
```

Error reports are never written by the engine: they come back in `result.error` for the host to print where it wants.

## The two entry points

| Use | When |
| :--- | :--- |
| `CuffEngine::execute(source, scriptDir, options)` | You have a whole program and want it run once. |
| `cuff::Session` | The program arrives in pieces (a shell, a notebook, a game console) and what an earlier piece declared must be visible to the next. |

Both report failures the same way: `success` is false and `error` holds the finished report, in the form `cuffc` prints, with the
source line and caret. A host prints `error` and does not format its own message; that is what keeps every front end in step when the report
changes. Neither entry point throws for a failing script, and neither lets an engine exception escape.

```cpp
#include "engine/Session.h"

cuff::Session session(options);
auto a = session.run("set number n to 5\n");
auto b = session.run("print(n * 2)\n");   // sees n
session.reset();                           // forget everything, keep the options
```

`scriptDir` is the folder that `use ... from` and `DLC:filesystem` resolve against (unless `Options::rootDir` is set). Pass `"."` when
the program has no file of its own.

A function declared in one piece that fails while a later piece runs is reported against the piece that declared it, and an error stops
the run without leaving half of an `async` queue behind. A `Session` keeps every program it has run until it is reset, because the interpreter
holds pointers into them, so a session that runs unbounded amounts of code should be reset now and then.

`CuffEngine::run(source)` only parses. It exists for tools that want the token streams and the syntax tree (`--ast`).

## Options

`CuffEngine::Options` is the interpreter's configuration (`Interpreter::Config`), the same struct a `Session` takes:

| Field | Meaning | Default |
| :--- | :--- | :--- |
| `rootDir` | Where `use ... from` and `DLC:filesystem` may reach. | The script's folder |
| `maxSteps` | Loop iterations plus function calls before `E6-001`; `0` is unlimited. | `0` |
| `timeoutMs` | Milliseconds before `E6-002`; `0` is unlimited. | `0` |
| `stackBudgetBytes` | Native stack the evaluator may use; `0` derives it from the real stack. A larger value than the thread has is reduced to what exists. | `0` |
| `networkEnabled` | Whether `DLC:network` calls are allowed. | `true` |
| `allowPrivateNetworkTargets` | Whether `DLC:network` may reach loopback and private addresses. | `false` |
| `filesystemEnabled` | Whether `DLC:filesystem` calls are allowed. | `true` |

A library that is switched off raises `E5-005` from every call; see [the rules for libraries](../reference/dlc/README.md#libraries-that-reach-outside-the-program).

Paths in the options and in `scriptDir` are the platform's own narrow encoding. On Windows that is the ANSI code page; the shell converts its
UTF-8 paths with `platform::enginePath` (see `cli/Platform.h`) before handing them over.

`cuffc` and `cuffsh` build their `Options` from the command line with `parseEngineOption` in `engine/EngineOptionsCli.h`, which a
host can call too, to accept the same flags with the same meaning.

A host that runs scripts it did not write should set `maxSteps` and `timeoutMs`, switch off the libraries it does not need, and apply
process-level memory and CPU limits as well. [Security](../../SECURITY.md) says why, and what the engine does and does not protect.

## Threads and stacks

A `Session` is not shared between threads; give each thread its own. Two pieces of state are shared by every `Session` in the process, and
both are locked, so sessions on different threads need no further care:

- the table of identifier names, which grows by every distinct name the process ever sees, so count it among the things a host limits if it
  runs unbounded numbers of unrelated untrusted scripts;
- the random generator behind [`DLC:random`](../reference/dlc/random.md). It is one generator for the process, so `random_seed` in one session
  changes the sequence the others see.

The engine measures the stack it is really running on, so it is safe on a thread with a small stack (the tests run it on threads of 512 KiB
and 1 MiB), and ends a runaway recursion with the catchable `E4-017` instead of overflowing. See [Resource limits](../internals/resource-limits.md).

## WebAssembly and npm

`make wasm` builds `wasm/bindings.cpp` with Emscripten into `npm/dist/`, and the [`cuffscript-wasm`](../../npm/README.md) package
ships the result. The binding exposes two calls to JavaScript:

| Call | What it does |
| :--- | :--- |
| `mod.cuffRun(source, scriptDir)` | Parses and runs the program, like `CuffEngine::execute`, and returns `{ success, error }`. |
| `mod.cuffDump(source)` | Parses without running and prints the token and syntax-tree dump through `print`, like `cuffc --ast`. It returns `{ success, error }`. |

The binding does not expose the engine options, so a script runs with the defaults: no step or time budget, and every library enabled. A browser cannot open
ordinary network connections, so do not expect `DLC:network` to work there. The memory of the module is capped by the build flags in the `Makefile`, which
also bounds what a script can allocate. A page that runs code it did not write should run the module in a Web Worker that it can terminate. Output reaches the page through the `print` callback given when the module is created, and `input` reads
through the `stdin` callback. Files that the program loads with `use ... from` are written into the module's virtual file system (`mod.FS`) before the call.
