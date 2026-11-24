#pragma once

#include <filesystem>
#include <string>

namespace cuff
{
    // A path that the host hands the engine as a string: the script's folder (`scriptDir`) and Engine::Options::rootDir.
    // It is UTF-8, as every other string of the engine is. On Windows, a string that is not UTF-8 is read in the ANSI
    // code page instead, as every such path was before 6.2.0, so a host that still passes one keeps working.
    inline std::filesystem::path hostPath(const std::string &text)
    {
#if defined(_WIN32)
        // Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
        const auto *s = reinterpret_cast<const unsigned char *>(text.data());
        const size_t n = text.size();
        bool utf8 = true;
        for (size_t i = 0; i < n && utf8;)
        {
            const unsigned char c = s[i];
            size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
            if (len == 0 || i + len > n)
            {
                utf8 = false;
                break;
            }
            for (size_t k = 1; k < len; ++k)
                if ((s[i + k] & 0xC0) != 0x80)
                    utf8 = false;
            if (utf8 && len == 3 && ((c == 0xE0 && s[i + 1] < 0xA0) || (c == 0xED && s[i + 1] > 0x9F)))
                utf8 = false;
            if (utf8 && len == 4 && ((c == 0xF0 && s[i + 1] < 0x90) || (c == 0xF4 && s[i + 1] > 0x8F)))
                utf8 = false;
            i += len;
        }
        return utf8 ? std::filesystem::u8path(text) : std::filesystem::path(text);
#else
        return std::filesystem::path(text);
#endif
    }

    // Component-wise containment check, not a string prefix test.
    inline bool isInsideRoot(const std::filesystem::path &path, const std::filesystem::path &root)
    {
        auto r = root.begin();
        auto p = path.begin();
        for (; r != root.end(); ++r, ++p)
        {
            if (r->empty())
                break;
            if (p == path.end() || *r != *p)
                return false;
        }
        return true;
    }
}
