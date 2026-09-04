#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff {

// Parses import statements:
//   use DLC:[name]          — built-in library load
//   use [name] from [path]  — custom module load
//
// Enforces: must be a single line (no newlines within the statement).
class ImportParser {
public:
    static std::unique_ptr<Stmt> parse(ParserCore& p) {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::USE, "Expected 'use'");

        // Pattern 1: use DLC:name  (DLC is IDENTIFIER, then COLON, then name)
        if (p.check(TokenType::IDENTIFIER) && p.peek(1).is(TokenType::COLON)) {
            if (p.current().value == "DLC") {
                p.advance(); // DLC
                p.consume(TokenType::COLON, "Expected ':' after DLC");

                std::string libName;
                if (p.check(TokenType::IDENTIFIER)) {
                    libName = p.current().value;
                    p.advance();
                } else {
                    throw SyntaxError("Expected library name after 'DLC:'", p.current().location);
                }

                UseStmt use;
                use.isDLC = true;
                use.name = libName;
                use.loc = loc;

                return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
            }
        }

        // Pattern 2: use [name] from [path]
        std::string moduleName;
        if (p.check(TokenType::IDENTIFIER)) {
            moduleName = p.current().value;
            p.advance();
        } else {
            throw SyntaxError("Expected module name after 'use'", p.current().location);
        }

        p.consume(TokenType::FROM, "Expected 'from' in custom import");

        // Path: reconstruct from tokens (./maps/core_engine → DOT, IDENTIFIER, SLASH, IDENTIFIER, SLASH, IDENTIFIER)
        std::string path;
        while (!p.check(TokenType::NEWLINE) && !p.check(TokenType::EOF_TOKEN)
               && !p.check(TokenType::END) && !p.check(TokenType::DEDENT)) {
            const Token& t = p.current();
            if (t.is(TokenType::DOT)) {
                path += ".";
            } else if (t.is(TokenType::SLASH)) {
                path += "/";
            } else if (t.is(TokenType::IDENTIFIER)) {
                path += t.value;
            } else {
                break;
            }
            p.advance();
        }

        if (path.empty()) {
            throw SyntaxError("Expected path after 'from'", loc);
        }

        UseStmt use;
        use.isDLC = false;
        use.name = moduleName;
        use.path = path;
        use.loc = loc;

        return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
    }
};

} // namespace cuff
