#pragma once

#include <string>

namespace cuff {

// Two-layer token type system:
//   Layer 1 (TOKENIZER output): physical token kinds — WORD, NUMBER, STRING, etc.
//   Layer 2 (LEXER output):     classified keyword / identifier / type tokens.
//
// The tokenizer produces raw physical tokens; the lexer re-classifies WORD tokens
// into specific keyword or identifier types.

enum class TokenType {
    // ---- Layer 1: raw tokenizer output ----
    WORD,           // alphanumeric identifier — lexer classifies into keyword/type/identifier
    NUMBER,
    STRING,
    FSTRING,

    // operators
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    GE,             // >=
    LE,             // <=
    GT,             // >
    LT,             // <
    BANG,           // ! (negation operator)

    // delimiters
    LPAREN,         // (
    RPAREN,         // )
    LBRACKET,       // [
    RBRACKET,       // ]
    LBRACE,         // {
    RBRACE,         // }
    COMMA,          // ,
    COLON,          // :
    DOT,            // .
    TILDE,          // ~

    // structural
    NEWLINE,
    INDENT,         // indentation increase (block body)
    DEDENT,         // indentation decrease (block end)
    EOF_TOKEN,

    // ---- Layer 2: lexer-classified keyword tokens ----
    SET, CHANGE, CONSTANT, IF, ELSE, LOOP, REPEAT, WHILE, MATCH,
    DO, END, STOP, RETURN, AWAIT, USE, FROM,
    NOTE, ENDNOTE, EMPTY, ASYNC, RETURNABLE, FUNCTION,
    ADD, REMOVE, REPLACE, TO, OR_ELSE,

    // type keywords
    NUMBER_TYPE,    // number
    STR_TYPE,       // str
    LIST_TYPE,      // list
    MAP_TYPE,       // map
    BOOLEAN_TYPE,   // boolean

    // literals classified by lexer
    TRUE,           // true
    FALSE,          // false

    // comparison
    IS_STRICT,          // is  (case-sensitive equality)
    IS_CASEINSENSITIVE, // IS  (case-insensitive equality for English strings)

    // classified identifiers
    IDENTIFIER,
};

inline std::string tokenTypeName(TokenType t) {
    switch (t) {
    case TokenType::WORD:             return "WORD";
    case TokenType::NUMBER:           return "NUMBER";
    case TokenType::STRING:           return "STRING";
    case TokenType::FSTRING:          return "FSTRING";
    case TokenType::PLUS:             return "+";
    case TokenType::MINUS:            return "-";
    case TokenType::STAR:             return "*";
    case TokenType::SLASH:            return "/";
    case TokenType::GE:               return ">=";
    case TokenType::LE:               return "<=";
    case TokenType::GT:               return ">";
    case TokenType::LT:               return "<";
    case TokenType::BANG:             return "!";
    case TokenType::LPAREN:           return "(";
    case TokenType::RPAREN:           return ")";
    case TokenType::LBRACKET:         return "[";
    case TokenType::RBRACKET:         return "]";
    case TokenType::LBRACE:           return "{";
    case TokenType::RBRACE:           return "}";
    case TokenType::COMMA:            return ",";
    case TokenType::COLON:            return ":";
    case TokenType::DOT:              return ".";
    case TokenType::TILDE:            return "~";
    case TokenType::NEWLINE:          return "NEWLINE";
    case TokenType::INDENT:           return "INDENT";
    case TokenType::DEDENT:           return "DEDENT";
    case TokenType::EOF_TOKEN:        return "EOF";
    case TokenType::SET:              return "set";
    case TokenType::CHANGE:           return "change";
    case TokenType::CONSTANT:         return "constant";
    case TokenType::IF:               return "if";
    case TokenType::ELSE:             return "else";
    case TokenType::LOOP:             return "loop";
    case TokenType::REPEAT:           return "repeat";
    case TokenType::WHILE:            return "while";
    case TokenType::MATCH:            return "match";
    case TokenType::DO:               return "do";
    case TokenType::END:              return "end";
    case TokenType::STOP:             return "stop";
    case TokenType::RETURN:           return "return";
    case TokenType::AWAIT:            return "await";
    case TokenType::USE:              return "use";
    case TokenType::FROM:             return "from";
    case TokenType::NOTE:             return "note";
    case TokenType::ENDNOTE:          return "endnote";
    case TokenType::EMPTY:            return "empty";
    case TokenType::ASYNC:            return "async";
    case TokenType::RETURNABLE:       return "returnable";
    case TokenType::FUNCTION:         return "function";
    case TokenType::ADD:              return "add";
    case TokenType::REMOVE:           return "remove";
    case TokenType::REPLACE:          return "replace";
    case TokenType::TO:               return "to";
    case TokenType::OR_ELSE:          return "or_else";
    case TokenType::NUMBER_TYPE:      return "number";
    case TokenType::STR_TYPE:         return "str";
    case TokenType::LIST_TYPE:        return "list";
    case TokenType::MAP_TYPE:         return "map";
    case TokenType::BOOLEAN_TYPE:     return "boolean";
    case TokenType::TRUE:             return "true";
    case TokenType::FALSE:            return "false";
    case TokenType::IS_STRICT:        return "is";
    case TokenType::IS_CASEINSENSITIVE: return "IS";
    case TokenType::IDENTIFIER:       return "IDENTIFIER";
    }
    return "UNKNOWN";
}

} // namespace cuff
