#pragma once

#include "TokenTypes.h"
#include "SourceLocation.h"
#include <string>

namespace cuff
{

    struct Token
    {
        TokenType type;
        std::string value;
        SourceLocation location;
        bool hasSpaceBefore = false;
        int indentLevel = 0; // indentation depth in spaces (for block-structure enforcement)

        Token() = default;
        Token(TokenType t, std::string v, SourceLocation loc, bool space = false, int indent = 0)
            : type(t), value(std::move(v)), location(loc), hasSpaceBefore(space), indentLevel(indent) {}

        bool is(TokenType t) const { return type == t; }
    };

} // namespace cuff
