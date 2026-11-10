# `DLC:datetime`

```cuff
use DLC:datetime
```

Calendar dates and times. An instant is a number of seconds since the Unix epoch (1970-01-01 00:00:00 UTC), the same number
[`time_now()`](time.md) gives, so the two libraries work together. The library does the arithmetic of the Gregorian calendar and
has no time zone database: a zone is a **whole number of minutes east of UTC**, which the script passes in (`540` is UTC+09:00,
`-330` is UTC-05:30), and `0` is UTC. Years 1 to 9999 are supported.

## Functions

| Function | Result |
| :--- | :--- |
| `datetime_parts(seconds [, offset])` | A map with the calendar fields of the instant in the zone: `year`, `month` (1 to 12), `day`, `hour`, `minute`, `second`, `weekday` (1 is Monday, 7 is Sunday) and `yearday` (1 to 366). A fraction of a second is dropped. |
| `datetime_make(year, month, day [, hour [, minute [, second [, offset]]]])` | The instant for those fields in the zone, or `empty` when the date does not exist (month 13, 31 April, 29 February of a year that is not a leap year, hour 24, a year outside 1 to 9999). `hour`, `minute` and the date are whole numbers; `second` may have a fraction. Missing fields are `0`. |
| `datetime_format(seconds, pattern [, offset])` | The instant written as text by the pattern, in the zone. |
| `datetime_parse(text, pattern [, offset])` | The instant that the text writes, read by the pattern, or `empty` when the text does not fit it. The offset applies unless the pattern has a `Z` and the text gives its own zone. |
| `datetime_offset([seconds])` | The offset from UTC that **this machine's** time zone has at that instant (now when it is left out), in minutes east, so `datetime_parts(t, datetime_offset(t))` is the local time. `0` when the system cannot say. |

The numbers must be numbers and the pattern and the text must be strings (`E4-005` otherwise), and the number of arguments must
match (`E4-010`). An instant outside years 1 to 9999, one that is not finite, an offset of a day or more, and a pattern with an unclosed
`[` are `E4-025`. A date that does not exist or text that does not fit is not an error: `datetime_make` and `datetime_parse` give
`empty`, because the data is what is wrong, and a script that reads dates from outside needs to ask.

```cuff
use DLC:datetime
number t = 1700000000
print(datetime_parts(t))
print(datetime_parts(t, 540))
print(datetime_make(2023, 11, 14, 22, 13, 20))
print(datetime_make(2023, 2, 29), datetime_make(2024, 2, 29))
```

```output
{"year": 2023, "month": 11, "day": 14, "hour": 22, "minute": 13, "second": 20, "weekday": 2, "yearday": 318}
{"year": 2023, "month": 11, "day": 15, "hour": 7, "minute": 13, "second": 20, "weekday": 3, "yearday": 319}
1700000000
empty 1709164800
```

## Patterns

`datetime_format` and `datetime_parse` read the same pattern.

| Written | Means | Example |
| :--- | :--- | :--- |
| `YYYY` | The year, four digits | `2023` |
| `MM` | The month, two digits | `03` |
| `DD` | The day of the month, two digits | `09` |
| `HH` | The hour of a 24-hour day, two digits | `07` |
| `mm` | The minute, two digits | `05` |
| `ss` | The second, two digits | `09` |
| `SSS` | The milliseconds, three digits | `250` |
| `Z` | The zone: `Z` for UTC, otherwise `+HH:mm` or `-HH:mm` | `+09:00` |
| `[text]` | The text between the brackets, as it is | `T` |

Any other character stands for itself, so `-`, `:`, `/`, a space and non-ASCII text can be written as they are. A word that contains
a token, such as `Class` (which has `ss` in it), goes in brackets: `[Class]`. The digit tokens are fixed width when parsing:
`datetime_parse("2023-2-3", "YYYY-MM-DD")` is `empty`, and so is text with anything left over. A pattern with no `YYYY`, `MM` or `DD` reads
that part as 1970, January or the 1st, which makes `datetime_parse("12:30", "HH:mm")` the number of seconds from midnight to half past twelve.
`ss` is 00 to 59 (there are no leap seconds), and `SSS` in a format shows the nearest millisecond without carrying into the seconds.

```cuff
use DLC:datetime
number t = 1700000000.123
print(datetime_format(t, "YYYY-MM-DD HH:mm:ss"))
print(datetime_format(t, "YYYY-MM-DD[T]HH:mm:ss.SSSZ", 540))
print(datetime_format(t, "DD/MM/YYYY [at] HH:mm", -330))
print(datetime_parse("2023-11-15T07:13:20+09:00", "YYYY-MM-DD[T]HH:mm:ssZ"))
print(datetime_parse("15/11/2023", "DD/MM/YYYY"), datetime_parse("32/11/2023", "DD/MM/YYYY"))
```

```output
2023-11-14 22:13:20
2023-11-15T07:13:20.123+09:00
14/11/2023 at 16:43
1700000000
1700006400 empty
```

A date with no time is midnight of that day in the zone: `15/11/2023` read in UTC is `1700006400`.

## Dates and durations

An instant is a plain number, so adding and comparing is ordinary arithmetic. A day is `86400` seconds in UTC, and a whole day in a zone
that changes its clock is not always that many; the library has no zone rules, so a script that must stay on "the same local time tomorrow"
makes the date again with `datetime_make`.

```cuff
use DLC:datetime
use DLC:math
number start = datetime_make(2024, 1, 31)
number later = datetime_make(2024, 3, 1)
print(math_floor((later - start) / 86400), "days")
map p = datetime_parts(start + 30 * 86400)
print(p["month"], p["day"])

map leap = datetime_parts(datetime_make(2024, 2, 29, 12, 0, 0))
print(leap["weekday"], leap["yearday"])
```

```output
30 days
3 1
4 60
```

## The machine's own zone

`datetime_offset` is the only function whose answer depends on the machine, so a script that must give the same result everywhere passes an
explicit offset instead. A host can run a script in any time zone, and an instant that the machine's C library cannot convert (some
systems refuse the years before 1970) gives `0`.

```cuff
use DLC:datetime
use DLC:time
number here = datetime_offset(time_now())
print(here > -1440 and here < 1440)
print(datetime_format(time_now(), "YYYY-MM-DD HH:mm", here) is not "")
```

```output
true
true
```
