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

    class StatementParser;

    class OrElseParser
    {
    public:
        static std::unique_ptr<Stmt> wrap(ParserCore &p, std::unique_ptr<Stmt> primaryStmt);
    };

}
