#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../../engine/regex/RegexParser.h"
#include <memory>
#include <string>

namespace cuff
{

    class ExpressionParser;

    inline void validatePatternLiteral(const std::string &pattern, const SourceLocation &loc)
    {
        cuff::regex::RegexParser rp(pattern, loc);
        int groupCount = 0;
        rp.parse(groupCount);
    }

    class RegexExprParser
    {
    public:
        static std::unique_ptr<Expr> parseMatchFrom(ParserCore &p);

        static std::unique_ptr<Expr> parseFind(ParserCore &p);

        static std::unique_ptr<Expr> parseReplace(ParserCore &p);

        static std::unique_ptr<Expr> parseSplit(ParserCore &p);

        static std::unique_ptr<Expr> parseCount(ParserCore &p);

        static bool looksLikeCollectionReplace(ParserCore &p)
        {
            return isWordLikeToken(p.peek(1)) && p.peek(2).is(TokenType::LBRACKET);
        }

    private:
        static PatternArg parsePatternArg(ParserCore &p);
        static std::string parseOptionalFlags(ParserCore &p);
    };

}
