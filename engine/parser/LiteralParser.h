#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>
#include <cstdlib>

namespace cuff {

// Forward declaration — ExpressionParser and LiteralParser are mutually recursive
class ExpressionParser;

// Parses literal values: numbers, strings, booleans, empty, lists, maps, f-strings.
// Also resolves identifiers as expressions.
class LiteralParser {
public:
    static std::unique_ptr<Expr> parsePrimary(ParserCore& p);

    // Parse a list literal: [expr, expr, ...]
    static std::unique_ptr<Expr> parseList(ParserCore& p) {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACKET, "Expected '[' for list literal");

        std::vector<std::unique_ptr<Expr>> elements;
        p.skipNewlines();

        if (p.check(TokenType::RBRACKET)) {
            p.advance();
            return std::make_unique<Expr>(ExprKind::List, ListLiteral(std::move(elements), loc));
        }

        elements.push_back(ExpressionParser::parse(p));
        while (true) {
            p.skipNewlines();
            if (p.match(TokenType::COMMA)) {
                p.skipNewlines();
                elements.push_back(ExpressionParser::parse(p));
            } else {
                break;
            }
        }

        p.skipNewlines();
        p.consume(TokenType::RBRACKET, "Expected ']' to close list literal");
        return std::make_unique<Expr>(ExprKind::List, ListLiteral(std::move(elements), loc));
    }

    // Parse a map literal: {"key": value, "key2": value2}
    static std::unique_ptr<Expr> parseMap(ParserCore& p) {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LBRACE, "Expected '{' for map literal");

        std::vector<MapLiteral::Pair> pairs;
        p.skipNewlines();

        if (p.check(TokenType::RBRACE)) {
            p.advance();
            return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
        }

        // Parse key: value pairs
        while (true) {
            auto key = ExpressionParser::parse(p);
            p.consume(TokenType::COLON, "Expected ':' after map key");
            p.skipNewlines();
            auto value = ExpressionParser::parse(p);

            MapLiteral::Pair pair;
            pair.key = std::move(key);
            pair.value = std::move(value);
            pairs.push_back(std::move(pair));

            p.skipNewlines();
            if (!p.match(TokenType::COMMA)) break;
            p.skipNewlines();
        }

        p.skipNewlines();
        p.consume(TokenType::RBRACE, "Expected '}' to close map literal");
        return std::make_unique<Expr>(ExprKind::Map, MapLiteral(std::move(pairs), loc));
    }

    // Parse an f-string into segments of literal text and embedded expressions.
    static std::unique_ptr<Expr> parseFString(ParserCore& p) {
        const Token& tok = p.current();
        p.advance();

        std::vector<FStringExpr::Segment> segments;
        const std::string& raw = tok.value;

        size_t i = 0;
        std::string currentText;

        while (i < raw.size()) {
            if (raw[i] == '{') {
                if (!currentText.empty()) {
                    FStringExpr::Segment seg;
                    seg.isExpression = false;
                    seg.text = currentText;
                    segments.push_back(std::move(seg));
                    currentText.clear();
                }

                size_t close = raw.find('}', i + 1);
                if (close == std::string::npos) {
                    throw SyntaxError("Unterminated '{}' in f-string", tok.location);
                }

                std::string exprSource = raw.substr(i + 1, close - i - 1);
                auto innerExpr = parseEmbeddedExpression(exprSource, tok.location);

                FStringExpr::Segment seg;
                seg.isExpression = true;
                seg.expr = std::move(innerExpr);
                segments.push_back(std::move(seg));

                i = close + 1;
            } else {
                currentText += raw[i];
                ++i;
            }
        }

        if (!currentText.empty()) {
            FStringExpr::Segment seg;
            seg.isExpression = false;
            seg.text = currentText;
            segments.push_back(std::move(seg));
        }

        return std::make_unique<Expr>(ExprKind::FString, FStringExpr(std::move(segments), tok.location));
    }

private:
    static std::unique_ptr<Expr> parseEmbeddedExpression(const std::string& exprSource,
                                                         const SourceLocation& fallbackLoc);
};

} // namespace cuff
