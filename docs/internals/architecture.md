# Architecture

CuffScript is a tree-walking interpreter written as header-only C++17. Everything that defines the language is in
`engine/`; programs that use it (`main.cpp` for `cuffc`, `cli/` for `cuffsh`, `wasm/` for WebAssembly) are thin.

## From text to a running program

```text
 source text
     |  makeSourceRef                       common/SourceText.h
     v
 SourceRef --------------------------------------------------------.
     |  Tokenizer      characters -> tokens, INDENT/DEDENT/NEWLINE   |  every stage that
     v                 tokenizer/                                     |  can fail raises a
 raw tokens                                                           |  CuffError whose
     |  Lexer          keyword classification, colon rules  lexer/    |  location points
     v                                                              |  into the SourceRef
 tokens                                                               |
     |  Parser         tokens -> Program (AST)             parser/    |
     v                 each FunctionDecl remembers its SourceRef      |
 Program  <-- parseProgram() chains the three stages    ParseProgram.h
     |  Interpreter    walks the tree                     interpreter/
     v
 output, or a CuffError ------> renderErrorWithSnippet() --> the report text
```

| Folder | Contents |
| :--- | :--- |
| `engine/common/` | Shared vocabulary: tokens, locations, the error type and its codes, limits, UTF-8 and width helpers, the path sandbox. |
| `engine/tokenizer/` | Scans characters into tokens and turns indentation into block tokens. |
| `engine/lexer/` | Classifies words as keywords and checks the rules for colons. |
| `engine/parser/` | One parser per kind of construct, over a shared `ParserCore`. The AST types are in `ASTNodes.h`. |
| `engine/interpreter/` | The evaluator, the values, scopes, the native function table, and the module loader. |
| `engine/regex/` | The pattern language: parser, AST and matcher with step, time and recursion budgets. |
| `engine/dlc/` | The libraries behind `use DLC:...`. One header per library. |
| `engine/net/` | The plain-HTTP client used by `DLC:network`. |
| `engine/debug/` | The AST printer behind `cuffc --ast`. |

## The entry points

- `parseProgram()` (`engine/ParseProgram.h`) is the only place the three front-end stages are chained. The engine, the
  shell and the module loader all call it, so a change to the pipeline lands everywhere at once.
- `Session` (`engine/Session.h`) parses and runs pieces of source against one interpreter and returns failures already
  rendered. It owns the parsed programs, because the interpreter keeps pointers into them.
- `CuffEngine` (`engine/CuffEngine.h`) is the one-call convenience over `Session`, plus parse-only `run` for tools.
- `EngineOptionsCli.h` reads the command-line flags that configure the engine. `cuffc` and `cuffsh` both call it, so
  the two accept the same flags.

## Front ends

A front end reads input, hands text to the engine, and prints what comes back. It does not parse code, run it or build
an error message. The rule is the point: when the engine changes a report, an option or a limit, the front ends follow
without being touched, and the shell's output stays identical to `cuffc`'s (a test compares them over every script in the
repository).

| Front end | Source | Notes |
| :--- | :--- | :--- |
| `cuffc` | `main.cpp` | Reads a file or standard input, runs it once through `CuffEngine::execute`. |
| `cuffsh` | `cli/` | A long-lived `Session` with its own line editor. See [shell architecture](shell-architecture.md). |
| WebAssembly | `wasm/bindings.cpp` | Exposes `execute` to JavaScript. |

## Where state lives

- The interpreter holds the global scope, the table of user functions, the queue of `async` calls, the loaded modules and
  the module sources. A `Session` keeps one interpreter for as long as it lasts.
- Values are small tagged unions. Strings are shared and immutable; lists and maps are shared and mutable, which is why
  assigning one to a second name shares it.
- The one piece of process-wide state is the table that turns identifier names into numbers (`NameInterner`). It is
  locked, so one `Session` per thread needs no further care. The rest is per `Session`, or per-thread bookkeeping of how
  much stack is left.

## Related reading

- [Source locations and error reports](source-locations.md) for how an error finds its line.
- [The error model](error-model.md) for what an error is and who may catch it.
- [Resource limits](resource-limits.md) for how the engine stays inside the stack and memory it has.
- [Design notes](design-notes.md) for the history of individual decisions.
