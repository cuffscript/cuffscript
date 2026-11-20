# `DLC:time`

```cuff
use DLC:time
```

The clock, and waiting.

| Function | Result |
| :--- | :--- |
| `time_now()` | Seconds since the Unix epoch (1970-01-01 00:00:00 UTC), with a fractional part. No arguments. |
| `time_timestamp()` | The same as `time_now()`; both names are kept. |
| `time_sleep(seconds)` | Waits at least `seconds` (a number, fractions allowed, from 0 to the [sleep limit](../limits.md)) and gives `empty`. A negative or too large number is `E4-025`. |

```cuff run
use DLC:time
number started = time_now()
number elapsed = time_now() - started
if elapsed < 0 do:
    print("the clock went backwards")
end
```

Calendar dates and formatting are in [`DLC:datetime`](datetime.md), which takes the number `time_now()` gives. This library gives you
the clock, and measuring how long something took is its main use. The values come from the system's wall clock, which an administrator or a time service can set backwards, so a
difference between two readings can in rare cases be negative or wrong. The `--timeout` budget uses the engine's own steady
clock, not this one, so changing the system time never shortens or extends it.

Seconds are a `number`, so a reading has a fractional precision of well under a millisecond, and fewer digits when it is
printed (a number prints at most 15 significant digits). To show a reading in whole seconds, round it with `math_floor` from
[`DLC:math`](math.md).

## Waiting

`time_sleep` waits, and while it waits the other [tasks](../language.md#65-asynchronous-functions-and-tasks) of the program run.
Two tasks that each sleep for a second take about one second together:

```cuff run
use DLC:time

async func nap(str name, number seconds) do:
    time_sleep(seconds)
    return name
end

number started = time_now()
Task a = nap("a", 0.2)
Task b = nap("b", 0.2)
print(await a, await b)
if time_now() - started < 0.35 do:
    print("the naps overlapped")
end
```

The sleep is measured on the engine's steady clock. A program that has a `--timeout` is ended when the limit is reached, even in
the middle of a sleep (`E6-002`). The engine wakes a sleeping task no earlier than asked, and as close after as the system allows:
usually within a millisecond or two. In the browser build (WebAssembly) there is nothing to wait on, so a sleep keeps the
processor busy until its time is up.
