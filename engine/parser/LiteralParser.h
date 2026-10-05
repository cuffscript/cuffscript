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

    class ExpressionParser;

    class LiteralParser
    {
    public:
        static std::unique_ptr<Expr> parsePrimary(ParserCore &p);

        static std::unique_ptr<Expr> parseList(ParserCore &p);

        static std::unique_ptr<Expr> parseMap(ParserCore &p);

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
                    if (i + 1 < raw.size() && raw[i + 1] == '{')
                    {
                        currentText += '{';
                        i += 2;
                        continue;
                    }

                    flushText();

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
                        throw SyntaxError("unterminated '{' in f-string", tok.location);

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

}
