#pragma once

#include <string>

namespace cuff
{

    // What a backslash escape inside a string literal stands for, appended to `out`. `esc` is the character after
    // the backslash. An escape that is not defined keeps its backslash, so "\q" is the two characters \q.
    //
    // One definition, used wherever escapes are read: the tokenizer decodes literals with it, and the parser decodes
    // them again to find where each character of an f-string came from (see LiteralParser.h), so the two cannot
    // disagree about what an escape means.
    inline void appendEscaped(std::string &out, int esc)
    {
        switch (esc)
        {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case '"':
            out += '"';
            break;
        case '\'':
            out += '\'';
            break;
        case '\\':
            out += '\\';
            break;
        default:
            out += '\\';
            out += static_cast<char>(esc);
            break;
        }
    }

}
