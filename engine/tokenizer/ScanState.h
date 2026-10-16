#pragma once

#include "../common/SourceLocation.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    class ScanState
    {
    public:
        const std::string &source;
        int offset;
        int line;
        int column;
        bool pendingSpaceBefore;

        explicit ScanState(const std::string &src)
            : source(src), offset(0), line(1), column(1), pendingSpaceBefore(false)
        {
            // A UTF-8 byte order mark (Windows editors add one) is not part of the program.
            if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
                static_cast<unsigned char>(src[1]) == 0xBB && static_cast<unsigned char>(src[2]) == 0xBF)
                offset = 3;
        }

        bool atEnd() const { return offset >= static_cast<int>(source.size()); }

        int peek(int ahead = 0) const
        {
            int idx = offset + ahead;
            if (idx >= static_cast<int>(source.size()))
                return -1;
            return static_cast<unsigned char>(source[idx]);
        }

        int advance()
        {
            if (atEnd())
                return -1;
            int c = source[offset++];
            if (c == '\n')
            {
                ++line;
                column = 1;
            }
            else if ((c & 0xC0) != 0x80)
            {
                ++column; // columns count characters: the continuation bytes of a multi-byte one do not
            }
            return c;
        }

        SourceLocation here() const
        {
            return SourceLocation(line, column, offset);
        }

        // Skips inline whitespace but leaves newlines for the tokenizer.
        void skipInlineWhitespace()
        {
            while (!atEnd())
            {
                int c = peek();
                if (isSpace(c))
                {
                    advance();
                    pendingSpaceBefore = true;
                }
                else
                {
                    break;
                }
            }
        }
    };

}
