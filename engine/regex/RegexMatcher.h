#pragma once

#include "RegexAst.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <cstddef>
#include <algorithm>

namespace cuff::regex
{

    struct MatchOutcome
    {
        bool matched = false;
        size_t start = 0;
        size_t end = 0; // half-open [start, end)
        // 1-based positional captures: positional[0] is group 1, etc. An
        // unmatched optional group is represented as an empty string (the
        // language doesn't need to distinguish "unmatched" from "matched
        // empty" for capture groups; REGEX.md never exercises that case).
        std::vector<std::string> positional;
        std::unordered_map<std::string, std::string> named;
    };

    // Safety limits guarding against catastrophic backtracking (REGEX.md
    // section 33: "최대 매칭 스텝 수(Step Limit)와 시간 제한(Timeout)"). Both a
    // step counter and a wall-clock deadline are enforced; either one tripping
    // aborts the match with a RegexRuntimeError. A recursion-depth counter is
    // also enforced so a pathological match can never grow the native C++
    // call stack large enough to crash the process outright — it fails
    // cleanly with a CuffScript-level error instead.
    struct RegexLimits
    {
        size_t stepLimit = 200000;
        size_t depthLimit = 20000;
        std::chrono::milliseconds timeLimit{500};
    };

    class RegexMatcher
    {
    public:
        RegexMatcher(RNodePtr root, int groupCount, bool caseInsensitive, bool multiline,
                     cuff::SourceLocation loc, RegexLimits limits = RegexLimits())
            : root_(std::move(root)), groupCount_(groupCount), ci_(caseInsensitive),
              multiline_(multiline), loc_(loc), limits_(limits)
        {
        }

        // Anchored, whole-string match (used by `is` / `IS`).
        bool fullMatch(const std::string &text, MatchOutcome &out)
        {
            return tryMatchAt(text, 0, out, /*requireFullConsumption=*/true);
        }

        // Leftmost match starting at or after `fromPos` (used by
        // match/find/replace/split/count).
        bool search(const std::string &text, size_t fromPos, MatchOutcome &out)
        {
            for (size_t p = fromPos; p <= text.size(); ++p)
            {
                if (tryMatchAt(text, p, out, /*requireFullConsumption=*/false))
                    return true;
            }
            return false;
        }

    private:
        RNodePtr root_;
        int groupCount_;
        bool ci_;
        bool multiline_;
        cuff::SourceLocation loc_;
        RegexLimits limits_;

        // Per-attempt mutable state.
        size_t steps_ = 0;
        size_t depth_ = 0;
        std::chrono::steady_clock::time_point deadline_;
        std::vector<std::pair<size_t, size_t>> groupSpans_;                     // 1-based; index 0 unused
        std::unordered_map<std::string, std::pair<size_t, size_t>> namedSpans_;
        const std::string *text_ = nullptr;

        using Cont = std::function<bool(size_t)>;

        bool tryMatchAt(const std::string &text, size_t startPos, MatchOutcome &out, bool requireFullConsumption)
        {
            text_ = &text;
            steps_ = 0;
            depth_ = 0;
            deadline_ = std::chrono::steady_clock::now() + limits_.timeLimit;
            groupSpans_.assign(static_cast<size_t>(groupCount_) + 1, {std::string::npos, std::string::npos});
            namedSpans_.clear();

            size_t matchedEnd = std::string::npos;
            Cont finish = [&](size_t pos) -> bool
            {
                if (requireFullConsumption && pos != text.size())
                    return false;
                matchedEnd = pos;
                return true;
            };

            bool ok = matchSequence(*root_, startPos, finish);
            if (!ok)
                return false;

            out.matched = true;
            out.start = startPos;
            out.end = matchedEnd;
            out.positional.assign(static_cast<size_t>(groupCount_), "");
            for (int i = 1; i <= groupCount_; ++i)
            {
                auto span = groupSpans_[static_cast<size_t>(i)];
                if (span.first != std::string::npos)
                    out.positional[static_cast<size_t>(i - 1)] = text.substr(span.first, span.second - span.first);
            }
            out.named.clear();
            for (auto &kv : namedSpans_)
            {
                if (kv.second.first != std::string::npos)
                    out.named[kv.first] = text.substr(kv.second.first, kv.second.second - kv.second.first);
            }
            return true;
        }

        void tick()
        {
            ++steps_;
            if (steps_ > limits_.stepLimit)
            {
                throw cuff::RegexRuntimeError(cuff::ErrorCode::RegexStepLimitExceeded,
                                               "pattern matching exceeded the maximum step limit (possible catastrophic backtracking)",
                                               loc_,
                                               "simplify the pattern — avoid nested unbounded quantifiers like ([any]+)+");
            }
            if ((steps_ & 0xFFF) == 0)
            {
                if (std::chrono::steady_clock::now() > deadline_)
                {
                    throw cuff::RegexRuntimeError(cuff::ErrorCode::RegexTimeout,
                                                   "pattern matching exceeded its time limit",
                                                   loc_,
                                                   "simplify the pattern or the input — avoid nested unbounded quantifiers");
                }
            }
        }

        struct DepthGuard
        {
            RegexMatcher *m;
            explicit DepthGuard(RegexMatcher *matcher) : m(matcher)
            {
                if (++m->depth_ > m->limits_.depthLimit)
                {
                    throw cuff::RegexRuntimeError(cuff::ErrorCode::RegexRecursionLimitExceeded,
                                                   "pattern matching recursed too deeply for this input",
                                                   m->loc_,
                                                   "the input or pattern is too large/complex for a single match attempt");
                }
            }
            ~DepthGuard() { --m->depth_; }
        };

        bool matchSequence(RNode &seq, size_t pos, const Cont &k)
        {
            return matchSeqAt(seq.children, 0, pos, k);
        }

        bool matchSeqAt(std::vector<RNodePtr> &nodes, size_t idx, size_t pos, const Cont &k)
        {
            if (idx == nodes.size())
                return k(pos);
            RNodePtr node = nodes[idx];
            return matchNode(*node, pos, [this, &nodes, idx, &k](size_t newPos)
                              { return matchSeqAt(nodes, idx + 1, newPos, k); });
        }

        bool matchNode(RNode &node, size_t pos, const Cont &k)
        {
            tick();
            DepthGuard guard(this);

            switch (node.kind)
            {
            case RNodeKind::CharTest:
                return matchCharTest(node, pos, k);
            case RNodeKind::Preset:
                return matchPreset(node, pos, k);
            case RNodeKind::WordBoundary:
                return matchWordBoundary(pos) && k(pos);
            case RNodeKind::StartAnchor:
                return matchStartAnchor(pos) && k(pos);
            case RNodeKind::EndAnchor:
                return matchEndAnchor(pos) && k(pos);
            case RNodeKind::Group:
                return matchGroup(node, pos, k);
            case RNodeKind::NamedGroup:
                return matchNamedGroup(node, pos, k);
            case RNodeKind::OneOf:
                return matchOneOf(node, pos, k);
            case RNodeKind::Sequence:
                return matchSequence(node, pos, k);
            case RNodeKind::Quantified:
                return matchQuantified(node, pos, 0, k);
            }
            return false;
        }

        bool charEquals(unsigned char textChar, const std::function<bool(unsigned char)> &test) const
        {
            if (test(textChar))
                return true;
            if (ci_ && test(toggleAsciiCase(textChar)))
                return true;
            return false;
        }

        bool matchCharTest(RNode &node, size_t pos, const Cont &k)
        {
            if (pos >= text_->size())
                return false;
            unsigned char c = static_cast<unsigned char>((*text_)[pos]);
            if (!charEquals(c, node.charTest))
                return false;
            return k(pos + 1);
        }

        bool matchWordBoundary(size_t pos)
        {
            bool before = pos > 0 && isWordChar(static_cast<unsigned char>((*text_)[pos - 1]));
            bool after = pos < text_->size() && isWordChar(static_cast<unsigned char>((*text_)[pos]));
            return before != after;
        }

        bool matchStartAnchor(size_t pos)
        {
            if (pos == 0)
                return true;
            return multiline_ && pos > 0 && (*text_)[pos - 1] == '\n';
        }

        bool matchEndAnchor(size_t pos)
        {
            if (pos == text_->size())
                return true;
            return multiline_ && (*text_)[pos] == '\n';
        }

        bool matchGroup(RNode &node, size_t pos, const Cont &k)
        {
            int idx = node.groupIndex;
            auto saved = groupSpans_[static_cast<size_t>(idx)];
            bool ok = matchNode(*node.child, pos, [&](size_t endPos)
                                 {
                groupSpans_[static_cast<size_t>(idx)] = {pos, endPos};
                if (k(endPos)) return true;
                return false; });
            if (!ok)
                groupSpans_[static_cast<size_t>(idx)] = saved;
            return ok;
        }

        bool matchNamedGroup(RNode &node, size_t pos, const Cont &k)
        {
            std::string name = node.groupName;
            auto it = namedSpans_.find(name);
            bool hadSaved = it != namedSpans_.end();
            std::pair<size_t, size_t> saved = hadSaved ? it->second : std::pair<size_t, size_t>{std::string::npos, std::string::npos};

            bool ok = matchNode(*node.child, pos, [&](size_t endPos)
                                 {
                namedSpans_[name] = {pos, endPos};
                if (k(endPos)) return true;
                return false; });
            if (!ok)
            {
                if (hadSaved)
                    namedSpans_[name] = saved;
                else
                    namedSpans_.erase(name);
            }
            return ok;
        }

        bool matchOneOf(RNode &node, size_t pos, const Cont &k)
        {
            for (const std::string &alt : node.alternatives)
            {
                tick();
                if (pos + alt.size() > text_->size())
                    continue;
                bool ok = true;
                for (size_t i = 0; i < alt.size() && ok; ++i)
                {
                    unsigned char tc = static_cast<unsigned char>((*text_)[pos + i]);
                    unsigned char pc = static_cast<unsigned char>(alt[i]);
                    if (tc == pc)
                        continue;
                    if (ci_ && toggleAsciiCase(tc) == pc)
                        continue;
                    ok = false;
                }
                if (ok && k(pos + alt.size()))
                    return true;
            }
            return false;
        }

        // Returns candidate match lengths at `pos`, longest first (greedy).
        std::vector<size_t> presetLengths(RNode &node, size_t pos)
        {
            const std::string &text = *text_;
            size_t n = text.size();
            std::vector<size_t> results;

            switch (node.presetKind)
            {
            case PresetKind::Int:
            {
                size_t i = pos;
                if (i < n && (text[i] == '+' || text[i] == '-'))
                    ++i;
                size_t digitsStart = i;
                while (i < n && classNum(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == digitsStart)
                    return results; // no digits at all
                for (size_t len = i - pos; len >= 1; --len)
                {
                    // Only accept lengths that don't cut off a sign with no digits after it
                    if (pos + len <= n)
                        results.push_back(len);
                }
                break;
            }
            case PresetKind::Float:
            {
                size_t i = pos;
                if (i < n && (text[i] == '+' || text[i] == '-'))
                    ++i;
                size_t intStart = i;
                while (i < n && classNum(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == intStart)
                    break; // need at least one digit before '.'
                if (i >= n || text[i] != '.')
                    break;
                size_t dotPos = i;
                ++i;
                size_t fracStart = i;
                while (i < n && classNum(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == fracStart)
                    break; // need at least one digit after '.'
                (void)dotPos;
                results.push_back(i - pos);
                break;
            }
            case PresetKind::Email:
            {
                size_t i = pos;
                size_t localStart = i;
                auto isLocalChar = [](unsigned char c)
                { return classStr(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; };
                while (i < n && isLocalChar(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == localStart || i >= n || text[i] != '@')
                    break;
                ++i; // consume '@'
                size_t domainStart = i;
                auto isDomainChar = [](unsigned char c)
                { return classStr(c) || c == '.' || c == '-'; };
                while (i < n && isDomainChar(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == domainStart)
                    break;
                // require at least one dot with 2+ trailing letters for a TLD
                if (i - pos < 1)
                    break;
                // Find longest valid split by trimming trailing chars until a
                // dot + 2+ letters is found at the end.
                for (size_t end = i; end > domainStart; --end)
                {
                    size_t dot = text.rfind('.', end - 1);
                    if (dot == std::string::npos || dot < domainStart || dot == end - 1)
                        continue;
                    size_t tldLen = end - dot - 1;
                    bool allLetters = tldLen >= 2;
                    for (size_t j = dot + 1; j < end && allLetters; ++j)
                        if (!classLet(static_cast<unsigned char>(text[j])))
                            allLetters = false;
                    if (allLetters)
                    {
                        results.push_back(end - pos);
                    }
                }
                break;
            }
            case PresetKind::Phone:
            {
                // Korean phone formats: 0XX(-)XXX(X)-XXXX — mobile (010/011/...)
                // and area-code lines (02, 0XX). Hand-matched, longest first.
                static const std::vector<std::string> shapes = {
                    // 3-4-4 (e.g. 010-1234-5678)
                    "DDD-DDDD-DDDD",
                    // 2-3/4-4 (Seoul, e.g. 02-123-4567 / 02-1234-5678)
                    "DD-DDD-DDDD",
                    "DD-DDDD-DDDD",
                    // 3-3-4 (other regions, e.g. 031-123-4567)
                    "DDD-DDD-DDDD",
                };
                size_t bestLen = 0;
                for (const auto &shape : shapes)
                {
                    size_t len = shape.size();
                    if (pos + len > n)
                        continue;
                    bool ok = true;
                    for (size_t j = 0; j < len && ok; ++j)
                    {
                        char want = shape[j];
                        char got = text[pos + j];
                        if (want == 'D')
                            ok = classNum(static_cast<unsigned char>(got));
                        else
                            ok = (got == want);
                    }
                    if (ok && len > bestLen)
                        bestLen = len;
                }
                if (bestLen > 0)
                    results.push_back(bestLen);
                break;
            }
            case PresetKind::Url:
            {
                static const std::vector<std::string> schemes = {"https://", "http://"};
                for (auto &scheme : schemes)
                {
                    if (text.compare(pos, scheme.size(), scheme) == 0)
                    {
                        size_t i = pos + scheme.size();
                        auto isUrlChar = [](unsigned char c)
                        { return classStr(c) || c == '.' || c == '-' || c == '_' || c == '/' || c == '?' || c == '=' || c == '&' || c == '%' || c == '#' || c == '~' || c == ':' || c == '+'; };
                        size_t start = i;
                        while (i < n && isUrlChar(static_cast<unsigned char>(text[i])))
                            ++i;
                        if (i > start)
                            results.push_back(i - pos);
                        break;
                    }
                }
                break;
            }
            }

            std::sort(results.begin(), results.end(), std::greater<size_t>());
            results.erase(std::unique(results.begin(), results.end()), results.end());
            return results;
        }

        bool matchPreset(RNode &node, size_t pos, const Cont &k)
        {
            for (size_t len : presetLengths(node, pos))
            {
                tick();
                if (k(pos + len))
                    return true;
            }
            return false;
        }

        bool matchQuantified(RNode &node, size_t pos, int count, const Cont &k)
        {
            tick();
            bool canStop = count >= node.minCount;
            bool canContinue = (node.maxCount < 0) || (count < node.maxCount);

            auto tryStop = [&]() -> bool
            { return canStop && k(pos); };

            auto tryContinue = [&]() -> bool
            {
                if (!canContinue)
                    return false;
                return matchNode(*node.child, pos, [this, &node, pos, count, &k](size_t newPos)
                                  {
                    // Zero-width guard: if the child matched without consuming
                    // any input and we've already satisfied the minimum, don't
                    // recurse forever — treat this repetition as done.
                    if (newPos == pos && count >= node.minCount)
                        return false;
                    return matchQuantified(node, newPos, count + 1, k); });
            };

            if (node.lazy)
            {
                if (tryStop())
                    return true;
                return tryContinue();
            }
            else
            {
                if (tryContinue())
                    return true;
                return tryStop();
            }
        }
    };

} // namespace cuff::regex
