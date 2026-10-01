#pragma once

#include "DLCCommon.h"

namespace cuff
{

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

} // namespace cuff
