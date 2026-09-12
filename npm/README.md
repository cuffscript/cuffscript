# cuffscript-wasm

Prebuilt WebAssembly build of the [CuffScript](https://github.com/cuffscript/cuffscript)
language engine (tokenizer → lexer → parser → interpreter), compiled with
[Emscripten](https://emscripten.org) from `engine/` and `wasm/bindings.cpp` in
the main repository.

This package ships **build output only** — `cuffscript.mjs` (ES module glue)
and `cuffscript.wasm` (the compiled engine). There is no JavaScript/TypeScript
wrapper here; that lives in the site that consumes this package (see the
`cuffscript-web-ide` repository's `src/engine/`).

## Install

```bash
npm install cuffscript-wasm
```

## Usage

```ts
import createCuffScriptModule from "cuffscript-wasm";

const mod = await createCuffScriptModule({
  print: (line) => console.log(line),
  printErr: (line) => console.error(line),
  stdin: () => null, // no input available
});

const result = mod.cuffRun('print("Hello, CuffScript!")', "/");
if (!result.success) {
  console.error(result.error);
}
```

To run a script that uses `use <name> from <path>`, write every source file
into `mod.FS` (e.g. `mod.FS.mkdirTree("/project/lib")`,
`mod.FS.writeFile("/project/lib/foo.cuff", "...")`) before calling `cuffRun`,
then pass the entry file's directory as `scriptDir`.

Full API surface: see `dist/cuffscript.d.ts`.

## Rebuilding

This package is not meant to build from source on install. To regenerate
`dist/cuffscript.mjs` and `dist/cuffscript.wasm`, install the
[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
and, from the repository root:

```bash
make wasm
```

This writes straight into `npm/dist/`. Then bump the version in
`package.json` (kept in step with the engine's own version) and run
`npm publish` from this directory.

## License

Apache-2.0, same as the parent [cuffscript](https://github.com/cuffscript/cuffscript)
repository — see `LICENSE`.
