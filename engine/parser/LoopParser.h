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

                if (isWordLikeToken(p.current()))
                {
                    loop.repeatVar = p.current().value;
                    loop.repeatVarId = internName(loop.repeatVar);
                    p.advance();
                }
                else
                {
                    throw SyntaxError("expected variable name after 'repeat'", p.current().location);
                }

                p.consume(TokenType::TO, "expected 'to' in repeat loop");

                loop.repeatStart = ExpressionParser::parse(p);
                p.consume(TokenType::TILDE, "expected '~' for repeat range");
                loop.repeatEnd = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for repeat loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");

                loop.body = parseLoopBody(p);
            }
            else if (p.match(TokenType::WHILE))
            {
                loop.kind = LoopStmt::LoopKind::While;
                loop.condition = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for while loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");

                loop.body = parseLoopBody(p);
            }
            else
            {
                throw SyntaxError("expected 'repeat' or 'while' after 'loop'",
                                  p.current().location);
            }

            p.consume(TokenType::END, "expected 'end' to close loop");
            p.match(TokenType::DEDENT);

            return std::make_unique<Stmt>(StmtKind::LoopStmt, std::move(loop));
        }

        static std::vector<std::unique_ptr<Stmt>> parseLoopBody(ParserCore &p);
    };

}
