#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace cuff::numtext
{

    // strtod and printf read and write the decimal point of the C library's current locale, and a program that hosts
    // the engine may have chosen one that writes it as ',' (setlocale(LC_ALL, "") does, on a machine set to German).
    // A number in CuffScript is written with '.' in every place: source, to_number, JSON and output. These two read and
    // write it so, whatever the locale is, and cost nothing extra where the locale already uses '.'.

    inline bool isNumberChar(char c)
    {
        return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e' || c == 'E' || c == '.';
    }

    // The value of `text`, which the caller has already checked to be [sign] digits [. [digits]] [e [sign] digits].
    // errno is what strtod left in it, so a caller can tell a value too large for a double (ERANGE).
    inline double parse(const std::string &text)
    {
        errno = 0;
        char *end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (end == text.c_str() + text.size())
            return d;
        // strtod stopped at the '.', so the locale writes the point some other way. printf says which way.
        char probe[16];
        const int n = std::snprintf(probe, sizeof probe, "%.1f", 0.5);
        if (n < 3 || static_cast<size_t>(n) >= sizeof probe)
            return d;
        const std::string point(probe + 1, static_cast<size_t>(n - 2));
        std::string local = text;
        const size_t dot = local.find('.');
        if (dot == std::string::npos)
            return d;
        local.replace(dot, 1, point);
        errno = 0;
        return std::strtod(local.c_str(), nullptr);
    }

    // printf's "%.15g" of a finite `d` with '.' as the decimal point, into `buf` (40 bytes is always enough). Returns the
    // length of the text.
    inline size_t format15g(char *buf, size_t size, double d)
    {
        int n = std::snprintf(buf, size, "%.15g", d);
        if (n <= 0 || static_cast<size_t>(n) >= size)
            return 0;
        size_t len = static_cast<size_t>(n);
        for (size_t i = 0; i < len; ++i)
        {
            if (isNumberChar(buf[i]))
                continue;
            // The locale's decimal point, which may take more than one byte: it becomes '.'.
            size_t j = i + 1;
            while (j < len && !isNumberChar(buf[j]))
                ++j;
            buf[i] = '.';
            std::memmove(buf + i + 1, buf + j, len - j);
            len -= j - i - 1;
            buf[len] = '\0';
            break;
        }
        return len;
    }

}
