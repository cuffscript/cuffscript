#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include <vector>

namespace cuff
{

    class ColonValidator
    {
    public:
        static void validate(const std::vector<Token> &tokens)
        {
            for (size_t i = 0; i < tokens.size(); ++i)
            {
                if (!tokens[i].is(TokenType::COLON))
                    continue;

                const Token &colon = tokens[i];

                if (colon.hasSpaceBefore)
                {
                    throw SyntaxError("space before ':' is not allowed", colon.location);
                }
            }
        }
    };

}
