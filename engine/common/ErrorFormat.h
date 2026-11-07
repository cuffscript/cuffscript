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

    // The source line an error points at, ready to be drawn: the line, its number, the padding that carries the
    // marks to the right column, and the marks themselves, one "^" per display column of the offending token.
    struct Snippet
    {
        int line = 1;
        std::string text;
        std::string pad;
        std::string marks;
    };

    // Where the token that starts at byte `i` of `t` ends: a word or number, a string (with its f or p prefix), a
    // two-character operator, or one character.
    inline size_t tokenEnd(const std::string &t, size_t i)
    {
        if (i >= t.size())
            return i;
        const auto isWord = [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        const auto stringEnd = [&t](size_t quote) {
            size_t j = quote + 1;
            while (j < t.size())
            {
                if (t[j] == '\\')
                    j += 2;
                else if (t[j] == t[quote])
                    return j + 1;
                else
                    ++j;
            }
            return t.size();
        };

        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c == '"' || c == '\'')
            return stringEnd(i);
        if (isWord(c))
        {
            size_t j = i + 1;
            while (j < t.size() && isWord(static_cast<unsigned char>(t[j])))
                ++j;
            if (c >= '0' && c <= '9' && j + 1 < t.size() && t[j] == '.' && t[j + 1] >= '0' && t[j + 1] <= '9')
            {
                ++j;
                while (j < t.size() && t[j] >= '0' && t[j] <= '9')
                    ++j;
            }
            if (j == i + 1 && (c == 'f' || c == 'p') && j < t.size() && (t[j] == '"' || t[j] == '\''))
                return stringEnd(j);
            return j;
        }
        if (c >= 0x80)
        {
            size_t j = i;
            utf8::decode(t, j);
            return j;
        }
        if (i + 1 < t.size())
        {
            const char a = t[i], b = t[i + 1];
            if ((a == '*' && b == '*') || (b == '=' && (a == '<' || a == '>' || a == '!' || a == '=')) || (a == '-' && b == '>'))
                return i + 2;
        }
        return i + 1;
    }

    // Finds the source line `loc` points at. The padding is placed by display width, not by byte or code point
    // count: a tab in the line is copied into the padding (so it lines up whatever the terminal's tab stops are),
    // and a wide character such as Hangul takes two columns. Returns false when there is no source to draw from.
    inline bool buildSnippet(const std::string &source, const SourceLocation &loc, Snippet &out)
    {
        if (source.empty() || loc.offset < 0)
            return false;

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
            return false;

        out.line = 1 + static_cast<int>(std::count(source.begin() + static_cast<std::ptrdiff_t>(bom),
                                                   source.begin() + static_cast<std::ptrdiff_t>(lineStart), '\n'));
        out.text = source.substr(lineStart, lineEnd - lineStart);
        if (!out.text.empty() && out.text.back() == '\r')
            out.text.pop_back();

        const size_t prefixBytes = std::min(offset - lineStart, out.text.size());
        out.pad.clear();
        for (size_t i = 0; i < prefixBytes;)
        {
            const char32_t cp = utf8::decode(out.text, i);
            if (cp == U'\t')
            {
                out.pad += '\t';
                continue;
            }
            out.pad.append(static_cast<size_t>(std::max(width::cpWidth(cp), 0)), ' ');
        }

        size_t columns = 0;
        for (size_t i = prefixBytes, end = tokenEnd(out.text, prefixBytes); i < end;)
        {
            const char32_t cp = utf8::decode(out.text, i);
            columns += static_cast<size_t>(std::max(width::cpWidth(cp), 1));
        }
        out.marks.assign(std::max<size_t>(columns, 1), '^');
        return true;
    }

    // The three lines that show where it went wrong, then the message beside the marks:
    //
    //       |
    //     3 |     set returnable func add(number a, number b) do:
    //       |     ^^^ unexpected keyword
    //
    // `indent` goes in front of every line. A message of several lines keeps its lines under the first one.
    inline std::string formatSnippet(const Snippet &s, const std::string &label, const std::string &indent = std::string())
    {
        const std::string number = std::to_string(s.line);
        const std::string gutter = indent + std::string(number.size(), ' ') + " |";
        std::string out = gutter + "\n" + indent + number + " |";
        if (!s.text.empty())
            out += " " + s.text;
        out += "\n" + gutter + " " + s.pad + s.marks;
        if (label.empty())
            return out;
        const std::string under = gutter + " " + s.pad + std::string(s.marks.size(), ' ');
        size_t from = 0;
        bool first = true;
        while (true)
        {
            const size_t nl = label.find('\n', from);
            const std::string piece = label.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
            out += first ? " " + piece : "\n" + under + " " + piece;
            first = false;
            if (nl == std::string::npos)
                break;
            from = nl + 1;
        }
        return out;
    }

    // "[E4-006] Runtime Error at line 3, column 9". An error that came from another file says which:
    // "[E4-006] Runtime Error in 'lib/helper.cuff' at line 3, column 9".
    inline std::string errorHeadlineOnly(const CuffError &e)
    {
        std::string out = "[" + errorCodeTag(e.code) + "] " + e.category;
        if (e.source && !e.source->name.empty())
            out += " in '" + e.source->name + "'";
        return out + " at " + e.location.toString();
    }

    // The same with the message after it, for an error that has no source line to draw:
    // "[E4-006] Runtime Error at line 3, column 9: division by zero".
    inline std::string errorHeadline(const CuffError &e)
    {
        return errorHeadlineOnly(e) + ": " + e.message;
    }

    // "  hint: ..." after a blank line; a hint of several lines keeps its lines under the first one.
    inline std::string formatHint(const std::string &hint)
    {
        std::string out = "\n\n  hint: ";
        for (const char c : hint)
        {
            out += c;
            if (c == '\n')
                out += "        ";
        }
        return out;
    }

    // The full report: the headline, the source line with the message beside its marks, anything related, then the
    // hint. The line is taken from the text the error itself points into (e.source) and only falls back to
    // `fallbackSource`, the program the caller ran, for an error that carries none.
    inline std::string renderErrorWithSnippet(const CuffError &e, const std::string &fallbackSource)
    {
        Snippet snippet;
        std::string out;
        if (buildSnippet(e.source ? e.source->text : fallbackSource, e.location, snippet))
            out = errorHeadlineOnly(e) + "\n" + formatSnippet(snippet, e.message);
        else
            out = errorHeadline(e);
        if (!e.related.empty())
            out += "\n\n" + e.related;
        if (!e.hint.empty())
            out += formatHint(e.hint);
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
