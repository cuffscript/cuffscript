#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include <string>
#include <unordered_map>

namespace cuff
{

    inline const std::unordered_map<std::string, TokenType> &keywordMap()
    {
        static const std::unordered_map<std::string, TokenType> m = {
            {"constant", TokenType::CONSTANT},
            {"global", TokenType::GLOBAL},
            {"add", TokenType::ADD},
            {"remove", TokenType::REMOVE},
            {"if", TokenType::IF},
            {"else", TokenType::ELSE},
            {"loop", TokenType::LOOP},
            {"repeat", TokenType::REPEAT},
            {"while", TokenType::WHILE},
            {"each", TokenType::EACH},
            {"stop", TokenType::STOP},
            {"skip", TokenType::SKIP},
            {"return", TokenType::RETURN},
            {"fail", TokenType::FAIL},
            {"await", TokenType::AWAIT},
            {"use", TokenType::USE},
            {"from", TokenType::FROM},
            {"in", TokenType::IN},
            {"to", TokenType::TO},
            {"at", TokenType::AT},
            {"do", TokenType::DO},
            {"end", TokenType::END},
            {"note", TokenType::NOTE},
            {"endnote", TokenType::ENDNOTE},
            {"func", TokenType::FUNCTION},
            {"class", TokenType::CLASS},
            {"extends", TokenType::EXTENDS},
            {"with", TokenType::WITH},
            {"async", TokenType::ASYNC},
            {"pure", TokenType::PURE},
            {"private", TokenType::PRIVATE},
            {"or_else", TokenType::OR_ELSE},
            {"self", TokenType::SELF},
            {"super", TokenType::SUPER},
            {"is", TokenType::IS},
            {"matches", TokenType::MATCHES},
            {"isa", TokenType::ISA},
            {"not", TokenType::NOT},
            {"and", TokenType::AND},
            {"or", TokenType::OR},
            {"true", TokenType::TRUE},
            {"false", TokenType::FALSE},
            {"empty", TokenType::EMPTY},
        };
        return m;
    }

    class KeywordClassifier
    {
    public:
        static TokenType classify(const std::string &word)
        {
            const auto &m = keywordMap();
            auto it = m.find(word);
            if (it != m.end())
                return it->second;
            return TokenType::IDENTIFIER;
        }
    };

}
