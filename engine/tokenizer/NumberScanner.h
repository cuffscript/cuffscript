#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans a numeric literal: integers and decimals.
    // Examples: 25, 3.14, 9999
    inline Token scanNumber(ScanState &s)
    {
        SourceLocation start = s.here();
        std::string value;

        while (!s.atEnd() && isDigit(s.peek()))
        {
            value += static_cast<char>(s.advance());
        }

        // Decimal part
        if (s.peek() == '.' && isDigit(s.peek(1)))
        {
            value += static_cast<char>(s.advance()); // '.'
            while (!s.atEnd() && isDigit(s.peek()))
            {
                value += static_cast<char>(s.advance());
            }
        }

        if (value.empty())
        {
            throw SyntaxError("Invalid number literal", start);
        }

        return Token(TokenType::NUMBER, value, start, s.pendingSpaceBefore);
    }

} // namespace cuff
