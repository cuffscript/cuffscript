#pragma once

#include "../common/Token.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans identifiers (alphanumeric words). No '!' prefix handling here —
    // '!' is a separate BANG operator token in CuffScript.
    inline Token scanIdentifier(ScanState &s)
    {
        SourceLocation start = s.here();
        std::string value;

        if (!isAlpha(s.peek()))
        {
            return Token(TokenType::WORD, "", start);
        }

        while (!s.atEnd() && isAlphaNum(s.peek()))
        {
            value += static_cast<char>(s.advance());
        }

        return Token(TokenType::WORD, value, start, s.pendingSpaceBefore);
    }

} // namespace cuff
