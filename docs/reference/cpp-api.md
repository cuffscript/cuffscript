# C++ API

Everything a program needs to run CuffScript from C++. The engine is header-only (C++17), lives in the `cuff` namespace, and is
used by including a header and compiling with `-I<repository root>`. For the reasoning behind these choices, and what to decide before you ship
a host, read [Embedding the engine](../guide/embedding.md); [`examples/embedding`](../../examples/embedding/README.md) is a complete program.

## Headers

| Header | What it gives you |
| :--- | :--- |
| `engine/CuffEngine.h` | `CuffEngine`, the one-call entry point. Includes `Session.h`, `ParseProgram.h`, the error types and the limits. This is the header most hosts need. |
| `engine/Session.h` | `Session`, a program that runs in pieces, and `EngineOptions`. |
| `engine/ParseProgram.h` | `parseProgram`, source text to a syntax tree. |
| `engine/EngineOptionsCli.h` | `parseEngineOption`, the command-line flags that fill an `EngineOptions`. |
| `engine/common/ErrorFormat.h` | The functions that turn an error into the report `cuffc` prints. |
| `engine/common/CuffError.h` | The error classes the engine throws internally. |
| `engine/common/Limits.h` | The `cuff::limits` constants. |
| `engine/common/SourceText.h` | `SourceText`, `SourceRef` and `makeSourceRef`. |

## `cuff::CuffEngine`

A class of static functions; there is nothing to construct.

```cpp
struct CuffEngine::Result
{
    bool success = false;
    std::string error;                  // the full report, empty on success
    std::vector<Token> rawTokens;       // filled by run() when keepTokens is true
    std::vector<Token> lexedTokens;     // filled by run() when keepTokens is true
    std::unique_ptr<Program> ast;       // filled by run() only
};
using CuffEngine::Options = EngineOptions;
```

| Function | Behaviour |
| :--- | :--- |
| `static Result execute(const std::string& source, const std::string& scriptDir)` | Parses and runs `source` with the default options. |
| `static Result execute(const std::string& source, const std::string& scriptDir, const Options& options)` | The same with your options. `scriptDir` is the folder that `use ... from` resolves against. |
| `static Result execute(std::string&& source, const std::string& scriptDir, const Options& options)` | The same, taking the program text instead of copying it. A program can be as large as the [source limit](limits.md), so prefer this form for big text. |
| `static Result run(const std::string& source, bool keepTokens = true)` | Parses without running. On success `ast` holds the syntax tree; with `keepTokens` the two token streams are kept too. |
| `static void debugDump(const Result& result)` | Prints what `cuffc --ast` prints: the token streams and the tree on standard output, or the error on standard error. |

`execute` creates a `Session` for the call and discards it afterwards, so nothing a program declares outlives the call.

A failing script does not throw. `success` is `false` and `error` holds the report: the headline `[E4-006] Runtime Error at line 1, column 9: division by zero`,
the source line with a caret, and a hint when there is one. It carries no `ERROR: ` prefix; `cuffc` adds that when it prints. A report has this
shape (see [errors and debugging](../guide/errors-and-debugging.md)):

```text
[E4-006] Runtime Error at line 1, column 9: division by zero
    print(1 / 0)
            ^
```

`execute` and `Session::run` catch every exception a script can cause, so a host needs no `try` block around them.

## `cuff::Session`

A program that arrives in pieces. Each `run` parses and executes one piece against the same interpreter, so variables, functions and
loaded modules from earlier pieces are visible to later ones.

```cpp
struct Session::Outcome
{
    bool success = true;
    std::string error;      // the same report as CuffEngine::Result::error
};
```

| Member | Behaviour |
| :--- | :--- |
| `explicit Session(EngineOptions options = EngineOptions())` | Creates a session with its own interpreter. |
| `Outcome run(const std::string& source, const std::string& scriptDir = ".")` | Parses and runs one piece. Never throws for a failing script. |
| `Outcome run(std::string&& source, const std::string& scriptDir = ".")` | The same, taking the text instead of copying it. |
| `Outcome run(const SourceRef& text, const std::string& scriptDir)` | Runs text that the caller also keeps (a shell keeps its entries for history). |
| `void reset()` | Forgets everything the session declared and starts again with the same options. |
| `const EngineOptions& options() const` | The options the session was created with. |

A `Session` cannot be copied. It keeps every program it has run alive until `reset()` or until it is destroyed, because the interpreter holds
pointers into them; its memory grows with the amount of code it has run. A `Session` is not safe to share between threads; see
[threads](../guide/embedding.md#threads-and-stacks).

## `cuff::EngineOptions`

`EngineOptions` is another name for `Interpreter::Config`. Its fields, their meanings and their defaults are in the table in
[Embedding the engine](../guide/embedding.md#options). `rootDir` empty means the script's folder, and a budget of `0` means unlimited.

## `cuff::parseProgram`

```cpp
std::unique_ptr<Program> parseProgram(const SourceRef& source,
                                      std::vector<Token>* rawTokens = nullptr,
                                      std::vector<Token>* lexedTokens = nullptr);
```

Tokenizes, lexes and parses `source`, the one place where the three front-end stages are chained. It throws a `SyntaxError` (a `CuffError`) for a syntax problem (`E2-001`) or a
source larger than the limit (`E2-009`); the codes are in the [catalogue](errors.md). The engine entry points, the shell and the
module loader all call it. Pass pointers to receive the token streams.

## Sources

```cpp
struct SourceText { std::string name; std::string text; };
using SourceRef = std::shared_ptr<const SourceText>;
SourceRef makeSourceRef(std::string text, std::string name = std::string());
```

A `SourceRef` is source text that can be shared cheaply; the name is how an error says which file a location belongs to (it is empty
for the program you pass to `run`).

## Error reports

| Function (`engine/common/ErrorFormat.h`) | Behaviour |
| :--- | :--- |
| `std::string errorHeadline(const CuffError& e)` | The first line: `[E4-006] Runtime Error at line 3, column 9: division by zero`, or `... in 'lib/helper.cuff' at line 3 ...` when the error came from another file. |
| `std::string renderErrorWithSnippet(const CuffError& e, const std::string& fallbackSource)` | The full report: headline, the source line with a caret, any related block, then the hint. The line comes from the text the error points into, and only falls back to `fallbackSource` for an error that carries none. |
| `std::string renderCurrentException(const SourceRef& source)` | The full report for the exception being handled. Call it from inside a `catch` block; it is what `Session::run` uses. |
| `std::string buildCaretSnippet(const std::string& source, const SourceLocation& loc)` | Just the source line and caret. The caret is placed by display width (a tab is copied, a Hangul character takes two columns). Empty when there is no source to draw. |

A host that calls `parseProgram` directly and catches the exception itself can use these to produce the same report as the engine.

## Errors

Everything the engine throws derives from `CuffError` (which derives from `std::runtime_error`). The public fields are `code` (an `ErrorCode`),
`category` (the name shown in the headline), `location`, `message`, `hint`, `recoverable` (whether `or_else` can catch it), `source` and `related`.
`what()` is the headline and the hint, without the source line.

| Class | Raised for |
| :--- | :--- |
| `SyntaxError` | Anything found while tokenizing or parsing, and a source that is too large. |
| `RegexSyntaxError`, `RegexRuntimeError` | A pattern that is malformed, and matching that runs past a budget. |
| `CuffRuntimeError` | The base of the run-time errors below. |
| `TypeError`, `UndefinedVariableError`, `UndefinedFunctionError`, `ConstantError`, `IndexError`, `KeyError`, `DivisionByZeroError`, `ArgumentError`, `ValueError`, `ElementNotFoundError`, `ScopeError`, `StackOverflowError` | The specific run-time errors. |
| `ModuleError` | A module or sandbox problem (`E5-` codes). |
| `InternalEngineError` | A fault in the engine itself; it is a bug, not a script error. |

`ErrorCode` lists every code (`ErrorCode::DivisionByZero` is `E4-006`); the [error catalogue](errors.md) is generated from it. `Session::run` and `CuffEngine::execute` catch
all of these and report them as text, so a host sees these classes only when it calls `parseProgram` or drives an `Interpreter` itself.

## Command-line options

```cpp
enum class OptionStatus { NotEngineOption, Consumed, Invalid };
OptionStatus parseEngineOption(const std::vector<std::string>& args, size_t& i,
                               EngineOptions& options, std::string& error);
const char* engineOptionsHelp();
bool parseCount(const std::string& text, uint64_t& out);
```

`parseEngineOption` looks at `args[i]` and, if it is one of the engine flags (`--max-steps`, `--timeout`, `--root`, `--no-network`, `--allow-private-network`, `--no-filesystem`), applies it to
`options` and advances `i` past the flag's value. It returns `NotEngineOption` for anything else, so the host can handle its own flags, and `Invalid` with `error` set for a flag that is malformed (a missing or
non-numeric value). `engineOptionsHelp()` is the usage text for those flags, so a host documents them exactly as `cuffc` does. `parseCount` reads a non-negative decimal integer and rejects signs, spaces,
trailing text and overflow. The flags themselves are described on the [`cuffc`](cuffc.md#options) page.

## Limits

`engine/common/Limits.h` defines the constants in `cuff::limits` that bound a program (sizes, depths, recursion, pattern work, HTTP). The values and the error each one raises are on the
[limits](limits.md) page, which is generated from that header.

## WebAssembly

The Emscripten binding in `wasm/bindings.cpp` exposes `cuffRun(source, scriptDir)` and `cuffDump(source)` and returns `{ success, error }`. See
[Embedding the engine](../guide/embedding.md#webassembly-and-npm) and the [npm package](../../npm/README.md).
