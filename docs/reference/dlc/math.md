# `DLC:math`

```cuff
use DLC:math
```

Mathematical functions. Numbers in CuffScript are double-precision floating point (see [types](../language.md#2-types-and-values)),
and the functions here take and return numbers, except where [bits and bases](#bits-and-bases) says otherwise; an
argument of any other type is `E4-005`, and the wrong number of arguments is `E4-010`.

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
| `math_min(a, ...)`, `math_max(a, ...)` | The smallest or largest of one or more numbers, or of the numbers in a single list. Called with no argument it is `E4-010`; an empty list is `E4-025`; an element that is not a number is `E4-005`. |
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

## Bits and bases

| Function | Result |
| :--- | :--- |
| `math_bit_and(a, b, ...)`, `math_bit_or(a, b, ...)`, `math_bit_xor(a, b, ...)` | The bits set in every number, in any of them, or in an odd number of them. Two or more numbers (`E4-010` otherwise). |
| `math_bit_not(x, width)` | `x` with its lowest `width` bits flipped: the complement in `width` bits. `width` is a whole number from 1 to 53, and `x` must fit in it (`E4-025`), so `math_bit_not(5, 4)` is `10` and `math_bit_not(16, 4)` is an error. |
| `math_bit_shl(x, n)`, `math_bit_shr(x, n)` | `x` shifted `n` bits to the left or to the right, `n` a whole number from 0 to 53. Shifting right drops the low bits. Shifting left a bit past the 53rd is `E4-025` rather than a silent loss: keep the bits that fit first, with `math_bit_and`. |
| `math_bit_test(x, position)` | Whether the bit at `position` is set, as a `boolean`. Position 0 is the lowest bit, and the highest is 52. |
| `math_to_base(x, base [, width])` | The digits of `x` in `base`, from 2 to 36, as a `str`: `0`-`9` then lowercase `a`-`z`. With `width` (1 to 64) the digits are padded on the left with zeros to at least that many; a number with more digits is not cut. |
| `math_from_base(text, base)` | The number that the digits in `text` write in `base` (2 to 36). Letters may be either case. Only digits are accepted: an empty string, a sign, a space, a prefix such as `0x` or a digit too large for the base is `E4-025`, as is a number above 2<sup>53</sup> - 1. |

The numbers these take are the whole numbers a `number` holds exactly, from `0` to `9007199254740991` (2<sup>53</sup> - 1),
which is 53 bits. A fraction, a negative number or a larger one is `E4-025`. There are no negative numbers, so a complement
is always taken in a width that you give. The language has no hexadecimal or binary literals; `math_from_base` reads them
from text, and `math_to_base` writes them. Each of these functions can also be called as a method:
`12.bit_and(10)`, `255.to_base(16)`, `"ff".from_base(16)`.

```cuff
use DLC:math
print(math_bit_and(12, 10), math_bit_or(12, 10), math_bit_xor(12, 10), math_bit_not(5, 4))
print(math_bit_shl(1, 10), math_bit_shr(1024, 3), math_bit_test(5, 2), 5.bit_test(1))
print(math_to_base(255, 16), math_to_base(5, 2, 8), math_from_base("FF", 16), "1010".from_base(2))
```

```output
8 14 6 10
1024 128 true false
ff 00000101 255 10
```

Setting and clearing a flag in an 8-bit register:

```cuff
use DLC:math
number status = math_from_base("10010110", 2)
number set = math_bit_or(status, math_bit_shl(1, 0))
number cleared = math_bit_and(set, math_bit_not(math_bit_shl(1, 7), 8))
print(math_to_base(set, 2, 8), math_to_base(cleared, 2, 8))
```

```output
10010111 00010111
```

`print` writes a number with at most 15 significant digits, so a number above 999999999999999 is shown in exponent form
with its last digits rounded. The number itself is exact; `math_to_base(x, 10)` writes all of its digits.

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
