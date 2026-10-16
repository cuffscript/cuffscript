# `wasm/`

`bindings.cpp` is the Emscripten entry point: it exposes `CuffEngine::execute` to JavaScript as `cuffRun(source, scriptDir)`.

```sh
make wasm        # needs the Emscripten SDK; writes npm/dist/
```

The output is published as the [`cuffscript-wasm`](../npm/README.md) package. Build options are in
[Building](../docs/contributing/building.md); how the engine is used by a host is in [Embedding the engine](../docs/guide/embedding.md).
