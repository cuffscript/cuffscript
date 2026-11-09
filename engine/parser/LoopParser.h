#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "BodyParser.h"
#include "ExpressionParser.h"
#include "DeclarationParser.h"
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
            NameScope names(p.declared);

            if (p.match(TokenType::REPEAT))
            {
                loop.kind = LoopStmt::LoopKind::Repeat;
                parseVariable(p, loop);

                p.consume(TokenType::ASSIGN, "expected '=' and the first number of the range after the loop variable");

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
                parseVariable(p, loop);

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

    private:
        // `TYPE NAME`: the loop variable is declared by the loop, in the same words as any other variable.
        static void parseVariable(ParserCore &p, LoopStmt &loop)
        {
            const Token typeTok = p.current();
            loop.typeLoc = typeTok.location;
            loop.varType = DeclarationParser::parseDeclaredType(p);
            DeclarationParser::requireName(p, typeTok, loop.kind == LoopStmt::LoopKind::Repeat ? "loop repeat number i = 1 ~ 3 do:" : "loop each str item in items do:");
            const Token var = p.consumeName("expected the name of the loop variable after its type");
            p.declareName(var);
            loop.repeatVar = var.value;
            loop.repeatVarId = internName(loop.repeatVar);
        }
    };

}
