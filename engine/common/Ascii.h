#pragma once

namespace cuff::ascii
{

    // The C library's toupper, tolower and isalnum follow the locale of the process, which a program that hosts the
    // engine may have set: in a Turkish locale toupper('i') is not 'I', and in a Latin-1 locale isalnum() is true for
    // bytes that are part of a UTF-8 character. What the engine means by these is ASCII, in every locale.

    inline bool isDigit(unsigned char c)
    {
        return c >= '0' && c <= '9';
    }

    inline bool isLetter(unsigned char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    inline bool isLetterOrDigit(unsigned char c)
    {
        return isLetter(c) || isDigit(c);
    }

    inline char upper(char c)
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }

    inline char lower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

}
