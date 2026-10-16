# `DLC:convert`

```cuff
use DLC:convert
```

The three conversion functions are part of the [always-available built-ins](../builtins.md), so this library
adds nothing you cannot already call; it exists so that `use DLC:convert` is a valid line in a program that wants
to say where its conversions come from.

| Function | Result |
| :--- | :--- |
| `to_number(x)` | A number. A string is read as a decimal number (`E4-025` if it is not one); a boolean is `1` or `0`; a number stays as it is. |
| `to_str(x)` | The display form of any value, as `print` would write it. |
| `to_boolean(x)` | The truthiness of any value: `false`, `0`, `empty`, an empty string, list or map are false; everything else is true. |

```cuff
use DLC:convert
print(to_number("3.5") + 1, to_str(12) + "px", to_boolean(0), to_boolean("no"))
```

```output
4.5 12px false true
```

A conversion that cannot succeed is an error you can catch:

```cuff
set number n to to_number("abc") or_else do:
    change n to 0
end
print(n)
```

```output
0
```
