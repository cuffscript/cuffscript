<p align="center">
    <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript logo" width="360" />
</p>

---

# The CuffScript Programming Language

CuffScript is a scripting language that uses natural-language keywords and concise syntax. This repository contains a complete engine: a tokenizer, lexer, parser, a custom regular-expression engine, and a tree-walking interpreter that actually runs CuffScript programs.

## Current Status

The engine implements the full pipeline described in [docs/SPEC.md](docs/SPEC.md) and [docs/REGEX.md](docs/REGEX.md):

```text
CuffScript source
    -> Tokenizer
    -> Lexer
    -> Parser  -> AST
    -> Interpreter (executes the AST)
```

By default, `./cuffc program.cuff` **runs** the program. Pass `--ast` to instead dump the tokenizer/lexer/parser stages without executing anything (useful when working on the engine itself).

Errors at every stage (lexical, syntax, pattern, runtime, module) are raised through a single, systematically-coded exception hierarchy — see [Error handling](#error-handling) below — so failures are consistent and easy to add to.

## Syntax Overview

### Variable Declaration and Modification

```cuff
set number age to 25
set str name to "Alice"
set list colors to ["red", "green", "blue"]

change age to 26
```

Constants use the `constant` keyword; their names must be written in ALL CAPS (checked at runtime — using a lowercase letter, or later trying to `change` one, raises a runtime error immediately).

```cuff
set constant number MAX_LEVEL to 99
```

Inside a function, a variable declared outside it can be modified (not just read) after bridging it once with `change name to global`:

```cuff
set number counter to 0
set function increment() do:
    change counter to global
    change counter to counter + 1
end
```

### Conditional Statements and Loops

Control statements begin their execution block with `do:` and are closed with `end`. Both single-line shorthand syntax and multi-line block syntax are supported. Multi-line blocks use indentation.

```cuff
if score >= 90 do: print("excellent") end

loop repeat i to 1 ~ 3 do:
    print(i)
end
```

The loop forms are `loop repeat`, `loop while`, and `loop match`. Inside a loop, `stop` terminates the nearest enclosing loop.

### Expressions and Collections

- Comparison: `is` (case-sensitive), `IS` (case-insensitive); negate either with `is not` / `IS not`
- Boolean negation: `!` — binds *looser* than comparison, so `!lvl is MAX_LEVEL` means `!(lvl is MAX_LEVEL)`
- Lists and maps: `[]`, `{}` (maps preserve insertion order)
- 1-based indexing and inclusive range slicing: `[1]`, `[2~4]`, negative indices count from the end (`[-1]` is the last element)
- f-strings: `f"Hello, {name}"` — use `'single quotes'` for any string literal *inside* the `{...}`, since `"` would otherwise close the f-string early; `{{`/`}}` produce literal braces
- Collection manipulation: `add`, `remove`, `replace`, or index-assignment via `change`

```cuff
set list items to ["sword", "shield"]
add "potion" to items
change items[1] to "magic_staff"
remove 2 from items
```

### Pattern Matching (custom regex dialect)

CuffScript's pattern syntax has no `\d`/`\w`/`^`/`$` — every atom is a plain character or a bracketed word like `[num]`, `<name:...>`, `[one:a|b|c]`. Full details: [docs/REGEX.md](docs/REGEX.md).

```cuff
if email is "[str]+@[str]2~10" do: print("looks like an email") end

set match parsed to match log_line from "<date:[num]4-[num]2-[num]2> <msg:[any]+>"
if parsed is not empty do:
    print(f"date={parsed['date']} msg={parsed['msg']}")
end

set list codes to find "T-[num]3" from article g
set str masked to replace "[num]4-[num]4" in phone to "****-****"
set list parts to split "a,b,c" by ","
set number n to count "[num]+" in text
```

Pattern matching is protected against catastrophic backtracking with a built-in step-count and time limit (raises a recoverable `RegexRuntimeError` instead of hanging).

### Functions and Modules

Function declarations combine the modifiers `returnable` and `async` freely (`set function`, `set returnable function`, `set async function`, `set async returnable function`, ...). `await` is required to call an `async` function and executes it synchronously to completion.

```cuff
set returnable function double(value) do:
    return value * 2
end

set number result to double(21)
```

`use DLC:<name>` loads a built-in library (`math`, `string`, `time`, `random`, `network` — see [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md) for the full function list). `use <name> from <path>` loads another `.cuff` file relative to the running script and merges its top-level functions/variables into the current scope.

The error-handling composition syntax is `or_else do: ... end`, which catches any recoverable runtime error (not syntax errors) raised by the statement it follows:

```cuff
set number result to risky_call() or_else do:
    change result to -1
end
```

## Error Handling

Every error the engine raises derives from `CuffError` (`engine/common/CuffError.h`) and carries a stable numeric code (`engine/common/ErrorCodes.h`), a category, a source location, a message, and an optional hint, e.g.:

```text
[E4008] Runtime Error at line 12, column 5: index 10 is out of range (length 3)
    hint: use 1 for the first element, or -1 for the last
```

Codes are grouped by range (1000s lexical, 2000s syntax, 3000s pattern syntax/runtime, 4000s interpreter runtime, 5000s modules, 9000s internal). Only Runtime/Regex-runtime/Module errors are `recoverable` — those are exactly the ones `or_else` can catch; a malformed program (lexical/syntax/pattern-syntax error) never is. Adding a new error kind is additive: add a code to `ErrorCodes.h` and, if useful, a small subclass in `CuffError.h` — nothing else needs to change.

## Directory Structure

```text
engine/
├── common/       Common types, tokens, the systematic error hierarchy, source locations
├── tokenizer/    Conversion of source code into raw tokens
├── lexer/        Keyword classification and colon-rule validation
├── parser/       Parsing of expressions, declarations, control statements, functions, modules
├── regex/        CuffScript's own pattern compiler + backtracking matcher (docs/REGEX.md)
├── interpreter/  Tree-walking interpreter: Value model, scoping, execution
└── debug/        Token and AST pretty-printing (used by --ast)
```

The main entry points are `CuffEngine::execute` (parse + run) and `CuffEngine::run` (parse only, for `--ast`) in `engine/CuffEngine.h`. For detailed language rules, see [docs/SPEC.md](docs/SPEC.md) and [docs/REGEX.md](docs/REGEX.md); for implementation decisions made where those specs are silent (async model, DLC functions, etc.), see [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md).

## Build

### Using Makefile

Run the following command from the repository root with a C++17 compiler installed:

```bash
make
```

The generated executable is named `cuffc`.

### Visual Studio

Install the `Desktop development with C++` workload in Visual Studio, then create an empty C++ project and add `main.cpp` and the header files under `engine/`. Set the project's C++ standard to C++17.

## Usage

Run a script:

```bash
./cuffc path/to/program.cuff
```

Or pipe source from standard input:

```bash
echo 'print("Hello, CuffScript!")' | ./cuffc
```

Dump tokens + AST instead of running (development/debugging):

```bash
./cuffc --ast path/to/program.cuff
```

On success the program's output appears on stdout and the process exits 0. On any error (lexical, syntax, pattern, runtime, or module), a single formatted error line is printed to stderr and the process exits 1.

## Specification

The official language specification is in [docs/SPEC.md](docs/SPEC.md), and the pattern-matching dialect in [docs/REGEX.md](docs/REGEX.md). Together they cover:

- Declaration rules using `set`, `change`, and `constant`
- Colon spacing rules and `note` / `endnote` comments
- Comparisons, negation, indexing, slicing, and the custom regex dialect
- List and map manipulation
- Conditionals, loops, functions, `await`, and `or_else`
- Input, output, and module loading syntax

Where either spec leaves the runtime behavior unspecified (this happens by design for `async`/`await` — the spec explicitly defers that to "a separate implementation spec"), [docs/IMPLEMENTATION_NOTES.md](docs/IMPLEMENTATION_NOTES.md) documents the choice this engine makes and why.

---
