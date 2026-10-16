# `DLC:list`

```cuff
use DLC:list
```

Functions return new lists; the list you pass in is not changed. To change a list in place use the `add`,
`change` and `remove` statements ([language reference](../language.md#51-collections-add-change-remove)).

| Function | Result |
| :--- | :--- |
| `length(x)` | The number of elements. |
| `contains(x, v)` | Whether the list has an element equal to `v`. |
| `index_of(x, v)` | The 1-based position of the first element equal to `v`, or `empty`. |
| `list_sort(xs)` | A copy in ascending order. All elements must be numbers, or all must be strings. |
| `list_reverse(xs)` | A copy in reverse order. |
| `list_join(xs, separator)` | The elements of a list of strings joined with `separator`. |
| `list_unique(xs)` | A copy with later duplicates removed (the first occurrence stays). |
| `list_sum(xs)`, `list_average(xs)` | The sum and the average of a list of numbers. |
| `list_flatten(xs)` | A copy with the lists inside it spliced in, one level deep. |
| `list_range(start, end [, step])` | The numbers from `start` to `end`, both included, counting down if `start` is larger. |

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

Sorting a list that mixes numbers and strings is `E4-005`.
