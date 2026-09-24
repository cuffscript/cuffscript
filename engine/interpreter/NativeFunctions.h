#pragma once

#include "Value.h"
#include "../common/Limits.h"
#include "../common/Utf8.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include "../net/HttpClient.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace cuff
{

    using NativeFn = std::function<Value(std::vector<Value> &, const SourceLocation &)>;

    inline void expectArgCount(const char *fn, std::vector<Value> &args, size_t n, const SourceLocation &loc)
    {
        if (args.size() != n)
            throw ArgumentError(std::string(fn) + "() expects " + std::to_string(n) + " argument(s), got " + std::to_string(args.size()), loc);
    }

    inline void expectArgRange(const char *fn, std::vector<Value> &args, size_t lo, size_t hi, const SourceLocation &loc)
    {
        if (args.size() < lo || args.size() > hi)
            throw ArgumentError(std::string(fn) + "() expects " + std::to_string(lo) + "-" + std::to_string(hi) + " argument(s), got " + std::to_string(args.size()), loc);
    }

    inline double expectNumber(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isNumber())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a number, got " + valueTypeName(args[i].type()), loc);
        return args[i].asNumber();
    }

    inline const std::string &expectStr(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isStr())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a str, got " + valueTypeName(args[i].type()), loc);
        return args[i].asStr();
    }

    inline const ValueList &expectList(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isList())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a list, got " + valueTypeName(args[i].type()), loc);
        return *args[i].asList();
    }

    inline const ValueMap &expectMap(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isMap())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a map, got " + valueTypeName(args[i].type()), loc);
        return *args[i].asMap();
    }

    // Whole numbers only, and only where a double is still exact (|n| <= 2^53).
    inline long long expectWhole(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        double d = expectNumber(fn, args, i, loc);
        if (!std::isfinite(d) || d != std::floor(d))
            throw ValueError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a whole number, got " + formatCuffNumber(d), loc);
        if (std::fabs(d) > 9007199254740992.0)
            throw ValueError(std::string(fn) + "() argument " + std::to_string(i + 1) + " is outside the supported range, got " + formatCuffNumber(d), loc);
        return static_cast<long long>(d);
    }

    inline void ensureStringSize(size_t n, const SourceLocation &loc)
    {
        if (n > limits::kMaxStringBytes)
            throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "string exceeds the maximum allowed size", loc);
    }

    inline void ensureItemCount(size_t n, const SourceLocation &loc)
    {
        if (n > limits::kMaxCollectionItems)
            throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "collection exceeds the maximum allowed size", loc);
    }

    inline bool valuesEqual(const Value &a, const Value &b, const SourceLocation &)
    {
        return a.strictEquals(b);
    }

    inline double checkedResult(const char *fn, double result, const char *problem, const SourceLocation &loc)
    {
        if (!std::isfinite(result))
            throw ValueError(std::string(fn) + "() " + problem, loc);
        return result;
    }

    // ---- Text helpers (UTF-8) ----

    namespace textutil
    {

        // Decodes the codepoint starting at `pos`. Returns false for a malformed
        // sequence (callers then copy the byte through unchanged).
        inline bool decodeAt(const std::string &s, size_t pos, unsigned int &cp, size_t &len)
        {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c < 0x80)
            {
                cp = c;
                len = 1;
                return true;
            }
            size_t n = utf8::seqLen(c);
            len = 1;
            if (n < 2 || pos + n > s.size())
                return false;
            unsigned int v = c & (0xFFu >> (n + 1));
            for (size_t i = 1; i < n; ++i)
            {
                unsigned char cc = static_cast<unsigned char>(s[pos + i]);
                if ((cc & 0xC0) != 0x80)
                    return false;
                v = (v << 6) | (cc & 0x3Fu);
            }
            cp = v;
            len = n;
            return true;
        }

        inline void appendUtf8(unsigned int cp, std::string &out)
        {
            if (cp <= 0x7F)
                out += static_cast<char>(cp);
            else if (cp <= 0x7FF)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp <= 0xFFFF)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        // Simple one-to-one case mapping for Latin (incl. Latin-1 and Extended-A),
        // Greek and Cyrillic. Scripts without case (Korean, CJK, ...) pass through.
        inline unsigned int toUpperCp(unsigned int c)
        {
            if (c < 0x80)
                return (c >= 'a' && c <= 'z') ? c - 32 : c;
            if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
                return c - 32;
            if (c == 0xFF)
                return 0x178;
            if (c >= 0x100 && c <= 0x17F)
            {
                if (c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F)
                    return c;
                if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
                    return (c & 1) ? c - 1 : c;
                if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
                    return (c & 1) ? c : c - 1;
                return c;
            }
            if (c >= 0x3B1 && c <= 0x3C9)
                return c == 0x3C2 ? 0x3A3 : c - 32;
            if (c == 0x3CA || c == 0x3CB)
                return c - 32;
            if (c == 0x3AC)
                return 0x386;
            if (c >= 0x3AD && c <= 0x3AF)
                return c - 37;
            if (c == 0x3CC)
                return 0x38C;
            if (c == 0x3CD || c == 0x3CE)
                return c - 63;
            if (c >= 0x430 && c <= 0x44F)
                return c - 32;
            if (c >= 0x450 && c <= 0x45F)
                return c - 80;
            return c;
        }

        inline unsigned int toLowerCp(unsigned int c)
        {
            if (c < 0x80)
                return (c >= 'A' && c <= 'Z') ? c + 32 : c;
            if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
                return c + 32;
            if (c >= 0x100 && c <= 0x17F)
            {
                if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F)
                    return c;
                if (c == 0x178)
                    return 0xFF;
                if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
                    return (c & 1) ? c : c + 1;
                if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
                    return (c & 1) ? c + 1 : c;
                return c;
            }
            if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
                return c + 32;
            if (c == 0x3AA || c == 0x3AB)
                return c + 32;
            if (c == 0x386)
                return 0x3AC;
            if (c >= 0x388 && c <= 0x38A)
                return c + 37;
            if (c == 0x38C)
                return 0x3CC;
            if (c == 0x38E || c == 0x38F)
                return c + 63;
            if (c >= 0x410 && c <= 0x42F)
                return c + 32;
            if (c >= 0x400 && c <= 0x40F)
                return c + 80;
            return c;
        }

        inline Value mapCase(const Value &v, bool upper)
        {
            const StrData &sd = v.asStrData();
            const std::string &s = sd.text();
            std::string out;
            out.reserve(s.size());
            if (sd.ascii())
            {
                for (char ch : s)
                {
                    unsigned char c = static_cast<unsigned char>(ch);
                    out += static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
                }
                return Value::makeStr(std::move(out));
            }
            for (size_t i = 0; i < s.size();)
            {
                unsigned int cp;
                size_t len;
                if (decodeAt(s, i, cp, len))
                    appendUtf8(upper ? toUpperCp(cp) : toLowerCp(cp), out);
                else
                    out.append(s, i, len);
                i += len;
            }
            return Value::makeStr(std::move(out));
        }

        // Parses a plain decimal number: [sign] digits [. digits] [e[sign]digits].
        // Hex floats, "nan", "inf" and anything strtod would otherwise accept are rejected.
        inline bool parseDecimal(const std::string &text, double &out)
        {
            size_t b = 0, e = text.size();
            auto isWs = [](char c)
            { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
            while (b < e && isWs(text[b]))
                ++b;
            while (e > b && isWs(text[e - 1]))
                --e;
            size_t i = b;
            if (i < e && (text[i] == '+' || text[i] == '-'))
                ++i;
            size_t digits = 0;
            while (i < e && std::isdigit(static_cast<unsigned char>(text[i])))
                ++i, ++digits;
            if (i < e && text[i] == '.')
            {
                ++i;
                while (i < e && std::isdigit(static_cast<unsigned char>(text[i])))
                    ++i, ++digits;
            }
            if (digits == 0)
                return false;
            if (i < e && (text[i] == 'e' || text[i] == 'E'))
            {
                ++i;
                if (i < e && (text[i] == '+' || text[i] == '-'))
                    ++i;
                size_t expDigits = 0;
                while (i < e && std::isdigit(static_cast<unsigned char>(text[i])))
                    ++i, ++expDigits;
                if (expDigits == 0)
                    return false;
            }
            if (i != e)
                return false;
            std::string clean = text.substr(b, e - b);
            errno = 0;
            double d = std::strtod(clean.c_str(), nullptr);
            if (!std::isfinite(d))
                return false;
            out = d;
            return true;
        }

    } // namespace textutil

    // ---- Functions shared by several DLC libraries ----

    inline Value nativeLength(std::vector<Value> &args, const SourceLocation &loc)
    {
        expectArgCount("length", args, 1, loc);
        if (args[0].isStr())
            return Value::makeNumber(static_cast<double>(args[0].asStrData().codepoints()));
        if (args[0].isList())
            return Value::makeNumber(static_cast<double>(args[0].asList()->items.size()));
        if (args[0].isMap())
            return Value::makeNumber(static_cast<double>(args[0].asMap()->size()));
        throw TypeError("length() expects a str, list, or map, got " + valueTypeName(args[0].type()), loc);
    }

    // contains(str, str) is a substring test, contains(list, value) an element
    // test (structural equality), and contains(map, str) a key test.
    inline Value nativeContains(std::vector<Value> &args, const SourceLocation &loc)
    {
        expectArgCount("contains", args, 2, loc);
        if (args[0].isList())
        {
            for (const auto &item : args[0].asList()->items)
                if (valuesEqual(item, args[1], loc))
                    return Value::makeBool(true);
            return Value::makeBool(false);
        }
        if (args[0].isMap())
        {
            if (!args[1].isStr())
                throw TypeError("contains() on a map expects a str key, got " + valueTypeName(args[1].type()), loc);
            return Value::makeBool(args[0].asMap()->has(args[1].asStr()));
        }
        const std::string &hay = expectStr("contains", args, 0, loc);
        const std::string &needle = expectStr("contains", args, 1, loc);
        return Value::makeBool(hay.find(needle) != std::string::npos);
    }

    // 1-based position of the first match, or `empty` when there is none.
    inline Value nativeIndexOf(std::vector<Value> &args, const SourceLocation &loc)
    {
        expectArgCount("index_of", args, 2, loc);
        if (args[0].isList())
        {
            const auto &items = args[0].asList()->items;
            for (size_t i = 0; i < items.size(); ++i)
                if (valuesEqual(items[i], args[1], loc))
                    return Value::makeNumber(static_cast<double>(i + 1));
            return Value::makeEmpty();
        }
        const StrData &hay = args[0].isStr() ? args[0].asStrData()
                                             : (expectStr("index_of", args, 0, loc), args[0].asStrData());
        const std::string &needle = expectStr("index_of", args, 1, loc);
        size_t pos = hay.text().find(needle);
        if (pos == std::string::npos)
            return Value::makeEmpty();
        size_t cp = hay.ascii() ? pos : utf8::length(hay.text().substr(0, pos));
        return Value::makeNumber(static_cast<double>(cp + 1));
    }

    // ---- Always-available builtins ----

    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg);

    inline void registerBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["print"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i)
                    std::cout << ' ';
                if (args[i].isStr())
                    std::cout << args[i].asStr();
                else
                    std::cout << args[i].toDisplayString();
            }
            std::cout << '\n';
            return Value::makeEmpty();
        };

        reg["input"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("input", args, 0, 1, loc);
            if (!args.empty())
                std::cout << expectStr("input", args, 0, loc);
            std::string line;
            if (!std::getline(std::cin, line))
                return Value::makeStr(std::string());
            ensureStringSize(line.size(), loc);
            return Value::makeStr(std::move(line));
        };

        reg["type_of"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("type_of", args, 1, loc);
            return Value::makeStr(valueTypeName(args[0].type()));
        };

        // to_number/to_str/to_boolean are common enough to be core builtins
        // rather than requiring `use DLC:convert` first. `use DLC:convert`
        // still works — it just re-registers the same functions.
        registerConvertDLC(reg);
    }

    // ---- DLC:math ----
    inline void registerMathDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // Applies a one-argument function whose result must be a real number.
        auto unary = [&reg](const char *name, double (*fn)(double), const char *problem)
        {
            reg[name] = [name, fn, problem](std::vector<Value> &args, const SourceLocation &loc) -> Value
            {
                expectArgCount(name, args, 1, loc);
                double x = expectNumber(name, args, 0, loc);
                return Value::makeNumber(checkedResult(name, fn(x), problem, loc));
            };
        };

        reg["sqrt"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sqrt", args, 1, loc);
            double v = expectNumber("sqrt", args, 0, loc);
            if (v < 0)
                throw ValueError("sqrt() cannot take the square root of a negative number", loc);
            return Value::makeNumber(std::sqrt(v));
        };
        reg["abs"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("abs", args, 1, loc);
            return Value::makeNumber(std::fabs(expectNumber("abs", args, 0, loc)));
        };
        reg["pow"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("pow", args, 2, loc);
            double base = expectNumber("pow", args, 0, loc);
            double exponent = expectNumber("pow", args, 1, loc);
            if (base == 0.0 && exponent < 0)
                throw DivisionByZeroError("pow() cannot raise 0 to a negative power (division by zero)", loc);
            if (base < 0 && exponent != std::floor(exponent))
                throw ValueError("pow() cannot raise a negative number to a fractional power", loc);
            double r = std::pow(base, exponent);
            if (std::isfinite(base) && std::isfinite(exponent) && !std::isfinite(r))
                throw ValueError("pow() result is too large to represent", loc);
            return Value::makeNumber(r);
        };
        reg["round"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("round", args, 1, 2, loc);
            double x = expectNumber("round", args, 0, loc);
            if (args.size() == 1)
                return Value::makeNumber(std::round(x));
            long long digits = expectWhole("round", args, 1, loc);
            if (digits < 0 || digits > 15)
                throw ValueError("round()'s digits must be a whole number from 0 to 15", loc);
            if (!std::isfinite(x))
                return Value::makeNumber(x);
            double scale = std::pow(10.0, static_cast<double>(digits));
            double scaled = x * scale;
            if (!std::isfinite(scaled))
                return Value::makeNumber(x);
            return Value::makeNumber(std::round(scaled) / scale);
        };
        reg["floor"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("floor", args, 1, loc);
            return Value::makeNumber(std::floor(expectNumber("floor", args, 0, loc)));
        };
        reg["ceil"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("ceil", args, 1, loc);
            return Value::makeNumber(std::ceil(expectNumber("ceil", args, 0, loc)));
        };
        reg["trunc"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("trunc", args, 1, loc);
            return Value::makeNumber(std::trunc(expectNumber("trunc", args, 0, loc)));
        };
        reg["sign"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sign", args, 1, loc);
            double x = expectNumber("sign", args, 0, loc);
            if (std::isnan(x))
                throw ValueError("sign() cannot take NaN", loc);
            return Value::makeNumber(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0));
        };

        // min/max accept either separate numbers or a single list of numbers.
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
        extremum("min", false);
        extremum("max", true);

        reg["clamp"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("clamp", args, 3, loc);
            double x = expectNumber("clamp", args, 0, loc);
            double lo = expectNumber("clamp", args, 1, loc);
            double hi = expectNumber("clamp", args, 2, loc);
            if (std::isnan(x) || std::isnan(lo) || std::isnan(hi))
                throw ValueError("clamp() cannot take NaN", loc);
            if (lo > hi)
                throw ValueError("clamp() expects the lower bound to be <= the upper bound", loc);
            return Value::makeNumber(std::min(std::max(x, lo), hi));
        };

        // Floored modulo: the result takes the sign of the divisor (like Python's %).
        reg["mod"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("mod", args, 2, loc);
            double a = expectNumber("mod", args, 0, loc);
            double b = expectNumber("mod", args, 1, loc);
            if (b == 0.0)
                throw DivisionByZeroError("mod() cannot divide by zero", loc);
            double r = std::fmod(a, b);
            if (r != 0.0 && ((r < 0) != (b < 0)))
                r += b;
            return Value::makeNumber(checkedResult("mod", r, "result is not a finite number", loc));
        };

        reg["log"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("log", args, 1, 2, loc);
            double x = expectNumber("log", args, 0, loc);
            if (!(x > 0))
                throw ValueError("log() requires a positive number", loc);
            if (args.size() == 1)
                return Value::makeNumber(std::log(x));
            double base = expectNumber("log", args, 1, loc);
            if (!(base > 0) || base == 1.0)
                throw ValueError("log()'s base must be positive and not 1", loc);
            return Value::makeNumber(std::log(x) / std::log(base));
        };
        reg["log10"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("log10", args, 1, loc);
            double x = expectNumber("log10", args, 0, loc);
            if (!(x > 0))
                throw ValueError("log10() requires a positive number", loc);
            return Value::makeNumber(std::log10(x));
        };
        reg["log2"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("log2", args, 1, loc);
            double x = expectNumber("log2", args, 0, loc);
            if (!(x > 0))
                throw ValueError("log2() requires a positive number", loc);
            return Value::makeNumber(std::log2(x));
        };

        unary("exp", [](double x) { return std::exp(x); }, "result is too large to represent");
        unary("sin", [](double x) { return std::sin(x); }, "requires a finite number");
        unary("cos", [](double x) { return std::cos(x); }, "requires a finite number");
        unary("tan", [](double x) { return std::tan(x); }, "requires a finite number");
        unary("asin", [](double x) { return std::asin(x); }, "requires a number between -1 and 1");
        unary("acos", [](double x) { return std::acos(x); }, "requires a number between -1 and 1");
        unary("atan", [](double x) { return std::atan(x); }, "requires a number");

        reg["atan2"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("atan2", args, 2, loc);
            double y = expectNumber("atan2", args, 0, loc);
            double x = expectNumber("atan2", args, 1, loc);
            return Value::makeNumber(checkedResult("atan2", std::atan2(y, x), "requires finite numbers", loc));
        };
        reg["pi"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("pi", args, 0, loc);
            return Value::makeNumber(3.14159265358979323846);
        };
        reg["e"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("e", args, 0, loc);
            return Value::makeNumber(2.71828182845904523536);
        };
    }

    // ---- DLC:string ----
    inline void registerStringDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["upper"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("upper", args, 1, loc);
            expectStr("upper", args, 0, loc);
            return textutil::mapCase(args[0], true);
        };
        reg["lower"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("lower", args, 1, loc);
            expectStr("lower", args, 0, loc);
            return textutil::mapCase(args[0], false);
        };

        auto trimmer = [&reg](const char *name, bool front, bool back)
        {
            reg[name] = [name, front, back](std::vector<Value> &args, const SourceLocation &loc) -> Value
            {
                expectArgCount(name, args, 1, loc);
                const std::string &s = expectStr(name, args, 0, loc);
                size_t a = front ? s.find_first_not_of(" \t\r\n") : 0;
                if (a == std::string::npos)
                    return Value::makeStr(std::string());
                size_t b = back ? s.find_last_not_of(" \t\r\n") : s.size() - 1;
                if (s.empty() || (a == 0 && b + 1 == s.size()))
                    return args[0];
                return Value::makeStr(s.substr(a, b - a + 1));
            };
        };
        trimmer("trim", true, true);
        trimmer("trim_start", true, false);
        trimmer("trim_end", false, true);

        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;
        reg["index_of"] = nativeIndexOf;

        reg["starts_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("starts_with", args, 2, loc);
            const std::string &s = expectStr("starts_with", args, 0, loc);
            const std::string &pre = expectStr("starts_with", args, 1, loc);
            return Value::makeBool(s.compare(0, pre.size(), pre) == 0);
        };
        reg["ends_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("ends_with", args, 2, loc);
            const std::string &s = expectStr("ends_with", args, 0, loc);
            const std::string &suf = expectStr("ends_with", args, 1, loc);
            if (suf.size() > s.size())
                return Value::makeBool(false);
            return Value::makeBool(s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
        };

        reg["repeat_str"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("repeat_str", args, 2, loc);
            const std::string &s = expectStr("repeat_str", args, 0, loc);
            long long n = expectWhole("repeat_str", args, 1, loc);
            if (n < 0)
                throw ValueError("repeat_str() expects a count of 0 or more", loc);
            if (s.empty() || n == 0)
                return Value::makeStr(std::string());
            if (static_cast<unsigned long long>(n) > limits::kMaxStringBytes / s.size())
                throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "string exceeds the maximum allowed size", loc);
            std::string out;
            out.reserve(s.size() * static_cast<size_t>(n));
            for (long long i = 0; i < n; ++i)
                out += s;
            return Value::makeStr(std::move(out));
        };

        auto padder = [&reg](const char *name, bool left)
        {
            reg[name] = [name, left](std::vector<Value> &args, const SourceLocation &loc) -> Value
            {
                expectArgRange(name, args, 2, 3, loc);
                const StrData &sd = (expectStr(name, args, 0, loc), args[0].asStrData());
                long long width = expectWhole(name, args, 1, loc);
                std::string fill = " ";
                if (args.size() == 3)
                {
                    fill = expectStr(name, args, 2, loc);
                    if (fill.empty() || utf8::length(fill) != 1)
                        throw ValueError(std::string(name) + "()'s fill must be a single character", loc);
                }
                size_t have = sd.codepoints();
                if (width < 0 || static_cast<unsigned long long>(width) <= have)
                    return args[0];
                size_t pad = static_cast<size_t>(width) - have;
                if (pad > limits::kMaxStringBytes / fill.size())
                    throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "string exceeds the maximum allowed size", loc);
                ensureStringSize(sd.text().size() + pad * fill.size(), loc);
                std::string out;
                out.reserve(sd.text().size() + pad * fill.size());
                if (!left)
                    out += sd.text();
                for (size_t i = 0; i < pad; ++i)
                    out += fill;
                if (left)
                    out += sd.text();
                return Value::makeStr(std::move(out));
            };
        };
        padder("pad_left", true);
        padder("pad_right", false);

        reg["char_code"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("char_code", args, 1, loc);
            const std::string &s = expectStr("char_code", args, 0, loc);
            if (s.empty())
                throw ValueError("char_code() cannot take an empty string", loc);
            unsigned int cp;
            size_t len;
            if (!textutil::decodeAt(s, 0, cp, len))
                throw ValueError("char_code() found invalid UTF-8", loc);
            return Value::makeNumber(static_cast<double>(cp));
        };
        reg["from_char_code"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("from_char_code", args, 1, loc);
            long long cp = expectWhole("from_char_code", args, 0, loc);
            if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                throw ValueError("from_char_code() expects a valid Unicode code point (0-1114111, excluding surrogates)", loc);
            std::string out;
            textutil::appendUtf8(static_cast<unsigned int>(cp), out);
            return Value::makeStr(std::move(out));
        };
    }

    // ---- DLC:time ----
    inline void registerTimeDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto nowFn = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("now", args, 0, loc);
            auto now = std::chrono::system_clock::now().time_since_epoch();
            double secs = std::chrono::duration<double>(now).count();
            return Value::makeNumber(secs);
        };
        reg["now"] = nowFn;
        reg["timestamp"] = nowFn;
    }

    // ---- DLC:random ----
    inline void registerRandomDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // One generator shared by every registration, so `use DLC:random` in
        // several modules doesn't reseed it and random_seed() applies everywhere.
        static const std::shared_ptr<std::mt19937_64> rng =
            std::make_shared<std::mt19937_64>(std::random_device{}());

        reg["random"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random", args, 0, loc);
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::makeNumber(dist(*rng));
        };
        reg["random_int"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_int", args, 2, loc);
            long long lo = expectWhole("random_int", args, 0, loc);
            long long hi = expectWhole("random_int", args, 1, loc);
            if (lo > hi)
                throw ValueError("random_int() expects the first argument to be <= the second", loc);
            std::uniform_int_distribution<long long> dist(lo, hi);
            return Value::makeNumber(static_cast<double>(dist(*rng)));
        };
        reg["random_seed"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_seed", args, 1, loc);
            long long seed = expectWhole("random_seed", args, 0, loc);
            rng->seed(static_cast<unsigned long long>(seed));
            return Value::makeEmpty();
        };
        reg["choice"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("choice", args, 1, loc);
            const auto &items = expectList("choice", args, 0, loc).items;
            if (items.empty())
                throw ValueError("choice() cannot pick from an empty list", loc);
            std::uniform_int_distribution<size_t> dist(0, items.size() - 1);
            return items[dist(*rng)];
        };
        reg["shuffle"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("shuffle", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectList("shuffle", args, 0, loc).items;
            std::shuffle(out->items.begin(), out->items.end(), *rng);
            return Value::makeList(std::move(out));
        };
    }

    // ---- DLC:list ----
    // Functional-style helpers: all of these return a *new* list/value and
    // never mutate the argument, so they behave predictably regardless of
    // list's reference semantics (see Value.h) — no aliasing surprises from
    // calling a library function.
    inline void registerListDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;
        reg["index_of"] = nativeIndexOf;

        reg["sort"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sort", args, 1, loc);
            const auto &src = expectList("sort", args, 0, loc).items;
            auto out = std::make_shared<ValueList>();
            out->items = src;
            bool allNumbers = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isNumber(); });
            bool allStrings = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isStr(); });
            if (allNumbers)
            {
                if (std::any_of(src.begin(), src.end(), [](const Value &v)
                                { return std::isnan(v.asNumber()); }))
                    throw ValueError("sort() cannot order a list that contains NaN", loc);
                std::sort(out->items.begin(), out->items.end(), [](const Value &a, const Value &b)
                          { return a.asNumber() < b.asNumber(); });
            }
            else if (allStrings)
            {
                std::sort(out->items.begin(), out->items.end(), [](const Value &a, const Value &b)
                          { return a.asStr() < b.asStr(); });
            }
            else
            {
                throw TypeError("sort() requires a list of all numbers or all strings (mixed/other types aren't orderable)", loc);
            }
            return Value::makeList(std::move(out));
        };

        reg["reverse"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("reverse", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectList("reverse", args, 0, loc).items;
            std::reverse(out->items.begin(), out->items.end());
            return Value::makeList(std::move(out));
        };

        reg["join"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("join", args, 2, loc);
            const auto &items = expectList("join", args, 0, loc).items;
            const std::string &sep = expectStr("join", args, 1, loc);
            size_t total = items.empty() ? 0 : sep.size() * (items.size() - 1);
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (!items[i].isStr())
                    throw TypeError("join() requires every element to be a str (index " + std::to_string(i + 1) +
                                        " is a " + valueTypeName(items[i].type()) + ") — use convert:to_str() first",
                                    loc);
                total += items[i].asStr().size();
                ensureStringSize(total, loc);
            }
            std::string out;
            out.reserve(total);
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i)
                    out += sep;
                out += items[i].asStr();
            }
            return Value::makeStr(std::move(out));
        };

        reg["unique"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("unique", args, 1, loc);
            const auto &src = expectList("unique", args, 0, loc).items;
            auto out = std::make_shared<ValueList>();
            bool allNumbers = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isNumber(); });
            bool allStrings = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isStr(); });
            if (allNumbers)
            {
                std::unordered_set<double> seen;
                for (const auto &v : src)
                    if (seen.insert(v.asNumber()).second)
                        out->items.push_back(v);
            }
            else if (allStrings)
            {
                std::unordered_set<std::string_view> seen;
                for (const auto &v : src)
                    if (seen.insert(std::string_view(v.asStr())).second)
                        out->items.push_back(v);
            }
            else
            {
                for (const auto &v : src)
                {
                    bool dup = std::any_of(out->items.begin(), out->items.end(), [&](const Value &existing)
                                           { return valuesEqual(existing, v, loc); });
                    if (!dup)
                        out->items.push_back(v);
                }
            }
            return Value::makeList(std::move(out));
        };

        reg["sum"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sum", args, 1, loc);
            double total = 0;
            for (const auto &v : expectList("sum", args, 0, loc).items)
            {
                if (!v.isNumber())
                    throw TypeError("sum() requires a list of numbers, found a " + valueTypeName(v.type()), loc);
                total += v.asNumber();
            }
            return Value::makeNumber(total);
        };

        reg["average"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("average", args, 1, loc);
            const auto &items = expectList("average", args, 0, loc).items;
            if (items.empty())
                throw ValueError("average() cannot take an empty list", loc);
            double total = 0;
            for (const auto &v : items)
            {
                if (!v.isNumber())
                    throw TypeError("average() requires a list of numbers, found a " + valueTypeName(v.type()), loc);
                total += v.asNumber();
            }
            return Value::makeNumber(total / static_cast<double>(items.size()));
        };

        // Expands one level of nesting; non-list elements are kept as they are.
        reg["flatten"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("flatten", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            for (const auto &v : expectList("flatten", args, 0, loc).items)
            {
                if (v.isList())
                {
                    ensureItemCount(out->items.size() + v.asList()->items.size(), loc);
                    out->items.insert(out->items.end(), v.asList()->items.begin(), v.asList()->items.end());
                }
                else
                {
                    ensureItemCount(out->items.size() + 1, loc);
                    out->items.push_back(v);
                }
            }
            return Value::makeList(std::move(out));
        };

        // Inclusive on both ends, like `loop repeat`; counts down when start > end.
        reg["range"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("range", args, 2, 3, loc);
            double start = expectNumber("range", args, 0, loc);
            double end = expectNumber("range", args, 1, loc);
            if (!std::isfinite(start) || !std::isfinite(end))
                throw ValueError("range() requires finite numbers", loc);
            double step = start <= end ? 1.0 : -1.0;
            if (args.size() == 3)
            {
                step = expectNumber("range", args, 2, loc);
                if (!std::isfinite(step) || step == 0.0)
                    throw ValueError("range()'s step must be a non-zero finite number", loc);
            }
            auto out = std::make_shared<ValueList>();
            if ((step > 0 && start > end) || (step < 0 && start < end))
                return Value::makeList(std::move(out));
            double count = std::floor((end - start) / step) + 1;
            if (!(count <= static_cast<double>(limits::kMaxCollectionItems)))
                throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "collection exceeds the maximum allowed size", loc);
            size_t n = static_cast<size_t>(count);
            out->items.reserve(n);
            for (size_t i = 0; i < n; ++i)
                out->items.push_back(Value::makeNumber(start + static_cast<double>(i) * step));
            return Value::makeList(std::move(out));
        };
    }

    // ---- DLC:map ----
    inline void registerMapDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;

        reg["keys"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("keys", args, 1, loc);
            const ValueMap &m = expectMap("keys", args, 0, loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(m.size());
            for (const auto &k : m.keys())
                out->items.push_back(Value::makeStr(k));
            return Value::makeList(std::move(out));
        };
        reg["values"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("values", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectMap("values", args, 0, loc).values();
            return Value::makeList(std::move(out));
        };
        reg["has_key"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("has_key", args, 2, loc);
            const ValueMap &m = expectMap("has_key", args, 0, loc);
            return Value::makeBool(m.has(expectStr("has_key", args, 1, loc)));
        };
        reg["entries"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("entries", args, 1, loc);
            const ValueMap &m = expectMap("entries", args, 0, loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(m.size());
            for (size_t i = 0; i < m.size(); ++i)
            {
                auto pair = std::make_shared<ValueList>();
                pair->items.push_back(Value::makeStr(m.keys()[i]));
                pair->items.push_back(m.values()[i]);
                out->items.push_back(Value::makeList(std::move(pair)));
            }
            return Value::makeList(std::move(out));
        };
        // Returns a new map; on duplicate keys the second map wins.
        reg["merge"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("merge", args, 2, loc);
            const ValueMap &a = expectMap("merge", args, 0, loc);
            const ValueMap &b = expectMap("merge", args, 1, loc);
            ensureItemCount(a.size() + b.size(), loc);
            auto out = std::make_shared<ValueMap>();
            out->reserve(a.size() + b.size());
            for (size_t i = 0; i < a.size(); ++i)
                out->set(a.keys()[i], a.values()[i]);
            for (size_t i = 0; i < b.size(); ++i)
                out->set(b.keys()[i], b.values()[i]);
            return Value::makeMap(std::move(out));
        };
    }

    // ---- DLC:convert ----
    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["to_number"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_number", args, 1, loc);
            const Value &v = args[0];
            if (v.isNumber())
                return v;
            if (v.isBool())
                return Value::makeNumber(v.asBool() ? 1.0 : 0.0);
            if (v.isStr())
            {
                // Only plain decimal text is accepted: partial parses like
                // "12abc" and forms like "nan", "inf" or hex floats would hide
                // bugs, so the whole string (minus surrounding whitespace)
                // must be an ordinary finite number.
                double d;
                if (!textutil::parseDecimal(v.asStr(), d))
                    throw ValueError("to_number() could not parse \"" + v.asStr() + "\" as a number", loc);
                return Value::makeNumber(d);
            }
            throw TypeError("to_number() cannot convert a " + valueTypeName(v.type()) + " to a number", loc);
        };

        reg["to_str"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_str", args, 1, loc);
            if (args[0].isStr())
                return args[0];
            return Value::makeStr(args[0].toDisplayString());
        };

        reg["to_boolean"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_boolean", args, 1, loc);
            return Value::makeBool(args[0].truthy());
        };
    }

    // ---- DLC:network ----
    // A real (plain-HTTP-only) client, backed by engine/net/HttpClient.h — see
    // that file for the socket-level implementation and the SSRF guard.
    struct NetworkDLCOptions
    {
        bool enabled = true;
        bool allowPrivateTargets = false;
    };

    inline Value makeHttpResultMap(const net::HttpResponse &resp)
    {
        auto m = std::make_shared<ValueMap>();
        m->set("status", Value::makeNumber(resp.status));
        m->set("ok", Value::makeBool(resp.status >= 200 && resp.status < 300));
        m->set("body", Value::makeStr(resp.body));
        return Value::makeMap(std::move(m));
    }

    [[noreturn]] inline void throwNetworkDisabled(const char *fn, const SourceLocation &loc)
    {
        throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                          std::string("DLC:network's ") + fn + "() is disabled for this run (network access was turned off by the host)",
                          loc, "the host embedding this engine controls this — see CuffEngine::Options::networkEnabled");
    }

    [[noreturn]] inline void throwNetworkFailed(const char *fn, const std::string &detail, const SourceLocation &loc)
    {
        throw CuffRuntimeError(ErrorCode::NetworkRequestFailed,
                               std::string(fn) + "() failed: " + detail, loc);
    }

    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg, NetworkDLCOptions opts)
    {
        reg["get"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("get", args, 1, loc);
            if (!opts.enabled)
                throwNetworkDisabled("get", loc);
            const std::string &url = expectStr("get", args, 0, loc);
            net::HttpOptions httpOpts;
            httpOpts.allowPrivateTargets = opts.allowPrivateTargets;
            net::HttpResponse resp = net::get(url, httpOpts);
            if (!resp.ok)
                throwNetworkFailed("get", resp.errorMessage, loc);
            return makeHttpResultMap(resp);
        };

        reg["post"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("post", args, 2, 3, loc);
            if (!opts.enabled)
                throwNetworkDisabled("post", loc);
            const std::string &url = expectStr("post", args, 0, loc);
            const std::string &body = expectStr("post", args, 1, loc);
            std::string contentType = args.size() == 3 ? expectStr("post", args, 2, loc) : std::string();
            net::HttpOptions httpOpts;
            httpOpts.allowPrivateTargets = opts.allowPrivateTargets;
            net::HttpResponse resp = net::post(url, body, contentType, httpOpts);
            if (!resp.ok)
                throwNetworkFailed("post", resp.errorMessage, loc);
            return makeHttpResultMap(resp);
        };
    }

    // ---- DLC:json ----
    // JSON maps onto CuffScript's value model almost exactly: object -> map,
    // array -> list, string/number/true/false/null -> str/number/boolean/empty.
    // Parsing is strict (RFC 8259): trailing commas, single quotes, unquoted
    // keys, and NaN/Infinity are all rejected, because silently accepting them
    // is how malformed data reaches production unnoticed.

    inline void jsonEscapeInto(const std::string &s, std::string &out)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20)
                {
                    static const char *hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                }
                else
                {
                    // UTF-8 bytes pass through unescaped — valid JSON, and it
                    // keeps Korean/emoji readable instead of \uXXXX soup.
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }

    inline void jsonNewlineIndent(std::string &out, int indentWidth, int depth)
    {
        if (indentWidth <= 0)
            return;
        out += '\n';
        out.append(static_cast<size_t>(indentWidth * depth), ' ');
    }

    inline void jsonStringifyInto(const Value &v, std::string &out, int indentWidth, int depth,
                                  const SourceLocation &loc)
    {
        if (out.size() > limits::kMaxStringBytes)
            throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "string exceeds the maximum allowed size", loc);
        switch (v.type())
        {
        case ValueType::Empty:
            out += "null";
            return;
        case ValueType::Boolean:
            out += v.asBool() ? "true" : "false";
            return;
        case ValueType::Number:
        {
            double d = v.asNumber();
            if (std::isnan(d) || std::isinf(d))
                throw ValueError("to_json() cannot serialize " + formatCuffNumber(d) + " (JSON has no NaN or Infinity)", loc);
            appendCuffNumber(out, d);
            return;
        }
        case ValueType::Str:
            jsonEscapeInto(v.asStr(), out);
            return;
        case ValueType::List:
        {
            const auto &items = v.asList()->items;
            if (items.empty()) { out += "[]"; return; }
            if (depth >= limits::kMaxJsonDepth)
                throw ValueError("to_json(): the value is nested too deeply or contains a circular reference", loc);
            out += '[';
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i) out += ',';
                jsonNewlineIndent(out, indentWidth, depth + 1);
                jsonStringifyInto(items[i], out, indentWidth, depth + 1, loc);
            }
            jsonNewlineIndent(out, indentWidth, depth);
            out += ']';
            return;
        }
        case ValueType::Map:
        {
            const auto &m = v.asMap();
            const auto &ks = m->keys();
            const auto &vs = m->values();
            if (ks.empty()) { out += "{}"; return; }
            if (depth >= limits::kMaxJsonDepth)
                throw ValueError("to_json(): the value is nested too deeply or contains a circular reference", loc);
            out += '{';
            for (size_t i = 0; i < ks.size(); ++i)
            {
                if (i) out += ',';
                jsonNewlineIndent(out, indentWidth, depth + 1);
                jsonEscapeInto(ks[i], out);
                out += ':';
                if (indentWidth > 0) out += ' ';
                jsonStringifyInto(vs[i], out, indentWidth, depth + 1, loc);
            }
            jsonNewlineIndent(out, indentWidth, depth);
            out += '}';
            return;
        }
        case ValueType::Match:
            throw TypeError("to_json() cannot serialize a match result", loc,
                            "pull the captures you need out of it first");
        }
    }

    class JsonParser
    {
    public:
        JsonParser(const std::string &text, const SourceLocation &loc) : t_(text), loc_(loc) {}

        Value parse()
        {
            skipWs();
            Value v = parseValue(0);
            skipWs();
            if (pos_ != t_.size())
                fail("unexpected trailing content after the JSON value");
            return v;
        }

    private:
        const std::string &t_;
        SourceLocation loc_;
        size_t pos_ = 0;
        static constexpr int kMaxDepth = limits::kMaxJsonDepth;

        [[noreturn]] void fail(const std::string &msg)
        {
            throw ValueError("from_json(): " + msg + " (at offset " + std::to_string(pos_) + ")", loc_);
        }

        bool atEnd() const { return pos_ >= t_.size(); }
        char peek() const { return pos_ < t_.size() ? t_[pos_] : '\0'; }

        void skipWs()
        {
            while (!atEnd())
            {
                char c = t_[pos_];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
                else break;
            }
        }

        void expect(char c)
        {
            if (atEnd() || t_[pos_] != c)
                fail(std::string("expected '") + c + "'");
            ++pos_;
        }

        Value parseValue(int depth)
        {
            if (depth > kMaxDepth)
                fail("JSON nested too deeply");
            if (atEnd())
                fail("unexpected end of input");
            char c = peek();
            if (c == '{') return parseObject(depth);
            if (c == '[') return parseArray(depth);
            if (c == '"') return Value::makeStr(parseString());
            if (c == 't') { expectWord("true"); return Value::makeBool(true); }
            if (c == 'f') { expectWord("false"); return Value::makeBool(false); }
            if (c == 'n') { expectWord("null"); return Value::makeEmpty(); }
            if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
            fail(std::string("unexpected character '") + c + "'");
        }

        void expectWord(const char *w)
        {
            size_t n = std::strlen(w);
            if (t_.compare(pos_, n, w) != 0)
                fail(std::string("expected '") + w + "'");
            pos_ += n;
        }

        Value parseObject(int depth)
        {
            expect('{');
            auto m = std::make_shared<ValueMap>();
            skipWs();
            if (peek() == '}') { ++pos_; return Value::makeMap(m); }
            while (true)
            {
                skipWs();
                if (peek() != '"')
                    fail("object keys must be double-quoted strings");
                std::string key = parseString();
                skipWs();
                expect(':');
                skipWs();
                m->set(key, parseValue(depth + 1));
                skipWs();
                if (peek() == ',') { ++pos_; continue; }
                if (peek() == '}') { ++pos_; break; }
                fail("expected ',' or '}' in object");
            }
            return Value::makeMap(m);
        }

        Value parseArray(int depth)
        {
            expect('[');
            auto l = std::make_shared<ValueList>();
            skipWs();
            if (peek() == ']') { ++pos_; return Value::makeList(l); }
            while (true)
            {
                skipWs();
                l->items.push_back(parseValue(depth + 1));
                skipWs();
                if (peek() == ',') { ++pos_; continue; }
                if (peek() == ']') { ++pos_; break; }
                fail("expected ',' or ']' in array");
            }
            return Value::makeList(l);
        }

        unsigned int parseHex4()
        {
            if (pos_ + 4 > t_.size())
                fail("incomplete \\u escape");
            unsigned int v = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = t_[pos_ + static_cast<size_t>(i)];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= static_cast<unsigned int>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned int>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned int>(c - 'A' + 10);
                else fail("invalid hex digit in \\u escape");
            }
            pos_ += 4;
            return v;
        }

        std::string parseString()
        {
            expect('"');
            std::string out;
            while (true)
            {
                if (atEnd())
                    fail("unterminated string");
                unsigned char c = static_cast<unsigned char>(t_[pos_]);
                if (c == '"') { ++pos_; break; }
                if (c < 0x20)
                    fail("raw control character in string (must be escaped)");
                if (c != '\\') { out += static_cast<char>(c); ++pos_; continue; }
                ++pos_;
                if (atEnd())
                    fail("unterminated escape sequence");
                char e = t_[pos_++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned int cp = parseHex4();
                    // Surrogate pair -> single codepoint, so \ud55c\uc544 style
                    // input round-trips to real UTF-8 rather than mojibake.
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < t_.size() &&
                        t_[pos_] == '\\' && t_[pos_ + 1] == 'u')
                    {
                        size_t save = pos_;
                        pos_ += 2;
                        unsigned int lo = parseHex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            pos_ = save;
                    }
                    textutil::appendUtf8(cp, out);
                    break;
                }
                default:
                    fail(std::string("invalid escape '\\") + e + "'");
                }
            }
            return out;
        }

        Value parseNumber()
        {
            size_t start = pos_;
            if (peek() == '-') ++pos_;
            if (atEnd() || !(peek() >= '0' && peek() <= '9'))
                fail("invalid number");
            // JSON forbids leading zeros ("01"), so accept "0" or [1-9][0-9]*
            if (peek() == '0') ++pos_;
            else while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            if (!atEnd() && peek() == '.')
            {
                ++pos_;
                if (atEnd() || !(peek() >= '0' && peek() <= '9'))
                    fail("digit expected after '.'");
                while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            }
            if (!atEnd() && (peek() == 'e' || peek() == 'E'))
            {
                ++pos_;
                if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
                if (atEnd() || !(peek() >= '0' && peek() <= '9'))
                    fail("digit expected in exponent");
                while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            }
            double d = std::strtod(t_.substr(start, pos_ - start).c_str(), nullptr);
            if (!std::isfinite(d))
                fail("number is out of range");
            return Value::makeNumber(d);
        }
    };

    inline void registerJsonDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["to_json"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("to_json", args, 1, 2, loc);
            int indent = 0;
            if (args.size() == 2)
            {
                double d = expectNumber("to_json", args, 1, loc);
                if (d != std::floor(d) || d < 0 || d > 10)
                    throw ValueError("to_json()'s indent must be a whole number from 0 to 10", loc);
                indent = static_cast<int>(d);
            }
            std::string out;
            jsonStringifyInto(args[0], out, indent, 0, loc);
            return Value::makeStr(std::move(out));
        };

        reg["from_json"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("from_json", args, 1, loc);
            const std::string &text = expectStr("from_json", args, 0, loc);
            JsonParser p(text, loc);
            return p.parse();
        };
    }

    // Dispatches `use DLC:<name>` to the right registration function.
    inline void registerDLC(const std::string &libName, std::unordered_map<std::string, NativeFn> &reg, const SourceLocation &loc,
                            NetworkDLCOptions networkOpts = NetworkDLCOptions())
    {
        if (libName == "math")
            registerMathDLC(reg);
        else if (libName == "string")
            registerStringDLC(reg);
        else if (libName == "time")
            registerTimeDLC(reg);
        else if (libName == "random")
            registerRandomDLC(reg);
        else if (libName == "list")
            registerListDLC(reg);
        else if (libName == "map")
            registerMapDLC(reg);
        else if (libName == "convert")
            registerConvertDLC(reg);
        else if (libName == "json")
            registerJsonDLC(reg);
        else if (libName == "network")
            registerNetworkDLC(reg, networkOpts);
        else
            throw ModuleError(ErrorCode::UnknownDLC, "unknown DLC library 'DLC:" + libName + "'", loc,
                               "available libraries: DLC:math, DLC:string, DLC:time, DLC:random, DLC:list, DLC:map, DLC:convert, DLC:json, DLC:network");
    }

} // namespace cuff
