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
| Program | `E7-xxx` | An error the program raises with `fail` | yes |
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

### Syntax Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e2-001"></a>`E2-001` | The token found cannot appear here. Every syntax problem found while tokenizing or parsing is reported with this code. | no |
| <a id="e2-002"></a>`E2-002` | A specific token was required. _Reserved: never raised._ | no |
| <a id="e2-003"></a>`E2-003` | A malformed function declaration. _Reserved: never raised._ | no |
| <a id="e2-004"></a>`E2-004` | A malformed `if` or loop. _Reserved: never raised._ | no |
| <a id="e2-005"></a>`E2-005` | A malformed `use` statement. _Reserved: never raised._ | no |
| <a id="e2-006"></a>`E2-006` | A block opened without a matching `end`. _Reserved: never raised._ | no |
| <a id="e2-007"></a>`E2-007` | Something that cannot be assigned to. | no |
| <a id="e2-008"></a>`E2-008` | Code nested deeper than the nesting limit, or a single expression longer than the expression limit. | no |
| <a id="e2-009"></a>`E2-009` | A program or shell entry larger than the source size limit. | no |
| <a id="e2-010"></a>`E2-010` | A name declared again where the same function or program already declares it. | no |

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
| <a id="e4-016"></a>`E4-016` | An operation not supported here, such as `return value` outside a function. | yes |
| <a id="e4-017"></a>`E4-017` | More calls active at once than the call depth limit allows, or nesting inside the engine deeper than the native stack budget allows. | yes |
| <a id="e4-018"></a>`E4-018` | A function declared inside another function. | yes |
| <a id="e4-019"></a>`E4-019` | `await` applied to a function that is not `async`. | yes |
| <a id="e4-020"></a>`E4-020` | A declared type that does not match the value. | yes |
| <a id="e4-021"></a>`E4-021` | `global` outside a function body. | yes |
| <a id="e4-022"></a>`E4-022` | `return` outside any function. | yes |
| <a id="e4-023"></a>`E4-023` | `stop` or `skip` outside any loop. | yes |
| <a id="e4-024"></a>`E4-024` | A position or range bound that is not a whole number. | yes |
| <a id="e4-025"></a>`E4-025` | An argument of the right type but a value the function or operator does not accept. | yes |
| <a id="e4-026"></a>`E4-026` | A string, list, map, `async` queue or class declaration larger than its limit. | yes |
| <a id="e4-027"></a>`E4-027` | A `pure` function that reaches a global variable. | yes |
| <a id="e4-028"></a>`E4-028` | A network request that failed, timed out, or targeted a blocked address. | yes |
| <a id="e4-029"></a>`E4-029` | A `pure` function that calls a function that is not `pure`. | yes |
| <a id="e4-030"></a>`E4-030` | An argument that does not match the parameter's declared type. | yes |
| <a id="e4-031"></a>`E4-031` | A class name that is not declared, used as a type, in `extends`, in `isa` or as a constructor. | yes |
| <a id="e4-032"></a>`E4-032` | A class that inherits from itself, through its ancestors, or from something that is not a class. | yes |
| <a id="e4-033"></a>`E4-033` | A field or method that the object's class does not have. | yes |
| <a id="e4-034"></a>`E4-034` | A `private` field or method used outside the class that declares it. | yes |
| <a id="e4-035"></a>`E4-035` | A `.name` access or a `with` block applied to a value that is not an object. | yes |
| <a id="e4-036"></a>`E4-036` | A class declared inside a block or a function; classes belong at the top level of a file. | yes |
| <a id="e4-037"></a>`E4-037` | Two fields or methods of one class with the same name. | yes |
| <a id="e4-038"></a>`E4-038` | A class named like a function, another class, or a built-in. | yes |
| <a id="e4-039"></a>`E4-039` | A class whose name does not start with an uppercase letter. | yes |
| <a id="e4-040"></a>`E4-040` | A method used as a value, or a field called as a method. | yes |
| <a id="e4-041"></a>`E4-041` | A `pure` function or method that changes a field. | yes |

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

### Program Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e7-001"></a>`E7-001` | An error raised by the program itself with `fail`. | yes |

### Internal Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e9-001"></a>`E9-001` | A bug in the engine: please report it. | no |
<!-- END GENERATED: error-table -->

## What each error looks like

Every example below, except the one for `E4-028`, is run by the documentation checks and produces exactly the code it is listed under. The text says what usually causes the error and how to fix it. Codes that need a command-line option to
reproduce, and codes that nothing in an ordinary program raises, are described at the end.

### Syntax

**E2-001.** The program is not valid CuffScript. The message says what was expected and the caret shows where the parser gave up. The usual causes are a declaration or an assignment that is not written `TYPE NAME = value` or `NAME = value` (the old `set`, `change` and `to` forms get a hint that says what to write), a block with no `end`, a body that is not indented, a missing `do:`, a space before a colon, a string with no closing quote and a stray character. Put the first error right and run again: later errors are often caused by the first.

```cuff error E2-001
number x
```

**E2-010.** A name is declared a second time where the same function, or the top level of the program, already declares it: in the same block, in a block inside it, or as a parameter or a loop variable with the name of a variable declared before. Give the second one another name, or assign to the first (`total = 2`). Two blocks side by side may declare the same name, and so may two functions.

```cuff error E2-010
number total = 1
number total = 2
```

### Patterns

**E3-001.** A pattern has a `(` that is never closed, or a `<` that does not start a named capture such as `<year:[num]4>`. Close the group, or escape the character you meant literally: `\(` or `\<`.

```cuff error E3-001
print("x" matches p"(a")
```

**E3-002.** A pattern has a `[` that is never closed. Close it, or write `\[` to match a bracket.

```cuff error E3-002
print("x" matches p"[num")
```

**E3-003.** There is a space between a token's word and its colon. Write `[one:a|b]`, with the colon right after the word.

```cuff error E3-003
print("x" matches p"[one :a|b]")
```

**E3-004.** A repeat range must run from the smaller number to the larger. `5~2` cannot match anything. The same code is raised for a character range written backwards, such as `[z-a]`, and for a repeat count larger than the pattern repeat limit (see [limits](limits.md)).

```cuff error E3-004
print("x" matches p"[num]5~2")
```

**E3-005.** A pattern item can have one quantifier. `[num]++` has two. Keep one of them.

```cuff error E3-005
print("x" matches p"[num]++")
```

**E3-006.** A token with nothing in it, or an alternative with nothing between its bars. `[one:]` and `[one:a||b]` are both errors.

```cuff error E3-006
print("x" matches p"[one:]")
```

**E3-008.** A backslash must be followed by one of the characters it can escape: `[ ] ( ) { } < > + * ? ~ | . \ : !`. Pattern escapes such as `\d` do not exist; use `[num]`. A backslash at the very end of a pattern is also this error.

```cuff error E3-008
print("x" matches p"\d")
```

**E3-009.** A `~` that is not part of a repeat range. To match a tilde, write `\~`.

```cuff error E3-009
print("x" matches p"a~b")
```

**E3-010.** A closing `)`, `]` or `>` with nothing to close. Escape it (`\)`) to match the character itself.

```cuff error E3-010
print("x" matches p"a)")
```

**E3-101.** A pattern ran past the budget of matching steps. Patterns that nest a repeat inside a repeat, like `([any]*)*`, can take an enormous number of steps on a text that almost matches. Simplify the pattern. This one can be caught with `or_else`; `E3-102` (time) and `E3-103` (recursion depth) are the same kind of error and are handled the same way.

```cuff error E3-101
str s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"
print(s matches p"([any]*)*x")
```

### Running the program

**E4-001.** A name that was never declared, was declared later in the program, or lives in another scope. Check the spelling. Inside a function, a variable of the main program is only visible after `global name`.

```cuff error E4-001
print(missing)
```

**E4-002.** A function that does not exist. Check the spelling, and remember that library functions need their `use DLC:...` line first; `length` needs one of the string, list or map libraries.

```cuff error E4-002
print(missing_fn(1))
```

**E4-003.** A constant, or a constant list, was changed. Declare the value without `constant` if it has to change. To change a constant list, copy it first into an ordinary list.

```cuff error E4-003
constant number MAX = 1
MAX = 2
```

**E4-004.** A constant's name must be all capital letters, digits and underscores, so that a constant stands out from a variable: `MAX_USERS`.

```cuff error E4-004
constant number max = 1
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
map m = {"a": 1}
print(m["b"])
```

**E4-010.** A function was called with too many or too few arguments. Compare the call with the function's parameters, or with its entry in the [function index](functions.md).

```cuff error E4-010
print(type_of(1, 2))
```

**E4-015.** `remove` was given a value that is not in the list. Check with `contains` first, or remove by position with `remove at`.

```cuff error E4-015
list a = [1]
remove "x" from a
```

**E4-016.** A `return` with a value at the top level of a program, outside any function. Put the code in a function, or show the value with `print`.

```cuff error E4-016
return 1
```

**E4-017.** Recursion went too deep: more calls were active at once than the call depth limit allows. Make sure the recursion has a base case that is always reached. The same error ends nesting inside the engine that reaches the stack budget, such as a field whose default value makes another object of the same class (see [limits](limits.md)).

```cuff error E4-017
func f() do:
    f()
end
f()
```

**E4-018.** A function was declared inside another function. Declare every function at the top level; functions are visible everywhere.

```cuff error E4-018
func outer() do:
    func inner() do:
    end
end
outer()
```

**E4-019.** `await` was applied to a call of a function that is not `async`. Declare the function `async func`, or drop the `await`.

```cuff error E4-019
func f() do:
end
await f()
```

**E4-020.** A value does not have the type the variable, field, loop variable or `or_else` name was declared with: `number n = "x"`, or `n = "x"` after `number n = 1`. Store a value of the declared type, or declare the type the value really has; a variable that holds different types is declared `any`, and so is one that may hold the `empty` of a failed `find` or `match`, because `empty` is not a `str` or a `match`.

```cuff error E4-020
number n = "x"
```

**E4-021.** `global name` is only meaningful inside a function, where it makes a global variable visible. At the top level every variable is already in reach.

```cuff error E4-021
global x
```

**E4-022.** A bare `return` outside any function. (With a value, it is `E4-016`.)

```cuff error E4-022
return
```

**E4-023.** `stop` ends the nearest loop and `skip` goes on with its next pass. Both are errors anywhere else, including inside a function that is called from a loop.

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
number g = 1
pure func f() do:
    global g
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
func helper() do:
end
pure func f() do:
    helper()
end
f()
```

**E4-030.** An argument does not match the type declared for the parameter (`number a`). Convert it before the call, or declare the parameter `any`.

```cuff error E4-030
func f(number a) do:
end
f("x")
```

### Classes and objects

**E4-031.** A class name that is not declared: as a type, after `extends`, after `isa`, or in a `with` or a declaration. Check the spelling, and remember that a class belongs at the top level of a file or of a module that was loaded with `use`.

```cuff error E4-031
print(1 isa Ghost)
```

**E4-032.** A class that extends itself, directly or through its ancestors, or extends something that is not a class.

```cuff error E4-032
class A extends A do:
end
```

**E4-033.** The class has no such field or method. The report lists the fields the class has; the usual cause is a misspelt name.

```cuff error E4-033
class Dog do:
    str name = "Rex"
end
Dog d = Dog()
print(d.nmae)
```

**E4-034.** A `private` field or method was used outside the class that declares it. Child classes are outside too: only the methods of the declaring class can use it.

```cuff error E4-034
class Safe do:
    private number code = 1234
end
print(Safe().code)
```

**E4-035.** A `.name` was used on a value that is not an object, or `with` was given one. Only objects have fields and methods.

```cuff error E4-035
number n = 5
print(n.size)
```

**E4-036.** A class was declared inside a block or a function. Move it to the top level of the file.

```cuff error E4-036
if true do:
    class Inner do:
    end
end
```

**E4-037.** A class declares two members with the same name, or a child declares a field its parent already has, or a method has the name of a field. Rename one of them.

```cuff error E4-037
class Box do:
    number size = 1
    number size = 2
end
```

**E4-038.** A class has the name of a function, of another class or of the built-in `Error`. Classes and functions share one namespace.

```cuff error E4-038
func Report() do:
    print("report")
end
class Report do:
end
```

**E4-039.** A class name must start with a capital letter, so that a class stands out from a variable.

```cuff error E4-039
class point do:
end
```

**E4-040.** A method was used without the parentheses that call it, or a field was called like a method. Write `object.method(...)` for a method and `object.field` for a field.

```cuff error E4-040
class Dog do:
    func bark() do:
        print("woof")
    end
end
print(Dog().bark)
```

**E4-041.** A `pure` function or method changed a field. A pure method may read fields but not write them; remove `pure`, or return the new value instead.

```cuff error E4-041
class Counter do:
    number n = 0

    pure func bump() do:
        self.n = self.n + 1
    end
end
Counter().bump()
```

### Modules and libraries

**E5-001.** No such module file. `use nothing from ./lib` loads `./lib/nothing.cuff`, resolved against the folder of the script; check the folder and the file name.

```cuff error E5-001
use nothing from ./lib
```

**E5-002.** A module file was found but does not parse. The report shows the `use` line and then the module's own error, with its file and line.

```cuff error E5-002
use DLC:filesystem
file_write("broken.cuff", "number z = 1 +\n")
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

### Errors raised by the program

**E7-001.** The program raised an error with `fail`, and nothing caught it. The message is the program's own. An error object of a class that extends `Error` shows the class name before the message. Catch it with `or_else`, or make sure the condition that leads to `fail` cannot happen.

```cuff error E7-001
func withdraw(number balance, number amount) do:
    if amount > balance do:
        fail "not enough money"
    end
    return balance - amount
end
print(withdraw(10, 50))
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
