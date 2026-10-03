#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace cuff::cli::text
{

    inline bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    inline std::string trim(std::string_view s)
    {
        size_t b = 0, e = s.size();
        while (b < e && isBlank(s[b]))
            ++b;
        while (e > b && isBlank(s[e - 1]))
            --e;
        return std::string(s.substr(b, e - b));
    }

    // Length of the well-formed UTF-8 sequence starting at s[i], or 0 if it is malformed.
    inline size_t utf8Sequence(std::string_view s, size_t i, char32_t &cp)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80)
        {
            cp = c;
            return 1;
        }
        size_t extra;
        char32_t minimum;
        if (c >= 0xC2 && c <= 0xDF)
        {
            extra = 1;
            cp = c & 0x1Fu;
            minimum = 0x80;
        }
        else if (c >= 0xE0 && c <= 0xEF)
        {
            extra = 2;
            cp = c & 0x0Fu;
            minimum = 0x800;
        }
        else if (c >= 0xF0 && c <= 0xF4)
        {
            extra = 3;
            cp = c & 0x07u;
            minimum = 0x10000;
        }
        else
            return 0;
        if (i + extra >= s.size())
            return 0;
        for (size_t k = 1; k <= extra; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0u) != 0x80u)
                return 0;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return 0;
        return extra + 1;
    }

    // C1 controls and bidi overrides can rewrite what a terminal shows.
    inline bool isRiskyCodePoint(char32_t cp)
    {
        return (cp >= 0x80 && cp <= 0x9F) || cp == 0x061C || cp == 0x200E || cp == 0x200F ||
               (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
    }

    inline void appendEscape(std::string &out, const char *prefix, unsigned value, int width)
    {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%s%0*x", prefix, width, value);
        out += buf;
    }

    // Makes arbitrary bytes safe to print on a terminal: control characters,
    // ESC sequences and malformed UTF-8 become visible escapes. When `quoted`,
    // the result is wrapped in double quotes with string-style escapes. At most
    // `maxBytes` of input are rendered.
    inline std::string escapeForDisplay(std::string_view s, bool quoted, size_t maxBytes)
    {
        std::string out;
        if (quoted)
            out += '"';
        const size_t limit = std::min(s.size(), maxBytes);
        size_t i = 0;
        while (i < limit)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\n')
            {
                out += quoted ? "\\n" : "\n";
                ++i;
            }
            else if (c == '\t')
            {
                out += quoted ? "\\t" : "\t";
                ++i;
            }
            else if (c == '\r')
            {
                out += "\\r";
                ++i;
            }
            else if (quoted && (c == '"' || c == '\\'))
            {
                out += '\\';
                out += static_cast<char>(c);
                ++i;
            }
            else if (c < 0x20 || c == 0x7F)
            {
                appendEscape(out, "\\x", c, 2);
                ++i;
            }
            else if (c < 0x80)
            {
                out += static_cast<char>(c);
                ++i;
            }
            else
            {
                char32_t cp = 0;
                const size_t len = utf8Sequence(s, i, cp);
                if (len == 0)
                {
                    appendEscape(out, "\\x", c, 2);
                    ++i;
                }
                else if (isRiskyCodePoint(cp))
                {
                    appendEscape(out, "\\u", static_cast<unsigned>(cp), 4);
                    i += len;
                }
                else
                {
                    out.append(s.data() + i, len);
                    i += len;
                }
            }
        }
        if (quoted)
            out += '"';
        if (i < s.size())
            out += "... (" + std::to_string(s.size() - i) + " more bytes)";
        return out;
    }

    inline std::string sanitize(std::string_view s, size_t maxBytes = 1u << 20)
    {
        return escapeForDisplay(s, false, maxBytes);
    }

} // namespace cuff::cli::text
