# Source map

Where each part of the repository lives and what each file is for. [Architecture](architecture.md) explains how the parts fit together; this page is the index to use when you know what you want to change and need to know where. File sizes are not
listed because they change; the order is the order a program travels through the engine.

## The shape of the repository

| Path | Contents |
| :--- | :--- |
| `engine/` | The language engine: headers only, C++17, in the `cuff` namespace. |
| `main.cpp` | `cuffc`, the script runner. |
| `cli/` | `cuffsh`, the interactive shell, and its tests (`cli/tests/`). |
| `wasm/` | The Emscripten binding for the browser. |
| `npm/` | The npm package that ships the WebAssembly build. |
| `tests/` | The engine and language suite: unit tests, cases, error cases. |
| `examples/` | Runnable programs with expected output, error cases, and a C++ embedding example. |
| `tools/` | `docs.py` (the documentation generator and checker) and `introspect.cpp` (what it asks the engine). |
| `docs/` | The documentation. |
| `.github/` | Workflows, issue and pull request templates. |
| `Makefile`, `Makefile.win` | The build for `cuffc` (and `cuffc.exe` with MinGW-w64). |

## `engine/`: the front end

The stages that turn source text into a syntax tree, in order. They are chained in one place, `ParseProgram.h`, which the engine entry points, the shell and the module loader all use.

| File | What it does |
| :--- | :--- |
| `tokenizer/Tokenizer.h` | Turns source text into raw tokens: words, numbers, strings, operators, newlines, indentation. |
| `tokenizer/ScanState.h` | The cursor over the text (offset, line, column, whether a space came before); skips a byte order mark. |
| `tokenizer/StringScanner.h`, `Escapes.h` | String and f-string literals, and what each backslash escape stands for. |
| `tokenizer/NumberScanner.h`, `IdentifierScanner.h`, `OperatorScanner.h`, `CommentScanner.h`, `CharUtils.h` | The scanners for the other token kinds, and character classes. |
| `lexer/Lexer.h` | Classifies words as keywords or identifiers and runs the colon check. |
| `lexer/KeywordClassifier.h` | The table of reserved words. |
| `lexer/ColonValidator.h` | Rejects a space before a `:`. |
| `parser/Parser.h`, `ParserCore.h` | The token cursor, error construction and helpers shared by the parsers. |
| `parser/StatementParser.h` | Chooses the parser for a statement from its first token and attaches an `or_else`. |
| `parser/DeclarationParser.h` | `set` and `change`. |
| `parser/FunctionParser.h` | `set ... func` declarations and their parameters. |
| `parser/ControlFlowParser.h`, `LoopParser.h` | `if` / `else`, and `loop repeat` / `loop while`. |
| `parser/CollectionOpParser.h` | `add`, `remove` and the `replace` statement for collections. |
| `parser/ImportParser.h` | `use DLC:...` and `use name from path`. |
| `parser/OrElseParser.h` | Wraps a statement in its `or_else` handler. |
| `parser/ExpressionParser.h` | Expressions by precedence, indexing, slicing and calls. |
| `parser/LiteralParser.h` | Literals, f-strings (with the true position of each embedded expression), list and map literals. |
| `parser/RegexExprParser.h` | `find`, `match`, `replace`, `split` and `count` expressions. |
| `parser/ASTNodes.h` | The syntax tree node types, built so that a very deep tree can be destroyed without recursion. |
| `debug/ASTPrinter.h` | Prints tokens and the tree for `--ast`. |

## `engine/`: running a program

| File | What it does |
| :--- | :--- |
| `interpreter/Interpreter.h` | Walks the syntax tree: statements, expressions, functions, `async` queue, modules, `or_else`, budgets, the stack plan. By far the largest file. |
| `interpreter/Value.h` | The value type (number, string, boolean, list, map, match, empty), equality, display form, and the read-only flag for constant lists. |
| `interpreter/Environment.h` | Scopes: function scopes are isolated from callers, block scopes chain to the enclosing scope; global bridging. |
| `interpreter/Signals.h` | How `return` and `stop` travel up the interpreter without exceptions. |
| `interpreter/NativeFunctions.h` | Registers the built-ins and the table of libraries that `use DLC:` looks up. |
| `common/StrData.h`, `Utf8.h`, `Width.h` | Shared immutable string storage with cached code point counts; UTF-8 helpers; terminal column widths. |
| `common/NameInterner.h` | Turns identifier names into small numbers; the one locked process-wide table. |
| `common/Arithmetic.h` | The floored remainder shared by `%` and `math_mod`. |
| `common/PathSandbox.h` | The containment check used for modules and files. |

## `engine/`: errors, limits and options

| File | What it does |
| :--- | :--- |
| `common/ErrorCodes.h` | Every error code, its one-line description, and which can be caught. The error catalogue is generated from it. |
| `common/CuffError.h` | The error classes the engine throws. |
| `common/ErrorFormat.h` | Turns an error into the report with the source line and caret. |
| `common/SourceLocation.h`, `SourceText.h`, `Token.h`, `TokenTypes.h` | Positions, the program text a position points into, and the token types. |
| `common/Limits.h` | Every limit the engine enforces. The limits page is generated from it. |
| `common/Attributes.h` | Compiler attributes (`CUFF_NOINLINE` and friends) and the Windows macro guards. |
| `CuffEngine.h`, `Session.h`, `ParseProgram.h` | The public API: [C++ API](../reference/cpp-api.md). |
| `EngineOptionsCli.h` | The command-line flags that fill the options, shared by `cuffc` and `cuffsh`. |

## `engine/`: patterns, libraries and the network

| File | What it does |
| :--- | :--- |
| `regex/RegexParser.h` | Turns a pattern string into a pattern tree: tokens, sets, groups, quantifiers, escapes. |
| `regex/RegexAst.h` | The pattern tree. |
| `regex/RegexMatcher.h` | Matches a tree against text, with the step, time and recursion budgets. |
| `regex/RegexEngine.h` | Compiles and caches patterns and exposes `is`, `match`, `find`, `replace`, `split` and `count`. |
| `dlc/DLCCommon.h` | Argument checking shared by every library, number parsing for `to_number`, case mapping. |
| `dlc/MathDLC.h`, `StringDLC.h`, `ListDLC.h`, `MapDLC.h`, `ConvertDLC.h`, `TimeDLC.h`, `RandomDLC.h`, `JsonDLC.h` | One library each: [`DLC:math`](../reference/dlc/math.md) and the others. |
| `dlc/FilesystemDLC.h`, `NetworkDLC.h` | The two libraries that act on the outside world. |
| `net/HttpClient.h` | The plain-HTTP client behind `DLC:network`, including the blocked-address check. |

## `main.cpp`, `cli/` and `wasm/`

| File | What it does |
| :--- | :--- |
| `main.cpp` | `cuffc`: reads the script (at most the source limit), parses the command line, runs or dumps the program, sets the exit status. |
| `cli/main.cpp` | `cuffsh`: parses the shell's options and starts the loop. |
| `cli/Repl.h` | The read-evaluate-print loop and the `:` commands, over one long-lived `Session`. |
| `cli/LineEditor.h` | The prompt editor: cursor movement, history, multi-line entries, paste. |
| `cli/Terminal.h`, `Platform.h`, `Text.h`, `Width.h`, `Interrupt.h`, `Banner.h`, `Version.h` | Terminal setup and restore, platform differences, text sanitising, Ctrl+C handling, the banner and the version. |
| `cli/build.sh`, `build.bat` | Builds `cuffsh` on Linux and macOS, and on Windows. |
| `wasm/bindings.cpp` | `cuffRun` and `cuffDump` for the browser. |

## `tests/`, `tools/` and `docs/`

| Path | What it does |
| :--- | :--- |
| `tests/run.sh`, `tests/run.ps1` | The runners for the engine and language suite. |
| `tests/unit/` | C++ unit tests, each a program with a `main()`. |
| `tests/cases/`, `tests/errors/` | Programs with the output they must print, and programs that must fail with a given code. |
| `cli/tests/` | The shell suites: `run_tests.sh`, `run_tests.ps1`, the Python harness and tests. |
| `tools/docs.py` | Generates the tables in the documentation and checks it; see [Documentation](../contributing/documentation.md). |
| `tools/introspect.cpp` | A small program that prints what the engine knows about itself (the libraries and their functions, the built-ins, the error codes and the limits), so the documentation check reads these from the engine. |
| `docs/guide/`, `docs/reference/`, `docs/internals/`, `docs/contributing/` | The four kinds of page; see [the documentation index](../README.md). |
