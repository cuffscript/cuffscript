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

    class ControlFlowParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::IF, "expected 'if'");

            auto condition = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "expected 'do' after if condition");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            IfStmt ifStmt;
            ifStmt.loc = loc;

            IfStmt::Branch firstBranch;
            firstBranch.condition = std::move(condition);
            firstBranch.body = parseBranchBody(p);
            ifStmt.branches.push_back(std::move(firstBranch));

            while (p.check(TokenType::ELSE))
            {
                p.advance();

                if (p.check(TokenType::IF))
                {
                    p.advance();
                    auto elseCond = ExpressionParser::parse(p);
                    p.consume(TokenType::DO, "expected 'do' after else-if condition");
                    p.consume(TokenType::COLON, "expected ':' after 'do'");

                    IfStmt::Branch elseIfBranch;
                    elseIfBranch.condition = std::move(elseCond);
                    elseIfBranch.body = parseBranchBody(p);
                    ifStmt.branches.push_back(std::move(elseIfBranch));
                }
                else
                {
                    p.consume(TokenType::DO, "expected 'do' after else");
                    p.consume(TokenType::COLON, "expected ':' after 'do'");

                    IfStmt::Branch elseBranch;
                    elseBranch.condition = nullptr;
                    elseBranch.body = parseBranchBody(p);
                    ifStmt.branches.push_back(std::move(elseBranch));
                    break;
                }
            }

            p.consume(TokenType::END, "expected 'end' to close if statement");
            p.match(TokenType::DEDENT);

            return std::make_unique<Stmt>(StmtKind::IfStmt, std::move(ifStmt));
        }

        static std::vector<std::unique_ptr<Stmt>> parseBranchBody(ParserCore &p);
    };

}
