#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans a pattern literal p"..." and the flag letters that follow its closing quote.
    //
    // The text between the quotes is raw: it reaches the pattern parser exactly as written, except that \" stands
    // for a double quote. Any other backslash and the character after it are kept together, so a backslash never
    // escapes the closing quote unless it is written \".
    inline Token scanPattern(ScanState &s)
    {
        SourceLocation start = s.here();
        s.advance(); // p
        s.advance(); // opening quote

        std::string value;
        for (;;)
        {
            if (s.atEnd())
                throw SyntaxError("unterminated pattern literal", start);
            int c = s.advance();
            if (c == '"')
                break;
            if (c == '\\')
            {
                if (s.atEnd())
                    throw SyntaxError("unterminated pattern literal", start);
                int next = s.advance();
                if (next != '"')
                    value += '\\';
                value += static_cast<char>(next);
                continue;
            }
            value += static_cast<char>(c);
        }

        uint8_t flags = 0;
        if (isAlphaNum(s.peek()))
        {
            const SourceLocation flagsAt = s.here();
            std::string letters;
            while (isAlphaNum(s.peek()))
                letters += static_cast<char>(s.advance());
            for (char f : letters)
            {
                uint8_t bit = f == 'i' ? kPatternFlagIgnoreCase : f == 'm' ? kPatternFlagMultiline : 0;
                if (bit == 0 || (flags & bit))
                {
                    std::string hint = f == 'g' ? "there is no 'g' flag: find_all, replace_all and split work on every match"
                                                : "the flags are 'i' (ignore case) and 'm' (line anchors), each at most once";
                    throw SyntaxError(ErrorCode::UnexpectedToken,
                                      std::string("invalid pattern flag '") + f + "' after pattern literal", flagsAt, hint);
                }
                flags |= bit;
            }
        }

        Token tok(TokenType::PATTERN, std::move(value), start, s.pendingSpaceBefore);
        tok.patternFlags = flags;
        return tok;
    }

}
