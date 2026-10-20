#pragma once

#include <cmath>

namespace cuff::arith
{

    // Floored modulo: the result takes the sign of the divisor (like Python's %). The divisor must not be 0.
    // The `%` operator and math_mod() share it.
    inline double floorMod(double dividend, double divisor)
    {
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
