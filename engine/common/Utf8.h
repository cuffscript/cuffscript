#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace cuff::utf8
{

    inline size_t seqLen(unsigned char c)
    {
        if ((c & 0x80) == 0x00) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 1; // stray continuation byte — advance by 1 to stay in sync
    }

    // Byte offset of each codepoint start, plus a trailing sentinel == s.size().
    // boundaries.size() - 1 is the codepoint count; boundaries[i]..boundaries[i+1]
    // is the byte range of codepoint i.
    inline std::vector<size_t> boundaries(const std::string &s)
    {
        std::vector<size_t> b;
        size_t i = 0;
        while (i < s.size())
        {
            b.push_back(i);
            size_t len = seqLen(static_cast<unsigned char>(s[i]));
            i += len;
            if (i > s.size())
                i = s.size();
        }
        b.push_back(s.size());
        return b;
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

} // namespace cuff::utf8
