<p align="center">
    <img src="https://raw.githubusercontent.com/cuffscript/cuffscript/refs/heads/main/assets/cuffscript_horiz.svg" alt="CuffScript logo" width="360" />
</p>

---

# The CuffScript Programming Language

CuffScript is a scripting language that uses natural-language keywords and concise syntax. The current engine in this repository provides a frontend pipeline that tokenizes CuffScript source code, classifies keywords, and converts the result into an Abstract Syntax Tree (AST).

## Current Status

The current processing flow is as follows:

```text
CuffScript source
    -> Tokenizer
    -> Lexer
    -> Parser
    -> AST
```

`main.cpp` accepts a file path as an argument or reads source code from standard input. When processing succeeds, it outputs the tokenization results, lexer results, and AST. If processing fails, it outputs an error message.

The `engine/` directory primarily consists of the language frontend implementation. The fact that all execution syntax defined in the specification is supported does not mean that all of it is currently executable at runtime. The current entry point only handles processing up to AST generation and output.

## Syntax Overview

### Variable Declaration and Modification

```cuff
set number age to 25
set str name to "Alice"
set list colors to ["red", "green", "blue"]

change age to 26
```

Constants use the `constant` keyword, and their names consist of uppercase letters and underscores.

```cuff
set constant number MAX_LEVEL to 99
```

### Conditional Statements and Loops

Control statements begin their execution block with `do:` and are closed with `end`. Both single-line shorthand syntax and multi-line block syntax are supported. Multi-line blocks use indentation.

```cuff
if score >= 90 do: print("excellent") end

loop repeat i to 1 ~ 3 do:
    print(i)
end
```

The defined loop forms are `loop repeat`, `loop while`, and `loop match`. Inside a loop, `stop` terminates the nearest enclosing loop.

### Expressions and Collections

- Comparison: `is`, case-insensitive string comparison: `IS`
- Negation: `!`
- Lists and maps: `[]`, `{}`
- 1-based indexing and inclusive range slicing: `[1]`, `[2~4]`
- f-strings: `f"Hello, {name}"`
- Collection manipulation: `add`, `remove`, `replace`

```cuff
set list items to ["sword", "shield"]
add "potion" to items
replace items[1] to "magic_staff"
remove 2 from items
```

### Functions and Modules

Function declarations can use `function`, `returnable`, and `async`. Modules are imported using `use` and `from` as defined in the specification.

```cuff
set returnable function double(value) do:
    return value * 2
end

set number result to double(21)
```

The error-handling composition syntax is defined as `or_else do: ... end`.

## Directory Structure

```text
engine/
├── common/       Common types, tokens, errors, and source locations
├── debug/        Token and AST output
├── lexer/        Keyword classification and colon rule validation
├── parser/       Parsing of expressions, declarations, control statements, functions, and modules
└── tokenizer/    Conversion of source code into raw tokens
```

The main public entry point is `CuffEngine::run` in `engine/CuffEngine.h`. For detailed language rules, refer to [docs/SPEC.md](docs/SPEC.md).

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

A file can be passed as an argument:

```bash
./cuffc path/to/program.cuff
```

Alternatively, source code can be provided through standard input:

```bash
echo 'print("Hello, CuffScript!")' | ./cuffc
```

When processing succeeds, the program outputs the tokens and AST. If a syntax error occurs, it outputs an error message and exits with a failure code.

## Specification

The official intermediate specification of the language is available in [docs/SPEC.md](docs/SPEC.md). The specification includes:

- Declaration rules using `set`, `change`, and `constant`
- Colon spacing rules and `note` / `endnote` comments
- Comparisons, negation, indexing, slicing, and regex shorthand syntax
- List and map manipulation
- Conditionals, loops, functions, `await`, and `or_else`
- Input, output, and module loading syntax

---