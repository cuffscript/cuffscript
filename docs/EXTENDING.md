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
  pass the value along `CuffEngine::Options` → `Interpreter::Config` → `DLCOptions`
  (filled in by `Interpreter::execUse()`) → your library (the CLI flags are in
  `main.cpp`).
- `expectArgCount`/`expectArgRange`/`expectNumber`/`expectStr` validate arguments
  and produce consistent `ArgumentError`/`TypeError` messages — reuse them in new
  functions. For integer arguments use `expectWhole` (which also checks the ±2^53
  range).
- A function that can build a large string or list should check the size with
  `ensureStringSize`/`ensureItemCount` before building the result (the limits are
  in `engine/common/Limits.h`).

## 2. Adding a new value type

Extend the `Value` class in `engine/interpreter/Value.h`.

1. Add an entry to `ValueType` and a name to `valueTypeName()`.
2. Add the storage type to the `Value::Storage` variant, and add `make*`/`as*`/`is*`
   helpers.
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
   it to `engine/common/TokenTypes.h` and `engine/lexer/KeywordClassifier.h`.
3. Add an output case to `engine/debug/ASTPrinter.h` (you can check it with `--ast`).
4. Add the execution logic to the `execStatement`/`evalExpr` switches in
   `engine/interpreter/Interpreter.h`.

If two parser classes have to call each other (for example, a new expression that
must parse sub-expressions), follow the pattern `RegexExprParser.h` uses: keep only
a forward declaration plus the member function **declarations** in the header, and
write the actual definitions at the bottom of `ExpressionParser.h`, after both
classes are complete types.

## 4. Adding a new kind of error

1. Add a new value to `ErrorCode` in `engine/common/ErrorCodes.h`, inside the right
   numeric band (1000=lexical, 2000=syntax, 3000/3100=regex syntax/runtime,
   4000=runtime, 5000=module, 6000=resource limit (not caught by `or_else`),
   9000=internal).
2. If needed, add a small subclass to `engine/common/CuffError.h` (skip this step if
   one of the existing classes is enough — for example, you can throw
   `CuffRuntimeError(ErrorCode::MyCode, ...)` directly).
3. The tag shown on screen (the `E4-030` form) is generated from the number (`4030`)
   by `errorCodeTag()` — do not write the tag string by hand; only write it in that
   form in a test's `.expected_code`.
4. Whether an error is `recoverable` is decided automatically by the code's numeric
   band (`errorCodeRecoverable()`) — 3100 to 5999 can be caught by `or_else`, the
   rest cannot.

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
   value — content changes (`add`/`remove`/`change x[i]`), not variable
   reassignment. Separately from the existing per-variable constant check
   (`constants_` in `Environment`), add code that checks the value's own flag to the
   execution part of those operations.
4. Decide up front whether nested elements are frozen too (deep immutability) or
   not (shallow). `constant list` is shallowly immutable, like a Python tuple: it
   locks only the top-level slots and leaves any lists/maps inside as they are — a
   new type may choose differently.

## 6. Toggling per-call state with function scope (e.g. `pure`)

This is the pattern for adding a modifier that is only valid "during this function
call", like `async`/`returnable`/`pure`.

1. Add a boolean field to `FunctionDecl` and fill it in the parser (mainly the
   modifier loop in `FunctionParser.h`). The lookahead condition in
   `DeclarationParser::isFunctionDecl` also needs the new keyword, so that
   `set newkeyword func ...` is recognized as a function declaration.
2. Add a `bool currentFlag_ = false;` member to `Interpreter`, and have `FrameGuard`
   save the previous value on call entry and restore it on exit (so each frame has
   its own value even under recursion). This state belongs to `Interpreter`, not to
   `Environment` — `returnable` is done the same way, for the same reason.
3. Put the actual restriction or behavior at the places that check the flag. A
   function scope sees only its own locals plus the names it bridged with
   `change x to global`, so `pure` is enforced where a name fails to resolve:
   `Interpreter::throwUnresolved()` (reached from the identifier lookup in
   `evalExpr`, `execChange` and `execCollectionOp`) raises the pure-specific error
   when the missing name exists as a global. A pure function can never bridge, so it
   can never reach a global. If your new restriction has a different condition,
   pick the points that fit it.
4. By default the flag applies **only to that function's own body** and is not
   propagated to other functions it calls (when the callee finishes, `FrameGuard`
   automatically restores the previous value). For `pure` that default is too easy
   to bypass (just wrap a general function that touches globals in one more call),
   so at the call site (`evalCall`/`invokeAwaited`) `checkPureCallAllowed()`
   additionally enforces "a pure function cannot call a user function that is not
   pure" — follow this pattern if you need a restriction that propagates through
   the whole call graph.

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
```

For a new feature, add one test alongside it to `tests/cases/` (success cases) or
`tests/errors/` (error cases) — see `tests/README.md`. When you refactor later, that
test is your safety net as is.

## 9. Allowing reserved words in name positions (parser)

When a new statement/declaration syntax has a spot where "a name goes here", use
`isWordLikeToken(p.current())` (`ParserCore.h`) instead of
`p.check(TokenType::IDENTIFIER)`. Otherwise words that are reserved elsewhere, such
as `add`, `count` and `find`, are rejected as names. A name position is a fixed spot
in the grammar where "exactly one word comes and the next token is determined", so
accepting reserved words there is not ambiguous.

Reading a name back in expression position (`LiteralParser::parsePrimary`) is
different — it is the only place where the token type decides "what to parse", so
the handling depends on whether the keyword means something else in expression
grammar:
- Connectors that mean nothing in expression grammar
  (`add`/`to`/`in`/`by`/`global`/`not`/`from`) become plain identifiers when added
  to `isBareIdentifierKeyword()`.
- Keywords that already have their own expression syntax
  (`match`/`find`/`replace`/`split`/`count`) use a one-token lookahead
  (`looksLikeConstructContinuation()`) to tell "that syntax starts here" from "it is
  just a name". `(` and `[` are deliberately left out of that check: postfix parsing
  turns `name(...)` into a call and `name[...]` into an indexing, so treating them
  as "construct start" here would leave no way to call or index a function/variable
  with that name.
- Block/control-flow keywords (`end`, `do`, `if`, ...) and the literals
  (`true`/`false`/`empty`) are deliberately kept reserved — such a token appearing
  where an expression should be usually means a real syntax error with a missing
  operand, and accepting it as a name would turn a clear "unexpected token" into a
  misleading runtime error.

## 10. Source line and caret (`^`) in error messages

`CuffError` does not know the source text — `what()` renders only the message and
the hint. The source line and `^` in the final message the user sees are attached
by `renderErrorWithSnippet()` at the point where `CuffEngine::execute()`/`run()`
catches the `CuffError` (where the original source is still in scope). Code that
throws a new error only has to fill in an accurate `SourceLocation` (especially
`offset`, a byte offset), and the caret is automatic — its horizontal position is
computed in code points rather than bytes, so it stays aligned even when Hangul or
similar text comes earlier on the same line. If you change this behavior,
`tests/unit/error_snippet_test.cpp` will tell you.

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
  `tests/errors/network_*.cuff` cases exercise the client itself, so they are
  expected to fail in such a build.
- **Paths from scripts.** Wherever text written in a script becomes a file system
  path, use `std::filesystem::u8path()` rather than `std::filesystem::path(str)`,
  which reads the string in the ANSI code page on Windows and garbles non-ASCII
  names. Paths the host hands in (`scriptDir`, `rootDir`) are used as given.
- **Text is UTF-8** everywhere inside the engine. Convert at the edge (a console, a
  file name from the OS), not in the middle.
- **Checking a change on other targets.** Run the suite under the sanitizers (see
  `tests/README.md`), build the `CUFF_DISABLE_NETWORK` variant if you touched
  `NetworkDLC.h`, and run `tests/run.ps1` on Windows if you touched paths or the
  CLI.

## 12. When touching function parameter syntax

The parameter list is parsed in one place, `parseParam` in `FunctionParser.h`
(`name` or `type name`). To allow a new type, add it in three places:
`ParamType`/`paramTypeName` in `ASTNodes.h`, `FunctionParser::paramTypeFromToken`,
and the switch in `Interpreter::checkParamTypes` (`-Wswitch` catches a missing
case in the switches). Type checking happens in `evalCall`/`invokeAwaited` **before**
dispatch, so an async function also errors at the call site — do not defer it until
after the job is queued.

## 13. When touching `use` statement syntax

A DLC import is the `UseStmt::dlcs` list (name + each one's location), and a module
import is `name`/`path` (three places: `ImportParser.h`, `Interpreter::execUse` and
`ASTPrinter.h`). A `use` spanning several lines is deliberately unsupported, because
of the SPEC rule "a module-load command is one line".

## 14. Code comment policy

Comments in `engine/` record only "what the code alone cannot tell you", in a line
or two — lifetime/ordering invariants, platform quirks, stack-safety limits, and the
reason for a non-obvious check. Let names and code say "what it does", and put long
explanations in this document or in `IMPLEMENTATION_NOTES.md` (see section 39).
When you change a feature, fix the document entry that explains it as well.
