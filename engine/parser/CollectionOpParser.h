#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "DeclarationParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

    class CollectionOpParser
    {
    public:
        // `add expression to place`
        static std::unique_ptr<Stmt> parseAdd(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::ADD, "expected 'add'");

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Add;
            stmt.value = ExpressionParser::parse(p);
            p.consume(TokenType::TO, "expected 'to' in 'add' statement");
            stmt.target = PlaceParser::parse(p, "add to");
            stmt.loc = loc;
            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        // `remove expression from place`
        static std::unique_ptr<Stmt> parseRemove(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REMOVE, "expected 'remove'");

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Remove;
            stmt.value = ExpressionParser::parse(p);
            p.consume(TokenType::FROM, "expected 'from' in 'remove' statement");
            stmt.target = PlaceParser::parse(p, "remove from");
            stmt.loc = loc;
            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }
    };

}
