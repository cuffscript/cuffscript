#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "LiteralParser.h"
#include "RegexExprParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../tokenizer/Tokenizer.h"
#include "../lexer/Lexer.h"
#include <cerrno>
#include <cmath>
#include <memory>
#include <string>
#include <cstdlib>

namespace cuff
{

    class IndexParser
    {
    public:
        static std::unique_ptr<Expr> parsePostfix(ParserCore &p, std::unique_ptr<Expr> base);
    };

    class ExpressionParser
    {
    public:
        static std::unique_ptr<Expr> parse(ParserCore &p)
        {
            return parseLogicalNot(p);
        }

        static std::unique_ptr<Expr> parseLogicalNot(ParserCore &p)
        {
            if (p.check(TokenType::BANG))
            {
                SourceLocation loc = p.current().location;
                ParseDepthScope depth(loc);
                p.advance();
                auto operand = parseLogicalNot(p);
                return std::make_unique<Expr>(ExprKind::UnaryOp,
                                              UnaryOp(UnOp::Not, std::move(operand), loc));
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
                BinOp binOp;
                switch (opTok.type)
                {
                case TokenType::IS_STRICT: binOp = BinOp::Is; break;
                case TokenType::IS_CASEINSENSITIVE: binOp = BinOp::IsCase; break;
                case TokenType::GE: binOp = BinOp::GreaterEq; break;
                case TokenType::LE: binOp = BinOp::LessEq; break;
                case TokenType::GT: binOp = BinOp::Greater; break;
                default: binOp = BinOp::Less; break;
                }
                bool negated = false;
                SourceLocation loc = opTok.location;
                p.advance();

                if ((opTok.is(TokenType::IS_STRICT) || opTok.is(TokenType::IS_CASEINSENSITIVE)) &&
                    p.check(TokenType::NOT))
                {
                    p.advance();
                    negated = true;
                    binOp = (binOp == BinOp::IsCase) ? BinOp::IsNotCase : BinOp::IsNot;
                }

                auto right = parseAdditive(p);

                if ((opTok.is(TokenType::IS_STRICT) || opTok.is(TokenType::IS_CASEINSENSITIVE)) && right->kind == ExprKind::String)
                {
                    std::string pattern = std::get<StringLiteral>(right->data).value;
                    bool caseInsensitive = opTok.is(TokenType::IS_CASEINSENSITIVE);
                    validatePatternLiteral(pattern, loc);
                    auto matchExpr = std::make_unique<Expr>(ExprKind::RegexMatch,
                                                            RegexMatchExpr(std::move(left), caseInsensitive, std::move(pattern), loc));
                    if (negated)
                    {
                        left = std::make_unique<Expr>(ExprKind::UnaryOp, UnaryOp(UnOp::Not, std::move(matchExpr), loc));
                    }
                    else
                    {
                        left = std::move(matchExpr);
                    }
                }
                else
                {
                    left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                                  BinaryOp(binOp, std::move(left), std::move(right), loc));
                }
            }

            return left;
        }

        static std::unique_ptr<Expr> parseAdditive(ParserCore &p)
        {
            auto left = parseMultiplicative(p);

            while (p.checkAny({TokenType::PLUS, TokenType::MINUS}))
            {
                BinOp op = p.check(TokenType::PLUS) ? BinOp::Add : BinOp::Sub;
                SourceLocation loc = p.current().location;
                p.advance();
                auto right = parseMultiplicative(p);
                left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                              BinaryOp(op, std::move(left), std::move(right), loc));
            }

            return left;
        }

        static std::unique_ptr<Expr> parseMultiplicative(ParserCore &p)
        {
            auto left = parseUnary(p);

            while (p.checkAny({TokenType::STAR, TokenType::SLASH}))
            {
                BinOp op = p.check(TokenType::STAR) ? BinOp::Mul : BinOp::Div;
                SourceLocation loc = p.current().location;
                p.advance();
                auto right = parseUnary(p);
                left = std::make_unique<Expr>(ExprKind::BinaryOp,
                                              BinaryOp(op, std::move(left), std::move(right), loc));
            }

            return left;
        }

        static std::unique_ptr<Expr> parseUnary(ParserCore &p)
        {
            if (p.check(TokenType::MINUS))
            {
                SourceLocation loc = p.current().location;
                ParseDepthScope depth(loc);
                p.advance();
                auto operand = parseUnary(p);
                return std::make_unique<Expr>(ExprKind::UnaryOp,
                                              UnaryOp(UnOp::Negate, std::move(operand), loc));
            }
            return parsePostfixExpr(p);
        }

        static std::unique_ptr<Expr> parsePostfixExpr(ParserCore &p)
        {
            auto base = LiteralParser::parsePrimary(p);
            return IndexParser::parsePostfix(p, std::move(base));
        }
    };

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
                    p.consume(TokenType::RBRACKET, "expected ']' to close slice");
                    base = std::make_unique<Expr>(ExprKind::SliceAccess,
                                                  SliceAccess(std::move(base), std::move(first), std::move(end), loc));
                }
                else
                {
                    p.consume(TokenType::RBRACKET, "expected ']' to close index");
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
                p.consume(TokenType::RPAREN, "expected ')' to close function call");

                std::string funcName;
                if (base->kind == ExprKind::Identifier)
                {
                    funcName = std::get<IdentifierExpr>(base->data).name;
                }
                else
                {
                    throw SyntaxError("cannot call non-identifier as function", loc);
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

    inline std::unique_ptr<Expr> LiteralParser::parseList(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACKET, "expected '[' for list literal");

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
        p.consume(TokenType::RBRACKET, "expected ']' to close list literal");
        return std::make_unique<Expr>(ExprKind::List, ListLiteral(std::move(elements), loc));
    }

    inline std::unique_ptr<Expr> LiteralParser::parseMap(ParserCore &p)
    {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACE, "expected '{' for map literal");

        std::vector<MapLiteral::Pair> pairs;
        p.skipNewlines();

        if (p.check(TokenType::RBRACE))
        {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
        }

        while (true)
        {
            auto key = ExpressionParser::parse(p);
            p.consume(TokenType::COLON, "expected ':' after map key");
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
            if (p.check(TokenType::RBRACE))
                break;
        }

        p.skipNewlines();
        p.consume(TokenType::RBRACE, "expected '}' to close map literal");
        return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
    }

    inline bool isBareIdentifierKeyword(TokenType t)
    {
        switch (t)
        {
        case TokenType::ADD:
        case TokenType::TO:
        case TokenType::IN:
        case TokenType::BY:
        case TokenType::GLOBAL:
        case TokenType::NOT:
        case TokenType::FROM:
            return true;
        default:
            return false;
        }
    }

    inline bool canStartExpressionToken(TokenType t)
    {
        switch (t)
        {
        case TokenType::IDENTIFIER:
        case TokenType::NUMBER:
        case TokenType::STRING:
        case TokenType::FSTRING:
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::EMPTY:
        case TokenType::LPAREN:
        case TokenType::LBRACKET:
        case TokenType::LBRACE:
        case TokenType::AWAIT:
        case TokenType::MATCH:
        case TokenType::FIND:
        case TokenType::REPLACE:
        case TokenType::SPLIT:
        case TokenType::COUNT:
        case TokenType::MINUS:
        case TokenType::BANG:
            return true;
        default:
            return isBareIdentifierKeyword(t);
        }
    }

    inline bool looksLikeConstructContinuation(TokenType t)
    {
        if (t == TokenType::LPAREN || t == TokenType::LBRACKET)
            return false;
        return canStartExpressionToken(t);
    }

    CUFF_NOINLINE inline std::unique_ptr<Expr> makeIdentifierExpr(ParserCore &p)
    {
        const SourceLocation loc = p.current().location;
        std::string name = p.current().value;
        p.advance();
        return std::make_unique<Expr>(ExprKind::Identifier, IdentifierExpr(std::move(name), loc));
    }

    inline std::unique_ptr<Expr> LiteralParser::parsePrimary(ParserCore &p)
    {
        const Token &tok = p.current();
        ParseDepthScope depth(tok.location);

        switch (tok.type)
        {
        case TokenType::NUMBER:
        {
            errno = 0;
            double val = std::strtod(tok.value.c_str(), nullptr);
            if (errno == ERANGE && std::isinf(val))
                throw SyntaxError(ErrorCode::InvalidNumberLiteral, "number literal is too large", tok.location);
            p.advance();
            return std::make_unique<Expr>(ExprKind::Number, NumberLiteral(val, tok.location));
        }
        case TokenType::STRING:
        {
            SourceLocation loc = tok.location;
            std::string val = tok.value;
            p.advance();
            return std::make_unique<Expr>(ExprKind::String, StringLiteral(std::move(val), loc));
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
            return makeIdentifierExpr(p);
        case TokenType::LBRACKET:
            return parseList(p);
        case TokenType::LBRACE:
            return parseMap(p);
        case TokenType::LPAREN:
        {
            p.advance();
            auto expr = ExpressionParser::parse(p);
            p.consume(TokenType::RPAREN, "expected ')' to close grouped expression");
            return expr;
        }
        case TokenType::AWAIT:
        {
            p.advance();
            auto inner = ExpressionParser::parse(p);
            if (inner->kind != ExprKind::FunctionCall)
                throw SyntaxError("expected a function call after 'await'", tok.location);
            FunctionCall fc = std::move(std::get<FunctionCall>(inner->data));
            auto callPtr = std::make_unique<FunctionCall>(std::move(fc));
            return std::make_unique<Expr>(ExprKind::Await, AwaitExpr(std::move(callPtr), tok.location));
        }
        case TokenType::MATCH:
            if (!looksLikeConstructContinuation(p.peek(1).type))
                goto bareIdentifier;
            return RegexExprParser::parseMatchFrom(p);
        case TokenType::FIND:
            if (!looksLikeConstructContinuation(p.peek(1).type))
                goto bareIdentifier;
            return RegexExprParser::parseFind(p);
        case TokenType::REPLACE:
            if (!looksLikeConstructContinuation(p.peek(1).type))
                goto bareIdentifier;
            return RegexExprParser::parseReplace(p);
        case TokenType::SPLIT:
            if (!looksLikeConstructContinuation(p.peek(1).type))
                goto bareIdentifier;
            return RegexExprParser::parseSplit(p);
        case TokenType::COUNT:
            if (!looksLikeConstructContinuation(p.peek(1).type))
                goto bareIdentifier;
            return RegexExprParser::parseCount(p);
        default:
            if (isBareIdentifierKeyword(tok.type))
                goto bareIdentifier;
            throw SyntaxError("unexpected token '" + tok.value + "' in expression", tok.location);
        }

    bareIdentifier:
        return makeIdentifierExpr(p);
    }

    inline std::unique_ptr<Expr> LiteralParser::parseEmbeddedExpression(
        const std::string &exprSource, const SourceLocation &fallbackLoc)
    {
        try
        {
            Tokenizer tokenizer(exprSource);
            std::vector<Token> rawTokens = tokenizer.tokenize();
            for (Token &t : rawTokens)
                t.location = fallbackLoc;
            Lexer lexer(std::move(rawTokens));
            std::vector<Token> innerTokens = lexer.lex();

            ParserCore innerParser(std::move(innerTokens));
            return ExpressionParser::parse(innerParser);
        }
        catch (const CuffError &e)
        {
            throw CuffError(e.code, e.message, fallbackLoc, e.hint);
        }
    }

    inline PatternArg RegexExprParser::parsePatternArg(ParserCore &p)
    {
        PatternArg arg;
        if (p.check(TokenType::STRING))
        {
            SourceLocation loc = p.current().location;
            arg.literalPattern = p.current().value;
            arg.isLiteral = true;
            p.advance();
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
        p.consume(TokenType::MATCH, "expected 'match'");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::FROM, "expected 'from' after match target");
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
        p.consume(TokenType::FIND, "expected 'find'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::FROM, "expected 'from' after find pattern");
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
        p.consume(TokenType::REPLACE, "expected 'replace'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::IN, "expected 'in' after replace pattern");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::TO, "expected 'to' after replace target");
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
        p.consume(TokenType::SPLIT, "expected 'split'");
        auto target = ExpressionParser::parseAdditive(p);
        p.consume(TokenType::BY, "expected 'by' after split target");
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
        p.consume(TokenType::COUNT, "expected 'count'");
        PatternArg pattern = parsePatternArg(p);
        p.consume(TokenType::IN, "expected 'in' after count pattern");
        auto target = ExpressionParser::parseAdditive(p);
        std::string flags = parseOptionalFlags(p);

        CountExpr c;
        c.pattern = std::move(pattern);
        c.target = std::move(target);
        c.flags = std::move(flags);
        c.loc = loc;
        return std::make_unique<Expr>(ExprKind::Count, std::move(c));
    }

}
