#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans a string literal starting at current position.
    // Handles regular "...", single-quoted '...', and f-strings f"...".
    // Escape sequences: \" \' \\ \n \t \r
    // Inside f-strings, {expr} blocks are preserved as raw text for the parser.
    //
    // Single-quoted strings exist specifically so that an embedded expression
    // inside an f-string (e.g. f"year: {res['year']}") can contain a nested
    // string literal without prematurely closing the outer f-string: the
    // f-string scanner only watches for an unescaped '"', so a '...' literal
    // used *inside* {...} passes straight through as ordinary text and is
    // re-tokenized correctly later when the embedded expression is re-parsed.
    inline Token scanString(ScanState &s)
    {
        SourceLocation start = s.here();
        bool isFString = false;

        if (s.peek() == 'f')
        {
            if (s.peek(1) != '"')
            {
                return Token(TokenType::WORD, "", start);
            }
            isFString = true;
            s.advance();
        }

        char quote = static_cast<char>(s.peek());
        if (quote != '"' && quote != '\'')
        {
            return Token(TokenType::WORD, "", start);
        }
        // f-strings open only with a double quote; f'...' falls through as an identifier character.
        if (isFString && quote != '"')
        {
            return Token(TokenType::WORD, "", start);
        }

        s.advance();

        std::string value;

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
                case '\'':
                    value += '\'';
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

            if (c == quote)
            {
                s.advance();
                TokenType tt = isFString ? TokenType::FSTRING : TokenType::STRING;
                return Token(tt, value, start, s.pendingSpaceBefore);
            }

            value += static_cast<char>(c);
            s.advance();
        }

        throw SyntaxError("unterminated string literal", start);
    }

}
