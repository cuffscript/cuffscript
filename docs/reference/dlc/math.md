# `DLC:math`

```cuff
use DLC:math
```

Mathematical functions. Numbers in CuffScript are double-precision floating point (see [types](../language.md#2-types-and-values)),
and every function here takes and returns numbers; an argument of any other type is `E4-005`, and the wrong number of
arguments is `E4-010`.

The arithmetic operators `%` and `**` do the same work as `math_mod` and `math_pow` (they share one implementation), so
`-7 % 3` and `math_mod(-7, 3)` are always the same number.

## Functions

| Function | Result |
| :--- | :--- |
| `math_sqrt(x)` | The square root. A negative `x` is `E4-025`. |
| `math_abs(x)` | The absolute value. |
| `math_pow(base, exponent)` | `base` to the power `exponent`, the same as `base ** exponent`. `0` to a negative power is `E4-006`; a negative base with a fractional exponent, or a result too large to represent, is `E4-025`. |
| `math_round(x [, digits])` | Rounds to `digits` decimal places. `digits` is a whole number from 0 to 15 (anything else is `E4-025`) and defaults to 0. Halves round away from zero: `2.5` gives `3` and `-2.5` gives `-3`. |
| `math_floor(x)`, `math_ceil(x)` | Round down, round up. |
| `math_trunc(x)` | Rounds toward zero. |
| `math_sign(x)` | `-1`, `0` or `1`. |
| `math_min(a, b, ...)`, `math_max(a, b, ...)` | The smallest or largest of one or more numbers, or of the numbers in a single list. Called with no argument it is `E4-010`; an empty list is `E4-025`; an element that is not a number is `E4-005`. |
| `math_clamp(value, low, high)` | `value`, kept between `low` and `high`. `low` greater than `high` is `E4-025`. |
| `math_mod(a, b)` | The remainder of `a / b`, the same as `a % b`. The result takes the sign of `b`, so `math_mod(-7, 3)` is `2` and `math_mod(7, -3)` is `-2`. A `b` of `0` is `E4-006`. |
| `math_log(x [, base])` | The natural logarithm, or the logarithm to `base`. `x` must be positive, and `base` must be positive and not `1` (`E4-025`). |
| `math_log2(x)`, `math_log10(x)` | Base-2 and base-10 logarithms. `x` must be positive. |
| `math_exp(x)` | `e` to the power `x`. A result too large to represent is `E4-025`. |
| `math_sin(x)`, `math_cos(x)`, `math_tan(x)` | Trigonometry, in radians. |
| `math_asin(x)`, `math_acos(x)`, `math_atan(x)` | The inverse functions, giving radians. `math_asin` and `math_acos` need `x` between `-1` and `1` (`E4-025`). |
| `math_atan2(y, x)` | The angle of the point `(x, y)`, in radians. |
| `math_pi()`, `math_e()` | The constants, called with no arguments. |

```cuff
use DLC:math
print(math_sqrt(16), math_pow(2, 10), math_round(2.5), math_round(3.14159, 2))
print(math_min(4, 9, 2), math_max([3, 8, 5]), math_clamp(15, 0, 10))
print(math_mod(-7, 3), math_floor(-1.5), math_trunc(-1.5), math_sign(-8))
```

```output
4 1024 3 3.14
2 8 10
2 -2 -1 -1
```

## Rounding

The four rounding functions differ for negative numbers and for halves:

| `x` | `math_floor` | `math_ceil` | `math_trunc` | `math_round` |
| ---: | ---: | ---: | ---: | ---: |
| `2.5` | `2` | `3` | `2` | `3` |
| `-2.5` | `-3` | `-2` | `-2` | `-3` |
| `-0.5` | `-1` | `0` | `0` | `-1` |

A result of negative zero prints as `0`.

```cuff
use DLC:math
print(math_round(-2.5), math_round(1234.5678, 2), math_ceil(-0.5), math_floor(-0.5))
```

```output
-3 1234.57 0 -1
```

## Floating point

Decimal fractions such as `0.1` have no exact binary form, so comparing the result of arithmetic with `is` can fail even
though both sides print the same:

```cuff
use DLC:math
print(0.1 + 0.2 is 0.3)
print(math_round(0.1 + 0.2, 10) is 0.3)
```

```output
false
true
```

Round both sides to the number of digits you care about before comparing. Whole numbers are exact up to 2<sup>53</sup>,
which is also the largest whole number the functions that need one (a `digits`, a count, an index) accept.

## Errors

A result that is not a finite number is reported as an error rather than returned as `inf` or `nan`, so a program never
has to test for them. All of these errors can be caught with `or_else`.

```cuff error E4-025
use DLC:math
print(math_sqrt(-1))
```

```cuff error E4-006
use DLC:math
print(math_mod(10, 0))
```
