# `DLC:time`

```cuff
use DLC:time
```

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

There is no calendar or formatting support: the library gives you the clock, and measuring how long something
took is its main use. The `--timeout` budget uses the engine's own clock, not this one.
