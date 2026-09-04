#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "KeywordClassifier.h"
#include "ColonValidator.h"
#include <vector>
#include <string>

namespace cuff
{

    // The Lexer takes the tokenizer's raw token stream and:
    //   1. Validates colon spacing rules (no space before ':').
    //   2. Classifies WORD tokens into keywords, type keywords, boolean literals, or identifiers.
    //   3. Preserves structural tokens (NEWLINE, INDENT, DEDENT, EOF) for the parser.
    //
    // Output: a classified, validated token stream ready for parsing.
    class Lexer
    {
    public:
        explicit Lexer(std::vector<Token> rawTokens) : tokens_(std::move(rawTokens)) {}

        std::vector<Token> lex()
        {
            // First pass: validate colon spacing
            ColonValidator::validate(tokens_);

            // Second pass: classify WORD tokens
            for (auto &tok : tokens_)
            {
                if (tok.type == TokenType::WORD)
                {
                    if (tok.value.empty())
                        continue;
                    tok.type = KeywordClassifierImpl::classify(tok.value);
                }
            }

            return tokens_;
        }

    private:
        std::vector<Token> tokens_;
    };

} // namespace cuff
