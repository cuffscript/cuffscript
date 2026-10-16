# `DLC:map`

```cuff
use DLC:map
```

A map keeps its keys in the order they were added. Functions return new values and never change the map you pass
in.

| Function | Result |
| :--- | :--- |
| `length(m)` | The number of keys. |
| `contains(m, key)` | Whether the map has the key. |
| `map_has_key(m, key)` | The same as `contains`, spelled for maps. |
| `map_keys(m)` | A list of the keys. |
| `map_values(m)` | A list of the values. |
| `map_entries(m)` | A list of `[key, value]` pairs. |
| `map_merge(a, b)` | A new map with the keys of both. Where both have a key, `b` wins. |

```cuff
use DLC:map
set map ages to {"ana": 31, "ben": 27}
print(map_keys(ages), map_values(ages), length(ages))
print(map_has_key(ages, "ben"), contains(ages, "cy"))
print(map_entries(ages))
print(map_merge(ages, {"ben": 28, "cy": 40}))
```

```output
["ana", "ben"] [31, 27] 2
true false
[["ana", 31], ["ben", 27]]
{"ana": 31, "ben": 28, "cy": 40}
```

Reading a key that is not there is `E4-009`; test first with `map_has_key`, or recover with `or_else`.
