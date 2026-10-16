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
`E2` codes are reserved numbers; the catalogue marks every code that nothing raises.

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
| <a id="e2-009"></a>`E2-009` | A program, module file or shell entry larger than the source size limit. | no |

### Regex Syntax Errors

| Code | Meaning | `or_else` can catch it |
| :--- | :--- | :---: |
| <a id="e3-001"></a>`E3-001` | A `(` or named capture that is never closed. | no |
| <a id="e3-002"></a>`E3-002` | A `[` that is never closed. | no |
| <a id="e3-003"></a>`E3-003` | Wrong spacing around the colon of a token such as `[one:a|b]`. | no |
| <a id="e3-004"></a>`E3-004` | A repeat range that cannot match anything, such as `5~2`. | no |
| <a id="e3-005"></a>`E3-005` | Two quantifiers applied to the same item, such as `[num]++`. | no |
| <a id="e3-006"></a>`E3-006` | An empty token or an empty alternative. | no |
| <a id="e3-007"></a>`E3-007` | A `[name]` that is not a known token. | no |
| <a id="e3-008"></a>`E3-008` | A backslash that escapes nothing, or something that cannot be escaped. | no |
| <a id="e3-009"></a>`E3-009` | A quantifier with nothing before it. | no |
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
| <a id="e4-006"></a>`E4-006` | Division by zero. | yes |
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
| <a id="e4-025"></a>`E4-025` | An argument of the right type but a value the function does not accept. | yes |
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
| <a id="e5-005"></a>`E5-005` | A library switched off for this run (`--no-network`, `--no-filesystem`). | yes |
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
