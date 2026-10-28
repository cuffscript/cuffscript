# Error catalogue

Every error has a code of the form `E<category>-<nnn>`. The category tells you what kind of problem it is, and
whether a script can recover from it with `or_else` (see the
[language reference](language.md#8-errors-and-or_else)). How to read an error report, and what to do about the
common ones, is in the [guide to errors](../guide/errors-and-debugging.md).

| Category | Range | Meaning | `or_else` can catch it |
| :--- | :--- | :--- | :---: |
| Lexical | `E1-xxx` | Reading characters into tokens | no |
| Syntax | `E2-xxx` | The shape of the program | no |
| Regex syntax | `E3-0xx` | A malformed pattern | no |
| Regex runtime | `E3-1xx` | Matching a pattern | yes |
| Runtime | `E4-xxx` | Running the program | yes |
| Module | `E5-xxx` | Modules and libraries | yes |
| Resource limit | `E6-xxx` | Steps, time, memory | no |
| Internal | `E9-xxx` | A bug in the engine | no |

In practice, malformed source found while tokenizing or parsing (a stray character, an unterminated string, a
missing `end`, bad indentation) is reported as `E2-001`, and the message says what is wrong. The other `E1` and
`E2` codes are, with a few exceptions, reserved numbers; the catalogue marks every code that nothing raises.

## Codes

<!-- BEGIN GENERATED: error-table -->
_This table is generated from `engine/common/ErrorCodes.h` by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._

### Lexical Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e1-001"></a>`E1-001` | A character that cannot begin any token. _Reserved: never raised._ | no |
| <a id="e1-002"></a>`E1-002` | A string literal that is never closed. _Reserved: never raised._ | no |
| <a id="e1-003"></a>`E1-003` | A malformed number literal. | no |
| <a id="e1-004"></a>`E1-004` | Indentation that does not line up with the enclosing block. _Reserved: never raised._ | no |
| <a id="e1-005"></a>`E1-005` | A space before the colon that opens a block. _Reserved: never raised._ | no |
| <a id="e1-006"></a>`E1-006` | A block comment that is never closed. _Reserved: never raised._ | no |
| <a id="e1-007"></a>`E1-007` | `=` written where `to` is required. _Reserved: never raised._ | no |

### Syntax Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e2-001"></a>`E2-001` | The token found cannot appear here. Every syntax problem found while tokenizing or parsing is reported with this code. | no |
| <a id="e2-002"></a>`E2-002` | A specific token was required. _Reserved: never raised._ | no |
| <a id="e2-003"></a>`E2-003` | A malformed function declaration. _Reserved: never raised._ | no |
| <a id="e2-004"></a>`E2-004` | A malformed `if` or loop. _Reserved: never raised._ | no |
| <a id="e2-005"></a>`E2-005` | A malformed `use` statement. _Reserved: never raised._ | no |
| <a id="e2-006"></a>`E2-006` | A block opened without a matching `end`. _Reserved: never raised._ | no |
| <a id="e2-007"></a>`E2-007` | Something that cannot be assigned to. _Reserved: never raised._ | no |
| <a id="e2-008"></a>`E2-008` | Code nested deeper than the nesting limit, or a single expression longer than the expression limit. | no |
| <a id="e2-009"></a>`E2-009` | A program or shell entry larger than the source size limit. | no |

### Regex Syntax Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e3-001"></a>`E3-001` | A `(` or named capture that is never closed, or a `<` that does not begin a `<name:...>` capture. | no |
| <a id="e3-002"></a>`E3-002` | A `[` that is never closed. | no |
| <a id="e3-003"></a>`E3-003` | Wrong spacing around the colon of a token such as `[one:a\|b]`. | no |
| <a id="e3-004"></a>`E3-004` | A repeat range or character range that cannot match anything, such as `5~2` or `[z-a]`, or a repeat count above the repeat limit. | no |
| <a id="e3-005"></a>`E3-005` | Two quantifiers applied to the same item, such as `[num]++`. | no |
| <a id="e3-006"></a>`E3-006` | An empty token or an empty alternative. | no |
| <a id="e3-007"></a>`E3-007` | A bracket token whose name before the colon is not `one`. A pattern cannot reach it today: an unknown word in brackets is read as a set of characters. | no |
| <a id="e3-008"></a>`E3-008` | A backslash that escapes nothing, or something that cannot be escaped. | no |
| <a id="e3-009"></a>`E3-009` | A `~` that is neither part of a repeat range nor escaped, such as `a~b`. | no |
| <a id="e3-010"></a>`E3-010` | A character that cannot appear at this point of a pattern. | no |
| <a id="e3-011"></a>`E3-011` | A pattern larger than the pattern size limit, or nested deeper than the pattern nesting limit. | no |

### Regex Runtime Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e3-101"></a>`E3-101` | Matching used more steps than its budget allows. | yes |
| <a id="e3-102"></a>`E3-102` | Matching ran longer than its time budget allows. | yes |
| <a id="e3-103"></a>`E3-103` | Matching recursed deeper than its limit. | yes |

### Runtime Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e4-001"></a>`E4-001` | A variable that has not been declared, or is not visible here. | yes |
| <a id="e4-002"></a>`E4-002` | A function that has not been declared. | yes |
| <a id="e4-003"></a>`E4-003` | A constant that is changed after it was created. | yes |
| <a id="e4-004"></a>`E4-004` | A constant whose name is not all uppercase letters, digits and underscores. | yes |
| <a id="e4-005"></a>`E4-005` | A value of the wrong type for an operation. | yes |
| <a id="e4-006"></a>`E4-006` | Division or remainder by zero, or zero to a negative power. | yes |
| <a id="e4-007"></a>`E4-007` | Position 0, which does not exist (positions start at 1). | yes |
| <a id="e4-008"></a>`E4-008` | A position outside a list, string or slice. | yes |
| <a id="e4-009"></a>`E4-009` | A map key that is not present. | yes |
| <a id="e4-010"></a>`E4-010` | A call with the wrong number of arguments. | yes |
| <a id="e4-011"></a>`E4-011` | Calling something that is not a function. _Reserved: never raised._ | yes |
| <a id="e4-012"></a>`E4-012` | An operand an operator cannot work with. _Reserved: never raised._ | yes |
| <a id="e4-013"></a>`E4-013` | A local variable used outside the function that declared it. _Reserved: never raised._ | yes |
| <a id="e4-014"></a>`E4-014` | An operation a list or map does not support. _Reserved: never raised._ | yes |
| <a id="e4-015"></a>`E4-015` | A value to remove that is not in the list. | yes |
| <a id="e4-016"></a>`E4-016` | An operation not supported here, such as `return value` in a function that is not `returnable`. | yes |
| <a id="e4-017"></a>`E4-017` | Recursion deeper than the call depth limit, or deeper than the native stack budget allows. | yes |
| <a id="e4-018"></a>`E4-018` | A function declared inside another function. | yes |
| <a id="e4-019"></a>`E4-019` | `await` applied to a function that is not `async`. | yes |
| <a id="e4-020"></a>`E4-020` | A declared type that does not match the value. | yes |
| <a id="e4-021"></a>`E4-021` | `change ... to global` outside a function body. | yes |
| <a id="e4-022"></a>`E4-022` | `return` outside any function. | yes |
| <a id="e4-023"></a>`E4-023` | `stop` outside any loop. | yes |
| <a id="e4-024"></a>`E4-024` | A position or range bound that is not a whole number. | yes |
| <a id="e4-025"></a>`E4-025` | An argument of the right type but a value the function or operator does not accept. | yes |
| <a id="e4-026"></a>`E4-026` | A string, list, map or `async` queue larger than its limit. | yes |
| <a id="e4-027"></a>`E4-027` | A `pure` function that reaches a global variable. | yes |
| <a id="e4-028"></a>`E4-028` | A network request that failed, timed out, or targeted a blocked address. | yes |
| <a id="e4-029"></a>`E4-029` | A `pure` function that calls a function that is not `pure`. | yes |
| <a id="e4-030"></a>`E4-030` | An argument that does not match the parameter's declared type. | yes |

### Module Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e5-001"></a>`E5-001` | A module file that does not exist or cannot be read. | yes |
| <a id="e5-002"></a>`E5-002` | A module file that does not parse. | yes |
| <a id="e5-003"></a>`E5-003` | Modules that import each other in a cycle. _Reserved: never raised._ | yes |
| <a id="e5-004"></a>`E5-004` | A library name that does not exist. | yes |
| <a id="e5-005"></a>`E5-005` | A call into a library that was switched off for this run (`--no-network`, `--no-filesystem`). | yes |
| <a id="e5-006"></a>`E5-006` | A module path that is absolute, or resolves outside the allowed directory. | yes |
| <a id="e5-007"></a>`E5-007` | Modules imported too deeply, or a module file larger than the source size limit. | yes |
| <a id="e5-008"></a>`E5-008` | A file path that resolves outside the allowed directory. | yes |

### Resource Limit Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e6-001"></a>`E6-001` | The run exceeded `--max-steps`. | no |
| <a id="e6-002"></a>`E6-002` | The run exceeded `--timeout`. | no |
| <a id="e6-003"></a>`E6-003` | The host ran out of memory. | no |

### Internal Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e9-001"></a>`E9-001` | A bug in the engine: please report it. | no |
<!-- END GENERATED: error-table -->

## What each error looks like

Every example below, except the one for `E4-028`, is run by the documentation checks and produces exactly the code it is listed under. The text says what usually causes the error and how to fix it. Codes that need a command-line option to
reproduce, and codes that nothing in an ordinary program raises, are described at the end.

### Syntax

**E2-001.** The program is not valid CuffScript. The message says what was expected and the caret shows where the parser gave up. The usual causes are `=` written where `to` is needed, a block with no `end`, a missing `do:`, a space before a colon, a string with no closing quote and a stray character. Put the first error right and run again: later errors are often caused by the first.

```cuff error E2-001
set number x = 5
```

### Patterns

**E3-001.** A pattern has a `(` that is never closed, or a `<` that does not start a named capture such as `<year:[num]4>`. Close the group, or escape the character you meant literally: `\(` or `\<`.

```cuff error E3-001
print("x" is "(a")
```

**E3-002.** A pattern has a `[` that is never closed. Close it, or write `\[` to match a bracket.

```cuff error E3-002
print("x" is "[num")
```

**E3-003.** There is a space between a token's word and its colon. Write `[one:a|b]`, with the colon right after the word.

```cuff error E3-003
print("x" is "[one :a|b]")
```

**E3-004.** A repeat range must run from the smaller number to the larger. `5~2` cannot match anything. The same code is raised for a character range written backwards, such as `[z-a]`, and for a repeat count larger than the pattern repeat limit (see [limits](limits.md)).

```cuff error E3-004
print("x" is "[num]5~2")
```

**E3-005.** A pattern item can have one quantifier. `[num]++` has two. Keep one of them.

```cuff error E3-005
print("x" is "[num]++")
```

**E3-006.** A token with nothing in it, or an alternative with nothing between its bars. `[one:]` and `[one:a||b]` are both errors.

```cuff error E3-006
print("x" is "[one:]")
```

**E3-008.** A backslash must be followed by one of the characters it can escape: `[ ] ( ) { } < > + * ? ~ | . \ : !`. Pattern escapes such as `\d` do not exist; use `[num]`. A backslash at the very end of a pattern is also this error.

```cuff error E3-008
print("x" is "\d")
```

**E3-009.** A `~` that is not part of a repeat range. To match a tilde, write `\~`.

```cuff error E3-009
print("x" is "a~b")
```

**E3-010.** A closing `)`, `]` or `>` with nothing to close. Escape it (`\)`) to match the character itself.

```cuff error E3-010
print("x" is "a)")
```

**E3-101.** A pattern ran past the budget of matching steps. Patterns that nest a repeat inside a repeat, like `([any]*)*`, can take an enormous number of steps on a text that almost matches. Simplify the pattern. This one can be caught with `or_else`; `E3-102` (time) and `E3-103` (recursion depth) are the same kind of error and are handled the same way.

```cuff error E3-101
set str s to "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"
print(s is "([any]*)*x")
```

### Running the program

**E4-001.** A name that was never declared with `set`, was declared later in the program, or lives in another scope. Check the spelling. Inside a function, a variable of the main program is only visible after `change name to global`.

```cuff error E4-001
print(missing)
```

**E4-002.** A function that does not exist. Check the spelling, and remember that library functions need their `use DLC:...` line first; `length` needs one of the string, list or map libraries.

```cuff error E4-002
print(missing_fn(1))
```

**E4-003.** A constant, or a constant list, was changed. Use `set` for a value that has to change. To change a constant list, copy it first into an ordinary list.

```cuff error E4-003
set constant number MAX to 1
change MAX to 2
```

**E4-004.** A constant's name must be all capital letters, digits and underscores, so that a constant stands out from a variable: `MAX_USERS`.

```cuff error E4-004
set constant number max to 1
```

**E4-005.** An operation was given the wrong kind of value, such as `"a" + 1`, a comparison between a number and a string, or a library function given a list where it wants a string. Convert explicitly with `to_str` or `to_number`.

```cuff error E4-005
print("a" + 1)
```

**E4-006.** A division or remainder by zero, or zero raised to a negative power. Test the divisor before dividing.

```cuff error E4-006
print(1 / 0)
```

**E4-007.** Positions start at 1. Position `0` does not exist; the first element is `1` and the last is `-1`.

```cuff error E4-007
print("abc"[0])
```

**E4-008.** A position past the end of a list or string, or a slice that runs past it. Positions run from `1` to the length, and from `-1` (the last) backwards.

```cuff error E4-008
print("abc"[5])
```

**E4-009.** A map has no such key. Ask `map_has_key(m, key)` first, or recover with `or_else`.

```cuff error E4-009
set map m to {"a": 1}
print(m["b"])
```

**E4-010.** A function was called with too many or too few arguments. Compare the call with the function's parameters, or with its entry in the [function index](functions.md).

```cuff error E4-010
print(type_of(1, 2))
```

**E4-015.** `remove` was given a value that is not in the list. Check with `contains` first.

```cuff error E4-015
set list a to [1]
remove "x" from a
```

**E4-016.** A `return` with a value in a function that is not `returnable`, or at the top level of a program. Declare the function with `set returnable func`.

```cuff error E4-016
set func f() do:
    return 1
end
f()
```

**E4-017.** Recursion went too deep: the call depth limit or the stack budget was reached. Make sure the recursion has a base case that is always reached (see [limits](limits.md)).

```cuff error E4-017
set func f() do:
    f()
end
f()
```

**E4-018.** A function was declared inside another function. Declare every function at the top level; functions are visible everywhere.

```cuff error E4-018
set func outer() do:
    set func inner() do:
    end
end
outer()
```

**E4-019.** `await` was applied to a call of a function that is not `async`. Declare the function `set async func`, or drop the `await`.

```cuff error E4-019
set func f() do:
end
await f()
```

**E4-020.** A `set` declared one type and the value has another: `set number n to "x"`. Declare the type the value really has. (`find` gives a `str`; `match` gives a `match`.)

```cuff error E4-020
set number n to "x"
```

**E4-021.** `change name to global` is only meaningful inside a function, where it makes a global variable visible. At the top level every variable is already in reach.

```cuff error E4-021
change x to global
```

**E4-022.** A bare `return` outside any function. (With a value, it is `E4-016`.)

```cuff error E4-022
return
```

**E4-023.** `stop` ends the nearest loop and is an error anywhere else, including inside a function that is called from a loop.

```cuff error E4-023
stop
```

**E4-024.** A position or a range bound that is not a whole number. Round it first with `math_floor` or `math_round`.

```cuff error E4-024
print("abc"[1.5])
```

**E4-025.** The argument has the right type, but a value the function cannot use: text that is not a number, a negative square root, an empty list where the function needs one element, and so on. The message says which. Check the value first, or recover with `or_else`.

```cuff error E4-025
print(to_number("abc"))
```

**E4-026.** A string, list, map or the queue of `async` calls grew past its limit (see [limits](limits.md)).

```cuff error E4-026
use DLC:string
print(str_repeat("abcdefgh", 1000000000))
```

**E4-027.** A `pure` function used a global variable. A pure function can use only its parameters and its own variables.

```cuff error E4-027
set number g to 1
set pure returnable func f() do:
    change g to global
    return g
end
print(f())
```

**E4-028.** A network request could not be completed: the address is blocked or uses a scheme the library does not support, the server could not be reached, it took too long, or the answer was too large. The message says which. The example uses a scheme that is not supported, so it fails before any connection is made. The documentation checks run with the network switched off, so they do not run this example.

```cuff
use DLC:network
print(network_get("ftp://example.com"))
```

**E4-029.** A `pure` function called a function of the program that is not `pure`. Mark the called function `pure` too, or do not use `pure` here. Library functions may be called freely.

```cuff error E4-029
set func helper() do:
end
set pure func f() do:
    helper()
end
f()
```

**E4-030.** An argument does not match the type declared for the parameter (`number a`). Convert it before the call.

```cuff error E4-030
set func f(number a) do:
end
f("x")
```

### Modules and libraries

**E5-001.** No such module file. `use nothing from ./lib` loads `./lib/nothing.cuff`, resolved against the folder of the script; check the folder and the file name.

```cuff error E5-001
use nothing from ./lib
```

**E5-002.** A module file was found but does not parse. The report shows the `use` line and then the module's own error, with its file and line.

```cuff error E5-002
use DLC:filesystem
file_write("broken.cuff", "set number z to 1 +\n")
use broken from .
```

**E5-004.** There is no library by that name. The message lists the libraries that exist.

```cuff error E5-004
use DLC:nothing
```

**E5-006.** A module path that is absolute, or that leaves the allowed folder. See [the sandbox](../guide/modules.md#the-sandbox).

```cuff error E5-006
use x from /etc
```

**E5-008.** A file path given to a `DLC:filesystem` function leaves the allowed folder or is absolute. Use a relative path below the script's folder.

```cuff error E5-008
use DLC:filesystem
print(file_read("../x"))
```

### Codes without an example

- **`E1-003`** is raised for a number literal that is too large to represent, one written with hundreds of digits.
- **`E2-008`** is raised for code nested deeper than the nesting limit, or a single expression longer than the expression limit, and **`E2-009`** for a program or shell entry larger than the source size limit. **`E3-011`** is the same kind of
  error for a pattern: one that is too large or nested too deeply. The values are in [limits](limits.md).
- **`E5-005`** is raised by a call into `DLC:network` or `DLC:filesystem` when the host switched the library off (`--no-network`, `--no-filesystem`). The `use` line itself succeeds.
- **`E5-007`** is raised when modules load other modules too deeply, or when a module file is larger than the source size limit.
- **`E6-001`** and **`E6-002`** are raised when a run reaches `--max-steps` or `--timeout`; **`E6-003`** when the host runs out of memory. They end the program and cannot be caught: the budget is the host's decision, not the program's.
- **`E9-001`** is a bug in the engine. If you meet it, please [report it](../../CONTRIBUTING.md#reporting-bugs) with the smallest program that shows it.
- The codes the catalogue marks _Reserved_ are numbers that nothing raises.
