# `DLC:string`

```cuff
use DLC:string
```

Strings are indexed and measured in characters, so `length("한글")` is 2. Functions return new strings; a string
is never changed in place.

| Function | Result |
| :--- | :--- |
| `length(x)` | The characters in a string. Also works on lists and maps; see below. |
| `contains(x, v)` | Whether the string `x` contains the substring `v`. |
| `index_of(x, v)` | The 1-based position of the first `v` in `x`, or `empty` if there is none. |
| `str_upper(s)`, `str_lower(s)` | The string in upper or lower case. |
| `str_trim(s)`, `str_trim_start(s)`, `str_trim_end(s)` | The string without surrounding white space (space, tab, CR, LF) on both sides, the start, or the end. |
| `str_starts_with(s, prefix)`, `str_ends_with(s, suffix)` | Whether the string begins or ends with the given text. |
| `str_repeat(s, n)` | The string repeated `n` times. A result longer than the [string limit](../limits.md) is `E4-026`. |
| `str_pad_left(s, width [, pad])`, `str_pad_right(s, width [, pad])` | The string padded to `width` characters, with spaces or with the optional pad string. |
| `str_char_code(s)` | The code point of the first character. |
| `str_from_char_code(n)` | The character with code point `n`. |

`length`, `contains` and `index_of` are shared with [`DLC:list`](list.md) and [`DLC:map`](map.md): loading any of
the three makes them available, and each works on all the kinds of value it is documented for.

```cuff
use DLC:string
set str name to "  Cuff Script  "
print(length(name), length(str_trim(name)))
print(str_upper("cuff"), str_lower("SCRIPT"), str_repeat("ab", 3))
print(str_pad_left("7", 3, "0"), str_pad_right("ab", 4, ".") + "|")
print(index_of("hello", "l"), contains("hello", "ell"), index_of("hello", "z"))
print(str_char_code("A"), str_from_char_code(97))
```

```output
15 11
CUFF script ababab
007 ab..|
3 true empty
65 a
```

For searching by shape rather than by exact text, see [patterns](../patterns.md).
