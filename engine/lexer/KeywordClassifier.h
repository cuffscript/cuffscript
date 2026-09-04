#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include <string>
#include <unordered_map>

namespace cuff {

// Classifies WORD tokens into keyword tokens or identifiers.
// Pure lookup — no context needed.
class KeywordClassifier {
public:
    static TokenType classify(const std::string& word) {
        auto it = keywords_.find(word);
        if (it != keywords_.end()) return it->second;
        return TokenType::IDENTIFIER;
    }

    static bool isKeyword(const std::string& word) {
        return keywords_.count(word) > 0;
    }

private:
    static const std::unordered_map<std::string, TokenType> keywords_;
};

inline const std::unordered_map<std::string, TokenType>& keywordMap() {
    static const std::unordered_map<std::string, TokenType> m = {
        {"set",        TokenType::SET},
        {"change",     TokenType::CHANGE},
        {"constant",   TokenType::CONSTANT},
        {"if",         TokenType::IF},
        {"else",       TokenType::ELSE},
        {"loop",       TokenType::LOOP},
        {"repeat",     TokenType::REPEAT},
        {"while",      TokenType::WHILE},
        {"match",      TokenType::MATCH},
        {"do",         TokenType::DO},
        {"end",        TokenType::END},
        {"stop",       TokenType::STOP},
        {"return",     TokenType::RETURN},
        {"await",      TokenType::AWAIT},
        {"use",        TokenType::USE},
        {"from",       TokenType::FROM},
        {"note",       TokenType::NOTE},
        {"endnote",    TokenType::ENDNOTE},
        {"empty",      TokenType::EMPTY},
        {"async",      TokenType::ASYNC},
        {"returnable", TokenType::RETURNABLE},
        {"function",   TokenType::FUNCTION},
        {"add",        TokenType::ADD},
        {"remove",     TokenType::REMOVE},
        {"replace",    TokenType::REPLACE},
        {"to",         TokenType::TO},
        {"or_else",    TokenType::OR_ELSE},
        {"number",     TokenType::NUMBER_TYPE},
        {"str",        TokenType::STR_TYPE},
        {"list",       TokenType::LIST_TYPE},
        {"map",        TokenType::MAP_TYPE},
        {"boolean",    TokenType::BOOLEAN_TYPE},
        {"true",       TokenType::TRUE},
        {"false",      TokenType::FALSE},
        {"is",         TokenType::IS_STRICT},
        {"IS",         TokenType::IS_CASEINSENSITIVE},
    };
    return m;
}

class KeywordClassifierImpl {
public:
    static TokenType classify(const std::string& word) {
        const auto& m = keywordMap();
        auto it = m.find(word);
        if (it != m.end()) return it->second;
        return TokenType::IDENTIFIER;
    }
};

} // namespace cuff
