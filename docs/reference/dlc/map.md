# `DLC:map`

```cuff
use DLC:map
```

Map functions. A map holds string keys and keeps them in the order they were added. These functions return new values and
never change the map you pass in. The values inside a returned map or list are the same values as in the original, not
copies. An argument of the wrong type is `E4-005` and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `length(m)` | The number of keys. |
| `contains(m, key)` | Whether the map has the key. `key` must be a string (`E4-005`). |
| `map_has_key(m, key)` | The same as `contains`, spelled for maps. |
| `map_keys(m)` | A list of the keys, in insertion order. |
| `map_values(m)` | A list of the values, in the same order as the keys. |
| `map_entries(m)` | A list of `[key, value]` pairs. |
| `map_merge(a, b)` | A new map with the keys of both. Where both have a key, `b` wins and the key keeps the position it has in `a`; keys only in `b` follow. |

`length` and `contains` are shared with [`DLC:string`](string.md) and [`DLC:list`](list.md). `index_of` is not a map
function: asking a map for it is `E4-005`, and loading only `DLC:map` does not provide it at all.

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

## Reading, changing and removing keys

Everyday map work uses the language itself, not this library:

```cuff
set map stock to {"apples": 3}
change stock["pears"] to 5
change stock["apples"] to stock["apples"] + 1
remove "pears" from stock
print(stock)
```

```output
{"apples": 4}
```

- `stock["key"]` reads a value. A key that is not there is `E4-009`; test first with `map_has_key` or recover with `or_else`.
- `change stock["key"] to value` sets a key, creating it when it is new.
- `remove "key" from stock` deletes a key; a key that is not there is ignored.
- A map key must be a string. `{1: "one"}` and `stock[1]` are `E4-005`.

## Looping over a map

A map is not looped over directly. Loop over its keys, or its entries, instead:

```cuff
use DLC:map
set map prices to {"tea": 3, "milk": 2}
set list names to map_keys(prices)
loop repeat i to 1 ~ length(names) do:
    print(names[i], prices[names[i]])
end
```

```output
tea 3
milk 2
```

## Merging

`map_merge` copies; the original maps are unchanged. Only the top level is merged: when both maps have a key whose value is a map,
the one from `b` replaces the one from `a` as a whole.

```cuff
use DLC:map
set map defaults to {"color": "blue", "size": 10}
set map chosen to map_merge(defaults, {"size": 12})
print(chosen, defaults)
```

```output
{"color": "blue", "size": 12} {"color": "blue", "size": 10}
```
