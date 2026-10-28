# Errors and debugging

When a program cannot continue, CuffScript stops it and prints a report. This page explains how to read one, how to
recover from the errors a program can recover from, and what to reach for when you are hunting a bug.

## Reading an error report

```text
ERROR: [E4-006] Runtime Error at line 3, column 9: division by zero
    print(a / b)
            ^
```

| Part | Meaning |
| :--- | :--- |
| `ERROR:` | Written by the program that ran the script (`cuffc` or `cuffsh`). |
| `[E4-006]` | The error code. The first digit is the kind of error; every code is described in the [catalogue](../reference/errors.md). |
| `Runtime Error` | The kind of error. |
| `at line 3, column 9` | Where it happened. Both start at 1, and the column counts characters, so it agrees with the caret even on a line with Korean text. |
| `division by zero` | What went wrong. |
| the source line and `^` | The line itself, with a caret under the position. Tabs are kept and wide characters take two columns, so the caret lines up in a terminal. |
| `hint:` | When there is one, a suggestion. It is the last line. |

An error is written to standard error, and `cuffc` exits with status 1.

### Where an incomplete statement is reported

A statement that stops too early is reported on **its own line**, at the point where it ends, not on the line after:

```text
ERROR: [E2-001] Syntax Error at line 1, column 20: unexpected end of line in expression
    set number total to
                       ^
```

A block that is never closed is reported at the end of the code, and the hint names the line that opened it, which is
usually where the mistake is:

```text
ERROR: [E2-001] Syntax Error at line 3, column 13: expected 'end' to close if statement (got end of file)
        print(x)
                ^
    hint: the block opened at line 2, column 1 has no matching 'end'
```

### Errors inside an imported file

An error in a function or statement that came from another file is drawn from **that file**, and the report names it:

```text
ERROR: [E4-006] Runtime Error in './lib/ratio.cuff' at line 3, column 17: division by zero
        return base / n
                    ^
```

If a module cannot be parsed, the report shows the `use` statement that loaded it and then the module's own line:

```text
ERROR: [E5-002] Module Error at line 1, column 1: failed to parse module './lib/bad.cuff': unexpected end of line in expression
    use bad from ./lib
    ^
    in './lib/bad.cuff' [E2-001] at line 1, column 20:
        set number z to 1 +
                           ^
```

The report names the file and line where the error happened, but not who called it; it is not a stack trace.

## Recovering with `or_else`

Errors raised while the program runs can be caught: the run-time errors (`E4-`), the module and sandbox errors (`E5-`) and the errors a pattern raises
while matching (`E3-1`). Put an `or_else` block after the statement that may fail:

```cuff
set number port to to_number("80a") or_else do:
    print("port is not a number, using 80")
    change port to 80
end
print(port)
```

```output
port is not a number, using 80
80
```

The handler runs in its own scope, what the statement did before failing stays done, and an error inside the handler
is not caught by the same `or_else`. Syntax errors (`E1-`, `E2-`), errors in the syntax of a pattern (`E3-0`) and resource-limit errors (`E6-`) cannot be caught: they
mean the program or its budget has to change. The last column of the [catalogue](../reference/errors.md) says which codes can be caught. The full rules are in
the [language reference](../reference/language.md#8-errors-and-or_else).

An error can also be *prevented*: check `length` before indexing, `map_has_key` before reading a key, and
`file_exist` before reading a file.

## The errors you will meet first

| Code | What it usually means | What to do |
| :--- | :--- | :--- |
| [`E2-001`](../reference/errors.md#e2-001) | The program does not parse. | Read the message and the caret. Check for a missing `end`, a missing `do:`, `=` instead of `to`, or a string with no closing quote. |
| [`E4-001`](../reference/errors.md#e4-001) | A variable that does not exist (yet). | Check the spelling, and that it was declared with `set` before this line, in a scope that can see it. |
| [`E4-002`](../reference/errors.md#e4-002) | A function that does not exist. | A library function needs its `use DLC:...` line first. |
| [`E4-005`](../reference/errors.md#e4-005) | A value of the wrong type, such as `"a" + 1`. | Convert explicitly with `to_str` or `to_number`. |
| [`E4-003`](../reference/errors.md#e4-003) | A change to a constant, or to a read-only list. | Use `set` for a value that changes; make a copy of a constant list with `+ []` before changing it. |
| [`E4-004`](../reference/errors.md#e4-004) | A constant whose name is not in capitals. | Write the name in capitals, digits and underscores: `MAX_USERS`. |
| [`E4-006`](../reference/errors.md#e4-006) | Division or remainder by zero, or zero to a negative power. | Test the divisor first. |
| [`E4-007`](../reference/errors.md#e4-007) | Position `0`. | Positions start at 1; the last element is `-1`. |
| [`E4-008`](../reference/errors.md#e4-008) | A position outside a list or string. | Positions run from 1 to the length, or from -1 (the last) backwards. |
| [`E4-009`](../reference/errors.md#e4-009) | A map key that is not there. | Test with `map_has_key` first, or recover with `or_else`. |
| [`E4-010`](../reference/errors.md#e4-010) | The wrong number of arguments. | Compare the call with the function's parameters. |
| [`E4-016`](../reference/errors.md#e4-016) | `return` with a value in a function that is not `returnable`, or at the top level. | Declare it `set returnable func`. |
| [`E4-017`](../reference/errors.md#e4-017) | Recursion that went too deep. | Make sure the recursion has a base case. |
| [`E4-020`](../reference/errors.md#e4-020) | The value does not have the type `set` declared. | Declare the type the value really has: `find` gives a `str`, `match` gives a `match`. |
| [`E4-024`](../reference/errors.md#e4-024) | A position or count that is not a whole number, such as `list[1.5]`. | Round it first with `math_floor` or `math_round`. |
| [`E4-025`](../reference/errors.md#e4-025) | A value of the right type that a function does not accept, such as `to_number("abc")` or `math_sqrt(-1)`. | Check the value first, or recover with `or_else`. |
| [`E4-028`](../reference/errors.md#e4-028) | A network request that could not be completed. | The message says why (blocked address, no TLS, timeout). Catch it with `or_else`. |
| [`E5-001`](../reference/errors.md#e5-001) | A module file that cannot be found. | `use name from ./dir` loads `./dir/name.cuff`, relative to the script. |
| [`E5-005`](../reference/errors.md#e5-005) | A call into `DLC:network` or `DLC:filesystem` that the host switched off. | Run without `--no-network` or `--no-filesystem`, or do not call the library. |
| [`E3-001`](../reference/errors.md#e3-001) | A pattern with an unclosed `(`, or with a `<` that is not a named capture. | Close it, or escape the character as `\<` or `\(`. |
| [`E6-001`](../reference/errors.md#e6-001), [`E6-002`](../reference/errors.md#e6-002) | The run used up `--max-steps` or `--timeout`. | Look for a loop that never ends, or raise the budget. |

## Finding a bug

- **Print what you have.** `print(type_of(x), x)` settles most questions about a value.
- **See how the program was read.** `cuffc --ast script.cuff` prints the tokens and the syntax tree instead of running
  the script.
- **Bound a runaway program.** `--max-steps 100000` or `--timeout 2000` turns an endless loop into an error, so a
  broken program ends with a report instead of hanging.
- **Try a piece in the shell.** [`cuffsh`](../reference/cuffsh.md) keeps what you declared, so you can call a
  function with different arguments until it misbehaves. `:load file.cuff` runs a file in the session.
- **Check the type.** Many surprises are a value that is not what you think: `print(type_of(x))`. A `number` and a `str` are never equal, and `"5" + 1` is an error.
- **Reduce it.** Delete lines until the error stops, then put back the last one. A short program that shows the
  error is also what a bug report needs.

Reporting a bug in CuffScript itself is described in [Contributing](../../CONTRIBUTING.md).
