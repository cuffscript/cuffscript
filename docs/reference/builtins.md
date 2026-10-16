# Built-in functions

These functions are always available. No `use` is needed.

| Function | Result |
| :--- | :--- |
| `print(a, b, ...)` | Writes its arguments to standard output separated by single spaces, then a newline. Returns `empty`. |
| `input([prompt])` | Writes the optional prompt, then reads one line from standard input and returns it as a string without the line ending. At the end of the input it returns an empty string. |
| `type_of(x)` | The type name: `"number"`, `"str"`, `"boolean"`, `"list"`, `"map"`, `"match"` or `"empty"`. |
| `to_number(x)` | A number; a decimal string is parsed (`E4-025` if it is not one) and a boolean is `1` or `0`. Other types are an error. |
| `to_str(x)` | The display form of any value: the text `print` would write. |
| `to_boolean(x)` | The truthiness of any value. |

`to_number`, `to_str` and `to_boolean` are also listed under [`DLC:convert`](dlc/convert.md); both ways of
reaching them are the same functions.

```cuff
print(type_of(1), type_of("a"), type_of([1]), type_of({}), type_of(empty))
print(to_number("42") + 1, to_str(3) + "!")
```

```output
number str list map empty
43 3!
```

`length`, `contains` and `index_of` are not built in; they come with the [string](dlc/string.md),
[list](dlc/list.md) and [map](dlc/map.md) libraries and work on all three kinds of value.
