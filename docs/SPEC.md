# CuffScript (Cuff) Language Specification

This document defines the CuffScript language as implemented by this repository's engine: what a
program may contain, what each construct means, and what a script can rely on while it runs.

**Related documents**

- [`REGEX.md`](REGEX.md) — the pattern language used by `is`/`IS`, `match`, `find`, `replace`,
  `split` and `count`.
- [`IMPLEMENTATION_NOTES.md`](IMPLEMENTATION_NOTES.md) — how this engine behaves where the language
  leaves room, why, and how that changed over time.
- [`SECURITY.md`](SECURITY.md) — what a host that runs untrusted scripts must configure.
- [`EXTENDING.md`](EXTENDING.md) — how to add features to the engine.

**Conventions**

- In a syntax summary, `[placeholder]` marks a part you replace with your own code.
- Code blocks tagged `cuff` show complete statements. Where a block uses a variable it does not
  define, assume an earlier line defined it; `...` stands for code that is not shown.
- *Error* means the program stops with a diagnostic whose code has the form `E<category>-<nnn>`
  (see [section 8](#8-errors-and-or_else) and [Appendix B](#appendix-b-error-catalogue)), unless
  `or_else` recovers from it.
- Within this document, `or_else`, `change ... to global` and similar constructs are cited by name;
  other documents cite sections of this one by title, so renumbering never breaks them.

---

## 1. Lexical structure

### 1.1 Source text

A program is UTF-8 text. Lines end with LF or CRLF. A program may be at most 16 MiB (`E2-009`).
Strings and comments may contain any Unicode text; everything else is ASCII.

### 1.2 Lines, blocks and indentation

A statement is written on one line. A block statement (`if`, `loop`, `set func`, and the
`or_else` handler) opens with `do:` at the end of its header line, holds a body of further
statements, and is closed by `end`. Every block needs its own `end`; a missing or surplus `end` is a
syntax error.

```cuff
if score >= 90 do:
    print("Excellent")
else if score >= 80 do:
    print("Good")
else do:
    print("Keep going")
end
```

- Write each body line one level deeper than its header. Indent with spaces or tabs (a tab counts
  as four columns) and use one style within a file.
- A line that returns to an indentation level which no enclosing block uses is rejected
  (`inconsistent indentation`, a syntax error).
- A body that is not indented is currently accepted, because `end` alone closes the block. Do not
  write programs that rely on it; a later revision may reject it.
- *One-line form:* when a block's body and its `end` fit on the header's line, no indentation is
  needed, for `if` and `loop` (`if x is 1 do: print("one") end`). A function declaration must
  always span several lines.
- Blank lines are ignored.

### 1.3 Comments

```cuff
note: a single-line comment, which can also follow code on a line
print("hi") note: like this

note:
A multi-line comment: it starts when `note:` is the last thing on its line, and runs
to the first occurrence of the word below.
endnote
```

A multi-line comment ends at the **first occurrence of the word `endnote`, wherever it appears**,
even in the middle of a line; the rest of that line is ignored. Do not write that word inside the
comment's text. `note` is a reserved word, so it cannot be used as a name.

### 1.4 The colon rule

Every `:` that is not inside a string — in `do:`, `note:`, `DLC:name`, or the `[one:...]` and
`<name:...>` forms of a pattern — has **no space before it**. A space before the colon is a syntax
error, raised before the program runs. A space after it is recommended.

### 1.5 Names and reserved words

A name starts with a letter or `_` and continues with letters, digits and `_`. Letters are ASCII
only. Names are case-sensitive. Variables and functions have separate name spaces, so a variable and
a function may share a name.

The reserved words are:

```text
set change constant if else loop repeat while match do end stop return await use from note endnote
empty async returnable pure func add remove replace to or_else global not find split count by in
number str list map boolean true false is IS
```

These reserved words may nevertheless be used as variable, function, parameter and loop-variable
names: `match`, `from`, `add`, `replace`, `to`, `global`, `not`, `find`, `split`, `count`, `by`, `in`.
`match`, `find`, `replace`, `split` and `count` also begin their own expressions (section 4.7), so
when that expression's syntax follows the word it is read as the expression, not as a name. Every
other reserved word is always reserved.

### 1.6 Literals

| Kind | Form | Notes |
| :--- | :--- | :--- |
| number | `42`, `3.14` | Decimal digits with an optional `.` followed by digits. There is no exponent form, no leading or trailing `.`, no digit separators. A leading `-` is the unary operator. |
| str | `"text"` or `'text'` | May span lines. Escapes: `\"` `\'` `\\` `\n` `\t` `\r`; any other `\x` is kept as written (backslash and character), which is what lets patterns contain `\.`. |
| f-string | `f"text {expression} text"` | Double quotes only. See section 4.8. |
| boolean | `true`, `false` | |
| empty | `empty` | The "no value" value. |
| list | `[1, "two", [3]]` | Elements may be of any type. |
| map | `{"name": "Ann", "tags": [1, 2]}` | Keys must be strings (`E4-005` otherwise). |

---

## 2. Types and values

| Type | Holds | Falsy when |
| :--- | :--- | :--- |
| `number` | An IEEE-754 double | `0` |
| `str` | UTF-8 text | the empty string |
| `boolean` | `true` or `false` | `false` |
| `list` | An ordered sequence of values | it has no elements |
| `map` | String keys to values, in insertion order | it has no keys |
| `match` | The result of a successful pattern match (see `REGEX.md`) | — (a failed match gives `empty`, not a `match` value) |
| `empty` | No value | always |

**Truthiness.** `if`, `else if`, `loop while` and `!` accept a value of any type and treat it as
*falsy* or *truthy* by the last column above. `to_boolean(x)` applies the same rule, so
`to_boolean("false")` is `true`: any non-empty string is truthy.

**References.** Lists and maps are references. Assigning one to another variable, or passing it to
a function, shares the same underlying value, so a change made through either name is seen through
both. Numbers, strings, booleans and `empty` behave as values. A list or map that contains itself is
legal; printing and comparing it terminates. (The engine never frees such a value; see
`IMPLEMENTATION_NOTES.md`, item 11.)

**Equality (`is`).** Values of different types are never equal. Numbers compare by value and strings
by content. Lists and maps compare structurally: the same length or keys, with equal elements. Two
circular structures of the same shape compare equal.

**Display form.** `print`, f-strings and `to_str` render a value as follows. A top-level `str` is
printed as is. Numbers show at most 15 significant digits and use exponent form for very large or
small magnitudes (`1e+20`, `1e-06`). `true`, `false` and `empty` print as those words. A list prints
as `[1, "a", [2]]` and a map as `{"k": "v"}`, with strings quoted inside collections.

---

## 3. Variables and constants

### 3.1 Declaring with `set`

```cuff
set number age to 25
set str name to "Ann"
set list items to [1, 2, 3]
set map profile to {"name": "Ann"}
set boolean active to true
set empty nothing to empty
```

- The type is one of `number`, `str`, `list`, `map`, `boolean`, `match`, or `empty`.
- The value's type must equal the declared type (`E4-020`). `empty` is accepted for every type, so
  the `empty` that a failed `find` or `match` produces can be stored and then tested.
- The assignment word is `to`; `=` is a syntax error.
- The type is checked **only at the declaration**. A later `change` may store any type.
- Declaring a name again in the same scope replaces the earlier binding.

### 3.2 Changing with `change`

```cuff
change age to 26
change name to "Bob"
```

`change` requires an existing binding (`E4-001` otherwise) and cannot target a constant.

### 3.3 Constants

```cuff
set constant number MAX_USERS to 100
set constant str API_URL to "https://example.com"
```

A constant's name is written entirely in uppercase letters, digits and underscores (`E4-004`
otherwise). Changing a constant is an error (`E4-003`). These checks run when the statement
executes, so a constant declared in a branch that never runs is never checked.

**Constant lists.** `set constant list PRIMES to [2, 3, 5, 7]` creates a read-only list. `add`,
`remove` and `change` on it are errors (`E4-003`). The value is copied when the constant is created,
so freezing it never freezes a list another variable already refers to; and the read-only property
belongs to that list, so it follows the value into other variables and function arguments. The
protection is shallow: a list or map stored inside the constant list can still be changed unless it
was itself declared constant.

```cuff
set list source to [1, 2, 3]
set constant list SNAPSHOT to source   note: a frozen copy of source
add 99 to source                       note: allowed; SNAPSHOT is still [1, 2, 3]
set list copy to SNAPSHOT + []         note: concatenation yields a new, mutable list
```

---

## 4. Expressions

### 4.1 Precedence

From the tightest to the loosest binding:

| Level | Operators | Associativity |
| :---: | :--- | :--- |
| 1 | call `f(...)`, index `x[i]`, slice `x[i~j]` | left |
| 2 | unary `-` | right |
| 3 | `*` `/` | left |
| 4 | `+` `-` | left |
| 5 | `is` `IS` `is not` `IS not` `>` `<` `>=` `<=` | left |
| 6 | `!` | right |

Parentheses group. Because `!` binds loosest, `!x is 3` means `!(x is 3)`.

There is no `and`, `or`, `%`, `==`, `!=` or exponent operator. Combine conditions with nested `if`
statements. Comparison does not chain: `1 < 2 < 3` compares the boolean `true` with `3` and is an
error (`E4-005`).

### 4.2 Arithmetic

`*`, `/` and unary `-` need numbers. `/` always divides as floating point (`5 / 2` is `2.5`), and
division by zero is an error (`E4-006`). `+` adds two numbers, concatenates two strings, or joins two
lists into a new list. Any other combination, such as `"a" + 1`, is an error (`E4-005`); convert
explicitly with `to_str` or `to_number`.

### 4.3 Comparison

`>`, `<`, `>=` and `<=` compare two numbers or two strings. Strings compare by their UTF-8 bytes, so
`"a" < "B"` is false and Hangul sorts in code-point order. Comparing different types is an error
(`E4-005`).

### 4.4 Equality and pattern tests: `is`, `IS`

`a is b` is true when the two values are equal (section 2); `a is not b` is its negation. `IS` is the
same test made **case-insensitive for English letters**.

**Special case — a string literal on the right.** When the right-hand operand is a string
*literal*, the test is not equality but a **full-match pattern test**: `a` must be a string
(`E4-005` otherwise) and the literal is read as a pattern (`REGEX.md`). For a literal without pattern
syntax the two readings agree. When the right-hand operand is anything else — a variable, a call, a
number, `empty` — `is` is plain equality, and a pattern held in a variable is *not* interpreted.

```cuff
if input_text is "apple" do: ... end          note: a pattern test (a literal on the right)
if input_text IS "apple" do: ... end          note: the same, ignoring case
if code is "[up]2-[num]4" do: ... end         note: a pattern with tokens
if age is not 18 do: ... end                  note: equality; 18 is not a string literal
```

A syntax error in a literal pattern is reported before the program runs; the escape rules for
patterns that contain `\` are in `REGEX.md`.

### 4.5 Negation: `!`

`!x` is `true` when `x` is falsy. It applies to a whole comparison to its right.

### 4.6 Indexing and slicing

Positions are **1-based**; position `0` does not exist (`E4-007`). A negative position counts from
the end: `-1` is the last element.

```cuff
set list colors to ["red", "green", "blue", "yellow"]
print(colors[1])        note: red
print(colors[-1])       note: yellow
print(colors[2~3])      note: ["green", "blue"]
```

- `x[i~j]` takes positions `i` through `j`, **both included**. If `i` is after `j` the result is
  empty (an empty list or string); a position outside the sequence is an error (`E4-008`).
- Strings are indexed by **code point**, not by byte, so `"안녕"[1]` is `"안"`.
- A map is indexed by a string key. A missing key is an error (`E4-009`); assigning to a new key with
  `change` creates it.
- A position must be a whole number (`E4-024` otherwise).

### 4.7 Pattern expressions

```cuff
set match r to match text from "SN-([num]4)-([num]+)"
set str first to find "T-[num]3" from article
set list all to find "T-[num]3" from article g
set str masked to replace "010-[num]4-" in raw to "010-****-"
set list parts to split line by "[,;][sp]*"
set number n to count "apple" in document i
```

`match`, `find`, `replace`, `split` and `count` are specified, with their flags (`i`, `g`, `m`), their
results and their failure values, in `REGEX.md`. In short: a failed `match` or single `find` yields
`empty`; `find ... g` yields a list (empty when nothing matched); `count` yields `0`.

### 4.8 f-strings

`f"Hello, {name}! 1 + 1 = {1 + 1}"` evaluates each `{expression}` and inserts its display form
(section 2). Write `{{` and `}}` for literal braces. An expression inside braces uses single quotes
for string literals, because a double quote would end the f-string: `f"year {res['year']}"`. An
error inside the braces is reported at the line of the f-string.

---

## 5. Statements

### 5.1 Collections: `add`, `change`, `remove`

| Statement | Effect |
| :--- | :--- |
| `add [value] to [list]` | Appends to the end of a list. |
| `change [list][i] to [value]` | Replaces the element at position `i`. |
| `change [map]["key"] to [value]` | Sets or creates a key. |
| `remove [position] from [list]` | A number removes the element at that position. |
| `remove [value] from [list]` | Any non-number removes the first element equal to it (`E4-015` if none). |
| `remove "key" from [map]` | Deletes the key; a missing key is ignored. |

These statements change the collection in place, so every name that refers to it sees the change.
They are errors on a constant collection (`E4-003`).

### 5.2 Conditionals

```cuff
if [condition] do:
    ...
else if [condition] do:
    ...
else do:
    ...
end
```

Conditions are truthy/falsy (section 2). A variable declared in a branch belongs to the enclosing
scope (section 7).

### 5.3 Loops

```cuff
loop repeat i to 1 ~ 10 do:
    print(i)
end

loop while [condition] do:
    ...
end
```

- `loop repeat [var] to [start] ~ [end]` runs the body once for each whole number from `start` to
  `end` inclusive, counting **down** when `start` is greater. Both bounds are evaluated once, before
  the first pass, and must be whole numbers (`E4-024`).
- `loop while` re-tests its condition before every pass.
- `stop` ends the **nearest enclosing** loop only. Outside a loop it is an error (`E4-023`).
- A loop does not open a scope: `[var]` and anything declared in the body remain visible after the
  loop, holding their last values.

### 5.4 Printing and input

`print(a, b, ...)` writes its arguments separated by spaces and ends the line; a `str` argument is
written as is and anything else in its display form. `input()` or `input(prompt)` writes the optional
prompt, reads one line and returns it as a `str` (empty at end of input).

---

## 6. Functions

### 6.1 Declaring and calling

```cuff
set func greet(name) do:
    print(f"Hello, {name}!")
end

set returnable func add_numbers(number a, number b) do:
    return a + b
end

greet("Ann")
set number total to add_numbers(2, 3)
```

- A function is declared with `set func [name]([parameters]) do:` followed by an indented body and
  `end`. The declaration must span several lines; the one-line form is a syntax error.
- Functions are declared at the top level of a program or module. Declaring one inside another
  function is an error (`E4-018`).
- A call is `name(arguments)`. It is a statement on its own line or an expression. It does not use
  `do:`. Calling a name that is not defined is an error (`E4-002`).
- Declarations are **hoisted**: a function may be called above the line that declares it. If a name is
  declared more than once, the last declaration in the file is the one every call uses.
- The number of arguments must equal the number of parameters (`E4-010`). There are no default values
  and no variadic parameters.
- A function that has the same name as a built-in function replaces it for the whole program.
- Functions are not values. They cannot be stored in a variable, passed, returned or compared, and
  there are no closures.
- A call may nest at most 1000 levels deep, which makes unbounded recursion an error (`E4-017`).

### 6.2 Parameters and their types

A parameter is written `name` or `type name`. The types are `number`, `str`, `boolean`, `list`, `map`
and `match`, and typed and untyped parameters may be mixed in one list.

A typed parameter checks its argument at the call: a value of another type is an error on the calling
line (`E4-030`). `empty` is accepted by every type.

### 6.3 Returning values

- In a `returnable` function, `return [value]` ends the call and gives that value to the caller. If
  the body ends without `return`, the call gives `empty`.
- In a function that is not `returnable`, a call gives `empty`, a bare `return` leaves the function
  early, and `return [value]` is an error (`E4-016`).
- `return` outside any function is an error (`E4-022`).

### 6.4 Modifiers

```text
set [returnable] [async] [pure] func [name]([parameters]) do:
```

`returnable`, `async` and `pure` are independent and may appear in any order and any combination.

### 6.5 Asynchronous functions

CuffScript's `async` is cooperative and single-threaded; nothing runs in parallel.

- `await f(...)` runs the function to completion immediately and gives its value.
- `f(...)` **without** `await` does not run the function now. The call is queued and returns `empty`
  at once. Queued calls run in the order they were queued, once, after all the top-level code of the
  program has finished, and their return values cannot be observed.
- A queued function may queue further calls; they run in the same pass, in order.
- An error raised by a queued call stops the program, and the calls queued after it do not run. Such
  an error is not caught by an `or_else` written on the line that queued the call, because that line
  finished long before.
- `await` on a function that is not `async` is an error (`E4-019`). The queue holds at most 1,000,000
  calls (`E4-026`).

### 6.6 Pure functions

A `pure` function may use its parameters, its own locals and the built-in and library functions. It
cannot touch global state:

- Reading, changing or bridging (`change x to global`) a global variable raises
  `PureFunctionGlobalAccess` (`E4-027`).
- Calling a user function that is not itself `pure` raises `PureFunctionImpureCall` (`E4-029`). A pure
  function may call itself and other pure functions.

The second rule is what keeps a pure function pure: otherwise a pure function could reach a global by
calling an ordinary helper. To lift both restrictions, remove the `pure` keyword.

```cuff
use DLC:math
set returnable pure func hypotenuse(a, b) do:
    return math_sqrt(math_pow(a, 2) + math_pow(b, 2))
end
print(hypotenuse(3, 4))   note: 5
```

---

## 7. Scope

CuffScript scopes by **function**, as Python does, with one deliberate difference: a function does not
see global variables unless it asks for them.

1. **Global scope** holds what the top level of a program declares.
2. **Function scope** is created on each call. It holds the parameters, every name the body declares
   with `set` (including inside `if` and `loop` bodies), and the names the body *bridges*.
3. **`if` and `loop` bodies do not create a scope.** A name declared inside one stays visible in the
   enclosing scope after the block, and the loop variable of `loop repeat` keeps its last value.
4. **An `or_else` handler runs in its own scope.** A new name declared in it is local to the handler;
   an existing name can still be changed with `change`.

### 7.1 Reaching a global from a function

A function sees its own locals and parameters and **nothing else** from outside itself: no global
variable, not even a constant, and no local of its caller. To use a global, bridge it:

```cuff
set number visits to 0
set constant number STEP to 5

set func record() do:
    change visits to global     note: from here on, `visits` is the global
    change STEP to global       note: constants are bridged the same way
    change visits to visits + STEP
end

set func peek() do:
    print(visits)               note: error (E4-001): `visits` is not visible here
end

record()
print(visits)                   note: 5
```

- `change [name] to global` is valid only inside a function body (`E4-021` elsewhere). It applies from
  that statement to the end of that call, to every block inside the function, and only to that call.
- After bridging, reading the name, `change`, and the collection statements (`add`, `remove`,
  `change x[i]`) all act on the global itself.
- A name declared with `set` in the function is a new local that **takes precedence** over a bridge of
  the same name. Declaring a name that also exists globally never touches the global.
- If no global of that name exists, bridging it has no effect and using the name is an error
  (`E4-001`).
- Using an unbridged global is reported as undefined, with a hint that names the bridge.
- A `pure` function can neither bridge nor see globals (section 6.6).

### 7.2 Modules

A module's top-level variables are copied into the program that loads it, and its functions are
registered globally (section 9.2). A function from a module follows the same rule as any other: to read
a variable its module defined at the top level, it bridges the name first.

---

## 8. Errors and `or_else`

### 8.1 What an error report contains

```text
ERROR: [E4-006] Runtime Error at line 3, column 9: division by zero
    print(a / b)
            ^
```

The tag has the form `E<category>-<nnn>`. The report gives the line and column, the source line with a
caret under the position (counted in characters, not bytes), and for some errors a `hint:` line. A
program that ends with an error exits with a non-zero status.

| Category | Meaning | Caught by `or_else` |
| :--- | :--- | :---: |
| `E1-xxx` | Tokenization | no |
| `E2-xxx` | Syntax | no |
| `E3-0xx` | Pattern syntax | no |
| `E3-1xx` | Pattern matching at run time | yes |
| `E4-xxx` | Run time | yes |
| `E5-xxx` | Modules and libraries | yes |
| `E6-xxx` | Resource limits (steps, time, memory) | no |
| `E9-xxx` | Internal | no |

In practice, malformed source found while tokenizing *or* parsing — a stray character, a space before
a colon, an unterminated string, a missing `end`, bad indentation — is reported as `E2-001 Syntax
Error`, and the message says what is wrong. [Appendix B](#appendix-b-error-catalogue) lists every
code that the engine raises, and those it defines but does not currently raise.

### 8.2 Recovering with `or_else`

An `or_else` handler goes after a statement, on the same line as its last token or after the `end` of a
block:

```cuff
set number quantity to to_number("abc") or_else do:
    print("Not a number; using 1.")
    change quantity to 1
end

loop repeat i to 1 ~ 3 do:
    print(10 / (i - 2)) or_else do:
        print("skipped a division by zero")
    end
end
```

**Semantics**

1. The statement runs. If it finishes, the handler is skipped.
2. If it raises a *catchable* error (the last column of the table above), the handler runs in its own
   scope (section 7) and the program continues after it.
3. Whatever the statement did before the error stays done: changes to variables, output already
   printed and calls already made are not undone.
4. If the failed statement was a declaration, its name is bound to `empty` before the handler runs, so
   the handler can `change` it.
5. An error raised **inside** the handler is not caught by the same `or_else`.
6. `return` inside a handler returns from the enclosing function, and `stop` ends the enclosing loop.
7. Errors that are not catchable (resource limits, internal errors) always end the program.

`or_else` guards the one statement it follows; to guard several, put them in a function and guard the
call. It cannot guard the work of an `async` call that is queued rather than awaited (section 6.5).

---

## 9. Modules and libraries

### 9.1 Libraries (`DLC`)

The built-in library packages are called DLCs. `use DLC:[name]` makes a library's functions available
from that line on:

```cuff
use DLC:math
use DLC:string, DLC:list, DLC:json    note: several on one line, comma-separated
```

- Every entry in a list needs its own `DLC:` prefix, and a `use` statement stays on one line.
- Loading a library twice is harmless. An unknown library is an error (`E5-004`) whose hint lists the
  available ones.
- The libraries are `math`, `string`, `time`, `random`, `list`, `map`, `convert`, `json`, `network`
  and `filesystem` (section 10).

### 9.2 Your own modules (`use ... from ...`)

```cuff
use stage_data from ./maps/core_engine
```

loads the file `./maps/core_engine/stage_data.cuff`.

- The path is relative to the directory of the running script and must stay inside it, or inside the
  directory the host configured as the root. An absolute path or one that escapes is an error
  (`E5-006`). A missing file is `E5-001`, and a file that fails to parse is `E5-002`.
- The module's code runs once, at the first `use`. Later `use` statements for it, including circular
  ones, do nothing.
- Its functions become available to the whole program. Its top-level variables are copied into the
  scope that wrote `use`, after the module has run (section 7.2).
- Modules may load modules, to a depth of 64 (`E5-007`).

---

## 10. Built-in functions and libraries

### 10.1 Rules shared by all library functions

- A function checks its arguments before it runs: the wrong number of arguments is `E4-010`, a wrong
  type `E4-005`, and a value outside what the function accepts `E4-025` (or `E4-006` for a division by
  zero).
- Functions return new values and never modify their arguments. Lists and maps passed in come back
  unchanged.
- Strings are measured and indexed in characters (code points).

### 10.2 Always available

| Function | Result |
| :--- | :--- |
| `print(...)`, `input([prompt])` | See section 5.4. |
| `type_of(x)` | The type name: `"number"`, `"str"`, `"boolean"`, `"list"`, `"map"`, `"match"` or `"empty"`. |
| `to_number(x)` | A number, a decimal string parsed as a number (`E4-025` if it is not one), or a boolean as `1`/`0`. Other types are an error. |
| `to_str(x)` | The display form of any value (section 2). |
| `to_boolean(x)` | The truthiness of any value (section 2). |

### 10.3 `length`, `contains`, `index_of`

These three work on strings, lists and maps alike and become available once any of `DLC:string`,
`DLC:list` or `DLC:map` is loaded.

| Function | Result |
| :--- | :--- |
| `length(x)` | Characters of a string, elements of a list, keys of a map. |
| `contains(x, v)` | Whether a string contains the substring `v`, a list contains an element equal to `v`, or a map has the key `v`. |
| `index_of(x, v)` | The 1-based position of the first occurrence of `v` in a string or list, or `empty` if there is none. |

### 10.4 Library reference

The number in parentheses is how many arguments the function takes. Names follow `library_verb`.

| Library | Functions |
| :--- | :--- |
| `DLC:math` | `math_sqrt(1)`, `math_abs(1)`, `math_pow(2)`, `math_round(1–2)` (to a number of decimal places; halves round away from zero), `math_floor(1)`, `math_ceil(1)`, `math_trunc(1)` (toward zero), `math_sign(1)` (−1, 0 or 1), `math_min` / `math_max` (one or more numbers, or one list of numbers), `math_clamp(3)` (value, low, high), `math_mod(2)` (the result takes the sign of the divisor), `math_log(1–2)` (natural, or to a given base), `math_log2(1)`, `math_log10(1)`, `math_exp(1)`, `math_sin`, `math_cos`, `math_tan`, `math_asin`, `math_acos`, `math_atan` (1 each, radians), `math_atan2(2)`, `math_pi(0)`, `math_e(0)` |
| `DLC:string` | `str_upper(1)`, `str_lower(1)`, `str_trim(1)`, `str_trim_start(1)`, `str_trim_end(1)` (space, tab, CR, LF), `str_starts_with(2)`, `str_ends_with(2)`, `str_repeat(2)`, `str_pad_left(2–3)`, `str_pad_right(2–3)` (to a width, with an optional pad string), `str_char_code(1)` (code point of the first character), `str_from_char_code(1)`, plus `length`, `contains`, `index_of` |
| `DLC:time` | `time_now(0)` and its alias `time_timestamp(0)`: seconds since the Unix epoch, with a fractional part |
| `DLC:random` | `random_float(0)` (from 0 up to, not including, 1), `random_int(2)` (both ends included), `random_seed(1)`, `random_choice(1)`, `random_shuffle(1)` (a new list) |
| `DLC:list` | `list_sort(1)` (all numbers or all strings, ascending), `list_reverse(1)`, `list_join(2)` (a list of strings and a separator), `list_unique(1)` (keeps first occurrences), `list_sum(1)`, `list_average(1)`, `list_flatten(1)` (one level), `list_range(2–3)` (start, end included, optional step; may count down), plus `length`, `contains`, `index_of` |
| `DLC:map` | `map_keys(1)`, `map_values(1)`, `map_has_key(2)`, `map_entries(1)` (a list of `[key, value]` pairs), `map_merge(2)` (a new map; the second map wins), plus `length`, `contains` |
| `DLC:convert` | `to_number`, `to_str`, `to_boolean` (always available, section 10.2) |
| `DLC:json` | See below. |
| `DLC:network` | See below. |
| `DLC:filesystem` | See below. |

### 10.5 `DLC:json`

- `to_json(value)` returns compact JSON; `to_json(value, indent)` returns it pretty-printed with that
  indent width. `empty` becomes `null`.
- `from_json(text)` parses JSON into numbers, strings, booleans, lists, maps and `empty` (for `null`).
  Malformed text is an error (`E4-025`). Nesting deeper than 200 levels is rejected.

### 10.6 `DLC:network`

A small client for plain HTTP (not HTTPS) requests.

- `network_get(url)` and `network_post(url, body[, content_type])` return a map
  `{"status": number, "ok": boolean, "body": str}`, where `ok` is true for statuses 200–299.
- `url` must start with `http://`. An `https://` address is refused with a clear error, because the
  client does not implement TLS.
- Connection failures, timeouts and blocked addresses are errors (`E4-028`) that `or_else` can catch.
- By default the client cannot reach loopback, private or link-local addresses (protection against
  server-side request forgery). A host can switch the library off or allow private addresses; see
  `SECURITY.md` for the options and their risks.

### 10.7 `DLC:filesystem`

Reads and writes files inside the folder of the running script, or inside the host's configured root.

| Function | Result |
| :--- | :--- |
| `file_exist(path)` | Whether the regular file exists. |
| `file_size(path)` | Its size in bytes, or `empty` on failure. |
| `file_read(path)` | Its text, or `empty` on failure. |
| `file_readlines(path)` | Its lines as a list, or `empty` on failure. |
| `file_write(path, text)` | Overwrites the file, or creates it. Gives `true` on success. |
| `file_add(path, text)` | Appends text, creating the file if needed. Gives `true` on success. |
| `file_remove(path)` | Deletes a regular file and gives whether it succeeded. A directory, even an empty one, is never removed; the result is `false`. |

- Paths are relative to that folder. A path that leaves it (an absolute path, `../`) is refused with
  `FilesystemAccessDenied` (`E5-008`). It is the same boundary that `use ... from` uses.
- Ordinary failures such as a missing file or no permission are **not** errors; the function gives
  `empty` or `false` as the table says. Use `file_exist` first when the difference matters.
- Names may be written in any language; they are read as UTF-8 on every platform.
- A host can switch the library off; see `SECURITY.md`.

---

## 11. Running programs

### 11.1 Command line

```text
cuffc [options] [script.cuff]        run a script (standard input when no file is given)
```

| Option | Effect |
| :--- | :--- |
| `--ast` | Print the tokens and syntax tree instead of running. |
| `--root <dir>` | Let `use ... from` load modules anywhere under `<dir>` (default: the script's folder). |
| `--max-steps <n>` | Stop after `n` loop iterations and function calls in total (`E6-001`). |
| `--timeout <ms>` | Stop after `ms` milliseconds (`E6-002`). |
| `--no-network` | Disable `DLC:network`. |
| `--allow-private-network` | Let `DLC:network` reach loopback, private and link-local addresses. |
| `--no-filesystem` | Disable `DLC:filesystem`. |

`cuffsh` is the interactive shell and takes the same engine options. A program that embeds the engine
sets the same choices through `CuffEngine::Options`.

### 11.2 Limits

These are the engine's defaults. Exceeding one is an error, as the table says.

| What | Limit | On excess |
| :--- | :--- | :--- |
| Source size | 16 MiB | `E2-009` |
| Nesting of blocks and expressions | 512 levels | `E2-008` |
| Call depth | 1000 | `E4-017` |
| String size | 128 MiB | `E4-026` |
| Elements in a list or map | 32 Mi | `E4-026` |
| Queued `async` calls | 1,000,000 | `E4-026` |
| Module nesting | 64 | `E5-007` |
| JSON nesting | 200 | `E4-025` |
| HTTP response | 8 MiB, 5 redirects, 5 s to connect, 15 s in total | `E4-028` |
| Pattern | 64 KiB, 64 levels of nesting, counts up to 100,000, and a step, time and recursion budget per match | `E3-xxx` |
| `--max-steps`, `--timeout` | off unless set | `E6-001`, `E6-002` |

---

## Appendix A: Comprehensive example

The same program ships as `examples/02_comprehensive_demo.cuff`.

```cuff
note: Step 1: load the official libraries (DLC) and a custom module (one statement per line)
use DLC:network
use stage_data from ./maps/core_engine

note:
This block is a multi-line comment.
It is the final end-to-end check covering 1-based indexes, or_else, pattern matching, and global/local variables.
endnote

note: Step 2: core variables and fixed constants (constants use only `to`)
set constant number MAX_LEVEL to 99
set constant str ENGINE_SIGNATURE to "CUFF_LANG_V1"

set number current_lvl to 1
set str user_email to "Player_One@CuffLang.com"
set list reward_tier_list to ["Gold", "Silver", "Bronze"]

note: Step 3: declare the returnable functions that handle logic checks and text pattern matching
set returnable func audit_and_assess_user(email, lvl) do:

    change MAX_LEVEL to global

    note: validate the email's structure with pattern matching (the `is` operator)
    if email is "[str]+@[str]2~10" do:
        print("Pattern matching engine: email structure verified")
    end

    note: the `!` negation operator combined with a strict, lowercase `is` comparison
    if !lvl is MAX_LEVEL do:
        print(f"Current level {lvl} is not the highest level.")
    end

    if lvl is MAX_LEVEL do:
        return "epic_rank"
    else if lvl >= 50 do:
        return "rare_rank"
    else do:
        return "common_rank"
    end
end

note: Step 4: define a standalone async function for background data processing
set async func backup_user_cloud_data() do:
    print("Sending a snapshot of the virtual machine's internal data to the remote cloud infrastructure.")
end

note: Step 5: run the main program logic and exercise 1-based collection operations
set str evaluation_result to audit_and_assess_user(user_email, current_lvl)

note: call a function with the or_else safety net
await backup_user_cloud_data() or_else do:
    print("Working around the cloud sync failure caused by the network environment.")
end

note: with 1-based indexing, index 1 is the first element, "Gold".
print(f"Data identifying the top-tier reward emblem: {reward_tier_list[1]}")

note: modify list contents with the standalone `add ... to` and `change` statements
add "None_Tier" to reward_tier_list
change reward_tier_list[1] to "Platinum_Tier" note: replace the existing word at position 1 with Platinum

note: Step 6: fast repetition using the range operator (~) and the `loop repeat` structure
loop repeat step to 1 ~ 5 do:
    if step is 4 do:
        print("Forcing the loop to stop.")
        stop
    end
    print(f"CuffScript fast VM sync engine running... current loop step: {step}")
end
```

---

## Appendix B: Error catalogue

| Code | Meaning | Caught by `or_else` |
| :--- | :--- | :---: |
| **E1** | **Tokenization** | |
| `E1-003` | Invalid number literal | no |
| **E2** | **Syntax** | |
| `E2-001` | Unexpected token | no |
| `E2-008` | Nesting too deep | no |
| `E2-009` | Source too large | no |
| **E3** | **Pattern** | |
| `E3-001` | Regex unclosed group | no |
| `E3-002` | Regex unclosed bracket | no |
| `E3-003` | Regex invalid colon spacing | no |
| `E3-004` | Regex invalid quantifier range | no |
| `E3-005` | Regex stacked quantifier | no |
| `E3-006` | Regex empty token | no |
| `E3-007` | Regex unknown token | no |
| `E3-008` | Regex invalid escape | no |
| `E3-009` | Regex dangling quantifier | no |
| `E3-010` | Regex unexpected character | no |
| `E3-011` | Regex pattern too complex | no |
| `E3-101` | Regex step limit exceeded | yes |
| `E3-102` | Regex timeout | yes |
| `E3-103` | Regex recursion limit exceeded | yes |
| **E4** | **Runtime** | |
| `E4-001` | Undefined variable | yes |
| `E4-002` | Undefined function | yes |
| `E4-003` | Constant reassignment | yes |
| `E4-004` | Invalid constant name | yes |
| `E4-005` | Type mismatch | yes |
| `E4-006` | Division by zero | yes |
| `E4-007` | Zero index access | yes |
| `E4-008` | Index out of range | yes |
| `E4-009` | Key not found | yes |
| `E4-010` | Argument count mismatch | yes |
| `E4-015` | Element not found | yes |
| `E4-016` | Unsupported operation | yes |
| `E4-017` | Stack overflow | yes |
| `E4-018` | Nested function not supported | yes |
| `E4-019` | Await on non async | yes |
| `E4-020` | Declaration type mismatch | yes |
| `E4-021` | Invalid global declaration | yes |
| `E4-022` | Return outside function | yes |
| `E4-023` | Stop outside loop | yes |
| `E4-024` | Fractional index | yes |
| `E4-025` | Invalid argument value | yes |
| `E4-026` | Size limit exceeded | yes |
| `E4-027` | Pure function global access | yes |
| `E4-028` | Network request failed | yes |
| `E4-029` | Pure function impure call | yes |
| `E4-030` | Parameter type mismatch | yes |
| **E5** | **Modules and libraries** | |
| `E5-001` | Module not found | yes |
| `E5-002` | Module parse failed | yes |
| `E5-004` | Unknown DLC | yes |
| `E5-005` | DLC feature unavailable | yes |
| `E5-006` | Module access denied | yes |
| `E5-007` | Module limit exceeded | yes |
| `E5-008` | Filesystem access denied | yes |
| **E6** | **Resource limits** | |
| `E6-001` | Execution step limit | no |
| `E6-002` | Execution timeout | no |
| `E6-003` | Out of memory | no |
| **E9** | **Internal** | |
| `E9-001` | Internal error | no |

All syntax problems found while tokenizing or parsing are reported as `E2-001`; the other `E1` and
`E2` numbers, and a few `E4` and `E5` ones, are defined but not currently raised. They are reserved,
and a script should not depend on seeing them: `E1-001`, `E1-002`, `E1-004`, `E1-005`, `E1-006`, `E1-007`, `E2-002`, `E2-003`, `E2-004`, `E2-005`, `E2-006`, `E2-007`, `E4-011`, `E4-012`, `E4-013`, `E4-014`, `E5-003`.
