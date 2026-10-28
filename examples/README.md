# Example programs

Read the numbered programs in order to pick up the language one feature at a time. Each one has an `.expected` file with its output, and the test suite runs them and compares (`08_dlc_libraries.cuff` uses random numbers, so it only has to run).

| File | What it shows |
| :--- | :--- |
| `01_hello.cuff` | The simplest `print` |
| `02_comprehensive_demo.cuff` | A program that uses most of the language: constants, `returnable` functions, `is` combined with `!`, `async` with `await` and `or_else`, 1-based lists, `loop repeat` with `stop`, `use DLC:` and `use ... from ...` |
| `03_pattern_matching.cuff` | `match`, `find`, `replace`, `split` and `count`, named captures (`<name:...>`), numbered captures, `[one:...]`, case-insensitive `IS`, escapes (`\.`) |
| `04_collections.cuff` | List and map `add`, `change` and `remove`, slicing (`[2~4]`, negative positions), recovering from an out-of-range position with `or_else` |
| `05_scoping_and_globals.cuff` | Function scope and `change x to global` |
| `06_error_recovery.cuff` | How `or_else` recovers a failed declaration |
| `07_functions_async.cuff` | Recursive `returnable` functions, and `async` combined with `returnable` |
| `08_dlc_libraries.cuff` | `DLC:math`, `DLC:string`, `DLC:random`, `DLC:list`, `DLC:convert` and `DLC:json`. Library functions are named `library_verb` (`math_sqrt`, `str_upper`, `list_sort`), and `use DLC:a, DLC:b` loads several on one line |
| `09_modules_demo.cuff`, `lib/greetings.cuff` | Loading another `.cuff` file with `use <name> from <path>` |
| `10_async_ordering.cuff` | An `async` function called without `await` runs after the code around it finishes, in the order it was called: cooperative scheduling, not concurrency |
| `11_utf8_strings.cuff` | Indexing, slicing and `length()` of multi-byte text such as Hangul, counted in characters |
| `12_filesystem.cuff` | `DLC:filesystem`: `file_write`, `file_add`, `file_readlines`, `file_size` and `file_remove`, only inside the script's folder; the temporary file removes itself |
| `maps/core_engine/stage_data.cuff` | A second module, loaded by `02_comprehensive_demo.cuff` |
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
| `return_from_nonreturnable.cuff` | `E4-016` | `return` with a value in a function that is not `returnable` |
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
