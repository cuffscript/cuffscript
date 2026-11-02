#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "LiteralParser.h"
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
        static std::vector<std::unique_ptr<Expr>> parseArgs(ParserCore &p);
    };

    class ExpressionParser
    {
    public:
        // Nodes are built in functions of their own, so that the temporaries of a node stay out of the frames of the
        // parse functions: every nesting level of the program passes through all of them, and the stack the parser
        // may use is budgeted per level (limits::kParseStackBytesPerLevel).
        CUFF_NOINLINE static std::unique_ptr<Expr> binary(BinOp op, std::unique_ptr<Expr> left,
                                                          std::unique_ptr<Expr> right, const SourceLocation &loc)
        {
            return std::make_unique<Expr>(ExprKind::BinaryOp, BinaryOp(op, std::move(left), std::move(right), loc));
        }

        CUFF_NOINLINE static std::unique_ptr<Expr> unary(UnOp op, std::unique_ptr<Expr> operand, const SourceLocation &loc)
        {
            return std::make_unique<Expr>(ExprKind::UnaryOp, UnaryOp(op, std::move(operand), loc));
        }

        // The common case, an expression without `or` or `and`, passes through here with a frame of a few words; the
        // operators are handled in a function of their own that is on the stack only when they occur.
        static std::unique_ptr<Expr> parse(ParserCore &p)
        {
            auto first = parseLogicalNot(p);
            if (!p.check(TokenType::AND) && !p.check(TokenType::OR))
                return first;
            return parseLogicalRest(p, std::move(first));
        }

        CUFF_NOINLINE static std::unique_ptr<Expr> parseLogicalRest(ParserCore &p, std::unique_ptr<Expr> first)
        {
            std::unique_ptr<Expr> disjunction;
            SourceLocation orLoc;
            auto conjunction = std::move(first);

            for (;;)
            {
                while (p.check(TokenType::AND))
                {
                    SourceLocation loc = p.current().location;
                    p.advance();
                    auto right = parseLogicalNot(p);
                    conjunction = binary(BinOp::And, std::move(conjunction), std::move(right), loc);
                }

                if (disjunction)
                    disjunction = binary(BinOp::Or, std::move(disjunction), std::move(conjunction), orLoc);
                else
                    disjunction = std::move(conjunction);

                if (!p.check(TokenType::OR))
                    return disjunction;
                orLoc = p.current().location;
                p.advance();
                conjunction = parseLogicalNot(p);
            }
        }

        static std::unique_ptr<Expr> parseLogicalNot(ParserCore &p)
        {
            if (p.check(TokenType::BANG))
            {
                SourceLocation loc = p.current().location;
                ParseDepthScope depth(loc);
                p.advance();
                auto operand = parseLogicalNot(p);
                return unary(UnOp::Not, std::move(operand), loc);
            }
            return parseComparison(p);
        }

        static std::unique_ptr<Expr> parseComparison(ParserCore &p)
        {
            auto left = parseAdditive(p);

            while (p.checkAny({TokenType::IS, TokenType::MATCHES, TokenType::ISA,
                               TokenType::GE, TokenType::LE, TokenType::GT, TokenType::LT}))
            {
                const Token &opTok = p.current();
                const SourceLocation loc = opTok.location;

                if (opTok.is(TokenType::ISA))
                {
                    p.advance();
                    const Token cls = p.consumeName("expected a class name after 'isa'");
                    left = std::make_unique<Expr>(ExprKind::Isa, IsaExpr(std::move(left), cls.value, loc));
                    continue;
                }

                BinOp binOp;
                switch (opTok.type)
                {
                case TokenType::IS: binOp = BinOp::Is; break;
                case TokenType::MATCHES: binOp = BinOp::Matches; break;
                case TokenType::GE: binOp = BinOp::GreaterEq; break;
                case TokenType::LE: binOp = BinOp::LessEq; break;
                case TokenType::GT: binOp = BinOp::Greater; break;
                default: binOp = BinOp::Less; break;
                }
                p.advance();

                if (binOp == BinOp::Is && p.check(TokenType::NOT))
                {
                    p.advance();
                    binOp = BinOp::IsNot;
                }

                auto right = parseAdditive(p);
                left = binary(binOp, std::move(left), std::move(right), loc);
            }

            if (p.current().is(TokenType::IDENTIFIER) && p.current().value == "IS")
                throw SyntaxError(ErrorCode::UnexpectedToken, "'IS' is not an operator", p.current().location, p.hintForUnexpected());
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
                left = binary(op, std::move(left), std::move(right), loc);
            }

            return left;
        }

        static std::unique_ptr<Expr> parseMultiplicative(ParserCore &p)
        {
            auto left = parseUnary(p);

            while (p.checkAny({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT}))
            {
                BinOp op = p.check(TokenType::STAR) ? BinOp::Mul : p.check(TokenType::SLASH) ? BinOp::Div : BinOp::Mod;
                SourceLocation loc = p.current().location;
                p.advance();
                auto right = parseUnary(p);
                left = binary(op, std::move(left), std::move(right), loc);
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
                return unary(UnOp::Negate, std::move(operand), loc);
            }

            auto base = parsePostfixExpr(p);
            if (!p.check(TokenType::POWER))
                return base;
            return parsePowerRest(p, std::move(base));
        }

        // `**` binds tighter than a unary minus on its left (-2 ** 2 is -4), takes a unary operand on its right
        // (2 ** -1) and groups to the right (2 ** 3 ** 2 is 2 ** 9).
        CUFF_NOINLINE static std::unique_ptr<Expr> parsePowerRest(ParserCore &p, std::unique_ptr<Expr> base)
        {
            SourceLocation loc = p.current().location;
            ParseDepthScope depth(loc);
            p.advance();
            auto exponent = parseUnary(p);
            return binary(BinOp::Pow, std::move(base), std::move(exponent), loc);
        }

        // The call after `await`: a function call, a method call or a `super` call.
        static std::unique_ptr<AwaitExpr> makeAwait(ParserCore &p, const SourceLocation &awaitLoc)
        {
            auto call = parsePostfixExpr(p);
            if (call->kind != ExprKind::FunctionCall && call->kind != ExprKind::MethodCall &&
                call->kind != ExprKind::SuperCall)
                throw SyntaxError("expected a function or method call after 'await'", awaitLoc);
            return std::make_unique<AwaitExpr>(std::move(call), awaitLoc);
        }

        static std::unique_ptr<Expr> parsePostfixExpr(ParserCore &p)
        {
            auto base = LiteralParser::parsePrimary(p);
            return IndexParser::parsePostfix(p, std::move(base));
        }
    };

    inline std::vector<std::unique_ptr<Expr>> IndexParser::parseArgs(ParserCore &p)
    {
        p.consume(TokenType::LPAREN, "expected '('");
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
        return args;
    }

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
                if (base->kind != ExprKind::Identifier && base->kind != ExprKind::Member)
                    throw SyntaxError("cannot call non-identifier as function", loc);

                auto args = IndexParser::parseArgs(p);

                if (base->kind == ExprKind::Identifier)
                {
                    const auto &id = std::get<IdentifierExpr>(base->data);
                    FunctionCall call(id.name, std::move(args), loc);
                    call.inWith = id.inWith;
                    base = std::make_unique<Expr>(ExprKind::FunctionCall, std::move(call));
                }
                else
                {
                    auto &member = std::get<MemberAccess>(base->data);
                    MethodCall call(std::move(member.target), member.name, std::move(args), member.loc);
                    base = std::make_unique<Expr>(ExprKind::MethodCall, std::move(call));
                }
            }
            else if (p.check(TokenType::DOT))
            {
                SourceLocation loc = p.current().location;
                p.advance();
                const Token name = p.consumeMemberName("expected a member name after '.'");
                base = std::make_unique<Expr>(ExprKind::Member, MemberAccess(std::move(base), name.value, loc));
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

    // The words of the operations that used to have a syntax of their own (find ... from, split ... by, and so on). Now
    // they are functions, so one of them followed by an operand is the old form; say what to write instead.
    inline bool isOldTextWord(const std::string &w)
    {
        return w == "find" || w == "match" || w == "replace" || w == "split" || w == "count";
    }

    inline bool startsOldTextOperand(const Token &t)
    {
        return t.is(TokenType::STRING) || t.is(TokenType::FSTRING) || t.is(TokenType::NUMBER) ||
               t.is(TokenType::IDENTIFIER) || t.is(TokenType::PATTERN);
    }

    [[noreturn]] CUFF_NOINLINE inline void throwOldTextOperation(ParserCore &p)
    {
        const Token &word = p.current();
        std::vector<const Token *> rest;
        int depth = 0;
        for (size_t i = p.pos + 1; i < p.tokens.size(); ++i)
        {
            const Token &t = p.tokens[i];
            if (t.is(TokenType::NEWLINE) || t.is(TokenType::EOF_TOKEN) || t.is(TokenType::END))
                break;
            if (t.is(TokenType::LPAREN) || t.is(TokenType::LBRACKET) || t.is(TokenType::LBRACE))
                ++depth;
            else if (t.is(TokenType::RPAREN) || t.is(TokenType::RBRACKET) || t.is(TokenType::RBRACE))
            {
                if (depth == 0)
                    break;
                --depth;
            }
            else if (depth == 0 && t.is(TokenType::COMMA))
                break;
            rest.push_back(&t);
        }

        auto find = [&](auto pred) -> int
        {
            int d = 0;
            for (size_t i = 0; i < rest.size(); ++i)
            {
                const Token &t = *rest[i];
                if (t.is(TokenType::LPAREN) || t.is(TokenType::LBRACKET) || t.is(TokenType::LBRACE))
                    ++d;
                else if (t.is(TokenType::RPAREN) || t.is(TokenType::RBRACKET) || t.is(TokenType::RBRACE))
                    --d;
                else if (d == 0 && pred(t))
                    return static_cast<int>(i);
            }
            return -1;
        };
        auto isFlags = [](const Token &t)
        {
            if (!t.is(TokenType::IDENTIFIER) || t.value.empty())
                return false;
            for (char c : t.value)
                if (c != 'g' && c != 'i' && c != 'm')
                    return false;
            return true;
        };

        const std::string name = word.value;
        std::string flags;
        std::string hint;
        if (name == "replace" && rest.size() >= 2 && rest[0]->is(TokenType::IDENTIFIER) && rest[1]->is(TokenType::LBRACKET))
        {
            hint = "use `change " + rest[0]->value + "[...] to value`";
        }
        else
        {
            const int anchor = name == "match" ? find([](const Token &t) { return t.is(TokenType::FROM); })
                             : name == "split" ? find([](const Token &t) { return t.is(TokenType::IDENTIFIER) && t.value == "by"; })
                             : name == "find" ? find([](const Token &t) { return t.is(TokenType::FROM); })
                                              : find([](const Token &t) { return t.is(TokenType::IN); });
            if (!rest.empty() && isFlags(*rest.back()) && rest.size() >= 3)
                flags = rest.back()->value;

            const Token *patternTok = nullptr;
            if (name == "match" || name == "split")
                patternTok = anchor >= 0 && static_cast<size_t>(anchor) + 1 < rest.size() ? rest[static_cast<size_t>(anchor) + 1] : nullptr;
            else
                patternTok = rest.empty() ? nullptr : rest[0];

            std::string pattern = "pattern";
            if (patternTok && patternTok->is(TokenType::STRING))
            {
                std::string escaped;
                for (char c : patternTok->value)
                {
                    if (c == '"')
                        escaped += '\\';
                    escaped += c;
                }
                pattern = "p\"" + escaped + "\"";
                if (flags.find('i') != std::string::npos)
                    pattern += 'i';
                if (flags.find('m') != std::string::npos)
                    pattern += 'm';
            }
            else if (patternTok && patternTok->is(TokenType::IDENTIFIER))
            {
                pattern = patternTok->value;
            }

            const bool all = flags.find('g') != std::string::npos;
            if (name == "find")
                hint = std::string("use ") + (all ? "find_all" : "find") + "(text, " + pattern + ")";
            else if (name == "match")
                hint = "use match(text, " + pattern + ")";
            else if (name == "split")
                hint = "use split(text, " + pattern + ")";
            else if (name == "count")
                hint = "use count(text, " + pattern + ")";
            else
            {
                std::string replacement = "replacement";
                const int toAt = find([](const Token &t) { return t.is(TokenType::TO); });
                if (toAt >= 0 && static_cast<size_t>(toAt) + 1 < rest.size() && rest[static_cast<size_t>(toAt) + 1]->is(TokenType::STRING))
                {
                    std::string escaped;
                    for (char c : rest[static_cast<size_t>(toAt) + 1]->value)
                    {
                        if (c == '"')
                            escaped += '\\';
                        escaped += c;
                    }
                    replacement = "\"" + escaped + "\"";
                }
                hint = std::string("use ") + (all ? "replace_all" : "replace") + "(text, " + pattern + ", " + replacement + ")";
            }
        }

        throw SyntaxError(ErrorCode::UnexpectedToken,
                          "unexpected " + describeToken(p.peek(1)) + " after '" + name + "'",
                          p.peek(1).location, hint);
    }

    CUFF_NOINLINE inline std::unique_ptr<Expr> makeIdentifierExpr(ParserCore &p)
    {
        const Token &tok = p.current();
        if (tok.is(TokenType::IDENTIFIER) && isOldTextWord(tok.value) && startsOldTextOperand(p.peek(1)))
            throwOldTextOperation(p);

        const SourceLocation loc = tok.location;
        std::string name = tok.value;
        p.advance();
        IdentifierExpr id(std::move(name), loc);
        id.inWith = p.ctx.withDepth > 0;
        return std::make_unique<Expr>(ExprKind::Identifier, std::move(id));
    }

    CUFF_NOINLINE inline std::unique_ptr<Expr> makePatternLiteral(ParserCore &p)
    {
        const Token &tok = p.current();
        auto data = makePatternData(tok.value, (tok.patternFlags & kPatternFlagIgnoreCase) != 0,
                                    (tok.patternFlags & kPatternFlagMultiline) != 0, tok.location);
        const SourceLocation loc = tok.location;
        p.advance();
        return std::make_unique<Expr>(ExprKind::Pattern, PatternLiteral(std::move(data), loc));
    }

    CUFF_NOINLINE inline std::unique_ptr<Expr> makeSuperCall(ParserCore &p)
    {
        const Token &tok = p.current();
        if (!p.ctx.inMethod)
            throw SyntaxError(ErrorCode::UnexpectedToken, "'super' can only be used inside a method", tok.location,
                              "a method is a func declared in a class body");
        if (!p.ctx.hasParent)
            throw SyntaxError(ErrorCode::UnexpectedToken, "'super' needs a parent class, and this class has no 'extends'",
                              tok.location, "declare the class as: class Name extends Parent do:");
        const SourceLocation loc = tok.location;
        p.advance();
        p.consume(TokenType::DOT, "expected '.' and a method name after 'super'");
        const Token name = p.consumeMemberName("expected a method name after 'super.'");
        if (!p.check(TokenType::LPAREN))
            p.failUnexpected("expected '(' after 'super." + name.value + "': super can only call a method");
        auto args = IndexParser::parseArgs(p);
        return std::make_unique<Expr>(ExprKind::SuperCall, SuperCall(name.value, std::move(args), loc));
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
        case TokenType::PATTERN:
            return makePatternLiteral(p);
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
        case TokenType::SELF:
        {
            requireSelfContext(p, tok);
            p.advance();
            return std::make_unique<Expr>(ExprKind::Self, SelfExpr(tok.location));
        }
        case TokenType::SUPER:
            return makeSuperCall(p);
        case TokenType::DOT:
        {
            requireWithContext(p, tok);
            const SourceLocation loc = tok.location;
            p.advance();
            const Token name = p.consumeMemberName("expected a member name after '.'");
            return std::make_unique<Expr>(ExprKind::Member, MemberAccess(nullptr, name.value, loc));
        }
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
            const SourceLocation loc = tok.location;
            p.advance();
            auto awaited = ExpressionParser::makeAwait(p, loc);
            return std::make_unique<Expr>(ExprKind::Await, std::move(*awaited));
        }
        default:
            if (isBareIdentifierKeyword(tok.type))
                return makeIdentifierExpr(p);
            throw SyntaxError(ErrorCode::UnexpectedToken, "unexpected " + describeToken(tok) + " in expression", tok.location,
                              p.hintForUnexpected());
        }
    }

    inline std::unique_ptr<Expr> LiteralParser::parseEmbeddedExpression(const std::string &exprSource,
                                                                        const FragmentOrigin &origin,
                                                                        const ParseContext &ctx)
    {
        // The fragment is scanned on its own, so its positions start from line 1, column 1; `origin` says where that
        // really is. An error is reported at the real place, and says which expression it is in.
        auto fail = [&](const CuffError &e, const SourceLocation &at)
        {
            return CuffError(e.code, "in f-string expression '{" + exprSource + "}': " + e.message, at, e.hint);
        };

        std::vector<Token> rawTokens;
        try
        {
            Tokenizer tokenizer(exprSource);
            rawTokens = tokenizer.tokenize();
        }
        catch (const CuffError &e)
        {
            throw fail(e, origin.map(e.location)); // raised while scanning the fragment: relative to it
        }
        for (Token &t : rawTokens)
            t.location = origin.map(t.location);

        try
        {
            Lexer lexer(std::move(rawTokens));
            std::vector<Token> innerTokens = lexer.lex();

            ParserCore innerParser(std::move(innerTokens));
            innerParser.ctx = ctx;
            std::unique_ptr<Expr> expr = ExpressionParser::parse(innerParser);
            innerParser.skipNewlinesOnly();
            if (!innerParser.atEnd())
                throw SyntaxError("unexpected " + describeToken(innerParser.current()) + " after the expression",
                                  innerParser.current().location);
            return expr;
        }
        catch (const CuffError &e)
        {
            throw fail(e, e.location); // raised on tokens that already carry real locations
        }
    }

}
