#pragma once

#include "../common/SourceLocation.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Shared cursor state for all scanner modules.
    // Tracks position in the source, line/column for error reporting,
    // and whether the next token is preceded by whitespace.
    class ScanState
    {
    public:
        const std::string &source;
        int offset;
        int line;
        int column;
        bool pendingSpaceBefore;

        explicit ScanState(const std::string &src)
            : source(src), offset(0), line(1), column(1), pendingSpaceBefore(false) {}

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
            else
            {
                ++column;
            }
            return c;
        }

        SourceLocation here() const
        {
            return SourceLocation(line, column, offset);
        }

        // Skip inline whitespace (spaces/tabs/CR), tracking space-before flag.
        // Does NOT skip newlines — the main loop handles those.
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

} // namespace cuff
