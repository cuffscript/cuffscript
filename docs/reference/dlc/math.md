# `DLC:math`

```cuff
use DLC:math
```

Numbers in CuffScript are double-precision floating point. The functions below take and return numbers.

| Function | Result |
| :--- | :--- |
| `math_sqrt(x)` | The square root. A negative `x` is `E4-025`. |
| `math_abs(x)` | The absolute value. |
| `math_pow(base, exponent)` | `base` to the power `exponent`, the same as `base ** exponent`. `0` to a negative power is `E4-006`; a negative base with a fractional exponent, or a result too large to represent, is `E4-025`. |
| `math_round(x [, digits])` | Rounds to `digits` decimal places (0 to 15, default 0). Halves round away from zero. |
| `math_floor(x)`, `math_ceil(x)` | Round down, round up. |
| `math_trunc(x)` | Rounds toward zero. |
| `math_sign(x)` | `-1`, `0` or `1`. |
| `math_min(a, b, ...)`, `math_max(a, b, ...)` | The smallest or largest of one or more numbers, or of the numbers in a single list. |
| `math_clamp(value, low, high)` | `value`, kept between `low` and `high`. |
| `math_mod(a, b)` | The remainder of `a / b`, the same as `a % b`. The result takes the sign of `b`. |
| `math_log(x [, base])` | The natural logarithm, or the logarithm to `base`. `x` must be positive. |
| `math_log2(x)`, `math_log10(x)` | Base-2 and base-10 logarithms. |
| `math_exp(x)` | `e` to the power `x`. |
| `math_sin(x)`, `math_cos(x)`, `math_tan(x)` | Trigonometry, in radians. |
| `math_asin(x)`, `math_acos(x)`, `math_atan(x)` | The inverse functions, giving radians. |
| `math_atan2(y, x)` | The angle of the point `(x, y)`, in radians. |
| `math_pi()`, `math_e()` | The constants. |

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

A result that is not a finite number (for example the logarithm of zero) is reported as an error rather than
returned as `inf` or `nan`, so a program never has to test for them.

```cuff error E4-025
use DLC:math
print(math_sqrt(-1))
```
