#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans a string literal starting at current position.
    // Handles both regular "..." and f-strings f"...".
    // Escape sequences: \" \\ \n \t \r
    // Inside f-strings, {expr} blocks are preserved as raw text for the parser.
    inline Token scanString(ScanState &s)
    {
        SourceLocation start = s.here();
        bool isFString = false;

        if (s.peek() == 'f')
        {
            if (s.peek(1) != '"')
            {
                // 'f' not followed by '"' — not a string, signal failure
                return Token(TokenType::WORD, "", start);
            }
            isFString = true;
            s.advance(); // consume 'f'
        }

        if (s.peek() != '"')
        {
            return Token(TokenType::WORD, "", start);
        }

        s.advance(); // consume opening quote

        std::string value;
        int braceDepth = 0;

        while (!s.atEnd())
        {
            int c = s.peek();

            if (c == '\\' && !s.atEnd())
            {
                s.advance();
                int esc = s.advance();
                switch (esc)
                {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                default:
                    value += '\\';
                    value += static_cast<char>(esc);
                    break;
                }
                continue;
            }

            if (c == '"')
            {
                s.advance(); // consume closing quote
                TokenType tt = isFString ? TokenType::FSTRING : TokenType::STRING;
                return Token(tt, value, start, s.pendingSpaceBefore);
            }

            // Track brace depth in f-strings so embedded {expr} is not misread
            if (isFString)
            {
                if (c == '{')
                    ++braceDepth;
                else if (c == '}')
                {
                    if (braceDepth > 0)
                        --braceDepth;
                }
            }

            value += static_cast<char>(c);
            s.advance();
        }

        throw SyntaxError("Unterminated string literal", start);
    }

} // namespace cuff
