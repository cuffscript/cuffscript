#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "LiteralParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../tokenizer/Tokenizer.h"
#include "../lexer/Lexer.h"
#include <memory>
#include <string>
#include <cstdlib>

namespace cuff {

// Index/postfix parser — handles [index], [start~end], and function calls (args).
class IndexParser {
public:
    static std::unique_ptr<Expr> parsePostfix(ParserCore& p, std::unique_ptr<Expr> base) {
        while (true) {
            if (p.check(TokenType::LBRACKET)) {
                SourceLocation loc = p.current().location;
                p.advance();

                auto first = ExpressionParser::parse(p);

                if (p.match(TokenType::TILDE)) {
                    auto end = ExpressionParser::parse(p);
                    p.consume(TokenType::RBRACKET, "Expected ']' to close slice");
                    base = std::make_unique<Expr>(ExprKind::SliceAccess,
                        SliceAccess(std::move(base), std::move(first), std::move(end), loc));
                } else {
                    p.consume(TokenType::RBRACKET, "Expected ']' to close index");
                    base = std::make_unique<Expr>(ExprKind::IndexAccess,
                        IndexAccess(std::move(base), std::move(first), loc));
                }
            } else if (p.check(TokenType::LPAREN)) {
                SourceLocation loc = p.current().location;
                p.advance();

                std::vector<std::unique_ptr<Expr>> args;
                p.skipNewlines();

                if (!p.check(TokenType::RPAREN)) {
                    args.push_back(ExpressionParser::parse(p));
                    while (p.match(TokenType::COMMA)) {
                        p.skipNewlines();
                        args.push_back(ExpressionParser::parse(p));
                    }
                }

                p.skipNewlines();
                p.consume(TokenType::RPAREN, "Expected ')' to close function call");

                std::string funcName;
                if (base->kind == ExprKind::Identifier) {
                    funcName = std::get<IdentifierExpr>(base->data).name;
                } else {
                    throw SyntaxError("Cannot call non-identifier as function", loc);
                }

                base = std::make_unique<Expr>(ExprKind::FunctionCall,
                    FunctionCall(std::move(funcName), std::move(args), loc));
            } else {
                break;
            }
        }
        return base;
    }
};

// Expression parser with operator precedence.
// Precedence (lowest to highest):
//   1. Comparison: is, IS, >=, <=, >, <
//   2. Additive: +, -
//   3. Multiplicative: *, /
//   4. Unary: ! (NOT), - (negation)
//   5. Postfix: index, slice, function call
//   6. Primary: literals, identifiers, parentheses
class ExpressionParser {
public:
    static std::unique_ptr<Expr> parse(ParserCore& p) {
        return parseComparison(p);
    }

    static std::unique_ptr<Expr> parseComparison(ParserCore& p) {
        auto left = parseAdditive(p);

        while (p.checkAny({TokenType::IS_STRICT, TokenType::IS_CASEINSENSITIVE,
                           TokenType::GE, TokenType::LE, TokenType::GT, TokenType::LT})) {
            const Token& opTok = p.current();
            std::string opStr = opTok.value;
            if (opTok.is(TokenType::IS_STRICT)) opStr = "is";
            else if (opTok.is(TokenType::IS_CASEINSENSITIVE)) opStr = "IS";
            SourceLocation loc = opTok.location;
            p.advance();

            auto right = parseAdditive(p);

            // Check for regex match: is/IS followed by a string literal
            if ((opTok.is(TokenType::IS_STRICT) || opTok.is(TokenType::IS_CASEINSENSITIVE))
                && right->kind == ExprKind::String) {
                std::string pattern = std::get<StringLiteral>(right->data).value;
                bool caseInsensitive = opTok.is(TokenType::IS_CASEINSENSITIVE);
                left = std::make_unique<Expr>(ExprKind::RegexMatch,
                    RegexMatchExpr(std::move(left), caseInsensitive, std::move(pattern), loc));
            } else {
                left = std::make_unique<Expr>(ExprKind::BinaryOp,
                    BinaryOp(std::move(opStr), std::move(left), std::move(right), loc));
            }
        }

        return left;
    }

    static std::unique_ptr<Expr> parseAdditive(ParserCore& p) {
        auto left = parseMultiplicative(p);

        while (p.checkAny({TokenType::PLUS, TokenType::MINUS})) {
            std::string op = p.current().value;
            SourceLocation loc = p.current().location;
            p.advance();
            auto right = parseMultiplicative(p);
            left = std::make_unique<Expr>(ExprKind::BinaryOp,
                BinaryOp(std::move(op), std::move(left), std::move(right), loc));
        }

        return left;
    }

    static std::unique_ptr<Expr> parseMultiplicative(ParserCore& p) {
        auto left = parseUnary(p);

        while (p.checkAny({TokenType::STAR, TokenType::SLASH})) {
            std::string op = p.current().value;
            SourceLocation loc = p.current().location;
            p.advance();
            auto right = parseUnary(p);
            left = std::make_unique<Expr>(ExprKind::BinaryOp,
                BinaryOp(std::move(op), std::move(left), std::move(right), loc));
        }

        return left;
    }

    static std::unique_ptr<Expr> parseUnary(ParserCore& p) {
        if (p.check(TokenType::BANG)) {
            SourceLocation loc = p.current().location;
            p.advance();
            auto operand = parseUnary(p);
            return std::make_unique<Expr>(ExprKind::UnaryOp,
                UnaryOp("!", std::move(operand), loc));
        }
        if (p.check(TokenType::MINUS)) {
            SourceLocation loc = p.current().location;
            p.advance();
            auto operand = parseUnary(p);
            return std::make_unique<Expr>(ExprKind::UnaryOp,
                UnaryOp("-", std::move(operand), loc));
        }
        return parsePostfixExpr(p);
    }

    static std::unique_ptr<Expr> parsePostfixExpr(ParserCore& p) {
        auto base = LiteralParser::parsePrimary(p);
        return IndexParser::parsePostfix(p, std::move(base));
    }
};

// ---- Deferred implementations ----

inline std::unique_ptr<Expr> LiteralParser::parsePrimary(ParserCore& p) {
    const Token& tok = p.current();

    switch (tok.type) {
    case TokenType::NUMBER: {
        double val = std::stod(tok.value);
        p.advance();
        return std::make_unique<Expr>(ExprKind::Number, NumberLiteral(val, tok.location));
    }
    case TokenType::STRING: {
        std::string val = tok.value;
        p.advance();
        return std::make_unique<Expr>(ExprKind::String, StringLiteral(std::move(val), tok.location));
    }
    case TokenType::FSTRING:
        return parseFString(p);
    case TokenType::TRUE: {
        p.advance();
        return std::make_unique<Expr>(ExprKind::Bool, BoolLiteral(true, tok.location));
    }
    case TokenType::FALSE: {
        p.advance();
        return std::make_unique<Expr>(ExprKind::Bool, BoolLiteral(false, tok.location));
    }
    case TokenType::EMPTY: {
        p.advance();
        return std::make_unique<Expr>(ExprKind::Empty, EmptyLiteral(tok.location));
    }
    case TokenType::IDENTIFIER: {
        std::string name = tok.value;
        p.advance();
        return std::make_unique<Expr>(ExprKind::Identifier, IdentifierExpr(std::move(name), tok.location));
    }
    case TokenType::LBRACKET:
        return parseList(p);
    case TokenType::LBRACE:
        return parseMap(p);
    case TokenType::LPAREN: {
        p.advance();
        auto expr = ExpressionParser::parse(p);
        p.consume(TokenType::RPAREN, "Expected ')' to close grouped expression");
        return expr;
    }
    default:
        throw SyntaxError("Unexpected token '" + tok.value + "' in expression", tok.location);
    }
}

inline std::unique_ptr<Expr> LiteralParser::parseEmbeddedExpression(
        const std::string& exprSource, const SourceLocation& /*fallbackLoc*/) {
    // Tokenize and lex the inner expression source
    Tokenizer tokenizer(exprSource);
    std::vector<Token> rawTokens = tokenizer.tokenize();
    Lexer lexer(std::move(rawTokens));
    std::vector<Token> innerTokens = lexer.lex();

    // Create a sub-parser and parse the expression
    ParserCore innerParser(std::move(innerTokens));
    return ExpressionParser::parse(innerParser);
}

} // namespace cuff
