#pragma once

#include "TokenTypes.h"
#include "SourceLocation.h"
#include <cstdint>
#include <string>

namespace cuff
{

    // Flag bits of a PATTERN token (the letters written after the closing quote of p"...").
    constexpr uint8_t kPatternFlagIgnoreCase = 1;
    constexpr uint8_t kPatternFlagMultiline = 2;

    struct Token
    {
        TokenType type;
        std::string value;
        SourceLocation location;
        bool hasSpaceBefore = false;
        uint8_t patternFlags = 0;
        int indentLevel = 0;

        Token() = default;
        Token(TokenType t, std::string v, SourceLocation loc, bool space = false, int indent = 0)
            : type(t), value(std::move(v)), location(loc), hasSpaceBefore(space), indentLevel(indent) {}

        bool is(TokenType t) const { return type == t; }
    };

}
