#pragma once

#include "DLCCommon.h"

#include <algorithm>
#include <cctype>

namespace cuff
{

    // ---- DLC:string ----
    inline void registerStringDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["str_upper"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_upper", args, 1, loc);
            expectStr("str_upper", args, 0, loc);
            return textutil::mapCase(args[0], true);
        };
        reg["str_lower"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_lower", args, 1, loc);
            expectStr("str_lower", args, 0, loc);
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
        trimmer("str_trim", true, true);
        trimmer("str_trim_start", true, false);
        trimmer("str_trim_end", false, true);

        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;
        reg["index_of"] = nativeIndexOf;

        reg["str_starts_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_starts_with", args, 2, loc);
            const std::string &s = expectStr("str_starts_with", args, 0, loc);
            const std::string &pre = expectStr("str_starts_with", args, 1, loc);
            return Value::makeBool(s.compare(0, pre.size(), pre) == 0);
        };
        reg["str_ends_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_ends_with", args, 2, loc);
            const std::string &s = expectStr("str_ends_with", args, 0, loc);
            const std::string &suf = expectStr("str_ends_with", args, 1, loc);
            if (suf.size() > s.size())
                return Value::makeBool(false);
            return Value::makeBool(s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
        };

        reg["str_repeat"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_repeat", args, 2, loc);
            const std::string &s = expectStr("str_repeat", args, 0, loc);
            long long n = expectWhole("str_repeat", args, 1, loc);
            if (n < 0)
                throw ValueError("str_repeat() expects a count of 0 or more", loc);
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
        padder("str_pad_left", true);
        padder("str_pad_right", false);

        reg["str_char_code"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_char_code", args, 1, loc);
            const std::string &s = expectStr("str_char_code", args, 0, loc);
            if (s.empty())
                throw ValueError("str_char_code() cannot take an empty string", loc);
            unsigned int cp;
            size_t len;
            if (!textutil::decodeAt(s, 0, cp, len))
                throw ValueError("str_char_code() found invalid UTF-8", loc);
            return Value::makeNumber(static_cast<double>(cp));
        };
        reg["str_from_char_code"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("str_from_char_code", args, 1, loc);
            long long cp = expectWhole("str_from_char_code", args, 0, loc);
            if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                throw ValueError("str_from_char_code() expects a valid Unicode code point (0-1114111, excluding surrogates)", loc);
            std::string out;
            textutil::appendUtf8(static_cast<unsigned int>(cp), out);
            return Value::makeStr(std::move(out));
        };
    }

} // namespace cuff
