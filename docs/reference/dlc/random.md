# `DLC:random`

```cuff
use DLC:random
```

| Function | Result |
| :--- | :--- |
| `random_float()` | A number from 0 up to, but not including, 1. |
| `random_int(low, high)` | A whole number from `low` to `high`, both included. |
| `random_choice(xs)` | An element of a list, chosen at random. |
| `random_shuffle(xs)` | A new list with the same elements in random order. |
| `random_seed(n)` | Starts the sequence from `n`, so that the numbers that follow can be repeated. |

```cuff run
use DLC:random
use DLC:list
random_seed(7)
set number roll to random_int(1, 6)
if roll < 1 do:
    print("never happens")
end
print(length([random_float(), random_choice([1, 2, 3])]))
```

The numbers depend on the random generator of the build, so the same seed gives the same sequence on one
build but is not promised to give the same numbers on another. Do not use this library for anything where
guessability matters.
