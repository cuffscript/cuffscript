#pragma once

#include "RegexParser.h"
#include "RegexMatcher.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/SourceText.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace cuff::regex
{

    struct CompiledPattern
    {
        RNodePtr root;
        int groupCount = 0;
        std::string source;
        // The bytes a match can begin with, exactly and with ASCII case ignored; meaningless when `canBeEmpty`.
        RNode::ByteSet first[2];
        bool canBeEmpty = true;
    };

    // Works out, once, what matching then only looks up: each character test as a table of the bytes it accepts.
    inline void prepareForMatching(RNode &node)
    {
        if (node.kind == RNodeKind::CharTest && node.charTest && !node.isAnyCodepoint && node.multiByteLiteral.empty())
        {
            for (int b = 0; b < 256; ++b)
            {
                const auto c = static_cast<unsigned char>(b);
                const bool exact = node.charTest(c);
                if (exact)
                    node.bytes[0].add(c);
                if (exact || node.charTest(toggleAsciiCase(c)))
                    node.bytes[1].add(c);
            }
            node.hasBytes = true;
        }
        if (node.child)
            prepareForMatching(*node.child);
        for (const RNodePtr &c : node.children)
            prepareForMatching(*c);
    }

    // The bytes that a match of `node` can begin with, added to `out` (exactly, and with case ignored); true when it
    // can match without taking a byte, so that what follows it can begin the match too. Anything not known for sure
    // adds every byte.
    inline bool firstBytes(const RNode &node, RNode::ByteSet out[2])
    {
        auto every = [&]()
        {
            for (int k = 0; k < 2; ++k)
                for (int w = 0; w < 4; ++w)
                    out[k].words[w] = ~uint64_t(0);
        };
        switch (node.kind)
        {
        case RNodeKind::CharTest:
            if (node.hasBytes)
            {
                out[0].addAll(node.bytes[0]);
                out[1].addAll(node.bytes[1]);
                if (node.negated) // a character of more than one byte passes a negated test
                    for (int b = 0x80; b < 256; ++b)
                    {
                        out[0].add(static_cast<unsigned char>(b));
                        out[1].add(static_cast<unsigned char>(b));
                    }
            }
            else if (!node.multiByteLiteral.empty())
            {
                const auto lead = static_cast<unsigned char>(node.multiByteLiteral[0]);
                out[0].add(lead);
                out[1].add(lead);
            }
            else
                every();
            return false;
        case RNodeKind::OneOf:
        {
            bool empty = false;
            for (const std::string &alt : node.alternatives)
            {
                if (alt.empty())
                {
                    empty = true;
                    continue;
                }
                const auto c = static_cast<unsigned char>(alt[0]);
                out[0].add(c);
                out[1].add(c);
                if (c < 0x80)
                    out[1].add(toggleAsciiCase(c));
            }
            return empty;
        }
        case RNodeKind::WordBoundary:
        case RNodeKind::StartAnchor:
        case RNodeKind::EndAnchor:
            return true;
        case RNodeKind::Group:
        case RNodeKind::NamedGroup:
            return node.child ? firstBytes(*node.child, out) : true;
        case RNodeKind::Sequence:
            for (const RNodePtr &c : node.children)
                if (!firstBytes(*c, out))
                    return false;
            return true;
        case RNodeKind::Quantified:
            return (node.child ? firstBytes(*node.child, out) : true) || node.minCount == 0;
        case RNodeKind::Preset:
            break;
        }
        every();
        return true;
    }

    // Parses `pattern` into a CompiledPattern, applying the pattern size limit. Pattern literals are compiled with
    // this when the program is parsed; RegexEngine::compile adds a cache for patterns built at run time.
    inline std::shared_ptr<CompiledPattern> compilePattern(const std::string &pattern, const cuff::SourceLocation &loc)
    {
        if (pattern.size() > limits::kMaxRegexPatternBytes)
            throw cuff::RegexSyntaxError(cuff::ErrorCode::RegexPatternTooComplex,
                                         "pattern is longer than the " + std::to_string(limits::kMaxRegexPatternBytes / 1024) + " KiB limit", loc);

        RegexParser parser(pattern, loc);
        int groupCount = 0;
        RNodePtr root = parser.parse(groupCount);

        auto compiled = std::make_shared<CompiledPattern>();
        prepareForMatching(*root);
        compiled->canBeEmpty = firstBytes(*root, compiled->first);
        compiled->root = root;
        compiled->groupCount = groupCount;
        compiled->source = pattern;
        return compiled;
    }

    struct Flags
    {
        bool global = false;
        bool caseInsensitive = false;
        bool multiline = false;

        static Flags parse(const std::string &raw)
        {
            Flags f;
            for (char c : raw)
            {
                if (c == 'g')
                    f.global = true;
                else if (c == 'i')
                    f.caseInsensitive = true;
                else if (c == 'm')
                    f.multiline = true;
            }
            return f;
        }
    };

    class RegexEngine
    {
    public:
        std::shared_ptr<CompiledPattern> compile(const std::string &pattern, const cuff::SourceLocation &loc)
        {
            auto it = cache_.find(pattern);
            if (it != cache_.end())
                return it->second;

            auto compiled = compilePattern(pattern, loc);

            if (cache_.size() >= limits::kMaxRegexCacheEntries)
                cache_.clear();
            cache_[pattern] = compiled;
            return compiled;
        }

        static bool fullMatch(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                              const Flags &flags, const cuff::SourceLocation &loc, MatchOutcome &out)
        {
            RegexMatcher matcher(pat->root, pat->groupCount, flags.caseInsensitive, flags.multiline, loc,
                                 limitsUntil(deadlineFor(text)));
            return matcher.fullMatch(text, out);
        }

        static bool fullMatch(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                              bool caseInsensitive, const cuff::SourceLocation &loc, MatchOutcome &out)
        {
            Flags flags;
            flags.caseInsensitive = caseInsensitive;
            return fullMatch(pat, text, flags, loc, out);
        }

        static bool search(const std::shared_ptr<CompiledPattern> &pat, const std::string &text, size_t fromPos,
                    const Flags &flags, const cuff::SourceLocation &loc, MatchOutcome &out)
        {
            return searchUntil(pat, text, fromPos, flags, loc, out, deadlineFor(text));
        }

        static std::vector<std::pair<size_t, size_t>> searchAllSpans(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                                                              const Flags &flags, const cuff::SourceLocation &loc)
        {
            std::vector<std::pair<size_t, size_t>> spans;
            const auto deadline = deadlineFor(text);
            size_t pos = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!searchUntil(pat, text, pos, flags, loc, out, deadline))
                    break;
                if (spans.size() >= limits::kMaxCollectionItems)
                    throwTooManyMatches(loc);
                spans.emplace_back(out.start, out.end);
                pos = (out.end > out.start) ? out.end : out.end + 1;
            }
            return spans;
        }

        static std::vector<MatchOutcome> searchAll(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                                             const Flags &flags, const cuff::SourceLocation &loc)
        {
            std::vector<MatchOutcome> results;
            const auto deadline = deadlineFor(text);
            size_t pos = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!searchUntil(pat, text, pos, flags, loc, out, deadline))
                    break;
                if (results.size() >= limits::kMaxCollectionItems)
                    throwTooManyMatches(loc);
                results.push_back(out);
                pos = (out.end > out.start) ? out.end : out.end + 1;
            }
            return results;
        }

        static std::string replace(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                             const std::string &replacement, const Flags &flags, const cuff::SourceLocation &loc)
        {
            std::string result;
            const auto deadline = deadlineFor(text);
            size_t pos = 0;
            bool replacedOnce = false;
            while (pos <= text.size())
            {
                if (!flags.global && replacedOnce)
                    break;
                MatchOutcome out;
                if (!searchUntil(pat, text, pos, flags, loc, out, deadline))
                    break;
                if (result.size() + (out.start - pos) + replacement.size() > limits::kMaxStringBytes)
                    throwResultTooLarge(loc);
                result.append(text, pos, out.start - pos);
                result.append(replacement);
                replacedOnce = true;
                if (out.end > out.start)
                {
                    pos = out.end;
                }
                else
                {
                    if (out.end < text.size())
                        result += text[out.end];
                    pos = out.end + 1;
                }
            }
            if (pos < text.size())
                result.append(text, pos, text.size() - pos);
            return result;
        }

        static std::vector<std::string> split(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                                        const cuff::SourceLocation &loc)
        {
            Flags flags;
            flags.global = true;
            std::vector<std::string> pieces;
            const auto deadline = deadlineFor(text);
            size_t pos = 0;
            size_t segmentStart = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!searchUntil(pat, text, pos, flags, loc, out, deadline))
                    break;
                if (out.end == out.start)
                {
                    pos = out.end + 1;
                    continue;
                }
                if (pieces.size() >= limits::kMaxCollectionItems)
                    throwTooManyMatches(loc);
                pieces.push_back(text.substr(segmentStart, out.start - segmentStart));
                segmentStart = out.end;
                pos = out.end;
            }
            pieces.push_back(text.substr(segmentStart));
            return pieces;
        }

        static size_t count(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                     const Flags &flags, const cuff::SourceLocation &loc)
        {
            Flags f = flags;
            f.global = true;
            const auto deadline = deadlineFor(text);
            size_t n = 0;
            size_t pos = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!searchUntil(pat, text, pos, f, loc, out, deadline))
                    break;
                ++n;
                pos = (out.end > out.start) ? out.end : out.end + 1;
            }
            return n;
        }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr int kBaseTimeMs = 5000;
        static constexpr int kTimeMsPerMiB = 2000;
        static constexpr long long kMaxTimeMs = 10LL * 60 * 1000;

        std::unordered_map<std::string, std::shared_ptr<CompiledPattern>> cache_;

        static Clock::time_point deadlineFor(const std::string &text)
        {
            long long ms = kBaseTimeMs + static_cast<long long>(text.size() >> 20) * kTimeMsPerMiB;
            return Clock::now() + std::chrono::milliseconds(std::min(ms, kMaxTimeMs));
        }

        static RegexLimits limitsUntil(Clock::time_point deadline)
        {
            RegexLimits l;
            l.operationDeadline = deadline;
            return l;
        }

        static bool searchUntil(const std::shared_ptr<CompiledPattern> &pat, const std::string &text, size_t fromPos,
                         const Flags &flags, const cuff::SourceLocation &loc, MatchOutcome &out, Clock::time_point deadline)
        {
            RegexMatcher matcher(pat->root, pat->groupCount, flags.caseInsensitive, flags.multiline, loc, limitsUntil(deadline));
            return matcher.search(text, fromPos, out, pat->canBeEmpty ? nullptr : &pat->first[flags.caseInsensitive ? 1 : 0]);
        }

        [[noreturn]] static void throwTooManyMatches(const cuff::SourceLocation &loc)
        {
            throw cuff::CuffRuntimeError(cuff::ErrorCode::SizeLimitExceeded,
                                         "pattern produced too many matches", loc);
        }

        [[noreturn]] static void throwResultTooLarge(const cuff::SourceLocation &loc)
        {
            throw cuff::CuffRuntimeError(cuff::ErrorCode::SizeLimitExceeded,
                                         "string exceeds the maximum allowed size", loc);
        }
    };

}

namespace cuff
{

    // The value behind a `pattern`: the compiled form plus the source text and flags it was written with. A
    // pattern is immutable and shared, so copying a pattern value copies a pointer.
    struct PatternData
    {
        std::shared_ptr<regex::CompiledPattern> compiled;
        std::string source;
        bool caseInsensitive = false;
        bool multiline = false;

        regex::Flags flags() const
        {
            regex::Flags f;
            f.caseInsensitive = caseInsensitive;
            f.multiline = multiline;
            return f;
        }

        // The pattern as it is written in a program: p"..."i
        std::string display() const
        {
            std::string out = "p\"";
            for (size_t i = 0; i < source.size(); ++i)
            {
                const char c = source[i];
                if (c == '\\' && i + 1 < source.size())
                {
                    out += c;
                    out += source[++i];
                }
                else if (c == '"')
                {
                    out += "\\\"";
                }
                else
                {
                    out += c;
                }
            }
            out += '"';
            if (caseInsensitive)
                out += 'i';
            if (multiline)
                out += 'm';
            return out;
        }

        bool sameAs(const PatternData &other) const
        {
            return source == other.source && caseInsensitive == other.caseInsensitive && multiline == other.multiline;
        }
    };

    inline std::shared_ptr<const PatternData> makePatternData(const std::string &source, bool caseInsensitive,
                                                              bool multiline, const SourceLocation &loc)
    {
        auto data = std::make_shared<PatternData>();
        data->compiled = regex::compilePattern(source, loc);
        data->source = source;
        data->caseInsensitive = caseInsensitive;
        data->multiline = multiline;
        return data;
    }

}
