#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include <vector>

namespace cuff {

// Enforces CuffScript's colon spacing rule:
//   - No space before ':'
//   - Space after ':' is allowed (free) — not mandatory in this spec version
//
// The tokenizer records hasSpaceBefore on each token.
// A COLON with hasSpaceBefore=true violates the "no space before" rule.
class ColonValidator {
public:
    static void validate(const std::vector<Token>& tokens) {
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (!tokens[i].is(TokenType::COLON)) continue;

            const Token& colon = tokens[i];

            // Rule: no space before ':'
            if (colon.hasSpaceBefore) {
                throw SyntaxError("Space before ':' is not allowed", colon.location);
            }
        }
    }
};

} // namespace cuff
