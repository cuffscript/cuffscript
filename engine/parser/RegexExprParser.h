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

    // Forward declaration — RegexExprParser needs ExpressionParser to parse
    // target/replacement sub-expressions, but ExpressionParser (via
    // LiteralParser::parsePrimary) dispatches *into* RegexExprParser for the
    // find/match/replace/split/count keywords. Same mutually-recursive
    // pattern used for LiteralParser <-> ExpressionParser: declare here,
    // define the bodies at the bottom of ExpressionParser.h once
    // ExpressionParser is a complete type.
    class ExpressionParser;

    // Eagerly compiles a literal pattern string to validate it, so a
    // malformed literal pattern is reported as a RegexSyntaxError at parse
    // time (per docs/REGEX.md section 32's "immediately" requirement)
    // instead of only when that line happens to execute.
    inline void validatePatternLiteral(const std::string &pattern, const SourceLocation &loc)
    {
        cuff::regex::RegexParser rp(pattern, loc);
        int groupCount = 0;
        rp.parse(groupCount);
    }

    class RegexExprParser
    {
    public:
        // match <target> from <pattern> [flags]
        static std::unique_ptr<Expr> parseMatchFrom(ParserCore &p);

        // find <pattern> from <target> [flags]
        static std::unique_ptr<Expr> parseFind(ParserCore &p);

        // replace <pattern> in <target> to <replacement> [flags]
        static std::unique_ptr<Expr> parseReplace(ParserCore &p);

        // split <target> by <pattern>
        static std::unique_ptr<Expr> parseSplit(ParserCore &p);

        // count <pattern> in <target> [flags]
        static std::unique_ptr<Expr> parseCount(ParserCore &p);

        // True when the upcoming tokens look like the *statement* form of
        // `replace` (collection element replacement: `replace name[i] to v`)
        // rather than the *expression* form (`replace "pat" in x to "y"`).
        // Used by StatementParser to disambiguate which parser to hand the
        // REPLACE token to.
        static bool looksLikeCollectionReplace(ParserCore &p)
        {
            return p.peek(1).is(TokenType::IDENTIFIER) && p.peek(2).is(TokenType::LBRACKET);
        }

    private:
        static PatternArg parsePatternArg(ParserCore &p);
        static std::string parseOptionalFlags(ParserCore &p);
    };

} // namespace cuff
