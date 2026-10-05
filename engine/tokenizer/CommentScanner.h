#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"

namespace cuff
{

    inline bool scanComment(ScanState &s)
    {
        if (s.peek(0) != 'n' || s.peek(1) != 'o' || s.peek(2) != 't' || s.peek(3) != 'e')
            return false;
        if (isAlphaNum(s.peek(4)))
            return false;

        for (int i = 0; i < 4; ++i)
            s.advance();

        if (s.peek() != ':')
        {
            throw SyntaxError("expected ':' after 'note'", s.here());
        }
        s.advance();

        while (!s.atEnd() && isSpace(s.peek()))
            s.advance();

        if (isNewline(s.peek()) || s.atEnd())
        {
            if (!s.atEnd())
                s.advance();

            while (!s.atEnd())
            {
                if (s.peek(0) == 'e' && s.peek(1) == 'n' && s.peek(2) == 'd' && s.peek(3) == 'n' && s.peek(4) == 'o' && s.peek(5) == 't' && s.peek(6) == 'e' && !isAlphaNum(s.peek(7)))
                {
                    for (int i = 0; i < 7; ++i)
                        s.advance();
                    s.pendingSpaceBefore = true;
                    return true;
                }
                s.advance();
            }
            throw SyntaxError("unterminated multi-line comment — missing 'endnote'", s.here());
        }

        while (!s.atEnd() && !isNewline(s.peek()))
        {
            s.advance();
        }
        s.pendingSpaceBefore = true;
        return true;
    }

}
