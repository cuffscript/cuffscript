#pragma once

#include "Value.h"
#include "../common/Utf8.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
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

    // ---- Always-available builtins ----

    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg);

    inline void registerBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["print"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i)
                    std::cout << " ";
                std::cout << args[i].toDisplayString();
            }
            std::cout << "\n";
            return Value::makeEmpty();
        };

        reg["input"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("input", args, 0, 1, loc);
            if (!args.empty())
                std::cout << expectStr("input", args, 0, loc);
            std::string line;
            if (!std::getline(std::cin, line))
                return Value::makeStr("");
            return Value::makeStr(line);
        };

        // to_number/to_str/to_boolean are common enough to be core builtins
        // rather than requiring `use DLC:convert` first. `use DLC:convert`
        // still works — it just re-registers the same functions.
        registerConvertDLC(reg);
    }

    // ---- DLC:math ----
    inline void registerMathDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
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
            return Value::makeNumber(std::pow(expectNumber("pow", args, 0, loc), expectNumber("pow", args, 1, loc)));
        };
        reg["round"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("round", args, 1, loc);
            return Value::makeNumber(std::round(expectNumber("round", args, 0, loc)));
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
        reg["min"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            if (args.empty())
                throw ArgumentError("min() expects at least 1 argument", loc);
            double best = expectNumber("min", args, 0, loc);
            for (size_t i = 1; i < args.size(); ++i)
                best = std::min(best, expectNumber("min", args, i, loc));
            return Value::makeNumber(best);
        };
        reg["max"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            if (args.empty())
                throw ArgumentError("max() expects at least 1 argument", loc);
            double best = expectNumber("max", args, 0, loc);
            for (size_t i = 1; i < args.size(); ++i)
                best = std::max(best, expectNumber("max", args, i, loc));
            return Value::makeNumber(best);
        };
    }

    // ---- DLC:string ----
    inline void registerStringDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["upper"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("upper", args, 1, loc);
            std::string s = expectStr("upper", args, 0, loc);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                            { return std::toupper(c); });
            return Value::makeStr(s);
        };
        reg["lower"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("lower", args, 1, loc);
            std::string s = expectStr("lower", args, 0, loc);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                            { return std::tolower(c); });
            return Value::makeStr(s);
        };
        reg["trim"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("trim", args, 1, loc);
            std::string s = expectStr("trim", args, 0, loc);
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return Value::makeStr("");
            size_t b = s.find_last_not_of(" \t\r\n");
            return Value::makeStr(s.substr(a, b - a + 1));
        };
        reg["length"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("length", args, 1, loc);
            if (args[0].isStr())
                return Value::makeNumber(static_cast<double>(utf8::length(args[0].asStr())));
            if (args[0].isList())
                return Value::makeNumber(static_cast<double>(args[0].asList()->items.size()));
            if (args[0].isMap())
                return Value::makeNumber(static_cast<double>(args[0].asMap()->size()));
            throw TypeError("length() expects a str, list, or map, got " + valueTypeName(args[0].type()), loc);
        };
        reg["contains"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("contains", args, 2, loc);
            const std::string &hay = expectStr("contains", args, 0, loc);
            const std::string &needle = expectStr("contains", args, 1, loc);
            return Value::makeBool(hay.find(needle) != std::string::npos);
        };
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
        auto rng = std::make_shared<std::mt19937>(std::random_device{}());
        reg["random"] = [rng](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random", args, 0, loc);
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::makeNumber(dist(*rng));
        };
        reg["random_int"] = [rng](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_int", args, 2, loc);
            long lo = static_cast<long>(expectNumber("random_int", args, 0, loc));
            long hi = static_cast<long>(expectNumber("random_int", args, 1, loc));
            if (lo > hi)
                throw ValueError("random_int() expects the first argument to be <= the second", loc);
            std::uniform_int_distribution<long> dist(lo, hi);
            return Value::makeNumber(static_cast<double>(dist(*rng)));
        };
    }

    // ---- DLC:list ----
    // Functional-style helpers: all of these return a *new* list/value and
    // never mutate the argument, so they behave predictably regardless of
    // list's reference semantics (see Value.h) — no aliasing surprises from
    // calling a library function.
    inline void registerListDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["sort"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sort", args, 1, loc);
            if (!args[0].isList())
                throw TypeError("sort() expects a list, got " + valueTypeName(args[0].type()), loc);
            auto out = std::make_shared<ValueList>();
            out->items = args[0].asList()->items;
            bool allNumbers = std::all_of(out->items.begin(), out->items.end(), [](const Value &v)
                                           { return v.isNumber(); });
            bool allStrings = std::all_of(out->items.begin(), out->items.end(), [](const Value &v)
                                           { return v.isStr(); });
            if (allNumbers)
            {
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
            return Value::makeList(out);
        };

        reg["reverse"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("reverse", args, 1, loc);
            if (!args[0].isList())
                throw TypeError("reverse() expects a list, got " + valueTypeName(args[0].type()), loc);
            auto out = std::make_shared<ValueList>();
            out->items = args[0].asList()->items;
            std::reverse(out->items.begin(), out->items.end());
            return Value::makeList(out);
        };

        reg["join"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("join", args, 2, loc);
            if (!args[0].isList())
                throw TypeError("join() expects a list as its first argument, got " + valueTypeName(args[0].type()), loc);
            const std::string &sep = expectStr("join", args, 1, loc);
            std::string out;
            const auto &items = args[0].asList()->items;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (!items[i].isStr())
                    throw TypeError("join() requires every element to be a str (index " + std::to_string(i + 1) +
                                        " is a " + valueTypeName(items[i].type()) + ") — use convert:to_str() first",
                                    loc);
                if (i)
                    out += sep;
                out += items[i].asStr();
            }
            return Value::makeStr(out);
        };

        reg["unique"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("unique", args, 1, loc);
            if (!args[0].isList())
                throw TypeError("unique() expects a list, got " + valueTypeName(args[0].type()), loc);
            auto out = std::make_shared<ValueList>();
            for (const auto &v : args[0].asList()->items)
            {
                bool seen = std::any_of(out->items.begin(), out->items.end(), [&](const Value &existing)
                                        { return existing.strictEquals(v); });
                if (!seen)
                    out->items.push_back(v);
            }
            return Value::makeList(out);
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
                const std::string &s = v.asStr();
                try
                {
                    size_t consumed = 0;
                    double d = std::stod(s, &consumed);
                    // Reject partial parses like "12abc" — silent truncation
                    // would hide bugs; require the whole string to be numeric
                    // (surrounding whitespace is tolerated).
                    while (consumed < s.size() && std::isspace(static_cast<unsigned char>(s[consumed])))
                        ++consumed;
                    if (consumed != s.size())
                        throw std::invalid_argument("trailing characters");
                    return Value::makeNumber(d);
                }
                catch (const std::exception &)
                {
                    throw ValueError("to_number() could not parse \"" + s + "\" as a number", loc);
                }
            }
            throw TypeError("to_number() cannot convert a " + valueTypeName(v.type()) + " to a number", loc);
        };

        reg["to_str"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_str", args, 1, loc);
            return Value::makeStr(args[0].toDisplayString());
        };

        reg["to_boolean"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_boolean", args, 1, loc);
            return Value::makeBool(args[0].truthy());
        };
    }

    // ---- DLC:network ----
    // Real network access is out of scope for this interpreter (no sandboxing
    // story for it yet). The module still loads successfully — `use
    // DLC:network` never fails by itself — but calling any of its functions
    // fails clearly, rather than pretending to succeed.
    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto stub = [](const std::string &name)
        {
            return [name](std::vector<Value> &, const SourceLocation &loc) -> Value
            {
                throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                                   "DLC:network's " + name + "() is not available in this interpreter (no network sandboxing implemented)",
                                   loc, "network access must be provided by the host embedding this engine");
            };
        };
        reg["fetch"] = stub("fetch");
        reg["get"] = stub("get");
        reg["post"] = stub("post");
    }

    // Dispatches `use DLC:<name>` to the right registration function.
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

    inline void jsonStringifyInto(const Value &v, std::string &out, int indentWidth, int depth,
                                  const SourceLocation &loc);

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
            out += formatCuffNumber(d);
            return;
        }
        case ValueType::Str:
            jsonEscapeInto(v.asStr(), out);
            return;
        case ValueType::List:
        {
            const auto &items = v.asList()->items;
            if (items.empty()) { out += "[]"; return; }
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
            if (ks.empty()) { out += "{}"; return; }
            out += '{';
            for (size_t i = 0; i < ks.size(); ++i)
            {
                if (i) out += ',';
                jsonNewlineIndent(out, indentWidth, depth + 1);
                jsonEscapeInto(ks[i], out);
                out += ':';
                if (indentWidth > 0) out += ' ';
                jsonStringifyInto(*m->get(ks[i]), out, indentWidth, depth + 1, loc);
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
        static constexpr int kMaxDepth = 200;

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

        void appendUtf8(unsigned int cp, std::string &out)
        {
            if (cp <= 0x7F) out += static_cast<char>(cp);
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
                    appendUtf8(cp, out);
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
            return Value::makeNumber(std::stod(t_.substr(start, pos_ - start)));
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
            return Value::makeStr(out);
        };

        reg["from_json"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("from_json", args, 1, loc);
            const std::string &text = expectStr("from_json", args, 0, loc);
            JsonParser p(text, loc);
            return p.parse();
        };
    }

    inline void registerDLC(const std::string &libName, std::unordered_map<std::string, NativeFn> &reg, const SourceLocation &loc)
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
        else if (libName == "convert")
            registerConvertDLC(reg);
        else if (libName == "json")
            registerJsonDLC(reg);
        else if (libName == "network")
            registerNetworkDLC(reg);
        else
            throw ModuleError(ErrorCode::UnknownDLC, "unknown DLC library 'DLC:" + libName + "'", loc,
                               "available libraries: DLC:math, DLC:string, DLC:time, DLC:random, DLC:list, DLC:convert, DLC:json, DLC:network");
    }

} // namespace cuff
