#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff {

// Forward declaration
class StatementParser;

// Parses or_else error handling:
//   [dangerous_stmt] or_else do: [fallback code] end
//
// The primary statement is typically an await call or function call.
// The fallback body is a block of statements.
class OrElseParser {
public:
    // Called after parsing the primary statement, when 'or_else' is the next token.
    // Wraps the primary statement in an OrElseStmt.
    static std::unique_ptr<Stmt> wrap(ParserCore& p, std::unique_ptr<Stmt> primaryStmt) {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::OR_ELSE, "Expected 'or_else'");
        p.consume(TokenType::DO, "Expected 'do' after 'or_else'");
        p.consume(TokenType::COLON, "Expected ':' after 'do'");

        // Parse fallback body — block form (newline + INDENT)
        std::vector<std::unique_ptr<Stmt>> fallbackBody;

        if (p.check(TokenType::NEWLINE)) {
            p.skipNewlines();
            if (p.check(TokenType::INDENT)) {
                p.advance(); // consume INDENT
            }
            fallbackBody = FunctionParser::parseBlockBody(p);
        } else {
            // One-line shorthand: single statement
            auto stmt = StatementParser::parseStatement(p);
            if (stmt) fallbackBody.push_back(std::move(stmt));
        }

        p.consume(TokenType::END, "Expected 'end' to close or_else block");
        p.match(TokenType::DEDENT);

        OrElseStmt orElse;
        orElse.primaryStmt = std::move(primaryStmt);
        orElse.fallbackBody = std::move(fallbackBody);
        orElse.loc = loc;

        return std::make_unique<Stmt>(StmtKind::OrElse, std::move(orElse));
    }
};

} // namespace cuff
