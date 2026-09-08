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
                // 'f' not followed by '"' — not a string, signal failure
                return Token(TokenType::WORD, "", start);
            }
            isFString = true;
            s.advance(); // consume 'f'
        }

        char quote = static_cast<char>(s.peek());
        if (quote != '"' && quote != '\'')
        {
            return Token(TokenType::WORD, "", start);
        }
        // f-strings are only ever opened with a double quote (f'...' is not
        // part of the language) — a bare f followed by a single quote should
        // just fall through as if 'f' were an ordinary identifier character.
        if (isFString && quote != '"')
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
