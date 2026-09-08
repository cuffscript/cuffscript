#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "LiteralParser.h"
#include "RegexExprParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../tokenizer/Tokenizer.h"
#include "../lexer/Lexer.h"
#include <memory>
#include <string>
#include <cstdlib>

namespace cuff
{

    // Index/postfix parser — handles [index], [start~end], and function calls (args).
    // (Body deferred to the bottom of this file — it calls ExpressionParser::parse,
    //  which is declared further down in this same file.)
    class IndexParser
    {
    public:
        static std::unique_ptr<Expr> parsePostfix(ParserCore &p, std::unique_ptr<Expr> base);
    };

    // Expression parser with operator precedence.
    // Precedence (lowest to highest):
    //   1. Comparison: is, IS, >=, <=, >, <
    //   2. Additive: +, -
    //   3. Multiplicative: *, /
    //   4. Unary: ! (NOT), - (negation)
    //   5. Postfix: index, slice, function call
    //   6. Primary: literals, identifiers, parentheses
    class ExpressionParser
    {
    public:
        static std::unique_ptr<Expr> parse(ParserCore &p)
        {
            return parseLogicalNot(p);
        }

        // `!` is CuffScript's boolean-negation operator. Unlike C-style
        // languages, it binds *looser* than comparison — `!lvl is MAX_LEVEL`
        // means `!(lvl is MAX_LEVEL)`, matching how the language spec's own
        // examples read it (the same way Python's `not` binds looser than
        // `==`). Arithmetic negation (`-x`) is a separate, tight-binding
        // operator handled down in parseUnary, since `-x + 1` should still
        // mean `(-x) + 1`.
        static std::unique_ptr<Expr> parseLogicalNot(ParserCore &p)
        {
            if (p.check(TokenType::BANG))
            {
                SourceLocation loc = p.current().location;
                p.advance();
                auto operand = parseLogicalNot(p);
                return std::make_unique<Expr>(ExprKind::UnaryOp,
                                              UnaryOp("!", std::move(operand), loc));
            }
            return parseComparison(p);
        }

        static std::unique_ptr<Expr> parseComparison(ParserCore &p)
        {
            auto left = parseAdditive(p);

            while (p.checkAny({TokenType::IS_STRICT, TokenType::IS_CASEINSENSITIVE,
                               TokenType::GE, TokenType::LE, TokenType::GT, TokenType::LT}))
            {
                const Token &opTok = p.current();
                std::string opStr = opTok.value;
                bool negated = false;
                if (opTok.is(TokenType::IS_STRICT))
                    opStr = "is";
                else if (opTok.is(TokenType::IS_CASEINSENSITIVE))
                    opStr = "IS";
                SourceLocation loc = opTok.location;
                p.advance();

                // "is not" / "IS not" — negated comparison. Handles the
                // general case (`x is not y`) as well as the specific
                // `is not empty` idiom used to check regex/match results.
                if ((opTok.is(TokenType::IS_STRICT) || opTok.is(TokenType::IS_CASEINSENSITIVE)) &&
                    p.check(TokenType::NOT))
                {
                    p.advance();
                    negated = true;
                    opStr += " not";
                }

                auto right = parseAdditive(p);

                // Check for regex match: is/IS (not) followed by a string literal
                if ((opTok.is(TokenType::IS_STRICT) || opTok.is(TokenType::IS_CASEINSENSITIVE)) && right->kind == ExprKind::String)
                {
                    std::string pattern = std::get<StringLiteral>(right->data).value;
                    bool caseInsensitive = opTok.is(TokenType::IS_CASEINSENSITIVE);
                    validatePatternLiteral(pattern, loc);
                    auto matchExpr = std::make_unique<Expr>(ExprKind::RegexMatch,
                                                            RegexMatchExpr(std::move(left), caseInsensitive, std::move(pattern), loc));
                    if (negated)
                    {
                        left = std::make_unique<Expr>(ExprKind::UnaryOp, UnaryOp("!", std::move(matchExpr), loc));
                    }
                    else
                    {
                        left = std::move(matchExpr);
                    }
                }
                else
                {
                    left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                                  BinaryOp(std::move(opStr), std::move(left), std::move(right), loc));
                }
            }

            return left;
        }

        static std::unique_ptr<Expr> parseAdditive(ParserCore &p)
        {
            auto left = parseMultiplicative(p);

            while (p.checkAny({TokenType::PLUS, TokenType::MINUS}))
            {
                std::string op = p.current().value;
                SourceLocation loc = p.current().location;
                p.advance();
                auto right = parseMultiplicative(p);
                left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                              BinaryOp(std::move(op), std::move(left), std::move(right), loc));
            }

            return left;
        }

        static std::unique_ptr<Expr> parseMultiplicative(ParserCore &p)
        {
            auto left = parseUnary(p);

            while (p.checkAny({TokenType::STAR, TokenType::SLASH}))
            {
                std::string op = p.current().value;
                SourceLocation loc = p.current().location;
                p.advance();
                auto right = parseUnary(p);
                left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                              BinaryOp(std::move(op), std::move(left), std::move(right), loc));
            }

            return left;
        }

        static std::unique_ptr<Expr> parseUnary(ParserCore &p)
        {
            if (p.check(TokenType::MINUS))
            {
                SourceLocation loc = p.current().location;
                p.advance();
                auto operand = parseUnary(p);
                return std::make_unique<Expr>(ExprKind::UnaryOp,
                                              UnaryOp("-", std::move(operand), loc));
            }
            return parsePostfixExpr(p);
        }

        static std::unique_ptr<Expr> parsePostfixExpr(ParserCore &p)
        {
            auto base = LiteralParser::parsePrimary(p);
            return IndexParser::parsePostfix(p, std::move(base));
        }
    };

    // ---- Deferred implementations ----

    inline std::unique_ptr<Expr> IndexParser::parsePostfix(ParserCore &p, std::unique_ptr<Expr> base)
    {
        while (true)
        {
            if (p.check(TokenType::LBRACKET))
            {
                SourceLocation loc = p.current().location;
                p.advance();

                auto first = ExpressionParser::parse(p);

                if (p.match(TokenType::TILDE))
                {
                    auto end = ExpressionParser::parse(p);
                    p.consume(TokenType::RBRACKET, "Expected ']' to close slice");
                    base = std::make_unique<Expr>(ExprKind::SliceAccess,
                                                  SliceAccess(std::move(base), std::move(first), std::move(end), loc));
                }
                else
                {
                    p.consume(TokenType::RBRACKET, "Expected ']' to close index");
                    base = std::make_unique<Expr>(ExprKind::IndexAccess,
                                                  IndexAccess(std::move(base), std::move(first), loc));
                }
            }
            else if (p.check(TokenType::LPAREN))
            {
                SourceLocation loc = p.current().location;
                p.advance();

                std::vector<std::unique_ptr<Expr>> args;
                p.skipNewlines();

                if (!p.check(TokenType::RPAREN))
                {
                    args.push_back(ExpressionParser::parse(p));
                    while (p.match(TokenType::COMMA))
                    {
                        p.skipNewlines();
                        args.push_back(ExpressionParser::parse(p));
                    }
                }

                p.skipNewlines();
                p.consume(TokenType::RPAREN, "Expected ')' to close function call");

                std::string funcName;
                if (base->kind == ExprKind::Identifier)
                {
                    funcName = std::get<IdentifierExpr>(base->data).name;
                }
                else
                {
                    throw SyntaxError("Cannot call non-identifier as function", loc);
                }

                base = std::make_unique<Expr>(ExprKind::FunctionCall,
                                              FunctionCall(std::move(funcName), std::move(args), loc));
            }
            else
            {
                break;
            }
        }
        return base;
    }

    // LiteralParser and ExpressionParser are mutually recursive (list/map/f-string
    // elements are expressions, and expressions can contain list/map/f-string
    // literals). Their bodies are defined here, after ExpressionParser is a
    // complete type, even though they're declared inside class LiteralParser
    // in LiteralParser.h.

    inline std::unique_ptr<Expr> LiteralParser::parseList(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACKET, "Expected '[' for list literal");

        std::vector<std::unique_ptr<Expr>> elements;
        p.skipNewlines();

        if (p.check(TokenType::RBRACKET))
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::List, ListLiteral(std::move(elements), loc));
        }

        elements.push_back(ExpressionParser::parse(p));
        while (true)
        {
            p.skipNewlines();
            if (p.match(TokenType::COMMA))
            {
                p.skipNewlines();
                // Allow a trailing comma right before ']'
                if (p.check(TokenType::RBRACKET))
                    break;
                elements.push_back(ExpressionParser::parse(p));
            }
            else
            {
                break;
            }
        }

        p.skipNewlines();
        p.consume(TokenType::RBRACKET, "Expected ']' to close list literal");
        return std::make_unique<Expr>(ExprKind::List, ListLiteral(std::move(elements), loc));
    }

    inline std::unique_ptr<Expr> LiteralParser::parseMap(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACE, "Expected '{' for map literal");

        std::vector<MapLiteral::Pair> pairs;
        p.skipNewlines();

        if (p.check(TokenType::RBRACE))
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
        }

        // Parse key: value pairs
        while (true)
        {
            auto key = ExpressionParser::parse(p);
            p.consume(TokenType::COLON, "Expected ':' after map key");
            p.skipNewlines();
            auto value = ExpressionParser::parse(p);

            MapLiteral::Pair pair;
            pair.key = std::move(key);
            pair.value = std::move(value);
            pairs.push_back(std::move(pair));

            p.skipNewlines();
            if (!p.match(TokenType::COMMA))
                break;
            p.skipNewlines();
            if (p.check(TokenType::RBRACE)) // trailing comma
                break;
        }

        p.skipNewlines();
        p.consume(TokenType::RBRACE, "Expected '}' to close map literal");
        return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
    }

    inline std::unique_ptr<Expr> LiteralParser::parsePrimary(ParserCore &p)
    {
        const Token &tok = p.current();

        switch (tok.type)
        {
        case TokenType::NUMBER:
        {
            double val = std::stod(tok.value);
            p.advance();
            return std::make_unique<Expr>(ExprKind::Number, NumberLiteral(val, tok.location));
        }
        case TokenType::STRING:
        {
            std::string val = tok.value;
            p.advance();
            return std::make_unique<Expr>(ExprKind::String, StringLiteral(std::move(val), tok.location));
        }
        case TokenType::FSTRING:
            return parseFString(p);
        case TokenType::TRUE:
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Bool, BoolLiteral(true, tok.location));
        }
        case TokenType::FALSE:
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Bool, BoolLiteral(false, tok.location));
        }
        case TokenType::EMPTY:
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Empty, EmptyLiteral(tok.location));
        }
        case TokenType::IDENTIFIER:
        {
            std::string name = tok.value;
            p.advance();
            return std::make_unique<Expr>(ExprKind::Identifier, IdentifierExpr(std::move(name), tok.location));
        }
        case TokenType::LBRACKET:
            return parseList(p);
        case TokenType::LBRACE:
            return parseMap(p);
        case TokenType::LPAREN:
        {
            p.advance();
            auto expr = ExpressionParser::parse(p);
            p.consume(TokenType::RPAREN, "Expected ')' to close grouped expression");
            return expr;
        }
        case TokenType::AWAIT:
        {
            // await used inline as an expression, e.g.
            //   set str result to await fetch_data()
            // (as opposed to the bare-statement form `await fetch_data()`,
            //  handled directly by StatementParser).
            p.advance();
            auto inner = ExpressionParser::parse(p);
            if (inner->kind != ExprKind::FunctionCall)
                throw SyntaxError("Expected a function call after 'await'", tok.location);
            FunctionCall fc = std::move(std::get<FunctionCall>(inner->data));
            auto callPtr = std::make_unique<FunctionCall>(std::move(fc));
            return std::make_unique<Expr>(ExprKind::Await, AwaitExpr(std::move(callPtr), tok.location));
        }
        case TokenType::MATCH:
            return RegexExprParser::parseMatchFrom(p);
        case TokenType::FIND:
            return RegexExprParser::parseFind(p);
        case TokenType::REPLACE:
            return RegexExprParser::parseReplace(p);
        case TokenType::SPLIT:
            return RegexExprParser::parseSplit(p);
        case TokenType::COUNT:
            return RegexExprParser::parseCount(p);
        default:
            throw SyntaxError("Unexpected token '" + tok.value + "' in expression", tok.location);
        }
    }

    inline std::unique_ptr<Expr> LiteralParser::parseEmbeddedExpression(
        const std::string &exprSource, const SourceLocation & /*fallbackLoc*/)
    {
        // Tokenize and lex the inner expression source
        Tokenizer tokenizer(exprSource);
        std::vector<Token> rawTokens = tokenizer.tokenize();
        Lexer lexer(std::move(rawTokens));
        std::vector<Token> innerTokens = lexer.lex();

        // Create a sub-parser and parse the expression
        ParserCore innerParser(std::move(innerTokens));
        return ExpressionParser::parse(innerParser);
    }

    // ---- RegexExprParser deferred implementations ----
    // (mutually recursive with ExpressionParser — see RegexExprParser.h)

    inline PatternArg RegexExprParser::parsePatternArg(ParserCore &p)
    {
        PatternArg arg;
        if (p.check(TokenType::STRING))
        {
            SourceLocation loc = p.current().location;
            arg.literalPattern = p.current().value;
            arg.isLiteral = true;
            p.advance();
            // Eagerly validate now — a malformed literal pattern is reported
            // immediately as a RegexSyntaxError, at parse time.
            validatePatternLiteral(arg.literalPattern, loc);
        }
        else
        {
            arg.isLiteral = false;
            arg.dynamicExpr = ExpressionParser::parse(p);
        }
        return arg;
    }

    inline std::string RegexExprParser::parseOptionalFlags(ParserCore &p)
    {
        if (p.check(TokenType::IDENTIFIER))
        {
            const std::string &val = p.current().value;
            bool allFlagChars = !val.empty();
            for (char c : val)
            {
                if (c != 'g' && c != 'i' && c != 'm')
                {
                    allFlagChars = false;
                    break;
                }
            }
            if (allFlagChars)
            {
                p.advance();
                return val;
            }
        }
        return "";
    }

    inline std::unique_ptr<Expr> RegexExprParser::parseMatchFrom(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::MATCH, "Expected 'match'");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::FROM, "Expected 'from' after match target");
        PatternArg pattern = parsePatternArg(p);
        std::string flags = parseOptionalFlags(p);

        MatchFromExpr m;
        m.target = std::move(target);
        m.pattern = std::move(pattern);
        m.flags = std::move(flags);
        m.loc = loc;
        return std::make_unique<Expr>(ExprKind::MatchFrom, std::move(m));
    }

    inline std::unique_ptr<Expr> RegexExprParser::parseFind(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::FIND, "Expected 'find'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::FROM, "Expected 'from' after find pattern");
        auto target = ExpressionParser::parseAdditive(p);
        std::string flags = parseOptionalFlags(p);

        FindExpr f;
        f.pattern = std::move(pattern);
        f.target = std::move(target);
        f.flags = std::move(flags);
        f.loc = loc;
        return std::make_unique<Expr>(ExprKind::Find, std::move(f));
    }

    inline std::unique_ptr<Expr> RegexExprParser::parseReplace(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::REPLACE, "Expected 'replace'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::IN, "Expected 'in' after replace pattern");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::TO, "Expected 'to' after replace target");
        auto replacement = ExpressionParser::parseAdditive(p);
        std::string flags = parseOptionalFlags(p);

        PatternReplaceExpr r;
        r.pattern = std::move(pattern);
        r.target = std::move(target);
        r.replacement = std::move(replacement);
        r.flags = std::move(flags);
        r.loc = loc;
        return std::make_unique<Expr>(ExprKind::PatternReplace, std::move(r));
    }

    inline std::unique_ptr<Expr> RegexExprParser::parseSplit(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::SPLIT, "Expected 'split'");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::BY, "Expected 'by' after split target");
        PatternArg pattern = parsePatternArg(p);

        SplitExpr s;
        s.target = std::move(target);
        s.pattern = std::move(pattern);
        s.loc = loc;
        return std::make_unique<Expr>(ExprKind::Split, std::move(s));
    }

    inline std::unique_ptr<Expr> RegexExprParser::parseCount(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::COUNT, "Expected 'count'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::IN, "Expected 'in' after count pattern");
        auto target = ExpressionParser::parseAdditive(p);
        std::string flags = parseOptionalFlags(p);

        CountExpr c;
        c.pattern = std::move(pattern);
        c.target = std::move(target);
        c.flags = std::move(flags);
        c.loc = loc;
        return std::make_unique<Expr>(ExprKind::Count, std::move(c));
    }

} // namespace cuff
