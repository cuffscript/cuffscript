#pragma once

#include "SourceLocation.h"
#include <cstddef>
#include <memory>
#include <string>

namespace cuff
{

    // The program text a SourceLocation's offset points into.
    //
    // An error has to be drawn against the text it was found in, which is not always the text that was
    // being run when it surfaced: a function from an imported module, or from an earlier shell entry, fails
    // while some other source is executing. The parser therefore stamps every function it declares with the
    // SourceRef it came from, and the interpreter copies it onto an error that passes out through the call.
    // `name` is how the program refers to the file in messages ("lib/helper.cuff"); it is empty for the
    // main program and for shell entries.
    struct SourceText
    {
        std::string name;
        std::string text;
    };

    using SourceRef = std::shared_ptr<const SourceText>;

    // The location of byte `offset` of `text`, counted forward from a location whose own offset is not later. It
    // applies the tokenizer's rules: a line ends at LF, and a column counts characters, not the continuation bytes
    // of a multi-byte one. Used to place something that was found inside a piece of text (the expression in an
    // f-string's braces) at its true position in the program.
    inline SourceLocation locationAfter(const std::string &text, SourceLocation from, size_t offset)
    {
        for (size_t i = static_cast<size_t>(from.offset); i < offset && i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == '\n')
            {
                ++from.line;
                from.column = 1;
            }
            else if ((c & 0xC0) != 0x80)
            {
                ++from.column;
            }
        }
        from.offset = static_cast<int>(offset);
        return from;
    }

    inline SourceRef makeSourceRef(std::string text, std::string name = std::string())
    {
        return std::make_shared<const SourceText>(SourceText{std::move(name), std::move(text)});
    }

}
