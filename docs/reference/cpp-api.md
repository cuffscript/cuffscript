# C++ API

Everything a program needs to run CuffScript from C++. The engine is header-only (C++17), lives in the `cuff` namespace, and is
used by including one header and compiling with `-I<repository root>`. For the reasoning behind these choices, and what to decide before you ship
a host, read [Embedding the engine](../guide/embedding.md); [`examples/embedding`](../../examples/embedding/README.md) is a complete program.

## Headers

| Header | What it gives you |
| :--- | :--- |
| `engine/CuffEngine.h` | `Engine`, `Args`, `HostFunction` and `EngineOptions`, with the values, the error types and the limits they use. This is the one header a host needs. |
| `engine/EngineOptionsCli.h` | `parseEngineOption`, the command-line flags that fill an `EngineOptions`. |
| `engine/DesktopHardware.h` | `desktopHardware()`, the parts of a desktop or laptop computer that `--hardware` gives [`DLC:hardware`](dlc/hardware.md). |
| `engine/common/ErrorFormat.h` | The functions that turn an error into the report `cuffc` prints. |
| `engine/common/CuffError.h` | The error classes the engine throws internally. |
| `engine/common/Limits.h` | The `cuff::limits` constants. |
| `engine/common/SourceText.h` | `SourceText`, `SourceRef` and `makeSourceRef`. |

## `cuff::Engine`

One `Engine` is one program, which can be run in pieces. Each `run` parses and executes a piece against the same interpreter, so the
variables, functions, classes and loaded modules of earlier pieces are there for later ones. The host can call the program's
functions, read and set its variables, and give it functions of its own.

```cpp
struct Engine::Outcome
{
    bool success = true;
    std::string error;   // the full report, empty on success
    Value value;         // what call() returned; empty otherwise
};
```

| Member | Behaviour |
| :--- | :--- |
| `explicit Engine(Options options = Options())` | An engine with its own interpreter. `Options` is `EngineOptions`. |
| `Outcome run(const std::string& source, const std::string& scriptDir = ".")` | Parses and runs one piece. `scriptDir` is the folder that `use ... from` resolves against. It returns when the piece and every [task](language.md#65-asynchronous-functions-and-tasks) it started have finished. The pieces of one engine are one program: a piece may not declare again a function, class or top-level variable that an earlier one declared (`E2-010`, `E4-038` for a class). |
| `Outcome run(std::string&& source, const std::string& scriptDir = ".")` | The same, taking the text instead of copying it. A program can be as large as the [source limit](limits.md), so prefer this form for big text. |
| `Outcome run(const SourceRef& text, const std::string& scriptDir)` | Runs text that the caller also keeps (a shell keeps its entries for history). |
| `Outcome call(const std::string& function, std::vector<Value> arguments = {})` | Calls a function an earlier `run` declared and gives its value in `Outcome::value`. The arguments are checked as a script's call is checked (`E4-010`, `E4-030`), and default values fill in the missing ones. An `async` function runs to its end, as `await f()` does; tasks the function starts have finished when `call` returns. |
| `std::optional<Value> get(const std::string& variable) const` | The value of a variable of the program's top level, or nothing when there is no such variable. A list, map or object is shared with the program. |
| `Outcome set(const std::string& variable, Value value)` | Assigns to a top-level variable as a script would: its type and `constant` are respected (`E4-020`, `E4-003`). A name the program does not have is declared as `any`. A name that is not an identifier, or is reserved, is `E4-025`. |
| `void addFunction(const std::string& name, HostFunction function)` | Makes `function` callable from scripts as `name(...)`, from the next piece that runs, for as long as the engine lasts. See [host functions](#host-functions). |
| `size_t collectCycles()` | Frees, now, the lists, maps and objects that refer only to each other, and gives how many there were. The engine does this by itself while a program runs; see [memory](#memory). |
| `void reset()` | Forgets everything the program declared and starts again with the same options and host functions. |
| `const Options& options() const` | The options the engine was made with. |
| `static Outcome execute(const std::string& source, const std::string& scriptDir = ".", const Options& options = Options())` | Runs a whole program once, with an engine made for the call and discarded after it. |
| `static Outcome execute(std::string&& source, const std::string& scriptDir, const Options& options = Options())` | The same, taking the text. |
| `static Parsed parse(const std::string& source, bool keepTokens = true)` | Parses without running. On success `ast` holds the syntax tree; with `keepTokens` the token streams before and after the lexer are kept in `rawTokens` and `lexedTokens`. |
| `static void dump(const Parsed& parsed)` | Prints what `cuffc --ast` prints: the token streams and the tree on standard output, or the error on standard error. |

Nothing here throws for something a script did. A failure comes back with `success` false and `error` holding the report: the headline `[E4-006] Runtime Error at line 1, column 9`,
the source line with marks under the place and the message beside them, and a hint when there is one. It carries no `ERROR: ` prefix; `cuffc` adds that when it prints. A report has this
shape (see [errors and debugging](../guide/errors-and-debugging.md)):

```text
[E4-006] Runtime Error at line 1, column 9
  |
1 | print(1 / 0)
  |         ^ division by zero
```

An error of `call` or `set` itself, such as a function that does not exist, has no source line to draw: its headline has no position and the message follows it.
Running out of memory is reported the same way (`E6-003`), from memory the engine set aside for it, and the engine can be used again afterwards.

An `Engine` cannot be copied. It keeps every program it has run alive until `reset()` or until it is destroyed, because the interpreter holds
pointers into them; its memory grows with the amount of code it has run. An engine runs one thing at a time: a host function that calls `run`,
`call` or `set` on the engine that is calling it gets an error (`E4-016`, "the engine is already running a program"). An `Engine` is not safe to
share between threads; see [threads](../guide/embedding.md#threads-and-stacks).

The 5.x names still compile: `Session` and `CuffEngine` are other names for `Engine`, and `CuffEngine::Result` and `Session::Outcome` for `Engine::Outcome`.
What 5.x called `CuffEngine::run(source)`, which only parsed, is `Engine::parse`, and `debugDump` is `Engine::dump`.

## Host functions

```cpp
using HostFunction = std::function<Value(Args&)>;
```

A host function receives the call's arguments and returns its value (`Value()` is `empty`). `Args` checks them the way a library function does,
so a wrong call is reported at the call, naming the function and the argument:

| Member of `Args` | Behaviour |
| :--- | :--- |
| `size_t size() const`, `const Value& operator[](size_t i) const` | The arguments, from 0. |
| `void expectCount(size_t n)`, `void expectCount(size_t lo, size_t hi)` | Exactly `n` arguments, or from `lo` to `hi` (`E4-010`). |
| `double number(size_t i)`, `const std::string& str(size_t i)`, `const ValueList& list(size_t i)`, `const ValueMap& map(size_t i)`, `bool boolean(size_t i)` | Argument `i` as that type: `E4-005` when it is another type, `E4-010` when there is no argument `i`. |
| `[[noreturn]] void fail(const std::string& message)` | Ends the call with `E4-042`, "`name() failed: message`", which a script can catch with `or_else`. |

A `std::exception` the function lets out is reported the same way, with its `what()` as the message; `std::bad_alloc` is `E6-003`. A function of
the program with the same name is the one a call reaches, as for the library functions, and a library loaded later does not replace a host
function. A class cannot take the name of one.

A host function runs inside the engine's run: it must not wait (a task that calls it is not set aside while it runs), and it may not use the
engine that called it.

```cpp
cuff::Engine engine;
engine.addFunction("host_add", [](cuff::Args& args) -> cuff::Value {
    args.expectCount(2);
    return cuff::Value::makeNumber(args.number(0) + args.number(1));
});
engine.run("func twice(number n) do:\n    return host_add(n, n)\nend\n");
cuff::Engine::Outcome r = engine.call("twice", {cuff::Value::makeNumber(21)});
// r.success is true and r.value.asNumber() is 42
```

## Values

`cuff::Value` is what a variable holds. Make one with `Value::makeNumber(double)`, `makeStr(std::string)`, `makeBool(bool)`, `Value()` for `empty`,
`makeList(std::make_shared<ValueList>())` or `makeMap(std::make_shared<ValueMap>())`; ask it with `type()`, `isNumber()`, `isStr()`, `isBool()`,
`isList()`, `isMap()`, `isObject()` and `isEmpty()`; read it with `asNumber()`, `asStr()`, `asBool()`, `asList()` (a `shared_ptr<ValueList>`, whose
`items` is a `std::vector<Value>`) and `asMap()` (a `shared_ptr<ValueMap>` with `get`, `set`, `has`, `remove`, `size`, `keys` and `values`).
Reading a value as a type it is not is a programming error, which a build without `NDEBUG` stops with an assertion. `toDisplayString()` is the text `print` writes.

A list, map or object is shared, as it is between two variables of a script: a change the host makes through a value it took with `get` is
seen by the program.

## Memory

Values are counted, and a list, map or object is freed when its last holder lets go of it. Those that refer to each other in a cycle are found
and freed by the engine while a program runs ([how](../internals/how-it-works.md#memory)); `collectCycles()` does it at once, and
`Options::collectCycles = false` leaves it to `collectCycles()` and to the engine's end. What the host holds is never freed under it. Two things are
the host's to watch:

- A value of an engine is let go of on the thread that uses that engine, or after the engine is gone.
- A cycle that the host still holds when its engine is destroyed is not freed by anything; take it apart (empty the list) before letting it go.

## `cuff::EngineOptions`

`EngineOptions` is another name for `Interpreter::Config`. Its fields, their meanings and their defaults are in the table in
[Embedding the engine](../guide/embedding.md#options). `rootDir` empty means the script's folder, and a budget of `0` means unlimited.

## Hardware

```cpp
#include "engine/CuffEngine.h"           // cuff::Hardware, cuff::BatteryState, cuff::HardwareUnsupported
#include "engine/DesktopHardware.h"      // cuff::desktopHardware(), only for what cuffc --hardware gives

struct BatteryState { double level = 0; bool charging = false; };
struct HardwareUnsupported : std::runtime_error { using std::runtime_error::runtime_error; };
struct Hardware {
    std::function<BatteryState()> battery;
    std::function<bool(int button)> button;
    std::function<void(int led, bool on)> led;
    std::function<void(double hertz, double seconds)> beep;
    std::function<void(int line, const std::string& text)> screenText;
    std::function<void(const std::string& text)> serialWrite;
    std::function<std::string()> serialRead;
    std::function<double(const std::string& name)> sensor;
    std::function<bool(int pin)> pinRead;
    std::function<void(int pin, bool high)> pinWrite;
    std::function<void(int pin, double duty)> pwm;
    std::function<double(int pin)> analog;
    std::function<bool(const std::string& part)> supports;  // optional
};
Hardware desktopHardware(const std::string& sysRoot = "/sys");
```

`EngineOptions::hardware` holds the parts that [`DLC:hardware`](dlc/hardware.md) reaches; an empty one is a part the device does not
have (`E4-044`). What each function is called with and must give back, and how what it throws reaches the script, is in
[Giving a program hardware](../guide/embedding.md#giving-a-program-hardware). The engine keeps its own copy, made with the engine and
kept by `reset()`.

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
| `std::string errorHeadlineOnly(const CuffError& e)` | The first line of a report: `[E4-006] Runtime Error at line 3, column 9`, or `... in 'lib/helper.cuff' at line 3 ...` when the error came from another file. |
| `std::string errorHeadline(const CuffError& e)` | The same with the message after it (`...column 9: division by zero`), for an error that has no source line to draw. |
| `std::string renderErrorWithSnippet(const CuffError& e, const std::string& fallbackSource)` | The full report: headline, the numbered source line with marks and the message beside them, any related block, then the hint. The line comes from the text the error points into, and only falls back to `fallbackSource` for an error that carries none. With no line to draw at all, the message goes on the headline. |
| `std::string renderCurrentException(const SourceRef& source)` | The full report for the exception being handled. Call it from inside a `catch` block. |
| `std::string renderCurrentExceptionSafely(const SourceRef& source, std::unique_ptr<char[]>* reserve = nullptr) noexcept` | The same, for a caller that must not fail in turn: when memory has run out it gives back `reserve` and tries again, and in the end gives an `E6-003` line or, failing even that, an empty string. It is what `Engine` uses. |
| `bool buildSnippet(const std::string& source, const SourceLocation& loc, Snippet& out)` | Finds the line `loc` points at: its number, text, the padding and the `^` marks. The padding is placed by display width (a tab is copied, a Hangul character takes two columns) and the marks cover the whole token. Returns `false` when there is no source to draw. |
| `std::string formatSnippet(const Snippet& s, const std::string& label, const std::string& indent)` | The three lines (blank gutter, numbered line, marks with `label` beside them), each starting with `indent`. |

## Errors

Everything the engine throws derives from `CuffError` (which derives from `std::runtime_error`). The public fields are `code` (an `ErrorCode`),
`category` (the name shown in the headline), `location`, `message`, `hint`, `recoverable` (whether `or_else` can catch it), `source`, `related`, and the backtrace:
`traceEntries()` gives the frames it came out of, innermost first (`TraceEntry`: `function`, `location`, `source`, `repeated`), with
`traceOmitted` frames left out after the first `kTraceHead`. `formatTrace(e)` in `engine/common/ErrorFormat.h` writes them as the report does.
`what()` is the headline and the hint, without the source line.

| Class | Raised for |
| :--- | :--- |
| `SyntaxError` | Anything found while tokenizing or parsing, and a source that is too large. |
| `RegexSyntaxError`, `RegexRuntimeError` | A pattern that is malformed, and matching that runs past a budget. |
| `CuffRuntimeError` | The base of the run-time errors below. |
| `TypeError`, `UndefinedVariableError`, `UndefinedFunctionError`, `ConstantError`, `IndexError`, `KeyError`, `DivisionByZeroError`, `ArgumentError`, `ValueError`, `ElementNotFoundError`, `ScopeError`, `StackOverflowError` | The specific run-time errors. |
| `ModuleError` | A module or sandbox problem (`E5-` codes). |
| `InternalEngineError` | A fault in the engine itself; it is a bug, not a script error. |

`ErrorCode` lists every code (`ErrorCode::DivisionByZero` is `E4-006`); the [error catalogue](errors.md) is generated from it. `Engine` catches
all of these and reports them as text, so a host sees these classes only when it drives an `Interpreter` or the parser itself.

## Command-line options

```cpp
enum class OptionStatus { NotEngineOption, Consumed, Invalid };
OptionStatus parseEngineOption(const std::vector<std::string>& args, size_t& i,
                               EngineOptions& options, std::string& error);
const char* engineOptionsHelp();
bool parseCount(const std::string& text, uint64_t& out);
```

`parseEngineOption` looks at `args[i]` and, if it is one of the engine flags (`--max-steps`, `--timeout`, `--root`, `--no-network`, `--allow-private-network`, `--no-filesystem`, `--hardware`), applies it to
`options` and advances `i` past the flag's value. It returns `NotEngineOption` for anything else, so the host can handle its own flags, and `Invalid` with `error` set for a flag that is malformed (a missing or
non-numeric value). `engineOptionsHelp()` is the usage text for those flags, so a host documents them exactly as `cuffc` does. `parseCount` reads a non-negative decimal integer and rejects signs, spaces,
trailing text and overflow. The flags themselves are described on the [`cuffc`](cuffc.md#options) page.

## Limits

`engine/common/Limits.h` defines the constants in `cuff::limits` that bound a program (sizes, depths, recursion, tasks, pattern work, HTTP). The values and the error each one raises are on the
[limits](limits.md) page, which is generated from that header.

## WebAssembly

The Emscripten binding in `wasm/bindings.cpp` exposes `cuffRun(source, scriptDir)`, `cuffDump(source)` and `cuffBytecode(source, scriptDir)` and returns `{ success, error }`. See
[Embedding the engine](../guide/embedding.md#webassembly-and-npm) and the [npm package](../../npm/README.md).
