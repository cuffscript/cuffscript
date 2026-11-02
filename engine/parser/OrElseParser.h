#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "BodyParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

    // `statement or_else [as NAME] do: fallback end`. `as` is a keyword only here, right after `or_else`.
    class OrElseParser
    {
    public:
        static std::unique_ptr<Stmt> wrap(ParserCore &p, std::unique_ptr<Stmt> primaryStmt)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::OR_ELSE, "Expected 'or_else'");

            OrElseStmt orElse;
            if (p.check(TokenType::IDENTIFIER) && p.current().value == "as")
            {
                p.advance();
                const Token name = p.consumeName("expected a name for the error after 'or_else as'");
                orElse.errorName = name.value;
                orElse.errorNameId = internName(orElse.errorName);
            }

            p.consume(TokenType::DO, "expected 'do' after 'or_else'");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            orElse.fallbackBody = BodyParser::parse(p);
            --p.ctx.blockDepth;

            closeBlock(p, "or_else block", loc);

            orElse.primaryStmt = std::move(primaryStmt);
            orElse.loc = loc;

            return std::make_unique<Stmt>(StmtKind::OrElse, std::move(orElse));
        }
    };

}
