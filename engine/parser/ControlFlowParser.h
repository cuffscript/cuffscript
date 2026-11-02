#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "BodyParser.h"
#include "ExpressionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

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

            ++p.ctx.blockDepth;

            IfStmt::Branch firstBranch;
            firstBranch.condition = std::move(condition);
            firstBranch.body = BodyParser::parse(p);
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
                    elseIfBranch.body = BodyParser::parse(p);
                    ifStmt.branches.push_back(std::move(elseIfBranch));
                }
                else
                {
                    p.consume(TokenType::DO, "expected 'do' after else");
                    p.consume(TokenType::COLON, "expected ':' after 'do'");

                    IfStmt::Branch elseBranch;
                    elseBranch.condition = nullptr;
                    elseBranch.body = BodyParser::parse(p);
                    ifStmt.branches.push_back(std::move(elseBranch));
                    break;
                }
            }

            --p.ctx.blockDepth;
            closeBlock(p, "if statement", loc);

            return std::make_unique<Stmt>(StmtKind::IfStmt, std::move(ifStmt));
        }

        // `with expression do: body end`
        static std::unique_ptr<Stmt> parseWith(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::WITH, "expected 'with'");

            WithStmt with;
            with.loc = loc;
            with.object = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "expected 'do' after the object of 'with'");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            ++p.ctx.withDepth;
            with.body = BodyParser::parse(p);
            --p.ctx.withDepth;
            --p.ctx.blockDepth;

            closeBlock(p, "with block", loc);
            return std::make_unique<Stmt>(StmtKind::WithStmt, std::move(with));
        }
    };

}
