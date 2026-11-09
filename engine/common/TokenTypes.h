#pragma once

#include <string>

namespace cuff
{

    // Raw tokens are classified into keywords and identifiers by the lexer.
    enum class TokenType
    {
        WORD,
        NUMBER,
        STRING,
        FSTRING,
        PATTERN,

        PLUS,  // +
        MINUS,  // -
        STAR,  // *
        SLASH,  // /
        PERCENT,  // %
        POWER,  // **
        GE,  // >=
        LE,  // <=
        GT,  // >
        LT,  // <
        BANG,  // ! (negation operator)

        LPAREN,  // (
        RPAREN,  // )
        LBRACKET,  // [
        RBRACKET,  // ]
        LBRACE,  // {
        RBRACE,  // }
        COMMA,  // ,
        COLON,  // :
        DOT,  // .
        TILDE,  // ~
        ASSIGN,  // =

        NEWLINE,
        INDENT,
        DEDENT,
        EOF_TOKEN,

        CONSTANT,
        IF,
        ELSE,
        LOOP,
        REPEAT,
        WHILE,
        EACH,
        DO,
        END,
        STOP,
        SKIP,
        RETURN,
        FAIL,
        AWAIT,
        USE,
        FROM,
        NOTE,
        ENDNOTE,
        EMPTY,
        ASYNC,
        PURE,
        PRIVATE,
        FUNCTION,
        CLASS,
        EXTENDS,
        WITH,
        SELF,
        SUPER,
        ADD,
        REMOVE,
        TO,
        AT,
        OR_ELSE,
        GLOBAL,
        NOT,
        AND,
        OR,
        IN,

        TRUE,  // true
        FALSE,  // false

        IS,
        MATCHES,
        ISA,

        IDENTIFIER,
    };

    inline std::string tokenTypeName(TokenType t)
    {
        switch (t)
        {
        case TokenType::WORD:
            return "WORD";
        case TokenType::NUMBER:
            return "NUMBER";
        case TokenType::STRING:
            return "STRING";
        case TokenType::FSTRING:
            return "FSTRING";
        case TokenType::PATTERN:
            return "PATTERN";
        case TokenType::PLUS:
            return "+";
        case TokenType::MINUS:
            return "-";
        case TokenType::STAR:
            return "*";
        case TokenType::SLASH:
            return "/";
        case TokenType::PERCENT:
            return "%";
        case TokenType::POWER:
            return "**";
        case TokenType::GE:
            return ">=";
        case TokenType::LE:
            return "<=";
        case TokenType::GT:
            return ">";
        case TokenType::LT:
            return "<";
        case TokenType::BANG:
            return "!";
        case TokenType::LPAREN:
            return "(";
        case TokenType::RPAREN:
            return ")";
        case TokenType::LBRACKET:
            return "[";
        case TokenType::RBRACKET:
            return "]";
        case TokenType::LBRACE:
            return "{";
        case TokenType::RBRACE:
            return "}";
        case TokenType::COMMA:
            return ",";
        case TokenType::COLON:
            return ":";
        case TokenType::DOT:
            return ".";
        case TokenType::TILDE:
            return "~";
        case TokenType::ASSIGN:
            return "=";
        case TokenType::NEWLINE:
            return "NEWLINE";
        case TokenType::INDENT:
            return "INDENT";
        case TokenType::DEDENT:
            return "DEDENT";
        case TokenType::EOF_TOKEN:
            return "EOF";
        case TokenType::CONSTANT:
            return "constant";
        case TokenType::IF:
            return "if";
        case TokenType::ELSE:
            return "else";
        case TokenType::LOOP:
            return "loop";
        case TokenType::REPEAT:
            return "repeat";
        case TokenType::WHILE:
            return "while";
        case TokenType::EACH:
            return "each";
        case TokenType::DO:
            return "do";
        case TokenType::END:
            return "end";
        case TokenType::STOP:
            return "stop";
        case TokenType::SKIP:
            return "skip";
        case TokenType::RETURN:
            return "return";
        case TokenType::FAIL:
            return "fail";
        case TokenType::AWAIT:
            return "await";
        case TokenType::USE:
            return "use";
        case TokenType::FROM:
            return "from";
        case TokenType::NOTE:
            return "note";
        case TokenType::ENDNOTE:
            return "endnote";
        case TokenType::EMPTY:
            return "empty";
        case TokenType::ASYNC:
            return "async";
        case TokenType::PURE:
            return "pure";
        case TokenType::PRIVATE:
            return "private";
        case TokenType::FUNCTION:
            return "func";
        case TokenType::CLASS:
            return "class";
        case TokenType::EXTENDS:
            return "extends";
        case TokenType::WITH:
            return "with";
        case TokenType::SELF:
            return "self";
        case TokenType::SUPER:
            return "super";
        case TokenType::ADD:
            return "add";
        case TokenType::REMOVE:
            return "remove";
        case TokenType::TO:
            return "to";
        case TokenType::AT:
            return "at";
        case TokenType::OR_ELSE:
            return "or_else";
        case TokenType::GLOBAL:
            return "global";
        case TokenType::NOT:
            return "not";
        case TokenType::AND:
            return "and";
        case TokenType::OR:
            return "or";
        case TokenType::IN:
            return "in";
        case TokenType::TRUE:
            return "true";
        case TokenType::FALSE:
            return "false";
        case TokenType::IS:
            return "is";
        case TokenType::MATCHES:
            return "matches";
        case TokenType::ISA:
            return "isa";
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        }
        return "UNKNOWN";
    }

}
