#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    // Scans operators and delimiters.
    // Multi-char operators (>=, <=, or_else) checked before single-char ones.
    inline Token scanOperator(ScanState &s)
    {
        SourceLocation start = s.here();
        int c = s.peek();

        auto make = [&](TokenType tt, const char *label) -> Token
        {
            s.advance();
            return Token(tt, label, start, s.pendingSpaceBefore);
        };

        // Two-char operators first
        if (c == '>')
        {
            s.advance();
            if (s.peek() == '=')
            {
                s.advance();
                return Token(TokenType::GE, ">=", start, s.pendingSpaceBefore);
            }
            return Token(TokenType::GT, ">", start, s.pendingSpaceBefore);
        }
        if (c == '<')
        {
            s.advance();
            if (s.peek() == '=')
            {
                s.advance();
                return Token(TokenType::LE, "<=", start, s.pendingSpaceBefore);
            }
            return Token(TokenType::LT, "<", start, s.pendingSpaceBefore);
        }

        // or_else keyword — scanned here because '_' is not isAlpha
        // Actually '_' IS isAlpha, so "or_else" would be scanned as a WORD by scanIdentifier.
        // The lexer classifies it. No special handling needed here.

        switch (c)
        {
        case '+':
            return make(TokenType::PLUS, "+");
        case '-':
            return make(TokenType::MINUS, "-");
        case '*':
            return make(TokenType::STAR, "*");
        case '/':
            return make(TokenType::SLASH, "/");
        case '!':
            return make(TokenType::BANG, "!");
        case '(':
            return make(TokenType::LPAREN, "(");
        case ')':
            return make(TokenType::RPAREN, ")");
        case '[':
            return make(TokenType::LBRACKET, "[");
        case ']':
            return make(TokenType::RBRACKET, "]");
        case '{':
            return make(TokenType::LBRACE, "{");
        case '}':
            return make(TokenType::RBRACE, "}");
        case ',':
            return make(TokenType::COMMA, ",");
        case ':':
            return make(TokenType::COLON, ":");
        case '.':
            return make(TokenType::DOT, ".");
        case '~':
            return make(TokenType::TILDE, "~");
        case '=':
            // '=' is not used as assignment in CuffScript (uses 'to'),
            // but >= and <= are handled above. A standalone '=' is an error.
            throw SyntaxError("Unexpected '=' — CuffScript uses 'to' for assignment, not '='", start);
        default:
            throw SyntaxError(std::string("Unexpected character '") + static_cast<char>(c) + "'", start);
        }
    }

} // namespace cuff
