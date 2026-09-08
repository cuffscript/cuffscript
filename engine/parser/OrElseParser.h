#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

    // Forward declaration
    class StatementParser;

    // Parses or_else error handling:
    //   [dangerous_stmt] or_else do: [fallback code] end
    //
    // The primary statement is typically an await call or function call.
    // The fallback body is a block of statements.
    class OrElseParser
    {
    public:
        // Called after parsing the primary statement, when 'or_else' is the next token.
        // Wraps the primary statement in an OrElseStmt.
        // (Body deferred to the bottom of StatementParser.h — see ControlFlowParser
        //  for why: it needs StatementParser::parseStatement to be complete.)
        static std::unique_ptr<Stmt> wrap(ParserCore &p, std::unique_ptr<Stmt> primaryStmt);
    };

} // namespace cuff
