#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "KeywordClassifier.h"
#include "ColonValidator.h"
#include <vector>
#include <string>

namespace cuff
{

    class Lexer
    {
    public:
        explicit Lexer(std::vector<Token> rawTokens) : tokens_(std::move(rawTokens)) {}

        std::vector<Token> lex()
        {
            ColonValidator::validate(tokens_);

            for (auto &tok : tokens_)
            {
                if (tok.type == TokenType::WORD)
                {
                    if (tok.value.empty())
                        continue;
                    tok.type = KeywordClassifier::classify(tok.value);
                }
            }

            return std::move(tokens_);
        }

    private:
        std::vector<Token> tokens_;
    };

}
