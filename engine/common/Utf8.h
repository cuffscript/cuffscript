#pragma once

#include <string>
#include <cstddef>

namespace cuff::utf8
{

    // A stray continuation or invalid lead byte counts as one character, so scanning always advances.
    inline size_t seqLen(unsigned char c)
    {
        if ((c & 0x80) == 0x00) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 1;
    }

    inline size_t length(const std::string &s)
    {
        size_t count = 0;
        size_t i = 0;
        while (i < s.size())
        {
            i += seqLen(static_cast<unsigned char>(s[i]));
            ++count;
        }
        return count;
    }

    // Decodes the code point starting at s[i] and advances i past it (always by at least one byte, so a
    // scan over damaged text still ends). A malformed sequence yields U+FFFD for its first byte.
    inline char32_t decode(const std::string &s, size_t &i)
    {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        const size_t len = seqLen(lead);
        if (len == 1 || i + len > s.size())
        {
            ++i;
            return lead < 0x80 ? static_cast<char32_t>(lead) : U'\uFFFD';
        }
        char32_t cp = lead & (0xFFu >> (len + 1));
        for (size_t k = 1; k < len; ++k)
        {
            const unsigned char b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80)
            {
                ++i;
                return U'\uFFFD';
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        i += len;
        return cp;
    }

}
