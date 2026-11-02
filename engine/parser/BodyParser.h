#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include <memory>
#include <vector>

namespace cuff
{

    // The statements between `do:` and `end` (or `else`): a block on the following lines, or a single statement on
    // the same line. Defined in StatementParser.h, which every construct that has a body is parsed through.
    class BodyParser
    {
    public:
        static std::vector<std::unique_ptr<Stmt>> parse(ParserCore &p);

        // The statements of a multi-line block, up to (not including) its `end` or `else`.
        static std::vector<std::unique_ptr<Stmt>> parseLines(ParserCore &p);
    };

    // Raises the error for a block whose body is open: `do:` followed by `end` needs at least ... nothing; an empty
    // body is allowed. Kept here so that every construct reports a missing `end` the same way.
    inline void closeBlock(ParserCore &p, const char *what, const SourceLocation &openedAt)
    {
        p.consumeEnd(std::string("expected 'end' to close ") + what, openedAt);
        p.match(TokenType::DEDENT);
    }

}
