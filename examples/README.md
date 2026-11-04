# Example programs

The numbered programs teach the language in order, one subject at a time; the last four are small programs of the kind people write. Each has an `.expected` file with its output, and the test suite runs them and compares.

| File | What it shows |
| :--- | :--- |
| `01_hello.cuff` | The simplest `print` |
| `02_values_and_text.cuff` | Numbers, text, booleans, lists, maps and constants; f-strings; positions that start at 1, negative positions and slices; text counted in characters; arithmetic; `empty` |
| `03_decisions_and_loops.cuff` | `if`, truthiness, `and`, `or`, `!`; `loop repeat` (also backwards), `loop while`, `loop each` over a list, a map and a text; `skip` and `stop`; a loop does not open a scope |
| `04_functions.cuff` | Parameters with types and defaults, calling a function above its declaration, recursion, `global`, a variable that is not visible, `pure` |
| `05_lists_and_maps.cuff` | `add`, `change` and `remove` on lists, maps and nested values; slices; shared lists and `copy`; recovering from a missing position |
| `06_text_and_patterns.cuff` | Pattern literals (`p"..."`), `matches`, `match`, `find`, `find_all`, `replace`, `replace_all`, `split` and `count`; captures, `[one:...]`, the `i` flag, escapes, patterns on Hangul |
| `07_or_else.cuff` | `or_else` around one statement, what is and is not an error, and `or_else as` |
| `08_errors_as_objects.cuff` | `fail`, the `Error` object a handler receives, and a class that extends `Error` |
| `09_async.cuff` | `async` calls that are queued and run after the main program, and `await` |
| `10_classes.cuff` | Fields, a constructor (`init`), methods, `private`, `to_str`, and objects that are shared rather than copied |
| `11_inheritance.cuff` | `extends`, replacing a method, `super`, `init` in a child class, `isa` |
| `12_with_block.cuff` | `with`: members by their plain names, `.name`, the lookup order, nested blocks |
| `13_modules.cuff`, `lib/greetings.cuff` | Loading another `.cuff` file with `use <name> from <path>` |
| `14_libraries.cuff` | `DLC:math`, `string`, `random`, `list`, `map`, `convert` and `json`; library functions are named `library_verb` |
| `15_files.cuff` | `DLC:filesystem`, only inside the script's folder; the temporary file removes itself |
| `16_word_frequency.cuff` | A text split into words with a pattern, counted in a map, and the most common words picked out |
| `17_inventory.cuff` | Two classes that work together, a method that returns a found object, and the result written as JSON |
| `18_bank_account.cuff` | Errors raised by methods, an error class with its own field, `private` methods, and `or_else as` with `isa` |
| `19_log_summary.cuff` | Log lines read with a named-capture pattern, counted by level and user in maps; lines that do not fit are skipped |
| [`embedding/`](embedding/README.md) | A complete C++ program that hosts the engine |

Run one from the repository root:

```sh
make
./cuffc examples/01_hello.cuff
./cuffc --ast examples/01_hello.cuff    # print the tokens and the syntax tree instead of running it
```

## `error_cases/`

Programs that are built to fail, one for each kind of error, so that you can see the report each one produces. They are not wrapped in `or_else`, so each ends with a non-zero exit status, and the test suite checks that each fails with the code in its `.expected_code` file.

| File | Error | What goes wrong |
| :--- | :--- | :--- |
| `zero_index.cuff` | `E4-007` | Position `0` |
| `index_out_of_range.cuff` | `E4-008` | A position past the end |
| `division_by_zero.cuff` | `E4-006` | Dividing by zero |
| `constant_reassignment.cuff` | `E4-003` | Changing a constant |
| `bad_constant_name.cuff` | `E4-004` | A constant whose name is not in capitals |
| `declaration_type_mismatch.cuff` | `E4-020` | A value that is not the declared type |
| `return_value_outside_function.cuff` | `E4-016` | `return` with a value outside any function |
| `nested_function.cuff` | `E4-018` | A function declared inside a function |
| `await_on_nonasync.cuff` | `E4-019` | `await` on a function that is not `async` |
| `stack_overflow.cuff` | `E4-017` | Recursion that never ends |
| `regex_syntax_error.cuff` | `E3-002` | A pattern with an unclosed `[` |
| `regex_redos_guard.cuff` | `E3-101` | A pattern that backtracks without end, stopped by the matching budget |
| `unknown_dlc.cuff` | `E5-004` | A library that does not exist |

```sh
for f in examples/error_cases/*.cuff; do
    echo "--- $f ---"
    ./cuffc "$f"
done
```

The [error catalogue](../docs/reference/errors.md) explains every code, with a smaller example and the usual fix.
