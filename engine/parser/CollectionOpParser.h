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

    // Parses collection manipulation statements:
    //   add [value] to [collectionName]
    //   replace [collection][index] to [newValue]
    //   replace [collection]["key"] to [newValue]
    //   remove [index/key/value] from [collectionName]
    class CollectionOpParser
    {
    public:
        // add [value] to [collectionName]
        static std::unique_ptr<Stmt> parseAdd(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::ADD, "Expected 'add'");

            auto value = ExpressionParser::parse(p);

            p.consume(TokenType::TO, "Expected 'to' in 'add' statement");

            std::string collectionName;
            if (p.check(TokenType::IDENTIFIER))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected collection name after 'to' in 'add'", p.current().location);
            }

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Add;
            stmt.addValue = std::move(value);
            stmt.collectionName = collectionName;
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        // replace [collection][index/key] to [newValue]
        static std::unique_ptr<Stmt> parseReplace(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REPLACE, "Expected 'replace'");

            // Parse the target: collectionName[index] or collectionName["key"]
            std::string collectionName;
            if (p.check(TokenType::IDENTIFIER))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected collection name after 'replace'", p.current().location);
            }

            // Parse [index] or ["key"]
            p.consume(TokenType::LBRACKET, "Expected '[' for index/key in 'replace'");
            auto indexOrKey = ExpressionParser::parse(p);
            p.consume(TokenType::RBRACKET, "Expected ']' to close index/key");

            p.consume(TokenType::TO, "Expected 'to' in 'replace' statement");

            auto newValue = ExpressionParser::parse(p);

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Replace;
            stmt.collectionName = collectionName;
            stmt.indexOrKey = std::move(indexOrKey);
            stmt.newValue = std::move(newValue);
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        // remove [index/key/value] from [collectionName]
        static std::unique_ptr<Stmt> parseRemove(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REMOVE, "Expected 'remove'");

            auto removeValue = ExpressionParser::parse(p);

            p.consume(TokenType::FROM, "Expected 'from' in 'remove' statement");

            std::string collectionName;
            if (p.check(TokenType::IDENTIFIER))
            {
                collectionName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected collection name after 'from' in 'remove'", p.current().location);
            }

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Remove;
            stmt.removeValue = std::move(removeValue);
            stmt.collectionName = collectionName;
            stmt.loc = loc;

            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }
    };

} // namespace cuff
