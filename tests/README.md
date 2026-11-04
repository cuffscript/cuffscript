# `tests/`

The engine and language test suite. It is run by `tests/run.sh` (Linux, macOS) and `tests/run.ps1` (Windows, and PowerShell 7 anywhere), which print the same sections and the same summary.

| Directory | Contents | A test is |
| :--- | :--- | :--- |
| `unit/` | C++ unit tests | A `.cpp` file with a `main()`. It is compiled and run; a non-zero exit fails it, and the last line of its output is shown. |
| `cases/<area>/` | Programs with their exact output | `name.cuff` and `name.expected`; the output must match exactly. |
| `errors/<area>/` | Programs that must fail | `name.cuff` and, optionally, `name.expected_code` with the error code it must fail with. A file `name.args` holds command-line options for the run (for example `--no-network`). |

A folder called `fixtures/` holds files that a test loads (modules, for instance) and is never run on its own.

The runners also run the programs in `examples/`, with their `.expected` output, and the programs in `examples/error_cases/`, with the codes in their `.expected_code` files.

## Areas

Both `cases/` and `errors/` are divided by the part of the language a test is about, so a test is found by what it checks, not by when it was written. The table says which part of the [grammar](../docs/reference/grammar.md) each area answers for; `python3 tests/coverage.py` reads it and fails if a production of the grammar belongs to no area.

<!-- coverage-map:begin -->
| Area | Grammar productions | What its tests cover |
| :--- | :--- | :--- |
| `syntax` | program, statement, simple_statement, block_statement, body, comment | Lines and layout: comments, literals, f-strings, one-line bodies, trailing commas, the spellings that are refused and the hint each one gets. |
| `values` | declaration, type, primary | The types, declarations and constants, truthiness, equality, number formatting, indexing of text, what a copy shares. |
| `operators` | expression, or_expr, and_expr, not_expr, comparison, additive, multiplicative, unary, power | Every operator, precedence, short-circuit evaluation and the errors an operator raises. |
| `collections` | place, change, add, remove, postfix, list_literal, map_literal, map_entry | Lists and maps: indexing, slices, places to any depth, `add`, `remove`, `change`, the order of map keys. |
| `control` | if, loop | Decisions, the three loops, `stop` and `skip`, the round a loop works from. |
| `functions` | function, parameter, return, global, await_statement, call_args | Functions, defaults, typed parameters, `global`, `pure`, `async` and `await`, and what a call may do. |
| `errors` | or_else, fail | Errors as objects, `or_else` on one statement and on a block, `fail`, and which errors can be caught. |
| `classes` | class, member, field, with, super_call | Classes, `extends`, `private`, `self` and `super`, `isa`, `with`, built-in methods, JSON. |
| `modules` | use, path | `use ... from`, `use DLC:`, loading once, paths and the root directory. |
| `text` | pattern, element, atom, token, set, one_of, group, named_group, quantifier | Patterns and the seven text functions, Unicode text. |
| `libraries` | — | The built-in functions and every `DLC:` library, in both the function and the method form. |
| `phenomena` | — | One program for each behaviour described in [Phenomena](../docs/guide/phenomena.md). The guide shows these programs word for word; `coverage.py` checks that they still match. |
| `limits` | — | What the engine refuses to do past a limit, and the error it gives. |
<!-- coverage-map:end -->

## What is covered

```sh
python3 tests/coverage.py          # what no test touches
python3 tests/coverage.py --list   # and, for the rest, the first tests that do
```

The checker asks the engine what it contains and reports an error code that no test expects, a library function that no program calls, a built-in method that is never called as `.name(`, and a grammar production that belongs to no area. A gap that cannot be closed is written in `coverage_exceptions.txt` with its reason. A change that adds an error code, a function or a production is not finished until this passes.

```sh
make                                   # build cuffc first
bash tests/run.sh                      # Linux, macOS
powershell -File tests\run.ps1         # Windows
```

A passing run ends with a summary line of the form `N passed, 0 failed`. Set `CUFFC` to test another binary (a sanitizer build, for example) and `UNIT_FLAGS` to change how the unit tests are compiled.

How the suites work, how to add a test, what each unit test protects, sanitizers and what CI runs are in [Testing](../docs/contributing/testing.md). The interactive shell has its own tests in `cli/tests/`.
