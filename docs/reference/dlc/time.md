# `DLC:time`

```cuff
use DLC:time
```

The clock. Both functions take no arguments (`E4-010` otherwise).

| Function | Result |
| :--- | :--- |
| `time_now()` | Seconds since the Unix epoch (1970-01-01 00:00:00 UTC), with a fractional part. |
| `time_timestamp()` | The same as `time_now()`; both names are kept. |

```cuff run
use DLC:time
set number started to time_now()
set number elapsed to time_now() - started
if elapsed < 0 do:
    print("the clock went backwards")
end
```

There is no calendar or formatting support: the library gives you the clock, and measuring how long something took is its
main use. The values come from the system's wall clock, which an administrator or a time service can set backwards, so a
difference between two readings can in rare cases be negative or wrong. The `--timeout` budget uses the engine's own steady
clock, not this one, so changing the system time never shortens or extends it.

Seconds are a `number`, so a reading has a fractional precision of well under a millisecond, and fewer digits when it is
printed (a number prints at most 15 significant digits). To show a reading in whole seconds, round it with `math_floor` from
[`DLC:math`](math.md).
