# `DLC:random`

```cuff
use DLC:random
```

Pseudo-random numbers. An argument of the wrong type is `E4-005`, and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `random_float()` | A number from 0 up to, but not including, 1. |
| `random_int(low, high)` | A whole number from `low` to `high`, both included. Both must be whole numbers and `low` must not be greater than `high` (`E4-025`). |
| `random_choice(xs)` | An element of a list, chosen at random. An empty list is `E4-025`. The element is not copied: a list or map that you get back is the one that is inside `xs`. |
| `random_shuffle(xs)` | A new list with the same elements in random order. The list you pass in is not changed. |
| `random_seed(n)` | Starts the sequence from the whole number `n`, so that the numbers that follow can be repeated. It gives `empty`. |

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

## Repeatable sequences

Seeding the generator with the same number gives the same numbers afterwards:

```cuff
use DLC:random
random_seed(2026)
set number first to random_int(1, 1000)
random_seed(2026)
set number second to random_int(1, 1000)
print(first is second)
```

```output
true
```

The sequence depends on the random generator of the build, so the same seed is not promised to give the same numbers
on another build, compiler or platform. Do not use this library for anything where guessability matters: it is not
suitable for passwords, tokens or anything an attacker must not predict.

## One generator per process

The generator is started once per process, from the operating system's entropy, and is shared by every program the
process runs: a program that calls `random_seed` changes the sequence that every other `DLC:random` user in the same process
sees next. Access to it is locked, so scripts running on several threads can use the library at the same time (see
[embedding](../../guide/embedding.md#threads-and-stacks)); which thread draws which number is then up to the scheduler, so a seed gives
a repeatable sequence only to a program that has the generator to itself.
