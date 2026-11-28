# `wasm/`

`bindings.cpp` is the Emscripten entry point. It exposes three calls to JavaScript:

| Call | What it does |
| :--- | :--- |
| `cuffRun(source, scriptDir)` | Parses and runs the program, as `CuffEngine::execute` does, and returns `{ success, error, exited, exitStatus }`. |
| `cuffDump(source)` | Parses without running and prints the token and syntax-tree dump through `print`, as `cuffc --ast` does. It returns `{ success, error, exited, exitStatus }`. |
| `cuffBytecode(source, scriptDir)` | Runs the script and prints each piece of bytecode through `print` as it is compiled, as `cuffc --bytecode` does. It returns `{ success, error, exited, exitStatus }`. |

The binding runs programs with the engine's default options; it does not expose the budgets or the switches for the network and file libraries.

```sh
make wasm        # needs the Emscripten SDK; writes npm/dist/
```

The output is published as the [`cuffscript-wasm`](../npm/README.md) package. Build options are in
[Building](../docs/contributing/building.md); how a host uses the engine, and what to decide before running code you did not write, is in
[Embedding the engine](../docs/guide/embedding.md#webassembly-and-npm).
