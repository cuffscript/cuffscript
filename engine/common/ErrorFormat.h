#pragma once

#include "CuffError.h"
#include "SourceText.h"
#include "Utf8.h"
#include "Width.h"
#include <algorithm>
#include <exception>
#include <new>
#include <string>

namespace cuff
{

    // Draws the source line `loc` points at, with a caret under the offending character:
    //
    //     print(1 / 0)
    //               ^
    //
    // The caret is placed by display width, not by byte or code point count: a tab in the line is copied
    // into the padding (so it lines up whatever the terminal's tab stops are), and a wide character such as
    // Hangul takes two columns. Returns "" when there is no source to draw from.
    inline std::string buildCaretSnippet(const std::string &source, const SourceLocation &loc)
    {
        if (source.empty() || loc.offset < 0)
            return "";

        const size_t bom = (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
                            static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF)
                               ? 3
                               : 0;
        size_t offset = std::min(static_cast<size_t>(loc.offset), source.size());

        size_t lineStart = 0;
        if (offset > 0)
        {
            const size_t prevNewline = source.rfind('\n', offset - 1);
            if (prevNewline != std::string::npos)
                lineStart = prevNewline + 1;
        }
        size_t lineEnd = source.find('\n', offset);
        if (lineEnd == std::string::npos)
            lineEnd = source.size();

        // A location just past the final newline would draw an empty line; use the last line of text instead.
        if (lineStart == lineEnd && lineStart == source.size() && lineStart > 0)
        {
            lineEnd = lineStart - 1;
            lineStart = lineEnd > 0 ? source.rfind('\n', lineEnd - 1) : std::string::npos;
            lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            offset = lineEnd;
        }
        if (lineStart < bom)
            lineStart = bom;
        if (offset < lineStart)
            offset = lineStart;
        if (lineStart > lineEnd)
            return "";

        std::string lineText = source.substr(lineStart, lineEnd - lineStart);
        if (!lineText.empty() && lineText.back() == '\r')
            lineText.pop_back();

        const size_t prefixBytes = std::min(offset - lineStart, lineText.size());
        std::string pad;
        for (size_t i = 0; i < prefixBytes;)
        {
            const char32_t cp = utf8::decode(lineText, i);
            if (cp == U'\t')
            {
                pad += '\t';
                continue;
            }
            pad.append(static_cast<size_t>(std::max(width::cpWidth(cp), 0)), ' ');
        }
        return "    " + lineText + "\n    " + pad + "^";
    }

    // "[E4-006] Runtime Error at line 3, column 9: division by zero". An error that came from another file
    // says which: "[E4-006] Runtime Error in 'lib/helper.cuff' at line 3, column 9: ...".
    inline std::string errorHeadline(const CuffError &e)
    {
        std::string out = "[" + errorCodeTag(e.code) + "] " + e.category;
        if (e.source && !e.source->name.empty())
            out += " in '" + e.source->name + "'";
        return out + " at " + e.location.toString() + ": " + e.message;
    }

    // The full report: headline, source line with caret, anything related, then the hint. The line is taken
    // from the text the error itself points into (e.source) and only falls back to `fallbackSource`, the
    // program the caller ran, for an error that carries none.
    inline std::string renderErrorWithSnippet(const CuffError &e, const std::string &fallbackSource)
    {
        std::string out = errorHeadline(e);
        const std::string snippet = buildCaretSnippet(e.source ? e.source->text : fallbackSource, e.location);
        if (!snippet.empty())
            out += "\n" + snippet;
        if (!e.related.empty())
            out += "\n" + e.related;
        if (!e.hint.empty())
            out += "\n    hint: " + e.hint;
        return out;
    }

    // What a front end prints for the exception currently being handled, in the engine's one report format.
    // Call it from inside a catch block. `source` is the text the failed run was given: an error that does
    // not already name its own text is drawn against that.
    inline std::string renderCurrentException(const SourceRef &source)
    {
        try
        {
            throw;
        }
        catch (CuffError &e)
        {
            e.attachSource(source);
            return renderErrorWithSnippet(e, std::string());
        }
        catch (const std::bad_alloc &)
        {
            return "[" + errorCodeTag(ErrorCode::OutOfMemory) + "] " + errorCategoryName(ErrorCode::OutOfMemory) +
                   ": the program ran out of memory";
        }
        catch (const std::exception &e)
        {
            return std::string("Internal error: ") + e.what();
        }
        catch (...)
        {
            return "Internal error: unknown exception";
        }
    }

}
