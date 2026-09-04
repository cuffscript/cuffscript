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

    // Parses control flow: if / else if / else ... end
    //
    //   if [cond] do: [stmt] end                          (one-line shorthand)
    //   if [cond] do: <newline> <INDENT> ... <DEDENT> end (block form)
    class ControlFlowParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::IF, "Expected 'if'");

            auto condition = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "Expected 'do' after if condition");
            p.consume(TokenType::COLON, "Expected ':' after 'do'");

            IfStmt ifStmt;
            ifStmt.loc = loc;

            // Parse the first branch
            IfStmt::Branch firstBranch;
            firstBranch.condition = std::move(condition);
            firstBranch.body = parseBranchBody(p);
            ifStmt.branches.push_back(std::move(firstBranch));

            // Parse else if / else branches
            // After branch body, we may see: else / else if / end
            // The parseBranchBody consumes up to (but not including) else/end
            while (p.check(TokenType::ELSE))
            {
                p.advance(); // consume 'else'

                if (p.check(TokenType::IF))
                {
                    // else if
                    p.advance(); // consume 'if'
                    auto elseCond = ExpressionParser::parse(p);
                    p.consume(TokenType::DO, "Expected 'do' after else-if condition");
                    p.consume(TokenType::COLON, "Expected ':' after 'do'");

                    IfStmt::Branch elseIfBranch;
                    elseIfBranch.condition = std::move(elseCond);
                    elseIfBranch.body = parseBranchBody(p);
                    ifStmt.branches.push_back(std::move(elseIfBranch));
                }
                else
                {
                    // plain else
                    p.consume(TokenType::DO, "Expected 'do' after else");
                    p.consume(TokenType::COLON, "Expected ':' after 'do'");

                    IfStmt::Branch elseBranch;
                    elseBranch.condition = nullptr;
                    elseBranch.body = parseBranchBody(p);
                    ifStmt.branches.push_back(std::move(elseBranch));
                    break; // else is always last
                }
            }

            p.consume(TokenType::END, "Expected 'end' to close if statement");
            p.match(TokenType::DEDENT);

            return std::make_unique<Stmt>(StmtKind::IfStmt, std::move(ifStmt));
        }

    private:
        // Parse the body of a branch — either:
        //   1. One-line shorthand: single statement on the same line
        //   2. Block form: newline + INDENT + statements + DEDENT
        static std::vector<std::unique_ptr<Stmt>> parseBranchBody(ParserCore &p)
        {
            // If next token is NEWLINE → block form
            if (p.check(TokenType::NEWLINE))
            {
                p.skipNewlines();
                if (p.check(TokenType::INDENT))
                {
                    p.advance(); // consume INDENT
                }
                return FunctionParser::parseBlockBody(p);
            }

            // One-line shorthand: parse a single statement on the same line
            std::vector<std::unique_ptr<Stmt>> body;
            auto stmt = StatementParser::parseStatement(p);
            if (stmt)
                body.push_back(std::move(stmt));
            return body;
        }
    };

} // namespace cuff
