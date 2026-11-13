#pragma once

#include <cmath>
#include <cstdint>

namespace cuff::arith
{

    // Floored modulo: the result takes the sign of the divisor (like Python's %). The divisor must not be 0.
    // The `%` operator and math_mod() share it.
    inline double floorMod(double dividend, double divisor)
    {
        // Two whole numbers below 2^53 are exact as doubles and as 64-bit integers, and the integer remainder is the
        // value fmod gives (down to the sign of a zero result, which is the dividend's) at a fraction of its cost.
        // Counting loops and `n % 2` tests take this route.
        constexpr double kExactWhole = 9007199254740992.0;
        if (std::fabs(dividend) < kExactWhole && std::fabs(divisor) < kExactWhole)
        {
            const int64_t a = static_cast<int64_t>(dividend);
            const int64_t b = static_cast<int64_t>(divisor);
            if (static_cast<double>(a) == dividend && static_cast<double>(b) == divisor)
            {
                int64_t r = a % b;
                if (r == 0)
                    return std::signbit(dividend) ? -0.0 : 0.0;
                if ((r < 0) != (b < 0))
                    r += b;
                return static_cast<double>(r);
            }
        }
        double r = std::fmod(dividend, divisor);
        if (r != 0.0 && ((r < 0) != (divisor < 0)))
            r += divisor;
        return r;
    }

    enum class PowStatus
    {
        Ok,
        ZeroToNegative,
        NegativeToFraction,
        TooLarge
    };

    // base raised to exponent, refusing what has no finite real value. The `**` operator and math_pow() share it.
    inline PowStatus power(double base, double exponent, double &result)
    {
        if (base == 0.0 && exponent < 0)
            return PowStatus::ZeroToNegative;
        if (base < 0 && exponent != std::floor(exponent))
            return PowStatus::NegativeToFraction;
        result = std::pow(base, exponent);
        if (std::isfinite(base) && std::isfinite(exponent) && !std::isfinite(result))
            return PowStatus::TooLarge;
        return PowStatus::Ok;
    }

}
