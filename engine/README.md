# `engine/`

The CuffScript language engine: header-only C++17, no dependencies. Everything that defines the language is here; the programs that
use it (`main.cpp`, `cli/`, `wasm/`) are thin.

| Folder | Contents |
| :--- | :--- |
| `common/` | Shared vocabulary: tokens, locations, errors and codes, limits, UTF-8 and width helpers, the path sandbox |
| `tokenizer/`, `lexer/`, `parser/` | Source text to syntax tree |
| `interpreter/` | The evaluator, values, scopes, native function table, module loader |
| `regex/` | The pattern language |
| `dlc/` | The libraries behind `use DLC:...` |
| `net/` | The HTTP client for `DLC:network` |
| `debug/` | The AST printer behind `cuffc --ast` |

Entry points: `CuffEngine.h` runs a whole program, `Session.h` runs it in pieces, `ParseProgram.h` is the one place the front-end
stages are chained, and `EngineOptionsCli.h` reads the command-line flags that configure the engine.

How it fits together is in [Architecture](../docs/internals/architecture.md), and which file does what is in the [Source map](../docs/internals/source-map.md); the public API is in the [C++ API reference](../docs/reference/cpp-api.md); how to add to it is in
[Extending the engine](../docs/contributing/extending.md); the conventions are in [Code style](../docs/contributing/code-style.md).
