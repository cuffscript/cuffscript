# CuffScript (Cuff) Language Specification

This document defines the CuffScript language as implemented by this repository's engine: what a
program may contain, what each construct means, and what a script can rely on while it runs.

**Related documents**

- [Classes and objects](classes.md): classes, objects, inheritance, the `with` block and error objects.
- [Grammar](grammar.md): the exact shapes the parser accepts.
- [Pattern syntax](patterns.md): the pattern language used by pattern literals (`p"..."`), `matches`, `find`,
  `replace`, `split` and `count`.
- [Libraries](dlc/README.md), [built-in functions](builtins.md), [error catalogue](errors.md),
  [limits](limits.md) and [the command line](cuffc.md): the parts of the language's surface that are
  generated from, or checked against, the engine itself.
- [Design notes](../internals/design-notes.md): how this engine behaves where the language leaves room,
  why, and how that changed over time.
- [Security policy](../../SECURITY.md): what a host that runs untrusted scripts must configure.
- [Extending the engine](../contributing/extending.md): how to add features.

**Contents**

- [1. Lexical structure](#1-lexical-structure)
- [2. Types and values](#2-types-and-values)
- [3. Variables and constants](#3-variables-and-constants)
- [4. Expressions](#4-expressions)
- [5. Statements](#5-statements)
- [6. Functions](#6-functions)
- [7. Scope](#7-scope)
- [8. Errors and `or_else`](#8-errors-and-or_else)
- [9. Modules and libraries](#9-modules-and-libraries)
- [10. Built-in functions and libraries](#10-built-in-functions-and-libraries)
- [11. Running programs](#11-running-programs)
- [Appendix A: Comprehensive example](#appendix-a-comprehensive-example)
- [Appendix B: Error catalogue](#appendix-b-error-catalogue)

**Conventions**

- In a syntax summary, `[placeholder]` marks a part you replace with your own code.
- Code blocks tagged `cuff` show complete statements. Where a block uses a variable it does not
  define, assume an earlier line defined it; `...` stands for code that is not shown.
- *Error* means the program stops with a diagnostic whose code has the form `E<category>-<nnn>`
  (see [section 8](#8-errors-and-or_else) and the [error catalogue](errors.md)), unless
  `or_else` recovers from it.
- Within this document, `or_else`, `global` and similar constructs are cited by name;
  other documents cite sections of this one by title, so renumbering never breaks them.

---

## 1. Lexical structure

### 1.1 Source text

A program is UTF-8 text. Lines end with LF or CRLF. A program has a size limit (see [limits](limits.md)). A UTF-8 byte order mark at the start is ignored.
Strings and comments may contain any Unicode text; everything else is ASCII. The engine does not check that the text inside a string literal is valid UTF-8, and passes it through unchanged.

### 1.2 Lines, blocks and indentation

A statement is written on one line. The exception is inside brackets: the elements of a list or map literal and the arguments of a call may continue over several lines between their opening and closing bracket, and a list or map literal may end with a trailing comma (a call or a parameter list may not). A line cannot end in the middle of an expression after an operator: `set number x to 1 +` is a syntax error. A block statement (`if`, `loop`, `with`, `func`, `class`, and the
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
  needed (`if x is 1 do: print("one") end`, `func twice(x) do: return x * 2 end`). Only a class may have an empty body on one line
  (`class Oops extends Error do: end`).
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
a function may share a name. The name of a class starts with a capital letter (see [classes](classes.md)).

The reserved words, 44 of them, are:

```text
set change constant global add remove if else loop repeat while each stop skip return fail await use from in to do end note endnote
func class extends with async pure private or_else self super is matches isa not and or true false empty
```

Six of them may nevertheless be used as variable, function, parameter and loop-variable names: `add`, `to`, `in`, `global`, `not` and
`from`. Every other reserved word is always reserved. `as` (after `or_else`) and `init` (the name of the method that sets up an
object) are not reserved; they have their meaning only in those places.

The names of the types (`number`, `str`, `boolean`, `list`, `map`, `match`, `pattern`, `object`, `empty`) and the names of classes are not reserved either. They are read as types only
where a type may stand: after `set [constant]`, before the name of a parameter, and in a field declaration. Anywhere else they are ordinary
names, so `set list list to [1, 2]` declares a list called `list`. The same goes for the names of the built-in functions (`find`, `count`, `copy` and so on): a variable may be called `count`, and a
function of your own with the name of a built-in replaces it.

### 1.6 Literals

| Kind | Form | Notes |
| :--- | :--- | :--- |
| number | `42`, `3.14` | Decimal digits with an optional `.` followed by digits. There is no exponent form, no leading or trailing `.`, no digit separators. A leading `-` is the unary operator. |
| str | `"text"` or `'text'` | May span lines. Escapes: `\"` `\'` `\\` `\n` `\t` `\r`; any other `\x` is kept as written (backslash and character), which is what lets patterns contain `\.`. |
| f-string | `f"text {expression} text"` | Double quotes only. See section 4.8. |
| pattern | `p"[num]+"`, `p"hello"i` | A pattern, written in the pattern language and checked when the program is read. Raw: a backslash is not an escape of the string, and `\"` is a quote. Flags `i` and `m` may follow. See section 4.7 and [patterns](patterns.md). |
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
| `match` | The result of a successful pattern match (see `patterns.md`) | — (a failed match gives `empty`, not a `match` value) |
| `pattern` | A compiled pattern: the value of a `p"..."` literal or of `pattern(text)` | never |
| *a class* | An object made from a class (see [classes](classes.md)); the type `object` stands for any of them | never |
| `empty` | No value | always |

**Truthiness.** `if`, `else if`, `loop while` and `!` accept a value of any type and treat it as
*falsy* or *truthy* by the last column above. `to_boolean(x)` applies the same rule, so
`to_boolean("false")` is `true`: any non-empty string is truthy.

**References.** Lists, maps and objects are references. Assigning one to another variable, or passing it to
a function, shares the same underlying value, so a change made through either name is seen through
both. Numbers, strings, booleans and `empty` behave as values. A list or map that contains itself is
legal; printing and comparing it terminates. (The engine never frees such a value; see
`design-notes.md`, item 11.)

**Equality (`is`).** Values of different types are never equal. Numbers compare by value and strings
by content. Lists and maps compare structurally: the same length or keys, with equal elements. Two
circular structures of the same shape compare equal. Two patterns are equal when their text and flags are the same. Two objects are equal only when
they are the same object (not merely equal in their fields).

**Display form.** `print`, f-strings and `to_str` render a value as follows.

| Value | Display |
| :--- | :--- |
| a top-level `str` | the text itself |
| a whole `number` smaller than 10<sup>15</sup> in magnitude | its digits, with no decimal point or exponent (`100`, `-3`, `100000000000000`); `-0` is `0` |
| any other `number` | at most 15 significant digits, in exponent form when the magnitude is very large or small (`0.3`, `1234567.891`, `1e+15`, `1e-06`) |
| `true`, `false`, `empty` | those words |
| a `list` | `[1, "a", [2]]` |
| a `map` | `{"k": "v"}` |
| a `match` | `<match "matched text">` |
| a `pattern` | the literal as written: `p"[num]+"i` |
| an object | the result of its `to_str()` method when its class has one, otherwise `<ClassName>` |

Strings inside a list or map are wrapped in double quotes and are **not** escaped, so a quote or line break inside such a string appears as it is. A list or map that contains itself shows `[...]` or `{...}` where it repeats, and very deep nesting is cut off the same way.

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

- The type is one of `number`, `str`, `list`, `map`, `boolean`, `match`, `pattern`, `object`, `empty`, or the name of a class, and it is required: `set x to 5` is a syntax error. There is no type inference.
- The value's type must equal the declared type (`E4-020`); a class accepts its own objects and those of its children. `empty` is accepted for every type, so
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

A constant can have any of the types above, and none of them accepts `change` on the constant's own name. What else is
protected depends on the type: a number, a string and a boolean have nothing else to protect, a list is read-only through every
name (below), and a map is protected only through the constant's own name.

**Constant lists.** `set constant list PRIMES to [2, 3, 5, 7]` creates a read-only list. `add`,
`remove` and `change` on it are errors (`E4-003`). The value is copied when the constant is created,
so freezing it never freezes a list another variable already refers to; and the read-only property
belongs to that list, so it follows the value into other variables and function arguments. The
protection is shallow: a list or map stored inside the constant list can still be changed unless it
was itself declared constant.

**Constant maps.** `change CONFIG["key"] to ...` and `remove "key" from CONFIG` are errors (`E4-003`) when `CONFIG` is a
constant map, but the map itself is not frozen: another variable that was given the same map can still change it, and the constant then
shows the change. Copy with `copy(CONFIG)` when a program needs a map that is independent of the constant.

```cuff
set list source to [1, 2, 3]
set constant list SNAPSHOT to source   note: a frozen copy of source
add 99 to source                       note: allowed; SNAPSHOT is still [1, 2, 3]
set list mine to copy(SNAPSHOT)        note: a copy is a new, mutable list
```

---

## 4. Expressions

### 4.1 Precedence

From the tightest to the loosest binding:

| Level | Operators | Associativity |
| :---: | :--- | :--- |
| 1 | call `f(...)`, index `x[i]`, slice `x[i~j]`, member `x.name` | left |
| 2 | `**` | right |
| 3 | unary `-` | right |
| 4 | `*` `/` `%` | left |
| 5 | `+` `-` | left |
| 6 | `is` `is not` `matches` `isa` `>` `<` `>=` `<=` | left |
| 7 | `!` | right |
| 8 | `and` | left |
| 9 | `or` | left |

Parentheses group. `**` binds tighter than a unary `-` on its left and takes a unary operand on its
right, so `-2 ** 2` is `-4` and `2 ** -1` is `0.5`. Because `!` binds looser than a comparison,
`!x is 3` means `!(x is 3)` and `!text matches p"a"` means `!(text matches p"a")`; because it binds tighter than `and`, `!a and b` means `(!a) and b`.

There is no `==` or `!=`; equality is `is` and `is not` (section 4.4). Comparison does not chain:
`1 < 2 < 3` compares the boolean `true` with `3` and is an error (`E4-005`).

### 4.2 Arithmetic

`*`, `/`, `%`, `**` and unary `-` need numbers. `/` always divides as floating point (`5 / 2` is
`2.5`), and division by zero is an error (`E4-006`). `+` adds two numbers, concatenates two strings, or
joins two lists into a new list. Any other combination, such as `"a" + 1`, is an error (`E4-005`);
convert explicitly with `to_str` or `to_number`.

`%` is the remainder of a division and takes the sign of the divisor: `-7 % 3` is `2` and `7 % -3`
is `-2`, so `x % n` stays from `0` up to `n` for a positive `n`. It is the operator form of
`math_mod`. A remainder by zero is `E4-006`; one that is not a finite number is `E4-025`.

`**` raises to a power, is the operator form of `math_pow`, and groups to the right: `2 ** 10` is
`1024`, `9 ** 0.5` is `3` and `2 ** 3 ** 2` is `2 ** 9`. `0 ** -1` is `E4-006`; a negative base with a
fractional exponent, or a result too large to represent, is `E4-025`.

```cuff
print(7 % 3, -7 % 3, 2 ** 10, 2 ** 3 ** 2, -2 ** 2)
```

```output
1 2 1024 512 -4
```

### 4.3 Comparison

`>`, `<`, `>=` and `<=` compare two numbers or two strings. Strings compare by their UTF-8 bytes, so
`"a" < "B"` is false and Hangul sorts in code-point order. Comparing different types is an error
(`E4-005`).

### 4.4 Comparing: `is`, `matches`, `isa`

`a is b` is true when the two values are equal (section 2); `a is not b` is its negation. `is` means the same for every type of value and for
every operand: a string on the right is just a string, never a pattern. Two objects are equal only when they are the same object.

`text matches pattern` is true when the **whole** of `text` fits the pattern. The left side must be a `str` and the right side a `pattern`, otherwise it is an error
(`E4-005`). `matches` is a comparison, so `!` negates it without parentheses. To look for a pattern *inside* a text, use `find`, `match` or `count` (section 4.7).

`value isa ClassName` is true when `value` is an object of that class or of one of its children, and false for any other value, including every value that is not an object. The name must be a class (`E4-031`).

```cuff
set str input_text to "Apple"
set str code to "AB-1234"
if input_text is "Apple" do:
    print("equal")
end
if input_text matches p"apple"i do:
    print("same word, ignoring case")
end
if code matches p"[up]2-[num]4" do:
    print("code shape matches")
end
if !code matches p"[num]+" do:
    print("not all digits")
end
```

```output
equal
same word, ignoring case
code shape matches
not all digits
```

A syntax error in a pattern literal is reported before the program runs; the rules for a literal that contains `\` are in [patterns](patterns.md).

### 4.5 Logic: `!`, `and`, `or`

`!x` is `true` when `x` is falsy. It applies to a whole comparison to its right.

`a and b` is `true` when both operands are truthy, and `a or b` when at least one is. The operands may
be of any type (section 2 lists what is falsy) and the result is always a `boolean`, never one of the
operands. The right operand is evaluated only when the left one does not already decide the result:
`and` stops at a falsy left operand and `or` at a truthy one, so `x is not 0 and 10 / x > 1` never
divides by zero.

```cuff
set number age to 30
if age >= 18 and age < 65 do:
    print("working age")
end
print(age < 18 or age >= 65, !(age > 20) or age is 30)
print(0 and 1 / 0, 7 or 1 / 0)
```

```output
working age
false true
false true
```

### 4.6 Indexing and slicing

Positions are **1-based**; position `0` does not exist (`E4-007`). A negative position counts from
the end: `-1` is the last element.

```cuff
set list colors to ["red", "green", "blue", "yellow"]
print(colors[1])        note: red
print(colors[-1])       note: yellow
print(colors[2~3])      note: ["green", "blue"]
```

- `x[i~j]` takes positions `i` through `j`, **both included**, and the bounds may be negative too: `colors[-2~-1]` is the
  last two elements. If `i` is after `j` the result is empty (an empty list or string). A position outside the sequence is an
  error (`E4-008`), and position `0` is an error in a slice as it is anywhere else (`E4-007`).
- A position must be a whole number (`E4-024` otherwise).
- Strings are indexed by **code point**, not by byte, so `"안녕"[1]` is `"안"`. A string cannot be changed in place:
  `change text[1] to "x"` is an error (`E4-005`); build a new string with `+` and a slice instead.
- A map is indexed by a string key. A missing key is an error (`E4-009`); assigning to a new key with
  `change` creates it.

### 4.7 Patterns and text functions

A pattern is a value of its own. A **pattern literal** is written `p"..."` and is checked for syntax when the program is read; the flags `i` (ignore case) and `m` (lines) may follow the closing quote. A pattern can be kept in a variable of the type `pattern`, and one can be built while the program runs with `pattern(text)` or `pattern(text, flags)` (a syntax error in the text is then raised at that call).

Seven built-in functions work on text and patterns. They need no `use`, and the text is always the first argument:

```cuff
set match r to match(text, p"SN-([num]4)-([num]+)")
set str first to find(article, p"T-[num]3")
set list all to find_all(article, p"T-[num]3")
set str masked to replace(raw, p"010-[num]4-", "010-****-")
set str cleaned to replace_all(raw, p"[sp]+", " ")
set list parts to split(line, p"[,;][sp]*")
set number n to count(document, p"apple"i)
```

Each is specified, with its results and its failure values, in [patterns](patterns.md). In short: a failed `match` or `find` yields `empty`; `find_all` yields a list (empty when nothing matched); `count` yields `0`; `replace` changes the first match and `replace_all` every match, and the replacement is used as plain text; `split` keeps empty pieces. A string where a pattern is wanted is an error (`E4-005`) with a hint to write `p"..."` or to use `pattern(text)`.

### 4.8 f-strings

`f"Hello, {name}! 1 + 1 = {1 + 1}"` evaluates each `{expression}` and inserts its display form
(section 2). Write `{{` and `}}` for literal braces. An expression inside braces uses single quotes
for string literals, because a double quote would end the f-string: `f"year {res['year']}"`. An
error inside the braces is reported at the line of the f-string.

---

## 5. Statements

### 5.1 Changing places: `change`, `add`, `remove`

`change`, `add` and `remove` all take a **place**: a spot that holds a value. A place is a name, `self`, or a `.name` (inside a `with` block), followed by any number of `[index]` and `.field` steps:

```text
x          grid[2]          grid[2][3]          player.score          self.items          team[1].tags[2]          .y
```

A slice (`x[1~2]`) and the result of a call (`f()`) are not places.

| Statement | Effect |
| :--- | :--- |
| `change [place] to [value]` | Replaces what the place holds. For a list item, that is the element at that position; for a map entry, it sets or creates the key; for a field, it sets the field. |
| `add [value] to [place]` | Appends to the end of the list the place holds. |
| `remove [position] from [place]` | A number removes the element at that position of the list. |
| `remove [value] from [place]` | Any non-number removes the first element of the list equal to it (`E4-015` if none). |
| `remove "key" from [place]` | Deletes the key of the map the place holds; a missing key is ignored. |

Every step before the last is only *read*, so these statements change the collection that the place leads to, in place, and every name that refers to it sees the change. On a constant
collection they are errors (`E4-003`).

```cuff
set list grid to [[1], [2]]
add 5 to grid[1]
change grid[2][1] to 9
print(grid)
```

```output
[[1, 5], [9]]
```

**Positions.** A position must exist: `0` is `E4-007` and one past the end is `E4-008`, for `change` and for `remove`
alike. A map's entries are changed with `change [place]["key"]` and removed by key. `add`, and `remove` with a position, need a
list (`E4-005` for a map).

**Position or value.** `remove` decides by the *type* of its first operand: a number is always a position. To delete the
number `20` from `[10, 20, 30]`, look its position up first:

```cuff
use DLC:list
set list items to [10, 20, 30]
remove index_of(items, 20) from items
print(items)
```

```output
[10, 30]
```

**Objects.** A field is a place like any other, so `change player.score to 10` sets a field, `add item to self.items` appends to a list that a field holds, and `change self.grid[2][3] to 0` reaches into it. See [classes](classes.md).

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

loop each item in [collection] do:
    ...
end
```

- `loop repeat [var] to [start] ~ [end]` runs the body once for each whole number from `start` to
  `end` inclusive, counting **down** when `start` is greater. Both bounds are evaluated once, before
  the first pass, and must be whole numbers (`E4-024`).
- A `repeat` range is never empty: `1 ~ 1` runs once, and `1 ~ 0` runs **twice**, with `i` equal to `1` and then `0`, because the loop counts down. To visit the elements of a list that may be empty, use `loop each`; a `repeat` loop over its positions has
  to test the length first:

  ```cuff
  use DLC:list
  set list items to []
  if length(items) > 0 do:
      loop repeat i to 1 ~ length(items) do:
          print(items[i])
      end
  end
  print("done")
  ```

  ```output
  done
  ```

- `loop while` re-tests its condition before every pass.
- `loop each [var] in [collection]` runs the body once for each element of a list, each key of a map (in the order the keys were added) or each character of a string (a code point). Anything else is an error (`E4-005`). An empty collection runs the body no times. The loop works from a snapshot taken when it starts, so adding to the list inside the body does not make the loop longer.
- `stop` ends the **nearest enclosing** loop only, and `skip` ends the current pass of the nearest enclosing loop and goes on with the next one. Outside a loop either is an error (`E4-023`).
- A loop does not open a scope: `[var]` and anything declared in the body remain visible after the
  loop, holding their last values.

```cuff
loop each word in ["a", "", "b"] do:
    if word is "" do:
        skip
    end
    print(word)
end
```

```output
a
b
```

### 5.4 Printing and input

`print(a, b, ...)` writes its arguments separated by spaces and ends the line; a `str` argument is
written as is and anything else in its display form. `print()` with no arguments writes an empty line,
and an empty string argument still contributes its separator (`print("x", "", "y")` writes `x  y`).
`print` gives `empty`.

`input()` or `input(prompt)` writes the optional prompt (which must be a `str`), reads one line and returns it as a `str`, without
its line ending: a line feed, or a carriage return and a line feed, so a program reads the same text from a file written on any
platform. A last line without an ending is returned as it is. At the end of the input it returns the empty string, so a loop that
reads until a blank line also stops at the end of a file.

### 5.5 Classes, `with` and `fail`

A **class** groups values (fields) and functions (methods) under one name, and an object made from it carries its own values:

```cuff
class Counter do:
    set number count to 0

    func bump() do:
        change self.count to self.count + 1
    end
end

set Counter c to Counter()
c.bump()
print(c.count)
```

```output
1
```

- `class Name extends Parent do: ... end` declares a class at the top level of a file. The body holds fields (`set [constant] type name to value`) and methods (`func`, optionally `async`, `pure` or `private`).
- `Name(arguments)` makes an object; the method `init` receives the arguments. `object.field` and `object.method(arguments)` use an object, and `self` and `super` are available inside methods.
- `with [object] do: ... end` lets a block use the object's members by their plain names.
- `fail [value]` raises an error, and `or_else as [name]` catches it as an object (section 8.3).

[Classes and objects](classes.md) is the full reference; the [guide](../guide/classes-and-objects.md) teaches it step by step.

---

## 6. Functions

### 6.1 Declaring and calling

```cuff
func greet(name) do:
    print(f"Hello, {name}!")
end

func add_numbers(number a, number b) do:
    return a + b
end

greet("Ann")
set number total to add_numbers(2, 3)
```

- A function is declared with `func [name]([parameters]) do:` followed by an indented body and
  `end`, or on one line: `func twice(x) do: return x * 2 end`. There is no `set` in front.
- Functions are declared at the top level of a program or module (or in a block of it). Declaring one inside another
  function or a method is an error (`E4-018`).
- A call is `name(arguments)`. It is a statement on its own line or an expression. It does not use
  `do:`. Calling a name that is not defined is an error (`E4-002`).
- Declarations are **hoisted**: a function may be called above the line that declares it. If a name is
  declared more than once, the last declaration in the file is the one every call uses.
- The number of arguments must be at least the number of parameters without a default and at most the number of parameters (`E4-010`). There are no variadic parameters.
- A function that has the same name as a built-in function replaces it for the whole program.
- Functions are not values. They cannot be stored in a variable, passed, returned or compared, and
  there are no closures.
- Calls may nest only so deep (see [limits](limits.md)), which makes unbounded recursion an error (`E4-017`).

### 6.2 Parameters and their types

A parameter is written `name` or `type name`, and may end with `to [default]`. The types are `number`, `str`, `boolean`, `list`, `map`,
`match`, `pattern`, `object` and the name of a class, and typed and untyped parameters may be mixed in one list. A word alone is always a name:
`f(list)` has a parameter called `list`, and `f(list items)` a parameter `items` of type `list`.

A typed parameter checks its argument at the call: a value of another type is an error on the calling
line (`E4-030`). `empty` is accepted by every type.

**Default values.** A parameter written `name to [value]` is optional. The parameters without a default come first (a syntax error otherwise). The default is worked out **each time** the function is called and
without that argument, in the scope of the function, so it can use the parameters before it but not a global variable. A default is checked against the parameter's type when it is used, like an argument (`E4-030`).

```cuff
func greet(str name, str greeting to "Hello") do:
    return f"{greeting}, {name}!"
end

print(greet("Ann"))
print(greet("Bob", "Hi"))
```

```output
Hello, Ann!
Hi, Bob!
```

### 6.3 Returning values

- `return [value]` ends the call and gives that value to the caller. A bare `return` ends the call and gives `empty`, and so does a body that ends without a `return`.
- Outside any function a bare `return` is an error (`E4-022`), and `return [value]` is `E4-016`.

### 6.4 Modifiers

```text
[async] [pure] func [name]([parameters]) do:
```

`async` and `pure` are independent and may appear in either order, each at most once. In a class, `private` may come first (see [classes](classes.md)).

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
- `await` on a function that is not `async` is an error (`E4-019`). The queue has a size limit
  (see [limits](limits.md); `E4-026`).

### 6.6 Pure functions

A `pure` function may use its parameters, its own locals and the built-in and library functions. It
cannot touch global state:

- Reading, changing or bridging (`global x`) a global variable raises
  `PureFunctionGlobalAccess` (`E4-027`).
- Calling a user function that is not itself `pure` raises `PureFunctionImpureCall` (`E4-029`). A pure
  function may call itself and other pure functions.
- A pure method may read the fields of `self` and call other pure methods, but changing a field raises `PureFunctionFieldWrite` (`E4-041`).

The second rule is what keeps a pure function pure: otherwise a pure function could reach a global by
calling an ordinary helper. To lift these restrictions, remove the `pure` keyword.

```cuff
use DLC:math
pure func hypotenuse(a, b) do:
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
4. **An `or_else` handler runs in its own scope.** A new name declared in it is local to the handler,
   and so is the name given by `or_else as`; an existing name can still be changed with `change`.
5. **A `with` block has a scope of its own.** A name declared in it is gone after the block's `end` (see [classes](classes.md#the-with-block)).
   A method of a class sees its own locals and parameters like any function, plus `self`.

### 7.1 Reaching a global from a function

A function sees its own locals and parameters and **nothing else** from outside itself: no global
variable, not even a constant, and no local of its caller. To use a global, bridge it:

```cuff
set number visits to 0
set constant number STEP to 5

func record() do:
    global visits     note: from here on, `visits` is the global
    global STEP       note: constants are bridged the same way
    change visits to visits + STEP
end

func peek() do:
    print(visits)               note: error (E4-001): `visits` is not visible here
end

record()
print(visits)                   note: 5
```

- `global [name]` (or `global [a], [b]` for several names) is valid only inside a function body (`E4-021` elsewhere). It applies from
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
| `E7-xxx` | Program errors, raised with `fail` | yes |
| `E6-xxx` | Resource limits (steps, time, memory) | no |
| `E9-xxx` | Internal | no |

In practice, malformed source found while tokenizing *or* parsing — a stray character, a space before
a colon, an unterminated string, a missing `end`, bad indentation — is reported as `E2-001 Syntax
Error`, and the message says what is wrong. The [error catalogue](errors.md) lists every
code, and marks those the engine defines but does not currently raise.

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

`or_else as [name] do:` also gives the handler the error as a value (section 8.3).

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
7. Errors that are not catchable (resource limits, internal errors, and syntax errors in a pattern) always end the program.

`or_else` guards the one statement it follows; to guard several, put them in a function and guard the
call. It cannot guard the work of an `async` call that is queued rather than awaited (section 6.5).

### 8.3 Error objects and `fail`

`or_else as [name] do:` runs the handler with the error in a variable. Every error a handler can catch arrives as an object of the built-in class `Error`, with the fields `message`, `code`, `line`, `column` and `hint`.

`fail [value]` raises an error from the program. The value is an `Error` object, or a string, which becomes `Error(string)`; its code is `E7-001`. A class of your own may extend `Error` to carry more. Inside a handler, `fail [name]` raises the same error again.

```cuff
func divide(number a, number b) do:
    if b is 0 do:
        fail "cannot divide by zero"
    end
    return a / b
end

print(divide(6, 3))
print(divide(1, 0)) or_else as err do:
    print(f"{err.code}: {err.message}")
end
```

```output
2
E7-001: cannot divide by zero
```

The fields, the classes that extend `Error` and the rules for catching one kind of error are in [classes](classes.md#errors-are-objects).

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
- Its functions and classes become available to the whole program. Its top-level variables are copied into the
  scope that wrote `use`, after the module has run (section 7.2).
- Modules may load modules; the nesting is limited (see [limits](limits.md); `E5-007`).

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
| `type_of(x)` | The type name: `"number"`, `"str"`, `"boolean"`, `"list"`, `"map"`, `"match"`, `"pattern"` or `"empty"`; for an object, the name of its class. |
| `to_number(x)` | A number is returned as it is; `true` and `false` give `1` and `0`; a string is read as plain decimal text (`E4-025` for any other text, [exactly which is listed here](dlc/convert.md#what-to_number-accepts)). Any other type is `E4-005`. |
| `to_str(x)` | The display form of any value (section 2). |
| `to_boolean(x)` | The truthiness of any value (section 2). |
| `copy(x)` | A new list, map or object with the same elements or fields (a shallow copy). Any other value is `E4-005`. |
| `pattern(text)`, `pattern(text, flags)` | A pattern made from text while the program runs; `flags` is a string of `i` and `m`. A syntax error in the text is `E3-0xx`. |
| `find`, `find_all`, `match`, `replace`, `replace_all`, `split`, `count` | Text functions that take the text first and a pattern second (section 4.7, [patterns](patterns.md)). |

### 10.3 `length`, `contains`, `index_of`

These three are not available until a library that registers them is loaded: `length` and `contains` come with
any of `DLC:string`, `DLC:list` or `DLC:map`, and `index_of` with `DLC:string` or `DLC:list`. Without one of those lines
the call is `E4-002`, and `use DLC:map` alone does not give `index_of`.

| Function | Result |
| :--- | :--- |
| `length(x)` | Characters of a string, elements of a list, keys of a map. Any other type is `E4-005`. |
| `contains(x, v)` | Whether a string contains the substring `v`, a list contains an element equal to `v`, or a map has the key `v`. For a string or a map `v` must be a `str` (`E4-005`). |
| `index_of(x, v)` | The 1-based position of the first occurrence of `v` in a string (counted in characters) or list, or `empty` if there is none. A map is `E4-005`. |

### 10.4 Method calls on built-in values

A built-in function whose first argument is the value it works on can also be written as a method of that value: `value.name(other arguments)` runs the function with `value` as its first argument.

```cuff
use DLC:string, DLC:list
set str title to "  hello  "
print(title.trim().upper())
print([3, 1, 2].sort(), "a,b".split(p","), [1, 2, 3].length())
```

```output
HELLO
[1, 2, 3] ["a", "b"] 3
```

- The method name is the function name without its library prefix: `str_upper(s)` is `s.upper()`, `list_sort(xs)` is `xs.sort()`, `map_keys(m)` is `m.keys()`, `math_round(x, d)` is `x.round(d)`. `length`, `contains` and `index_of` keep their names, and the conversions are `x.to_str()`, `x.to_number()`, `x.to_boolean()` and `x.to_json()`.
- The text functions are methods too (`text.find(p"[num]+")`), and `copy` is a method of lists, maps and objects.
- A method needs the library of its function, loaded with `use DLC:...`, just as the function does. Without it the call is `E4-033` and the hint names the library.
- A function that has no receiver (`math_min`, `random_*`, `time_*`, `file_*`, `network_*`) is not a method.
- The function form always works as well. The [function index](functions.md) lists both forms.

### 10.5 Library reference

Each library has its own page in the [library reference](dlc/README.md), and
[all functions](functions.md) are indexed in one place. Names follow `library_verb`. The libraries are
`math`, `string`, `time`, `random`, `list`, `map`, `convert`, `json`, `network` and `filesystem`.

A host can switch `DLC:network` and `DLC:filesystem` off, and nothing a library does can leave the sandbox described in
[section 9.2](#92-your-own-modules-use--from-). Both act on the outside world and are described together with their
risks in [the security policy](../../SECURITY.md); [the rules for libraries](dlc/README.md#libraries-that-reach-outside-the-program) say what a switched-off library does.

---

## 11. Running programs

### 11.1 Command line

`cuffc` runs a script and `cuffsh` is the interactive shell; both take the same engine options. A program
that embeds the engine sets the same choices through `CuffEngine::Options`. See [`cuffc`](cuffc.md) and
[`cuffsh`](cuffsh.md) for every option.

### 11.2 Limits

The engine bounds nesting, recursion, sizes and the work a pattern may do, and reports exceeding a
bound as an ordinary error. The values, and the error each one raises, are in [limits](limits.md); the
execution budgets `--max-steps` and `--timeout` are off unless set (`E6-001`, `E6-002`).

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

note: Step 3: declare the functions that handle logic checks and text pattern matching
func audit_and_assess_user(email, lvl) do:

    global MAX_LEVEL

    note: validate the email's structure with pattern matching (the `is` operator)
    if email matches p"[str]+@[str]2~10" do:
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
async func backup_user_cloud_data() do:
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

Every error code, what it means and whether `or_else` can catch it is in the [error catalogue](errors.md).
