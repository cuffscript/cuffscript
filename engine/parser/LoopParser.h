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

// Parses loop statements:
//   loop repeat [var] to [start] ~ [end] do: ... end
//   loop while [cond] do: ... end
//   loop match [expr] is/IS [target] do: ... end
class LoopParser {
public:
    static std::unique_ptr<Stmt> parse(ParserCore& p) {
        SourceLocation loc = p.current().location;
        p.consume(TokenType::LOOP, "Expected 'loop'");

        LoopStmt loop;
        loop.loc = loc;

        if (p.match(TokenType::REPEAT)) {
            loop.kind = LoopStmt::LoopKind::Repeat;

            // Parse loop variable
            if (p.check(TokenType::IDENTIFIER)) {
                loop.repeatVar = p.current().value;
                p.advance();
            } else {
                throw SyntaxError("Expected variable name after 'repeat'", p.current().location);
            }

            // 'to' keyword (not '=' in CuffScript)
            p.consume(TokenType::TO, "Expected 'to' in repeat loop");

            loop.repeatStart = ExpressionParser::parse(p);
            p.consume(TokenType::TILDE, "Expected '~' for repeat range");
            loop.repeatEnd = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "Expected 'do' for repeat loop");
            p.consume(TokenType::COLON, "Expected ':' after 'do'");

            loop.body = parseLoopBody(p);

        } else if (p.match(TokenType::WHILE)) {
            loop.kind = LoopStmt::LoopKind::While;
            loop.condition = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "Expected 'do' for while loop");
            p.consume(TokenType::COLON, "Expected ':' after 'do'");

            loop.body = parseLoopBody(p);

        } else if (p.match(TokenType::MATCH)) {
            loop.kind = LoopStmt::LoopKind::Match;
            // match [expr] is/IS [target]
            // The condition is a comparison expression
            loop.condition = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "Expected 'do' for match loop");
            p.consume(TokenType::COLON, "Expected ':' after 'do'");

            loop.body = parseLoopBody(p);

        } else {
            throw SyntaxError("Expected 'repeat', 'while', or 'match' after 'loop'",
                              p.current().location);
        }

        p.consume(TokenType::END, "Expected 'end' to close loop");
        p.match(TokenType::DEDENT);

        return std::make_unique<Stmt>(StmtKind::LoopStmt, std::move(loop));
    }

private:
    static std::vector<std::unique_ptr<Stmt>> parseLoopBody(ParserCore& p) {
        // One-line shorthand or block form
        if (p.check(TokenType::NEWLINE)) {
            p.skipNewlines();
            if (p.check(TokenType::INDENT)) {
                p.advance(); // consume INDENT
            }
            return FunctionParser::parseBlockBody(p);
        }

        // One-line shorthand
        std::vector<std::unique_ptr<Stmt>> body;
        auto stmt = StatementParser::parseStatement(p);
        if (stmt) body.push_back(std::move(stmt));
        return body;
    }
};

} // namespace cuff
