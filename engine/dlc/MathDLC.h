#pragma once

#include "DLCCommon.h"

#include <cmath>

namespace cuff
{

    inline void registerMathDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto unary = [&reg](const char *name, double (*fn)(double), const char *problem)
        {
            reg[name] = [name, fn, problem](std::vector<Value> &args, const SourceLocation &loc) -> Value
            {
                expectArgCount(name, args, 1, loc);
                double x = expectNumber(name, args, 0, loc);
                return Value::makeNumber(checkedResult(name, fn(x), problem, loc));
            };
        };

        reg["math_sqrt"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_sqrt", args, 1, loc);
            double v = expectNumber("math_sqrt", args, 0, loc);
            if (v < 0)
                throw ValueError("math_sqrt() cannot take the square root of a negative number", loc);
            return Value::makeNumber(std::sqrt(v));
        };
        reg["math_abs"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_abs", args, 1, loc);
            return Value::makeNumber(std::fabs(expectNumber("math_abs", args, 0, loc)));
        };
        reg["math_pow"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_pow", args, 2, loc);
            double base = expectNumber("math_pow", args, 0, loc);
            double exponent = expectNumber("math_pow", args, 1, loc);
            if (base == 0.0 && exponent < 0)
                throw DivisionByZeroError("math_pow() cannot raise 0 to a negative power (division by zero)", loc);
            if (base < 0 && exponent != std::floor(exponent))
                throw ValueError("math_pow() cannot raise a negative number to a fractional power", loc);
            double r = std::pow(base, exponent);
            if (std::isfinite(base) && std::isfinite(exponent) && !std::isfinite(r))
                throw ValueError("math_pow() result is too large to represent", loc);
            return Value::makeNumber(r);
        };
        reg["math_round"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("math_round", args, 1, 2, loc);
            double x = expectNumber("math_round", args, 0, loc);
            if (args.size() == 1)
                return Value::makeNumber(std::round(x));
            long long digits = expectWhole("math_round", args, 1, loc);
            if (digits < 0 || digits > 15)
                throw ValueError("math_round()'s digits must be a whole number from 0 to 15", loc);
            if (!std::isfinite(x))
                return Value::makeNumber(x);
            double scale = std::pow(10.0, static_cast<double>(digits));
            double scaled = x * scale;
            if (!std::isfinite(scaled))
                return Value::makeNumber(x);
            return Value::makeNumber(std::round(scaled) / scale);
        };
        reg["math_floor"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_floor", args, 1, loc);
            return Value::makeNumber(std::floor(expectNumber("math_floor", args, 0, loc)));
        };
        reg["math_ceil"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_ceil", args, 1, loc);
            return Value::makeNumber(std::ceil(expectNumber("math_ceil", args, 0, loc)));
        };
        reg["math_trunc"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_trunc", args, 1, loc);
            return Value::makeNumber(std::trunc(expectNumber("math_trunc", args, 0, loc)));
        };
        reg["math_sign"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_sign", args, 1, loc);
            double x = expectNumber("math_sign", args, 0, loc);
            if (std::isnan(x))
                throw ValueError("math_sign() cannot take NaN", loc);
            return Value::makeNumber(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0));
        };

        auto extremum = [&reg](const char *name, bool wantMax)
        {
            reg[name] = [name, wantMax](std::vector<Value> &args, const SourceLocation &loc) -> Value
            {
                if (args.empty())
                    throw ArgumentError(std::string(name) + "() expects at least 1 argument", loc);
                std::vector<double> nums;
                if (args.size() == 1 && args[0].isList())
                {
                    for (const auto &item : args[0].asList()->items)
                    {
                        if (!item.isNumber())
                            throw TypeError(std::string(name) + "() expects a list of numbers, found a " + valueTypeName(item.type()), loc);
                        nums.push_back(item.asNumber());
                    }
                    if (nums.empty())
                        throw ValueError(std::string(name) + "() cannot take an empty list", loc);
                }
                else
                {
                    for (size_t i = 0; i < args.size(); ++i)
                        nums.push_back(expectNumber(name, args, i, loc));
                }
                double best = nums[0];
                for (double n : nums)
                {
                    if (std::isnan(n))
                        throw ValueError(std::string(name) + "() cannot compare NaN", loc);
                    best = wantMax ? std::max(best, n) : std::min(best, n);
                }
                return Value::makeNumber(best);
            };
        };
        extremum("math_min", false);
        extremum("math_max", true);

        reg["math_clamp"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_clamp", args, 3, loc);
            double x = expectNumber("math_clamp", args, 0, loc);
            double lo = expectNumber("math_clamp", args, 1, loc);
            double hi = expectNumber("math_clamp", args, 2, loc);
            if (std::isnan(x) || std::isnan(lo) || std::isnan(hi))
                throw ValueError("math_clamp() cannot take NaN", loc);
            if (lo > hi)
                throw ValueError("math_clamp() expects the lower bound to be <= the upper bound", loc);
            return Value::makeNumber(std::min(std::max(x, lo), hi));
        };

        // Floored modulo: the result takes the sign of the divisor (like Python's %).
        reg["math_mod"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_mod", args, 2, loc);
            double a = expectNumber("math_mod", args, 0, loc);
            double b = expectNumber("math_mod", args, 1, loc);
            if (b == 0.0)
                throw DivisionByZeroError("math_mod() cannot divide by zero", loc);
            double r = std::fmod(a, b);
            if (r != 0.0 && ((r < 0) != (b < 0)))
                r += b;
            return Value::makeNumber(checkedResult("math_mod", r, "result is not a finite number", loc));
        };

        reg["math_log"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("math_log", args, 1, 2, loc);
            double x = expectNumber("math_log", args, 0, loc);
            if (!(x > 0))
                throw ValueError("math_log() requires a positive number", loc);
            if (args.size() == 1)
                return Value::makeNumber(std::log(x));
            double base = expectNumber("math_log", args, 1, loc);
            if (!(base > 0) || base == 1.0)
                throw ValueError("math_log()'s base must be positive and not 1", loc);
            return Value::makeNumber(std::log(x) / std::log(base));
        };
        reg["math_log10"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_log10", args, 1, loc);
            double x = expectNumber("math_log10", args, 0, loc);
            if (!(x > 0))
                throw ValueError("math_log10() requires a positive number", loc);
            return Value::makeNumber(std::log10(x));
        };
        reg["math_log2"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_log2", args, 1, loc);
            double x = expectNumber("math_log2", args, 0, loc);
            if (!(x > 0))
                throw ValueError("math_log2() requires a positive number", loc);
            return Value::makeNumber(std::log2(x));
        };

        unary("math_exp", [](double x) { return std::exp(x); }, "result is too large to represent");
        unary("math_sin", [](double x) { return std::sin(x); }, "requires a finite number");
        unary("math_cos", [](double x) { return std::cos(x); }, "requires a finite number");
        unary("math_tan", [](double x) { return std::tan(x); }, "requires a finite number");
        unary("math_asin", [](double x) { return std::asin(x); }, "requires a number between -1 and 1");
        unary("math_acos", [](double x) { return std::acos(x); }, "requires a number between -1 and 1");
        unary("math_atan", [](double x) { return std::atan(x); }, "requires a number");

        reg["math_atan2"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_atan2", args, 2, loc);
            double y = expectNumber("math_atan2", args, 0, loc);
            double x = expectNumber("math_atan2", args, 1, loc);
            return Value::makeNumber(checkedResult("math_atan2", std::atan2(y, x), "requires finite numbers", loc));
        };
        reg["math_pi"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_pi", args, 0, loc);
            return Value::makeNumber(3.14159265358979323846);
        };
        reg["math_e"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("math_e", args, 0, loc);
            return Value::makeNumber(2.71828182845904523536);
        };
    }

}
