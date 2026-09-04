#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "ASTNodes.h"
#include <vector>
#include <string>

namespace cuff {

// Core parser state — token cursor + utility helpers.
// Shared by all sub-parsers.
class ParserCore {
public:
    std::vector<Token> tokens;
    size_t pos = 0;

    explicit ParserCore(std::vector<Token> t) : tokens(std::move(t)) {}

    // ---- Token cursor helpers ----

    const Token& current() const { return tokens[pos]; }

    const Token& peek(int ahead = 0) const {
        size_t idx = pos + ahead;
        if (idx >= tokens.size()) return tokens.back();
        return tokens[idx];
    }

    bool atEnd() const {
        return current().is(TokenType::EOF_TOKEN);
    }

    bool check(TokenType t) const {
        return current().is(t);
    }

    bool checkAny(std::initializer_list<TokenType> types) const {
        for (TokenType t : types) {
            if (current().is(t)) return true;
        }
        return false;
    }

    const Token& advance() {
        if (!atEnd()) ++pos;
        return tokens[pos - 1];
    }

    Token consume(TokenType t, const std::string& errMsg) {
        if (!check(t)) {
            throw SyntaxError(errMsg + " (got '" + current().value + "')", current().location);
        }
        return advance();
    }

    bool match(TokenType t) {
        if (check(t)) {
            advance();
            return true;
        }
        return false;
    }

    // Skip NEWLINE tokens (and INDENT/DEDENT for structural flexibility)
    void skipNewlines() {
        while (check(TokenType::NEWLINE) || check(TokenType::INDENT) || check(TokenType::DEDENT)) {
            advance();
        }
    }

    // Skip only NEWLINE tokens
    void skipNewlinesOnly() {
        while (check(TokenType::NEWLINE)) advance();
    }

    // Check if the next non-trivial token (skipping NEWLINE/INDENT/DEDENT) is a terminator
    bool peekTerminator(std::initializer_list<TokenType> terminators) const {
        size_t idx = pos;
        while (idx < tokens.size()) {
            TokenType tt = tokens[idx].type;
            if (tt == TokenType::NEWLINE || tt == TokenType::INDENT || tt == TokenType::DEDENT) {
                ++idx;
                continue;
            }
            for (TokenType term : terminators) {
                if (tt == term) return true;
            }
            return false;
        }
        return false;
    }

    // Get current indentation level (from the most recent INDENT/DEDENT or current token)
    int currentIndent() const {
        // Walk backwards to find the last INDENT/DEDENT
        for (int i = static_cast<int>(pos) - 1; i >= 0; --i) {
            if (tokens[i].is(TokenType::INDENT)) return tokens[i].indentLevel;
            if (tokens[i].is(TokenType::DEDENT)) return tokens[i].indentLevel;
        }
        return 0;
    }
};

} // namespace cuff
