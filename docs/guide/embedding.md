# Embedding the engine

The language engine is a set of C++17 headers under `engine/`. There is nothing to link: include the header you need
and compile with `-I<repository root>`. A program that uses it looks like this:

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

`print` writes to `std::cout` and `input` reads from `std::cin`; to capture or supply them, replace the stream buffers
(`std::cout.rdbuf(...)`) around the call.

## The two entry points

| Use | When |
| :--- | :--- |
| `CuffEngine::execute(source, scriptDir, options)` | You have a whole program and want it run once. |
| `cuff::Session` | The program arrives in pieces (a shell, a notebook, a game console) and what an earlier piece declared must be visible to the next. |

Both report failures the same way: `success` is false and `error` holds the finished report, in the form `cuffc`
prints, with the source line and caret. A host prints `error` and does not format its own message; that is what keeps
every front end in step when the report changes. Neither entry point throws for a failing script.

```cpp
#include "engine/Session.h"

cuff::Session session(options);
auto a = session.run("set number n to 5\n");
auto b = session.run("print(n * 2)\n");   // sees n
session.reset();                           // forget everything
```

A function declared in one piece that fails while a later piece runs is reported against the piece that declared it,
and an error stops the run without leaving half of an `async` queue behind.

`CuffEngine::run(source)` only parses. It exists for tools that want the token streams and the syntax tree
(`--ast`).

## Options

`CuffEngine::Options` is the interpreter's configuration (`Interpreter::Config`), the same struct a `Session` takes:

| Field | Meaning | Default |
| :--- | :--- | :--- |
| `rootDir` | Where `use ... from` and `DLC:filesystem` may reach. | The script's folder |
| `maxSteps` | Loop iterations plus function calls before `E6-001`; `0` is unlimited. | `0` |
| `timeoutMs` | Milliseconds before `E6-002`; `0` is unlimited. | `0` |
| `stackBudgetBytes` | Native stack the evaluator may use; `0` derives it from the real stack. A larger value than the thread has is reduced to what exists. | `0` |
| `networkEnabled` | Whether `DLC:network` may be used. | `true` |
| `allowPrivateNetworkTargets` | Whether `DLC:network` may reach loopback and private addresses. | `false` |
| `filesystemEnabled` | Whether `DLC:filesystem` may be used. | `true` |

Paths in the options and in `scriptDir` are the platform's own narrow encoding. On Windows that is the ANSI code
page; the shell converts its UTF-8 paths with `platform::enginePath` (see `cli/Platform.h`) before handing them over.

A host that runs scripts it did not write should set `maxSteps` and `timeoutMs`, switch off the libraries it does not need,
and apply process-level memory and CPU limits as well. [Security](../../SECURITY.md) says why, and what the engine does and
does not protect.

## Threads and stacks

A `Session` is not shared between threads; give each thread its own. The only state threads do share is the table of
identifier names, which is locked (and grows by every distinct name the process ever sees, so count it among the things a
host limits if it runs unbounded numbers of unrelated untrusted scripts). The engine measures the stack
it is really running on, so it is safe on a thread with a small stack (the tests run it on threads of 512 KiB and 1 MiB),
and ends a runaway recursion with the catchable `E4-017` instead of overflowing. See
[Resource limits](../internals/resource-limits.md).

## WebAssembly and npm

`make wasm` builds `wasm/bindings.cpp` with Emscripten into `npm/dist/`, and the [`cuffscript-wasm`](../../npm/README.md)
package ships the result. The binding exposes the same `execute` call to JavaScript: `mod.cuffRun(source, scriptDir)`.
