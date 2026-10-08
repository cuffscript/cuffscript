#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "../common/Utf8.h"
#include "ScanState.h"
#include "CharUtils.h"
#include <string>

namespace cuff
{

    inline Token scanOperator(ScanState &s)
    {
        SourceLocation start = s.here();
        int c = s.peek();

        auto make = [&](TokenType tt, const char *label) -> Token
        {
            s.advance();
            return Token(tt, label, start, s.pendingSpaceBefore);
        };

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
            throw SyntaxError("unexpected '=' — CuffScript uses 'to' for assignment, not '='", start);
        default:
        {
            // Show the whole character: its first byte alone is not valid UTF-8.
            size_t len = utf8::seqLen(static_cast<unsigned char>(c));
            throw SyntaxError("Unexpected character '" + s.source.substr(static_cast<size_t>(s.offset), len) + "'", start);
        }
        }
    }

}
