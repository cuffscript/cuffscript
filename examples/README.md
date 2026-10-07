# Example scripts

Read them in numeric order to pick up the language one feature at a time. Every
one of them has been run for real (`make && ./cuffc examples/0X_....cuff`).

| File | What it shows |
|---|---|
| `01_hello.cuff` | The simplest `print` |
| `02_comprehensive_demo.cuff` | Constants, `returnable` functions, combining `is` and `!`, `async` + `await` + `or_else`, 1-based lists, `loop repeat` + `stop`, `use DLC:` / `use ... from ...` — the comprehensive check code from the spec, as is |
| `03_pattern_matching.cuff` | `match/find/replace/split/count`, named captures (`<name:...>`), numbered captures, `[one:...]`, case-insensitive `IS`, escapes (`\.`) |
| `04_collections.cuff` | List/map `add`/`change`/`remove`, slicing (`[2~4]`, negative indexes), recovering from an out-of-range index with `or_else` |
| `05_scoping_and_globals.cuff` | Function-level scope, `change x to global` |
| `06_error_recovery.cuff` | How `or_else` recovers a failed declaration |
| `07_functions_async.cuff` | Recursive `returnable` functions, combining `async` + `returnable` |
| `08_dlc_libraries.cuff` | `DLC:math` / `DLC:string` / `DLC:random` / `DLC:list` / `DLC:convert` / `DLC:json` (function names follow the `library_verb` form, such as `math_sqrt`, `str_upper`, `list_sort`; `use DLC:a, DLC:b` loads several on one line) |
| `09_modules_demo.cuff` + `lib/greetings.cuff` | Loading another `.cuff` file with `use <name> from <path>` |
| `10_async_ordering.cuff` | An `async` function called without `await` runs after the synchronous code finishes, in queue (FIFO) order (cooperative scheduling, not concurrency) |
| `11_utf8_strings.cuff` | Indexing, slicing and `length()` of multi-byte strings such as Hangul, counted in UTF-8 code points |
| `12_filesystem.cuff` | `DLC:filesystem`: `file_write` / `file_add` / `file_readlines` / `file_size` / `file_remove` (only inside the script's folder; the temporary file cleans itself up) |
| `maps/core_engine/stage_data.cuff`, referenced by `02_comprehensive_demo.cuff` | A second example of loading a custom module, like the one above |

## error_cases/

Scripts that are built to fail — use them to see the message each kind of error
actually produces. (They are not wrapped in `or_else`, so it is normal for all of
them to end with a non-zero exit code.)

```bash
for f in examples/error_cases/*.cuff; do
    echo "--- $f ---"
    ./cuffc "$f"
done
```

## Running them yourself

```bash
make
./cuffc examples/01_hello.cuff
./cuffc --ast examples/01_hello.cuff   # print the tokens/AST instead of running
```
