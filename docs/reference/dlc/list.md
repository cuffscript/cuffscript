# `DLC:list`

```cuff
use DLC:list
```

List functions. They return new lists and never change the list you pass in. To change a list in place use the `add`,
`change` and `remove` statements ([language reference](../language.md#51-collections-add-change-remove)). The new list is
a shallow copy: lists and maps stored inside it are the same values as in the original, not copies. An argument of the
wrong type is `E4-005` and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `length(x)` | The number of elements. |
| `contains(x, v)` | Whether the list has an element equal to `v`. Elements are compared the way `is` compares them, so `[2]` is found in `[1, [2]]` and `"1"` is not found in `[1]`. |
| `index_of(x, v)` | The 1-based position of the first element equal to `v`, or `empty`. |
| `list_sort(xs)` | A copy in ascending order. All elements must be numbers, or all must be strings (`E4-005` otherwise); an empty list is fine. Strings are ordered by code point, so every capital letter comes before every lowercase one. |
| `list_reverse(xs)` | A copy in reverse order. |
| `list_join(xs, separator)` | The elements of a list of strings joined with `separator`. Every element must already be a string (`E4-005`; use `to_str` first). An empty list gives the empty string. |
| `list_unique(xs)` | A copy with later duplicates removed; the first occurrence stays. Elements are compared by value, so equal lists inside are duplicates too, and `1` and `"1"` are different. |
| `list_sum(xs)`, `list_average(xs)` | The sum and the average of a list of numbers. The sum of an empty list is `0`; the average of an empty list is `E4-025`. |
| `list_flatten(xs)` | A copy with the lists inside it spliced in, one level deep. |
| `list_range(start, end [, step])` | The numbers `start`, `start + step`, `start + 2 * step`, ... up to `end`. `end` is included only when a whole number of steps reaches it. The step defaults to `1`, or to `-1` when `start` is larger than `end`. `start`, `end` and `step` need not be whole numbers; a `step` of `0` is `E4-025`; a step that points away from `end` gives an empty list; a result larger than the [collection limit](../limits.md) is `E4-026`. |

```cuff
use DLC:list
set list xs to [5, 3, 9, 3]
print(list_sort(xs), list_unique(xs), list_reverse(xs))
print(list_sum(xs), list_average(xs), length(xs))
print(list_join(["a", "b", "c"], "-"), list_flatten([[1, 2], [3], 4]))
print(list_range(1, 10, 3), list_range(3, 1))
print(index_of(xs, 9), contains(xs, 4))
```

```output
[3, 3, 5, 9] [5, 3, 9] [3, 9, 3, 5]
20 5 4
a-b-c [1, 2, 3, 4]
[1, 4, 7, 10] [3, 2, 1]
3 false
```

## Ranges

`list_range` is the way to build a list of numbers, including fractions and countdowns:

```cuff
use DLC:list
print(list_range(1, 9, 3), list_range(0, 1, 0.25), list_range(5, 1, -2))
print(list_range(1, 5, -1), list_range(2, 2))
```

```output
[1, 4, 7] [0, 0.25, 0.5, 0.75, 1] [5, 3, 1]
[] [2]
```

To repeat something a number of times, `loop repeat i to 1 ~ 10 do:` needs no list at all.

## Sorting

Only lists whose elements can be put in an order are sorted. To sort records, sort a list of the field you order by, or build
the order yourself with a loop.

```cuff error E4-005
use DLC:list
print(list_sort([2, "a"]))
```

## Copies

```cuff
use DLC:list
set list original to [3, 1, 2]
set list ordered to list_sort(original)
print(original, ordered)
```

```output
[3, 1, 2] [1, 2, 3]
```
