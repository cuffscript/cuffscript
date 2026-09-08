#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>
#include <cstdlib>

namespace cuff
{

    // Forward declaration — ExpressionParser and LiteralParser are mutually recursive
    class ExpressionParser;

    // Parses literal values: numbers, strings, booleans, empty, lists, maps, f-strings.
    // Also resolves identifiers as expressions.
    class LiteralParser
    {
    public:
        static std::unique_ptr<Expr> parsePrimary(ParserCore &p);

        // Parse a list literal: [expr, expr, ...]
        // (Body deferred to the bottom of ExpressionParser.h — it calls
        //  ExpressionParser::parse, which is mutually recursive with LiteralParser
        //  and therefore only forward-declared at this point in the header chain.)
        static std::unique_ptr<Expr> parseList(ParserCore &p);

        // Parse a map literal: {"key": value, "key2": value2}
        // (Body deferred — see parseList above.)
        static std::unique_ptr<Expr> parseMap(ParserCore &p);

        // Parse an f-string into segments of literal text and embedded expressions.
        static std::unique_ptr<Expr> parseFString(ParserCore &p)
        {
            const Token &tok = p.current();
            p.advance();

            std::vector<FStringExpr::Segment> segments;
            const std::string &raw = tok.value;

            size_t i = 0;
            std::string currentText;

            auto flushText = [&]()
            {
                if (!currentText.empty())
                {
                    FStringExpr::Segment seg;
                    seg.isExpression = false;
                    seg.text = currentText;
                    segments.push_back(std::move(seg));
                    currentText.clear();
                }
            };

            while (i < raw.size())
            {
                char c = raw[i];

                if (c == '{')
                {
                    // "{{" is an escaped literal '{' (mirrors "}}" below).
                    if (i + 1 < raw.size() && raw[i + 1] == '{')
                    {
                        currentText += '{';
                        i += 2;
                        continue;
                    }

                    flushText();

                    // Find the matching '}', tracking nested brace depth so an
                    // embedded expression that itself contains braces (e.g. a
                    // map literal: f"{ {"a": 1} }") is captured as one piece
                    // instead of stopping at the first '}'.
                    size_t j = i + 1;
                    int depth = 1;
                    while (j < raw.size() && depth > 0)
                    {
                        if (raw[j] == '{')
                            ++depth;
                        else if (raw[j] == '}')
                        {
                            --depth;
                            if (depth == 0)
                                break;
                        }
                        ++j;
                    }
                    if (depth != 0)
                        throw SyntaxError("Unterminated '{' in f-string", tok.location);

                    std::string exprSource = raw.substr(i + 1, j - i - 1);
                    auto innerExpr = parseEmbeddedExpression(exprSource, tok.location);

                    FStringExpr::Segment seg;
                    seg.isExpression = true;
                    seg.expr = std::move(innerExpr);
                    segments.push_back(std::move(seg));

                    i = j + 1;
                }
                else if (c == '}')
                {
                    // "}}" is an escaped literal '}'. A lone, unmatched '}'
                    // (not part of any "{...}" above) is also treated as a
                    // literal '}' rather than an error — there's no ambiguity
                    // the way there is for an unmatched '{'.
                    if (i + 1 < raw.size() && raw[i + 1] == '}')
                    {
                        currentText += '}';
                        i += 2;
                        continue;
                    }
                    currentText += '}';
                    ++i;
                }
                else
                {
                    currentText += c;
                    ++i;
                }
            }

            flushText();

            return std::make_unique<Expr>(ExprKind::FString, FStringExpr(std::move(segments), tok.location));
        }

    private:
        static std::unique_ptr<Expr> parseEmbeddedExpression(const std::string &exprSource,
                                                             const SourceLocation &fallbackLoc);
    };

} // namespace cuff
