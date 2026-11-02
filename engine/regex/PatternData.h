#pragma once

#include "RegexEngine.h"
#include <memory>
#include <string>

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
