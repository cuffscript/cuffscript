#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../common/SourceText.h"
#include "../tokenizer/Escapes.h"
#include <cstdint>
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

            // Where each character of the literal's text really is in the program, so that what is inside the braces
            // can be placed at its true position and not just "somewhere in the f-string".
            std::vector<uint32_t> offsets;
            const bool mapped = p.source && buildValueOffsets(p.source->text, tok.location.offset, raw, offsets);
            auto originAt = [&](size_t valueIndex) { return FragmentOrigin{mapped ? &p.source->text : nullptr, &offsets, valueIndex, tok.location}; };

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
                        throw SyntaxError("unterminated '{' in f-string", originAt(i).map(SourceLocation()));

                    std::string exprSource = raw.substr(i + 1, j - i - 1);
                    auto innerExpr = parseEmbeddedExpression(exprSource, originAt(i + 1));

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
        // Where the text of one {...} lies in the program. An f-string's value is its text after escapes were
        // decoded, so an offset into the value is not an offset into the source; `offsets` bridges the two. With no
        // source text (a parser built without one) everything is placed at the start of the literal, as before.
        struct FragmentOrigin
        {
            const std::string *source;
            const std::vector<uint32_t> *offsets;
            size_t start;           // index in the value where this fragment's text begins
            SourceLocation literal; // where the literal itself begins (its `f`)

            // The location of a position given relative to the start of the fragment (line 1, column 1, offset 0).
            SourceLocation map(const SourceLocation &inFragment) const
            {
                const size_t index = start + static_cast<size_t>(inFragment.offset);
                if (!source || index >= offsets->size())
                    return literal;
                return locationAfter(*source, literal, (*offsets)[index]);
            }
        };

        // Decodes the literal that starts at `tokenOffset` of `source` the way the tokenizer did, recording for each
        // byte of the result the offset it came from (plus one entry for the end). Returns false, and the caller
        // falls back to placing everything at the literal's start, unless decoding reproduces `value` exactly: a
        // mismatch means the two disagree about something, and a wrong position is worse than a coarse one.
        static bool buildValueOffsets(const std::string &source, int tokenOffset, const std::string &value,
                                      std::vector<uint32_t> &offsets)
        {
            if (tokenOffset < 0 || static_cast<size_t>(tokenOffset) + 2 > source.size() ||
                source.compare(static_cast<size_t>(tokenOffset), 2, "f\"") != 0)
                return false;
            std::string rebuilt;
            offsets.clear();
            offsets.reserve(value.size() + 1);
            size_t at = static_cast<size_t>(tokenOffset) + 2;
            while (at < source.size() && rebuilt.size() < value.size())
            {
                if (source[at] == '\\' && at + 1 < source.size())
                {
                    const size_t before = rebuilt.size();
                    appendEscaped(rebuilt, static_cast<unsigned char>(source[at + 1]));
                    offsets.insert(offsets.end(), rebuilt.size() - before, static_cast<uint32_t>(at));
                    at += 2;
                }
                else
                {
                    rebuilt += source[at];
                    offsets.push_back(static_cast<uint32_t>(at));
                    ++at;
                }
            }
            offsets.push_back(static_cast<uint32_t>(at));
            return rebuilt == value;
        }

        static std::unique_ptr<Expr> parseEmbeddedExpression(const std::string &exprSource, const FragmentOrigin &origin);
    };

}
