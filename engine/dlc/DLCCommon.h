#pragma once

#include "../interpreter/Value.h"
#include "../common/Limits.h"
#include "../common/Utf8.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>
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

    namespace textutil
    {

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

    }

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

}
