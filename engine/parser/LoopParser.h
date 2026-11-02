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

    class LoopParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::LOOP, "expected 'loop'");

            LoopStmt loop;
            loop.loc = loc;

            if (p.match(TokenType::REPEAT))
            {
                loop.kind = LoopStmt::LoopKind::Repeat;

                const Token var = p.consumeName("expected variable name after 'repeat'");
                loop.repeatVar = var.value;
                loop.repeatVarId = internName(loop.repeatVar);

                p.consume(TokenType::TO, "expected 'to' in repeat loop");

                loop.repeatStart = ExpressionParser::parse(p);
                p.consume(TokenType::TILDE, "expected '~' for repeat range");
                loop.repeatEnd = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for repeat loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else if (p.match(TokenType::WHILE))
            {
                loop.kind = LoopStmt::LoopKind::While;
                loop.condition = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for while loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else if (p.match(TokenType::EACH))
            {
                loop.kind = LoopStmt::LoopKind::Each;

                const Token var = p.consumeName("expected variable name after 'each'");
                loop.repeatVar = var.value;
                loop.repeatVarId = internName(loop.repeatVar);

                p.consume(TokenType::IN, "expected 'in' in each loop");
                loop.collection = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for each loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else
            {
                p.failUnexpected("expected 'repeat', 'while' or 'each' after 'loop'");
            }

            ++p.ctx.blockDepth;
            loop.body = BodyParser::parse(p);
            --p.ctx.blockDepth;

            closeBlock(p, "loop", loc);

            return std::make_unique<Stmt>(StmtKind::LoopStmt, std::move(loop));
        }
    };

}
