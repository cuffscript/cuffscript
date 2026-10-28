# `DLC:convert`

```cuff
use DLC:convert
```

The three conversion functions are part of the [always-available built-ins](../builtins.md), so this library adds
nothing you cannot already call; it exists so that `use DLC:convert` is a valid line in a program that wants to say where
its conversions come from.

| Function | Result |
| :--- | :--- |
| `to_number(x)` | A number. A number stays as it is, `true` and `false` become `1` and `0`, and a string is read as plain decimal text (see below). Any other type is `E4-005`. |
| `to_str(x)` | The display form of any value, as `print` would write it (see [types](../language.md#2-types-and-values)). |
| `to_boolean(x)` | The truthiness of any value: `false`, `0`, `empty`, an empty string, an empty list and an empty map are false; everything else is true, including the string `"false"`. |

```cuff
use DLC:convert
print(to_number("3.5") + 1, to_str(12) + "px", to_boolean(0), to_boolean("no"))
```

```output
4.5 12px false true
```

## What `to_number` accepts

A string must be a plain decimal number, with optional white space on either side (space, tab, line feed, carriage return,
form feed, vertical tab). The number can have a sign, digits with an optional fraction, and an optional exponent:

| Text | Result |
| :--- | :--- |
| `"42"`, `" 42 "`, `"+42"` | `42` |
| `"-3.5"`, `".5"`, `"5."` | `-3.5`, `0.5`, `5` |
| `"1e3"`, `"2.5E-1"` | `1000`, `0.25` |
| `""`, `"12abc"`, `"1 2"`, `"0x10"`, `"1_000"`, `"inf"`, `"nan"` | `E4-025` |
| `"1e999"` (too large to represent) | `E4-025` |

```cuff
use DLC:convert
print(to_number(" 42 "), to_number(".5"), to_number("1e3"), to_number(true))
```

```output
42 0.5 1000 1
```

A string that is not a number is an error you can catch:

```cuff
set number n to to_number("abc") or_else do:
    change n to 0
end
print(n)
```

```output
0
```

`to_number` does not accept a comma as a decimal separator or as a thousands separator: `"1,5"` is `E4-025`. Remove the commas first with
`replace "," in text to "" g`, or split the text.
