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

## API

| Member | What it does |
| :--- | :--- |
| `mod.cuffRun(source, scriptDir)` | Parses and runs the program. `scriptDir` is the directory in the virtual file system that `use <name> from <path>` is resolved against. Returns `{ success, error }`; on failure `error` is the complete report, with the source line and a caret. |
| `mod.cuffDump(source)` | Parses without running and writes the token and syntax-tree dump through `print`. Returns `{ success, error }`. |
| `mod.FS` | Emscripten's virtual file system, for the files of a multi-file project. |

The module configuration takes `print` (called once per line a program prints), `printErr`, `stdin` (called once per byte that `input()` reads; return a byte from 0 to 255, or `null` at the end of the input) and `locateFile` (to fetch the `.wasm` file from somewhere else). Full types: `dist/cuffscript.d.ts`.

## Limits of the browser build

The module runs a program with the engine's default options: no step or time budget, and every library enabled. A browser cannot make ordinary network connections, so `DLC:network` is not usable there. Memory is capped by the build flags in the repository's `Makefile`.
A page that runs code it did not write should run the module in a Web Worker that it can terminate. [Embedding the engine](https://github.com/cuffscript/cuffscript/blob/main/docs/guide/embedding.md#webassembly-and-npm) says more.

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
