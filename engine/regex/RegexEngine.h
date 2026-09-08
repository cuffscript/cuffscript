#pragma once

#include "RegexAst.h"
#include "RegexParser.h"
#include "RegexMatcher.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
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
    };

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
                // Unknown flag characters are ignored rather than treated as
                // an error — flags are meant to be a small, low-ceremony
                // convenience, not another place to trip a syntax error.
            }
            return f;
        }
    };

    // Compiles CuffScript pattern strings into RNode trees, caching by
    // (pattern text) so a pattern used inside a loop is only compiled once.
    // One engine instance is shared for the whole interpreter run.
    class RegexEngine
    {
    public:
        std::shared_ptr<CompiledPattern> compile(const std::string &pattern, const cuff::SourceLocation &loc)
        {
            auto it = cache_.find(pattern);
            if (it != cache_.end())
                return it->second;

            RegexParser parser(pattern, loc);
            int groupCount = 0;
            RNodePtr root = parser.parse(groupCount);

            auto compiled = std::make_shared<CompiledPattern>();
            compiled->root = root;
            compiled->groupCount = groupCount;
            compiled->source = pattern;
            cache_[pattern] = compiled;
            return compiled;
        }

        bool fullMatch(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                        bool caseInsensitive, const cuff::SourceLocation &loc, MatchOutcome &out)
        {
            RegexMatcher matcher(pat->root, pat->groupCount, caseInsensitive, /*multiline=*/false, loc);
            return matcher.fullMatch(text, out);
        }

        bool search(const std::shared_ptr<CompiledPattern> &pat, const std::string &text, size_t fromPos,
                    const Flags &flags, const cuff::SourceLocation &loc, MatchOutcome &out)
        {
            RegexMatcher matcher(pat->root, pat->groupCount, flags.caseInsensitive, flags.multiline, loc);
            return matcher.search(text, fromPos, out);
        }

        std::vector<MatchOutcome> searchAll(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                                             const Flags &flags, const cuff::SourceLocation &loc)
        {
            std::vector<MatchOutcome> results;
            size_t pos = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!search(pat, text, pos, flags, loc, out))
                    break;
                results.push_back(out);
                pos = (out.end > out.start) ? out.end : out.end + 1; // always advance on empty matches
            }
            return results;
        }

        // replace: without 'g' replaces only the first match, with 'g' all of them.
        std::string replace(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                             const std::string &replacement, const Flags &flags, const cuff::SourceLocation &loc)
        {
            std::string result;
            size_t pos = 0;
            bool replacedOnce = false;
            while (pos <= text.size())
            {
                if (!flags.global && replacedOnce)
                    break;
                MatchOutcome out;
                if (!search(pat, text, pos, flags, loc, out))
                    break;
                result.append(text, pos, out.start - pos);
                result.append(replacement);
                replacedOnce = true;
                if (out.end > out.start)
                {
                    pos = out.end;
                }
                else
                {
                    // Zero-width match: keep the character under it (if any)
                    // so it isn't silently dropped, then advance by one.
                    if (out.end < text.size())
                        result += text[out.end];
                    pos = out.end + 1;
                }
            }
            if (pos < text.size())
                result.append(text, pos, text.size() - pos);
            return result;
        }

        // split: cut `text` on every non-overlapping match of `pat`.
        std::vector<std::string> split(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                                        const cuff::SourceLocation &loc)
        {
            Flags flags;
            flags.global = true;
            std::vector<std::string> pieces;
            size_t pos = 0;
            size_t segmentStart = 0;
            while (pos <= text.size())
            {
                MatchOutcome out;
                if (!search(pat, text, pos, flags, loc, out))
                    break;
                if (out.end == out.start)
                {
                    // Avoid infinite loop / degenerate zero-width splits.
                    pos = out.end + 1;
                    continue;
                }
                pieces.push_back(text.substr(segmentStart, out.start - segmentStart));
                segmentStart = out.end;
                pos = out.end;
            }
            pieces.push_back(text.substr(segmentStart));
            return pieces;
        }

        size_t count(const std::shared_ptr<CompiledPattern> &pat, const std::string &text,
                     const Flags &flags, const cuff::SourceLocation &loc)
        {
            Flags f = flags;
            f.global = true;
            return searchAll(pat, text, f, loc).size();
        }

    private:
        std::unordered_map<std::string, std::shared_ptr<CompiledPattern>> cache_;
    };

} // namespace cuff::regex
