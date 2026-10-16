# `DLC:json`

```cuff
use DLC:json
```

| Function | Result |
| :--- | :--- |
| `to_json(value [, indent])` | The value as JSON text, compact, or pretty-printed with an indent of `indent` spaces. `empty` becomes `null`. |
| `from_json(text)` | The JSON text as a number, string, boolean, list, map or `empty` (for `null`). Malformed text is `E4-025`. |

Nesting deeper than the [JSON limit](../limits.md) is rejected on the way in and on the way out (`E4-025`).

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

A map becomes a JSON object with its keys in order, and numbers that are whole are written without a fraction.
