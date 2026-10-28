# Libraries (`DLC`)

The built-in libraries are called DLCs. `use DLC:<name>` makes a library's functions available from that line on:

```cuff
use DLC:math
use DLC:string, DLC:list    note: several on one line, comma-separated
print(math_sqrt(16), str_upper("cuff"))
```

```output
4 CUFF
```

A library name needs its `DLC:` prefix every time, and a `use` statement stays on one line. Loading a library
twice is harmless. An unknown library is `E5-004`, and the error lists the ones that exist.

Function names follow `library_verb`: `math_sqrt`, `str_upper`, `list_sort`. The exceptions are `length`,
`contains` and `index_of`, which are shared by the string, list and map libraries, and the conversions
(`to_number` and friends), which are also [always available](../builtins.md).

## The libraries

<!-- BEGIN GENERATED: dlc-table -->
_This table is generated from the engine's library tables by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._

| Library | Load it with | Functions | Reference |
| :--- | :--- | ---: | :--- |
| math | `use DLC:math` | 25 | [math](math.md) |
| string | `use DLC:string` | 15 | [string](string.md) |
| time | `use DLC:time` | 2 | [time](time.md) |
| random | `use DLC:random` | 5 | [random](random.md) |
| list | `use DLC:list` | 11 | [list](list.md) |
| map | `use DLC:map` | 7 | [map](map.md) |
| convert | `use DLC:convert` | 3 | [convert](convert.md) |
| json | `use DLC:json` | 2 | [json](json.md) |
| network | `use DLC:network` | 2 | [network](network.md) |
| filesystem | `use DLC:filesystem` | 7 | [filesystem](filesystem.md) |
<!-- END GENERATED: dlc-table -->

All functions are also listed together in the [function index](../functions.md).

## Rules every library function follows

- A function checks its arguments before it runs. The wrong number of arguments is `E4-010`, a wrong type
  `E4-005`, a value of the right type that the function does not accept `E4-025`, and a division by zero
  `E4-006`. All of these can be caught with `or_else`.
- Functions return new values and never change their arguments. A list or map you pass in comes back unchanged.
- Strings are measured and indexed in characters (Unicode code points), not bytes.

## Libraries that reach outside the program

[`DLC:network`](network.md) and [`DLC:filesystem`](filesystem.md) touch the real world, so a host that runs
scripts it did not write should confine them or switch them off. The [security policy](../../../SECURITY.md) explains what each
one can do and what protects it.

A host switches a library off with `--no-network` or `--no-filesystem`, or with the matching engine options. `use DLC:network`
and `use DLC:filesystem` still succeed, so a program loads the same way everywhere, and every call into a library that is
switched off is refused with `E5-005`, before its arguments are looked at and before anything is sent or touched. The error can be
caught with `or_else`.
