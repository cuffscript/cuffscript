# `DLC:string`

```cuff
use DLC:string
```

Text functions. A string is a sequence of Unicode characters (code points): `length("한글")` is 2, and positions and lengths in
every function below are counted in characters, never bytes. Functions return new strings; a string is never changed in
place. An argument of the wrong type is `E4-005` and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `length(x)` | The characters in a string. Also works on lists and maps; see below. |
| `contains(x, v)` | Whether the string `x` contains the substring `v`. `v` must be a string, and an empty `v` is always found. |
| `index_of(x, v)` | The 1-based position, in characters, of the first `v` in `x`, or `empty` if there is none. An empty `v` is found at position `1`. |
| `str_upper(s)`, `str_lower(s)` | The string in upper or lower case; see [case conversion](#case-conversion). |
| `str_trim(s)`, `str_trim_start(s)`, `str_trim_end(s)` | The string without surrounding white space on both sides, the start, or the end. White space here is exactly space, tab, CR and LF; a form feed or a non-breaking space is not trimmed. |
| `str_starts_with(s, prefix)`, `str_ends_with(s, suffix)` | Whether the string begins or ends with the given text. The empty string is a prefix and a suffix of everything. |
| `str_repeat(s, n)` | The string repeated `n` times. `n` must be a whole number of 0 or more (`E4-025`), and a result longer than the [string limit](../limits.md) is `E4-026`. |
| `str_pad_left(s, width [, fill])`, `str_pad_right(s, width [, fill])` | The string padded on the left or the right with `fill` until it is `width` characters long. `fill` is exactly one character and defaults to a space (`E4-025` otherwise). A string that is already `width` characters or longer comes back unchanged; a string is never shortened. |
| `str_char_code(s)` | The code point of the first character. An empty string is `E4-025`. |
| `str_from_char_code(n)` | The character with code point `n`, from 0 to 1114111, excluding the surrogate range (`E4-025`). |

`length`, `contains` and `index_of` are shared with [`DLC:list`](list.md) and, for the first two, [`DLC:map`](map.md).
`length` and `contains` become available when any of the three libraries is loaded, and `index_of` when `DLC:string` or
`DLC:list` is. Each one works on every kind of value it is documented for, whichever library loaded it.

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

## Case conversion

`str_upper` and `str_lower` convert the letters of these ranges, one character to one character:

- the English letters `A`-`Z` and `a`-`z`;
- the Latin-1 and Latin Extended-A letters (`é`, `ñ`, `ø`, `ő`, ...);
- the Greek and Cyrillic alphabets (`α`/`Α`, `ж`/`Ж`, ...).

Every other character stays as it is, including Hangul, which has no case. There is no locale, and nothing that needs to
change the length of the text: `ß` has no single-character capital, so `str_upper("straße")` keeps it.

```cuff
use DLC:string
print(str_upper("héllo ñ ж α"), str_lower("HÉLLO Ñ Ж Α"))
print(str_upper("straße"))
```

```output
HÉLLO Ñ Ж Α héllo ñ ж α
STRAßE
```

Compare text ignoring case with `IS` (`name IS "yes"`) rather than by converting, or [pattern flags](../patterns.md#28-the-flags-system) for searching.

## Characters, not bytes

```cuff
use DLC:string
set str word to "안녕"
print(length(word), word[2], str_char_code(word), str_from_char_code(128512))
```

```output
2 녕 50504 😀
```

A character here is one code point. A letter drawn from several code points, such as `e` followed by a combining accent, counts as
two characters. There are no `\u` escapes in a string literal; write the character itself, or build it with
`str_from_char_code`.

## Trimming and padding

```cuff
use DLC:string
print(str_trim("\t x \n") + "|", str_trim_start("  x ") + "|", str_trim_end("  x ") + "|")
print(str_pad_left("abc", 2) + "|", str_pad_left("é", 3, "*"))
```

```output
x| x |   x|
abc| **é
```

## Errors

```cuff error E4-025
use DLC:string
print(str_pad_left("7", 3, "ab"))
```

For searching by shape rather than by exact text, see [patterns](../patterns.md).
