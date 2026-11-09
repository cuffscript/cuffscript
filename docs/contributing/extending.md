# Extending the Engine

This guide helps you find "which files do I touch?" when adding a feature to the
engine. Each entry is the minimal procedure, verified to actually work.

## 1. Adding a new built-in function (e.g. `print`, `math_sqrt`)

Built-ins that are always available go in `registerBuiltins()` in
`engine/interpreter/NativeFunctions.h`. Library functions that are only enabled by
`use DLC:name` go in that library's file under `engine/dlc/` (`MathDLC.h`,
`StringDLC.h`, `FilesystemDLC.h`, ...). The argument-validation helpers shared by
the libraries (`expectArgCount` and friends) and the UTF-8 text utilities live in
`engine/dlc/DLCCommon.h`. The signature is always
`Value(std::vector<Value>& args, const SourceLocation& loc)`.

```cpp
reg["my_func"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
{
    expectArgCount("my_func", args, 1, loc);
    double n = expectNumber("my_func", args, 0, loc);
    return Value::makeNumber(n * 2);
};
```

- If it must always exist, add it to `registerBuiltins()`.
- If its first argument is the value it works on (`str_upper(s)`, `list_sort(xs)`), also add one row to
  `methodTable()` in `engine/dlc/MethodTable.h`: the method name (the function name without its library prefix), the function,
  the kinds of value it accepts and the library it comes from. That one row makes `s.upper()` work, produces the "undefined
  method" hint and fills the Method column of the function index. A function with no receiver (`math_min`, `random_*`) gets no row.
- For a brand-new library, create a new file in `engine/dlc/` (include only
  `DLCCommon.h`), write a `registerXxxDLC()` function, `#include` the file from
  `NativeFunctions.h`, and add one row to `dlcTable()` in that file. The "unknown
  library" hint is generated from the table, so there is no second list of names to
  update. Make sure the name does not collide with an existing library.
- Name DLC functions `library_verb` (`math_sqrt`, `str_upper`, `list_sort`,
  `file_read`, ...). The name alone tells you which library a function belongs to,
  and because every library shares a single `natives_` map, it also lowers the risk
  of colliding with another library. The only exceptions are the polymorphic
  functions that deliberately share one implementation across several types
  (`length`/`contains`/`index_of`).
- A DLC function that touches the file system must not open paths directly; route
  them through `resolveSandboxedPath()` (`engine/dlc/FilesystemDLC.h`, which uses
  `isInsideRoot()` from `engine/common/PathSandbox.h` internally). Module loading
  and `DLC:filesystem` share the same sandbox root.
- A dangerous library that the host must be able to switch off entirely (such as
  network or file system) should get an options struct following the pattern of
  `NetworkDLCOptions`/`FilesystemDLCOptions`: add a member for it to `DLCOptions`, and
  pass the value along `Interpreter::Config` (the type `CuffEngine::Options` and
  `Session` use as well) → `DLCOptions` (filled in by `Interpreter::execUse()`) → your
  library. The command-line flag is added once, in `engine/EngineOptionsCli.h`
  (`parseEngineOption` and `engineOptionsHelp`); `cuffc` and `cuffsh` both read their
  engine flags through it, so neither needs to change.
- Every function you add must be mentioned on its library's page under
  `docs/reference/dlc/`: `python3 tools/docs.py check` fails otherwise, and the function
  index is generated.
- `expectArgCount`/`expectArgRange`/`expectNumber`/`expectStr` validate arguments
  and produce consistent `ArgumentError`/`TypeError` messages — reuse them in new
  functions. For integer arguments use `expectWhole` (which also checks the ±2^53
  range).
- A function that can build a large string or list should check the size with
  `ensureStringSize`/`ensureItemCount` before building the result (the limits are
  in `engine/common/Limits.h`).

## 2. Adding a new value type

Extend the `Value` class in `engine/interpreter/Value.h`.

1. Add an entry to `ValueType` and a name to `valueTypeName()` (`Pattern` and `Object` are the most recent additions,
   and show what a reference-like type needs: its data lives behind a `shared_ptr`).
2. Add a member to the `Value::Payload` union and a case to `copyCounted()`, `moveCounted()` and `releaseCounted()` (a type that
   holds no pointer is also listed in `isPlain()`), and add `make*`/`as*`/`is*` helpers. A `Value` is a tag and a payload that is
   written by hand rather than a `std::variant`, so that copying, moving and dropping a number costs no call. Add the type to
   `receiverBit()` in `engine/dlc/MethodTable.h` so that methods can be called on it.
3. Add a new case to the switches in `truthy()`, `appendDisplay()` and
   `strictEquals()` (`equalsImpl`/`scalarEquals`) — the compiler catches a missing
   case via `-Wswitch`. The same goes for any other switch that branches on
   `ValueType`, such as `jsonStringifyInto()` in `engine/dlc/JsonDLC.h`.


## 3. Adding a new statement or expression

1. `engine/parser/ASTNodes.h`: define the new struct and add it to `StmtKind`/
   `ExprKind` and the matching `variant`.
2. Add the parsing logic to the appropriate parser file (`StatementParser.h`
   dispatches on a statement's first token, and `LiteralParser::parsePrimary`
   dispatches on an expression's first token). If you need a new reserved word, add
   it to `engine/common/TokenTypes.h` and `engine/lexer/KeywordClassifier.h`, and decide whether it may still be used as a
   name (see section 9). A new reserved word breaks every program that used it as a name, so it needs an entry in the
   changelog.
3. Add an output case to `engine/debug/ASTPrinter.h` (you can check it with `--ast`).
4. Add the execution logic to the `execStatement`/`evalExpr` switches in
   `engine/interpreter/Interpreter.h`.

If two parser classes have to call each other (for example, a new expression that
must parse sub-expressions), follow the pattern `LiteralParser.h` uses: keep only
a forward declaration plus the member function **declarations** in the header, and
write the actual definitions at the bottom of `ExpressionParser.h` (where
`LiteralParser::parsePrimary` is defined), after both classes are complete types.

A statement that changes a value takes a **place** as its target: an assignment, `add` and `remove` all read it with the same
function (`PlaceParser`), so a new statement of that kind needs no target rule of its own.

## 4. Adding a new kind of error

1. Add a new value to `ErrorCode` in `engine/common/ErrorCodes.h`, inside the right
   numeric band (1000=lexical, 2000=syntax, 3000/3100=regex syntax/runtime,
   4000=runtime, 5000=module, 6000=resource limit (not caught by `or_else`),
   7000=errors the program raises with `fail`, 9000=internal).
2. If needed, add a small subclass to `engine/common/CuffError.h` (skip this step if
   one of the existing classes is enough — for example, you can throw
   `CuffRuntimeError(ErrorCode::MyCode, ...)` directly).
3. The tag shown on screen (the `E4-030` form) is generated from the number (`4030`)
   by `errorCodeTag()` — do not write the tag string by hand; only write it in that
   form in a test's `.expected_code`.
4. Whether an error is `recoverable` is decided automatically by the code's numeric
   band (`errorCodeRecoverable()`) — 3100 to 5999 and 7000 to 7999 can be caught by `or_else`, the
   rest cannot. A caught error reaches the program as an `Error` object, so a new catchable code needs nothing else to be
   readable from a handler.
5. Write a one-line description after the enumerator (`Name = 4031, // What this means.`).
   The [error catalogue](../reference/errors.md) is generated from it: run
   `python3 tools/docs.py generate`. A code that nothing raises is listed as reserved
   automatically. Add a `cuff error CODE` example for it to the catalogue by hand; the documentation check runs it.
6. Add a `tests/errors/<area>/` case for the new code (the areas are listed in `tests/README.md`); `python3 tests/coverage.py` fails until the code has a test.

## 5. Adding a "freezable" state to a value type (e.g. `isConstant` on `constant list`)

This is the pattern for changing behavior by adding a few flags to an existing type
instead of creating a new value type (see `constant list` in v2.0.0).

1. Put the flag directly on the C++ struct/class (`ValueList::isConstant`) — not on
   `Value` itself or on a separate wrapper. Lists and maps are reference types
   (`shared_ptr`), so the flag has to live on the value object for it to follow
   aliases and function arguments.
2. At the point that first creates the state (a declaration, say), do not reuse the
   existing value as is: **copy it into a new object** and set the flag on that.
   Otherwise you can silently freeze an object another variable already refers to
   (see `Interpreter::freezeList`).
3. The places that must check the state are the ones that "directly mutate" the
   value — content changes (`add`/`remove`/`x[i] = ...`), not variable
   reassignment. Separately from the existing per-variable constant check
   (`constants_` in `Environment`), add code that checks the value's own flag to the
   execution part of those operations.
4. Decide up front whether nested elements are frozen too (deep immutability) or
   not (shallow). `constant list` is shallowly immutable, like a Python tuple: it
   locks only the top-level slots and leaves any lists/maps inside as they are — a
   new type may choose differently.

## 6. Toggling per-call state with function scope (e.g. `pure`)

This is the pattern for adding a modifier that is only valid "during this function
call", like `async` and `pure`.

1. Add a boolean field to `FunctionDecl` and fill it in the parser (the modifier loop
   in `FunctionParser::parse`). The new keyword also has to be accepted by
   `FunctionParser::atFunctionStart`, so that `newkeyword func ...` is recognized as a
   function declaration where `StatementParser.h` dispatches on the first token. A
   modifier that is written twice is a syntax error.
2. Add a member to `Interpreter` for the state, and have `FrameGuard` save the previous
   value on call entry and restore it on exit (so each frame has its own value even
   under recursion). This state belongs to `Interpreter`, not to `Environment`;
   `currentFunctionPure_` is done this way.
3. Put the actual restriction or behavior at the places that check the flag. A
   function scope sees only its own locals plus the names it bridged with
   `global x`, so `pure` is enforced where a name fails to resolve:
   `Interpreter::throwUnresolved()` (reached from the identifier lookup in
   `evalExpr`, `execChange` and `execCollectionOp`) raises the pure-specific error
   when the missing name exists as a global. A pure function can never bridge, so it
   can never reach a global. A pure method may also not change a field
   (`PureFunctionFieldWrite`), which is checked where a field is written. If your new
   restriction has a different condition, pick the points that fit it.
4. By default the flag applies **only to that function's own body** and is not
   propagated to other functions it calls (when the callee finishes, `FrameGuard`
   automatically restores the previous value). For `pure` that default is too easy
   to bypass (just wrap a general function that touches globals in one more call),
   so at the call sites (`evalCall`, `invokeAwaited` and the method call path)
   `checkPureCallAllowed()` additionally enforces "a pure function cannot call a user
   function that is not pure" — follow this pattern if you need a restriction that
   propagates through the whole call graph.

## 7. Adding a new token to regex patterns (e.g. `[newtoken]`)

1. `engine/regex/RegexAst.h`: add a new `RNodeKind` or a new character-class
   predicate if needed.
2. Add a `body == "newtoken"` branch to `parseBracket()` in
   `engine/regex/RegexParser.h`.
3. For a variable-length pattern (like email/phone/URL), add it to `PresetKind` in
   `RegexAst.h` and add the matching logic to `presetLengths()` in
   `RegexMatcher.h`. For a fixed-length character test, all you need is a
   `std::function<bool(unsigned char)>`, like the named classes in
   `RegexParser.h`.
4. Write a small test program, in `/tmp` or similar, that includes only
   `engine/regex/RegexEngine.h` and use it to verify the new token (this is the
   method actually used while building this repository, and it lets you check the
   regex engine quickly without the whole language pipeline).

## 8. How to verify

After adding a feature, check at least the following.

```bash
make clean && make        # -Wall -Wextra -Werror, so a warning is a failure
./cuffc --ast your_test.cuff   # inspect the parse result (AST) by eye
./cuffc your_test.cuff         # check the actual run result
bash tests/run.sh              # the full regression suite (did existing features break?)
python3 tools/docs.py check    # are the docs still true? (see "Documentation" below)
```

For a new feature, add one test alongside it to `tests/cases/<area>/` (success cases) or
`tests/errors/<area>/` (error cases) — see [Testing](testing.md). When you refactor later, that
test is your safety net as is. A feature that changes what a user can see also changes a
page under `docs/`; [Documentation](documentation.md) says which, and what is generated.

## 9. Reserved words and names (parser)

Every reserved word is always reserved: `isNameToken()` in `ParserCore.h` accepts an identifier and nothing else. When a new statement/declaration syntax has a spot where
"a name goes here", use `isNameToken(p.current())` (or `p.consumeName(...)`). A reserved word there:
a block or control-flow keyword (`end`, `do`, `if`, ...), an operator written as a word (`is`, `and`, `or`, `matches`,
`isa`) or a literal (`true`/`false`/`empty`) appearing where an expression should be usually means a real syntax error
with a missing operand, and accepting it as a name would turn a clear "unexpected token" into a misleading runtime error.

A name that a construct *declares* (a variable, a parameter, a loop variable, the name of `or_else as`) goes through `ParserCore::declareName`, which refuses a name that the function has already declared (`E2-010`). `NameScope` ends the names of a block with the block, and `FunctionNames` gives a function a list of its own, so a new construct that declares a name calls `declareName` and opens a `NameScope` for its body.

Two more rules decide what a word means from where it stands:

- **After a dot.** A member name is read by `consumeMemberName()`, which accepts any word, keywords included
  (`text.repeat(3)`, `m.end`), because a dot leaves no doubt about what the word is.
- **Type names.** The names of the types (`number`, `list`, `pattern`, ...) and of classes are not reserved. They are
  read as types only where a type may stand, which `TypeParser::atType` decides: at the start of a declaration (a type, a name and `=`), before a
  parameter name, in a field declaration, in `loop repeat` and `loop each`, and after `or_else as`. Anywhere else they are ordinary identifiers.

Adding a new reserved word is a breaking change for programs that used it as a name; see section 3.

## 10. Source line and caret (`^`) in error messages

`CuffError::what()` renders only the message and the hint. The report a user sees,
with the source line and the `^`, is built by `renderErrorWithSnippet()`
(`engine/common/ErrorFormat.h`), which `Session::run()` and `CuffEngine::execute()` call
for you, so a front end never builds one. Code that throws a new error only has to fill
in an accurate `SourceLocation`: take it from the token that starts the construct. The
caret is automatic and is placed by display width, so it stays aligned across tabs and
Hangul.

Which text a location points into is carried by the error (`CuffError::source`); the
parser stamps every function it declares, so an error inside a module or inside a
function from an earlier shell entry is drawn from the right file. If you add something
that runs code from another source (a new kind of import, say), run its body inside the
same `attachSource` guard `execFunctionBody` uses. [Source locations and error
reports](../internals/source-locations.md) explains all of it. If you change this
behaviour, `tests/unit/error_snippet_test.cpp` will tell you, including a check that
the reported line and column always agree with the offset.

## 11. Portability and build switches

The engine is standard C++17 with no third-party dependencies. The only code that
touches the platform is isolated: `engine/net/HttpClient.h` (POSIX sockets, or
WinSock behind `#ifdef _WIN32`) and the terminal code in `cli/` (`Platform.h`,
`Terminal.h`, `LineEditor.h`). Keep it that way: put anything platform-specific in
its own header behind a macro and let the rest of the engine see a small interface,
as `NetworkDLC.h` does with the HTTP client.

- **No sockets.** Build with `-DCUFF_DISABLE_NETWORK` to leave out the HTTP client
  and its headers (`make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK`, and the same
  variable works for `make wasm` and `Makefile.win`). `use DLC:network` still loads;
  its functions fail with `DLCFeatureUnavailable` (E5-005). The four
  `tests/errors/libraries/network_*.cuff` cases exercise the client itself, so they are
  expected to fail in such a build.
- **Paths from scripts.** Wherever text written in a script becomes a file system
  path, use `std::filesystem::u8path()` rather than `std::filesystem::path(str)`,
  which reads the string in the ANSI code page on Windows and garbles non-ASCII
  names. Paths the host hands in (`scriptDir`, `rootDir`) are used as given.
- **Text is UTF-8** everywhere inside the engine. Convert at the edge (a console, a
  file name from the OS), not in the middle.
- **Checking a change on other targets.** Run the suite under the sanitizers (see
  `tests/README.md`), build the `CUFF_DISABLE_NETWORK` variant if you touched
  `NetworkDLC.h`. CI also runs `tests/run.ps1` on Windows, so a change to paths or
  the CLI is checked there.

## 12. When touching function parameter syntax

The parameter list is parsed in one place, `FunctionParser::parseParam`
(`type name [= default]`). A new built-in type is added in `TypeKind` and `builtinTypeFromName`
(`ASTNodes.h`) and in `Interpreter::acceptsType`, which declarations, assignments, parameters, fields, loop variables and `or_else as`
all use (`-Wswitch` catches a missing case in the switches). Type checking happens in
`evalCall`/`invokeAwaited` **before** dispatch, so an async function also errors at the
call site — do not defer it until after the job is queued. Default values are worked out
at each call, in the scope of the function, after the arguments that were passed.

## 13. When touching `use` statement syntax

A DLC import is the `UseStmt::dlcs` list (name + each one's location), and a module
import is `name`/`path` (three places: `ImportParser.h`, `Interpreter::execUse` and
`ASTPrinter.h`). A `use` spanning several lines is deliberately unsupported, because
the language defines a module-load command as one line (`language.md`, section 9).

## 14. Code comment policy

Comments in `engine/` record only "what the code alone cannot tell you", in a line
or two — lifetime/ordering invariants, platform quirks, stack-safety limits, and the
reason for a non-obvious check. Let names and code say "what it does", and put long
explanations in this document or in `design-notes.md` (see section 39).
When you change a feature, fix the document entry that explains it as well.
