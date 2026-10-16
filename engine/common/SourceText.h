#pragma once

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

    inline SourceRef makeSourceRef(std::string text, std::string name = std::string())
    {
        return std::make_shared<const SourceText>(SourceText{std::move(name), std::move(text)});
    }

}
