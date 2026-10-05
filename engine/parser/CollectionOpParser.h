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

    class CollectionOpParser
    {
    public:
        static std::unique_ptr<Stmt> parseAdd(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::ADD, "expected 'add'");

            auto value = ExpressionParser::parse(p);

            p.consume(TokenType::TO, "expected 'to' in 'add' statement");

            std::string collectionName;
            if (isWordLikeToken(p.current()))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected collection name after 'to' in 'add'", p.current().location);
            }

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Add;
            stmt.addValue = std::move(value);
            stmt.collectionName = collectionName;
            stmt.collectionNameId = internName(collectionName);
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        static std::unique_ptr<Stmt> parseReplace(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REPLACE, "expected 'replace'");

            std::string collectionName;
            if (isWordLikeToken(p.current()))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected collection name after 'replace'", p.current().location);
            }

            p.consume(TokenType::LBRACKET, "expected '[' for index/key in 'replace'");
            auto indexOrKey = ExpressionParser::parse(p);
            p.consume(TokenType::RBRACKET, "expected ']' to close index/key");

            p.consume(TokenType::TO, "expected 'to' in 'replace' statement");

            auto newValue = ExpressionParser::parse(p);

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Replace;
            stmt.collectionName = collectionName;
            stmt.collectionNameId = internName(collectionName);
            stmt.indexOrKey = std::move(indexOrKey);
            stmt.newValue = std::move(newValue);
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        static std::unique_ptr<Stmt> parseRemove(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REMOVE, "expected 'remove'");

            auto removeValue = ExpressionParser::parse(p);

            p.consume(TokenType::FROM, "expected 'from' in 'remove' statement");

            std::string collectionName;
            if (isWordLikeToken(p.current()))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected collection name after 'from' in 'remove'", p.current().location);
            }

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Remove;
            stmt.removeValue = std::move(removeValue);
            stmt.collectionName = collectionName;
            stmt.collectionNameId = internName(collectionName);
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }
    };

}
