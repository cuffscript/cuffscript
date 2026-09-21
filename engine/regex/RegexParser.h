#pragma once

#include "RegexAst.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/SourceLocation.h"
#include "../common/Utf8.h"
#include <string>
#include <vector>
#include <cctype>

namespace cuff::regex
{

    // Recursive-descent compiler: pattern text -> RNode tree.
    //
    // Grammar (informal — see docs/REGEX.md):
    //   Sequence   := Atom*
    //   Atom       := PrimaryAtom Quantifier?
    //   PrimaryAtom:=
    //         '(' Sequence ')'                    -> numbered Group
    //       | '<' name ':' Sequence '>'            -> NamedGroup
    //       | '[' BracketBody ']'                  -> CharTest / Preset / OneOf / anchors
    //       | '\' EscapedChar                      -> literal CharTest
    //       | AnyOtherChar                         -> literal CharTest
    //   Quantifier := ('+' | '*' | '?' | Digits ('~' Digits?)? | '~' Digits) '?'?
    //
    // There is no bare top-level alternation (`|`) outside of `[one:...]` —
    // the language spec never shows one, so keeping the grammar to this
    // (much simpler, unambiguous) shape is a deliberate scope decision.
    class RegexParser
    {
    public:
        // `loc` is only used to attribute thrown errors to a sensible source
        // location in the *host* CuffScript program (the pattern string
        // itself has no meaningful line/column of its own).
        RegexParser(const std::string &pattern, cuff::SourceLocation loc)
            : text_(pattern), loc_(loc), pos_(0), nextGroupIndex_(1) {}

        // Returns the compiled root (always a Sequence node) plus how many
        // numbered capture groups it declared.
        RNodePtr parse(int &outGroupCount)
        {
            RNodePtr root = parseSequence(/*stopChars=*/"");
            if (pos_ != text_.size())
            {
                // A stray unmatched closing bracket of some kind.
                fail(cuff::ErrorCode::RegexUnexpectedCharacter,
                     std::string("pattern has an unexpected trailing character '") + text_[pos_] + "'");
            }
            outGroupCount = nextGroupIndex_ - 1;
            return root;
        }

    private:
        const std::string &text_;
        cuff::SourceLocation loc_;
        size_t pos_;
        int nextGroupIndex_;
        int nesting_ = 0;

        // Groups are parsed recursively, so their nesting depth is capped to
        // keep a hostile pattern from exhausting the native stack.
        struct NestingScope
        {
            RegexParser &p;
            explicit NestingScope(RegexParser &parser) : p(parser)
            {
                if (++p.nesting_ > limits::kMaxRegexNesting)
                    p.fail(cuff::ErrorCode::RegexPatternTooComplex,
                           "groups are nested too deeply (maximum is " + std::to_string(limits::kMaxRegexNesting) + ")");
            }
            ~NestingScope() { --p.nesting_; }
        };

        [[noreturn]] void fail(cuff::ErrorCode code, const std::string &msg, const std::string &hint = "")
        {
            const std::string shown = text_.size() > 120 ? text_.substr(0, 120) + "..." : text_;
            throw cuff::RegexSyntaxError(code, "in pattern \"" + shown + "\": " + msg, loc_, hint);
        }

        bool atEnd() const { return pos_ >= text_.size(); }
        char peek(int ahead = 0) const
        {
            size_t i = pos_ + static_cast<size_t>(ahead);
            return i < text_.size() ? text_[i] : '\0';
        }
        char advance() { return text_[pos_++]; }

        static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

        int parseNumber()
        {
            long long value = 0;
            while (!atEnd() && isDigitChar(peek()))
            {
                value = value * 10 + (advance() - '0');
                if (value > limits::kMaxRegexQuantifier)
                    fail(cuff::ErrorCode::RegexInvalidQuantifierRange,
                         "quantifier count is too large (maximum is " + std::to_string(limits::kMaxRegexQuantifier) + ")");
            }
            return static_cast<int>(value);
        }

        // Sequence of atoms, stopping at end-of-text or when the current
        // character is one of `stopChars` (used for ')' and '>' terminators
        // of nested groups — the caller consumes the terminator itself).
        RNodePtr parseSequence(const std::string &stopChars)
        {
            RNodePtr seq = makeNode(RNodeKind::Sequence);
            while (!atEnd() && stopChars.find(peek()) == std::string::npos)
            {
                seq->children.push_back(parseAtom());
            }
            return seq;
        }

        RNodePtr parseAtom()
        {
            RNodePtr primary = parsePrimaryAtom();
            return applyQuantifier(primary);
        }

        RNodePtr parsePrimaryAtom()
        {
            char c = peek();

            if (c == '(')
            {
                advance();
                NestingScope nest(*this);
                int idx = nextGroupIndex_++;
                RNodePtr inner = parseSequence(")");
                if (atEnd())
                    fail(cuff::ErrorCode::RegexUnclosedGroup, "unclosed '(' — missing ')'");
                advance(); // consume ')'
                RNodePtr g = makeNode(RNodeKind::Group);
                g->groupIndex = idx;
                g->child = inner;
                return g;
            }

            if (c == '<')
            {
                advance();
                std::string name;
                while (!atEnd() && peek() != ':' && peek() != '>')
                    name += advance();
                if (name.empty())
                    fail(cuff::ErrorCode::RegexEmptyToken, "named capture is missing a name, e.g. <name:...>");
                if (atEnd() || peek() != ':')
                    fail(cuff::ErrorCode::RegexUnclosedGroup, "named capture <" + name + "...> is missing ':'");
                // Colon-spacing rule: no space directly before ':'.
                if (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                    fail(cuff::ErrorCode::RegexInvalidColonSpacing,
                         "no space is allowed before ':' in a named capture",
                         "write <" + trimTrailingSpace(name) + ":...> instead");
                advance(); // consume ':'
                NestingScope nest(*this);
                RNodePtr inner = parseSequence(">");
                if (atEnd())
                    fail(cuff::ErrorCode::RegexUnclosedGroup, "unclosed named capture '<" + name + ":...>' — missing '>'");
                advance(); // consume '>'
                RNodePtr g = makeNode(RNodeKind::NamedGroup);
                g->groupName = name;
                g->child = inner;
                return g;
            }

            if (c == '[')
            {
                return parseBracket();
            }

            if (c == '\\')
            {
                advance();
                if (atEnd())
                    fail(cuff::ErrorCode::RegexInvalidEscape, "dangling '\\' at end of pattern");
                char esc = advance();
                static const std::string escapable = "[](){}<>+*?~|.\\:!";
                if (escapable.find(esc) == std::string::npos)
                    fail(cuff::ErrorCode::RegexInvalidEscape,
                         std::string("'\\") + esc + "' is not a recognized escape sequence",
                         "CuffScript patterns only support escaping literal symbols such as \\. \\+ \\( \\[ — there is no \\d/\\w/\\s style escape");
                return literalNode(static_cast<unsigned char>(esc));
            }

            if (c == ')' || c == '>' || c == ']')
            {
                fail(cuff::ErrorCode::RegexUnexpectedCharacter,
                     std::string("unexpected '") + c + "' with no matching opener");
            }

            // Every other character (including '.') is a plain literal.
            // Design decision: unlike traditional regex, '.' is NOT a wildcard
            // here — CuffScript already provides [any] for that — so literal
            // dots (extremely common in emails/filenames/version strings)
            // compare exactly like every other character. `\.` is still
            // accepted (see escape handling above) and produces the same node.
            //
            // A non-ASCII byte here starts a multi-byte UTF-8 character (e.g.
            // a literal Korean character written directly in the pattern) —
            // consume the whole sequence and match it as one codepoint, not
            // byte-by-byte (which would require the *text* to also happen to
            // split at the same byte offsets to match).
            {
                unsigned char uc = static_cast<unsigned char>(c);
                size_t seqLen = cuff::utf8::seqLen(uc);
                if (seqLen == 1)
                {
                    advance();
                    return literalNode(uc);
                }
                std::string seq;
                for (size_t i = 0; i < seqLen && !atEnd(); ++i)
                    seq += advance();
                return literalCodepointNode(seq);
            }
        }

        static std::string trimTrailingSpace(std::string s)
        {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            return s;
        }

        RNodePtr literalNode(unsigned char c)
        {
            RNodePtr n = makeNode(RNodeKind::CharTest);
            n->charTest = [c](unsigned char x)
            { return x == c; };
            return n;
        }

        RNodePtr literalCodepointNode(const std::string &seq)
        {
            RNodePtr n = makeNode(RNodeKind::CharTest);
            n->multiByteLiteral = seq;
            return n;
        }

        // Parses the content of '[' ... ']' — dispatches to a named class,
        // a preset, [one:...], an anchor/boundary token, or a literal/range
        // character set (with optional leading '!' negation).
        RNodePtr parseBracket()
        {
            advance(); // consume '['
            size_t bodyStart = pos_;

            // Find the matching ']' (patterns never nest brackets).
            size_t closeAt = text_.find(']', pos_);
            if (closeAt == std::string::npos)
                fail(cuff::ErrorCode::RegexUnclosedBracket, "unclosed '[' — missing ']'");

            std::string body = text_.substr(bodyStart, closeAt - bodyStart);
            pos_ = closeAt + 1; // consume through ']'

            if (body.empty())
                fail(cuff::ErrorCode::RegexEmptyToken, "empty '[]' token");

            // Named-class / preset / anchor tokens (exact keyword match).
            if (body == "num")
                return classNode(classNum);
            if (body == "let")
                return classNode(classLet);
            if (body == "low")
                return classNode(classLow);
            if (body == "up")
                return classNode(classUp);
            if (body == "str")
                return classNode(classStr);
            if (body == "word")
                return classNode(classWord);
            if (body == "sp")
                return classNode(classSp);
            if (body == "nl")
                return classNode(classNl);
            if (body == "any")
                return anyCodepointNode();
            if (body == "hex")
                return classNode(classHex);
            if (body == "edge")
                return makeNode(RNodeKind::WordBoundary);
            if (body == "start")
                return makeNode(RNodeKind::StartAnchor);
            if (body == "end")
                return makeNode(RNodeKind::EndAnchor);
            if (body == "int")
                return presetNode(PresetKind::Int);
            if (body == "float")
                return presetNode(PresetKind::Float);
            if (body == "email")
                return presetNode(PresetKind::Email);
            if (body == "phone")
                return presetNode(PresetKind::Phone);
            if (body == "url")
                return presetNode(PresetKind::Url);

            // Route to the [one:...] alternative-selection parser whenever the
            // body contains a ':' and the part before it (ignoring trailing
            // spaces, so "[one :a|b]" is recognized as a *misspaced* one-token
            // rather than silently falling through) is "one". A bare "[one]"
            // with no colon at all is deliberately treated as an ordinary
            // 3-character literal set {o, n, e} — there is no reserved-word
            // collision unless a colon is actually present.
            size_t colonProbe = body.find(':');
            if (colonProbe != std::string::npos && trimTrailingSpace(body.substr(0, colonProbe)) == "one")
            {
                return parseOneOf(body);
            }

            // Negated set: '!' followed by either a named class or a literal set.
            bool negate = false;
            std::string setBody = body;
            if (!setBody.empty() && setBody[0] == '!')
            {
                negate = true;
                setBody = setBody.substr(1);
            }

            if (setBody.empty())
                fail(cuff::ErrorCode::RegexEmptyToken, "empty character set '[" + body + "]'");

            // Negating a named class, e.g. [!sp], [!num].
            std::function<bool(unsigned char)> base;
            if (setBody == "num")
                base = classNum;
            else if (setBody == "let")
                base = classLet;
            else if (setBody == "low")
                base = classLow;
            else if (setBody == "up")
                base = classUp;
            else if (setBody == "str")
                base = classStr;
            else if (setBody == "word")
                base = classWord;
            else if (setBody == "sp")
                base = classSp;
            else if (setBody == "nl")
                base = classNl;
            else if (setBody == "hex")
                base = classHex;
            else
                base = buildLiteralSetPredicate(setBody);

            if (negate)
            {
                RNodePtr n = makeNode(RNodeKind::CharTest);
                n->negated = true;
                n->charTest = [base](unsigned char c)
                { return !base(c); };
                return n;
            }
            RNodePtr n = makeNode(RNodeKind::CharTest);
            n->charTest = base;
            return n;
        }

        // Builds a predicate for a literal character set body like "abc" or
        // "a-z0-9" (used for both `[abc]` and the inside of `[!...]`).
        std::function<bool(unsigned char)> buildLiteralSetPredicate(const std::string &body)
        {
            std::vector<std::pair<unsigned char, unsigned char>> ranges;
            size_t i = 0;
            while (i < body.size())
            {
                unsigned char lo = static_cast<unsigned char>(body[i]);
                if (i + 2 < body.size() && body[i + 1] == '-')
                {
                    unsigned char hi = static_cast<unsigned char>(body[i + 2]);
                    if (lo > hi)
                        fail(cuff::ErrorCode::RegexInvalidQuantifierRange,
                             std::string("invalid character range '") + body[i] + "-" + body[i + 2] + "' (start after end)");
                    ranges.emplace_back(lo, hi);
                    i += 3;
                }
                else
                {
                    ranges.emplace_back(lo, lo);
                    i += 1;
                }
            }
            return [ranges](unsigned char c)
            {
                for (auto &r : ranges)
                    if (c >= r.first && c <= r.second)
                        return true;
                return false;
            };
        }

        RNodePtr classNode(bool (*pred)(unsigned char))
        {
            RNodePtr n = makeNode(RNodeKind::CharTest);
            n->charTest = pred;
            return n;
        }

        RNodePtr anyCodepointNode()
        {
            RNodePtr n = makeNode(RNodeKind::CharTest);
            n->isAnyCodepoint = true;
            return n;
        }

        RNodePtr presetNode(PresetKind k)
        {
            RNodePtr n = makeNode(RNodeKind::Preset);
            n->presetKind = k;
            return n;
        }

        // [one:apple|banana|orange] — colon-spacing rule enforced, empty
        // alternatives rejected.
        RNodePtr parseOneOf(const std::string &body)
        {
            size_t colonPos = body.find(':');
            if (colonPos == std::string::npos)
                fail(cuff::ErrorCode::RegexUnclosedGroup, "'[one...]' is missing ':' — expected '[one:a|b|c]'");
            if (colonPos > 0 && (body[colonPos - 1] == ' ' || body[colonPos - 1] == '\t'))
                fail(cuff::ErrorCode::RegexInvalidColonSpacing,
                     "no space is allowed before ':' in '[one:...]'",
                     "write [one:...] instead of [one :...]");
            if (body.compare(0, colonPos, "one") != 0)
                fail(cuff::ErrorCode::RegexUnknownToken, "unknown bracket token '[" + body + "]'");

            std::string rest = body.substr(colonPos + 1);
            if (rest.empty())
                fail(cuff::ErrorCode::RegexEmptyToken, "'[one:...]' has no alternatives");

            RNodePtr n = makeNode(RNodeKind::OneOf);
            std::string current;
            for (size_t i = 0; i <= rest.size(); ++i)
            {
                if (i == rest.size() || rest[i] == '|')
                {
                    if (current.empty())
                        fail(cuff::ErrorCode::RegexEmptyToken, "'[one:...]' contains an empty alternative");
                    n->alternatives.push_back(current);
                    current.clear();
                }
                else
                {
                    current += rest[i];
                }
            }
            return n;
        }

        // Attaches an optional quantifier to `atom`. Every atom is always
        // wrapped in a Quantified node (default {1,1,greedy} when no explicit
        // quantifier is written) so the matcher only ever has to deal with
        // one uniform shape.
        RNodePtr applyQuantifier(RNodePtr atom)
        {
            int minC = 1, maxC = 1;
            bool sawQuantifier = false;

            if (!atEnd())
            {
                char c = peek();
                if (c == '+')
                {
                    advance();
                    minC = 1;
                    maxC = -1;
                    sawQuantifier = true;
                }
                else if (c == '*')
                {
                    advance();
                    minC = 0;
                    maxC = -1;
                    sawQuantifier = true;
                }
                else if (c == '?')
                {
                    advance();
                    minC = 0;
                    maxC = 1;
                    sawQuantifier = true;
                }
                else if (c == '~')
                {
                    advance();
                    // "~M" form: 0 to M
                    if (!atEnd() && isDigitChar(peek()))
                    {
                        minC = 0;
                        maxC = parseNumber();
                    }
                    else
                    {
                        fail(cuff::ErrorCode::RegexDanglingQuantifier,
                             "'~' must be followed by a number (as in '~5') or preceded by one (as in '3~5', '3~')",
                             "use \\~ to match a literal '~'");
                    }
                    sawQuantifier = true;
                }
                else if (isDigitChar(c))
                {
                    int n1 = parseNumber();
                    if (!atEnd() && peek() == '~')
                    {
                        advance();
                        if (!atEnd() && isDigitChar(peek()))
                        {
                            int n2 = parseNumber();
                            if (n1 > n2)
                                fail(cuff::ErrorCode::RegexInvalidQuantifierRange,
                                     "quantifier range " + std::to_string(n1) + "~" + std::to_string(n2) +
                                         " has a start greater than its end");
                            minC = n1;
                            maxC = n2;
                        }
                        else
                        {
                            // "N~" form: N or more, unbounded
                            minC = n1;
                            maxC = -1;
                        }
                    }
                    else
                    {
                        // exact count
                        minC = maxC = n1;
                    }
                    sawQuantifier = true;
                }
            }

            bool lazy = false;
            if (sawQuantifier && !atEnd() && peek() == '?')
            {
                advance();
                lazy = true;
            }

            // Reject stacked/redundant quantifiers, e.g. "[num]++" or "a**".
            if (sawQuantifier && !atEnd())
            {
                char after = peek();
                if (after == '+' || after == '*' || after == '~' || (after == '?' && lazy))
                {
                    fail(cuff::ErrorCode::RegexStackedQuantifier,
                         "quantifiers cannot be stacked directly on top of each other",
                         "combine into a single quantifier, e.g. use '+' or a 'N~M' range, not both");
                }
            }

            if (!sawQuantifier)
                return wrapQuantified(atom, 1, 1, false);
            return wrapQuantified(atom, minC, maxC, lazy);
        }

        RNodePtr wrapQuantified(RNodePtr atom, int minC, int maxC, bool lazy)
        {
            RNodePtr q = makeNode(RNodeKind::Quantified);
            q->child = atom;
            q->minCount = minC;
            q->maxCount = maxC;
            q->lazy = lazy;
            return q;
        }
    };

} // namespace cuff::regex
