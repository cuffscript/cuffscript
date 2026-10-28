# `wasm/`

`bindings.cpp` is the Emscripten entry point. It exposes two calls to JavaScript:

| Call | What it does |
| :--- | :--- |
| `cuffRun(source, scriptDir)` | Parses and runs the program, as `CuffEngine::execute` does, and returns `{ success, error }`. |
| `cuffDump(source)` | Parses without running and prints the token and syntax-tree dump through `print`, as `cuffc --ast` does. It returns `{ success, error }`. |

The binding runs programs with the engine's default options; it does not expose the budgets or the switches for the network and file libraries.

```sh
make wasm        # needs the Emscripten SDK; writes npm/dist/
```

The output is published as the [`cuffscript-wasm`](../npm/README.md) package. Build options are in
[Building](../docs/contributing/building.md); how a host uses the engine, and what to decide before running code you did not write, is in
[Embedding the engine](../docs/guide/embedding.md#webassembly-and-npm).
