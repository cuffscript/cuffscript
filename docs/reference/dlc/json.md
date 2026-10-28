# `DLC:json`

```cuff
use DLC:json
```

Reads and writes [JSON](https://www.json.org). An argument of the wrong type is `E4-005`, and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `to_json(value [, indent])` | The value as JSON text. Without `indent` the text is compact. With `indent`, a whole number from 0 to 10 (`E4-025` otherwise), it is pretty-printed with that many spaces per level; `0` is the same as compact. |
| `from_json(text)` | The JSON text as a number, string, boolean, list, map or `empty` (for `null`). Malformed text is `E4-025`, and the message names the offset where the text went wrong. |

Nesting deeper than the [JSON limit](../limits.md) is rejected on the way in and on the way out (`E4-025`). A list or map that contains itself is rejected on the way out for the same reason.

```cuff
use DLC:json
use DLC:list
set map person to {"name": "Ana", "tags": ["a", "b"], "age": empty}
print(to_json(person))
set map data to from_json("{\"n\": 3, \"ok\": true, \"items\": [1, 2]}")
print(data["n"] + 1, data["ok"], length(data["items"]))
```

```output
{"name":"Ana","tags":["a","b"],"age":null}
4 true 2
```

## Writing JSON

| CuffScript value | JSON |
| :--- | :--- |
| `number` | A number, in the form `print` shows it: a whole number has no fraction (`3`, not `3.0`), and a very large or small one uses an exponent (`1e+20`). |
| `str` | A string. `"`, `\`, line feed, tab and the other control characters are escaped; every other character, Hangul included, is written as it is, not as `\u` escapes. |
| `boolean` | `true` or `false`. |
| `empty` | `null`. |
| `list` | An array. |
| `map` | An object, with its keys in insertion order. |
| `match` | Not supported (`E4-005`): copy the captures you need into a list or map first. |

Compact output has no spaces at all. Pretty output puts each element or key on its own line, writes `"key": value`, and writes an empty list or map as `[]` or `{}`:

```cuff
use DLC:json
print(to_json({"a": [1, {"b": 2}], "c": []}, 2))
```

```output
{
  "a": [
    1,
    {
      "b": 2
    }
  ],
  "c": []
}
```

## Reading JSON

`from_json` is strict: the text must be exactly one JSON value, optionally surrounded by white space.

| JSON | CuffScript value |
| :--- | :--- |
| a number | a `number`; one too large to represent (`1e999`) is `E4-025`. `-0` becomes `0`. |
| a string | a `str`, with `\n`, `\"`, `\uXXXX` and the other escapes decoded |
| `true`, `false` | a `boolean` |
| `null` | `empty` |
| an array | a `list` |
| an object | a `map`. If a key appears twice, the last value wins. |

These are errors: an empty text, a trailing comma (`[1,]`, `{"a": 1,}`), single-quoted strings, an unquoted key, `NaN` and `Infinity`, comments, numbers written as `01`, `.5`, `1.` or `+1`, a raw line break or other control character inside a string, an unknown escape such as `\x`, and anything after the value (`1 2`).

```cuff
use DLC:json
print(from_json("[1, 2.5e1, null, true]"))
print(from_json("  {\"a\" : 1}  "))
```

```output
[1, 25, empty, true]
{"a": 1}
```

A text that might be malformed is best read with `or_else`:

```cuff
use DLC:json
set map config to from_json("{oops") or_else do:
    change config to {"retries": 3}
end
print(config)
```

```output
{"retries": 3}
```

## Round trips

Writing and reading again gives back an equal value, with these differences: a number such as `1.0` comes back as `1`, and a key's position is kept, but a `match` cannot be written at all.

```cuff
use DLC:json
set map original to {"id": 7, "tags": ["x"], "note": empty}
print(from_json(to_json(original)) is original)
```

```output
true
```
