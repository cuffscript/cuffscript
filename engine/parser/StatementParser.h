#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "BodyParser.h"
#include "ExpressionParser.h"
#include "DeclarationParser.h"
#include "FunctionParser.h"
#include "ClassParser.h"
#include "ControlFlowParser.h"
#include "LoopParser.h"
#include "CollectionOpParser.h"
#include "ImportParser.h"
#include "OrElseParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

    class StatementParser
    {
    public:
        // One statement, including a trailing `or_else`. A statement ends at the end of its line, or where the
        // enclosing one-line construct ends (`end`, `else`, `or_else`); anything else after it is an error.
        static std::unique_ptr<Stmt> parseStatement(ParserCore &p)
        {
            p.skipNewlines();
            if (p.atEnd())
                return nullptr;

            const Token &tok = p.current();
            ParseDepthScope depth(tok.location);
            std::unique_ptr<Stmt> stmt = parseOne(p, tok);

            if (stmt && p.check(TokenType::OR_ELSE))
                stmt = OrElseParser::wrap(p, std::move(stmt));

            if (!p.atStatementEnd())
                p.failUnexpected("expected the end of the line after the statement");

            return stmt;
        }

    private:
        static std::unique_ptr<Stmt> simple(StmtKind kind, const SourceLocation &loc)
        {
            if (kind == StmtKind::StopStmt)
                return std::make_unique<Stmt>(kind, StopStmt(loc));
            return std::make_unique<Stmt>(kind, SkipStmt(loc));
        }

        static std::unique_ptr<Stmt> parseOne(ParserCore &p, const Token &tok)
        {
            switch (tok.type)
            {
            case TokenType::CONSTANT:
                return DeclarationParser::parseDeclaration(p);

            case TokenType::GLOBAL:
                if (isNameToken(p.peek(1)))
                    return DeclarationParser::parseGlobal(p);
                break;

            case TokenType::FUNCTION:
            case TokenType::ASYNC:
            case TokenType::PURE:
                return FunctionParser::parse(p);

            case TokenType::CLASS:
                return ClassParser::parse(p);

            case TokenType::PRIVATE:
                throw SyntaxError(ErrorCode::UnexpectedToken, "'private' can only be used on a member of a class", tok.location,
                                  "put the field or method inside a class body");

            case TokenType::IF:
                return ControlFlowParser::parse(p);

            case TokenType::WITH:
                return ControlFlowParser::parseWith(p);

            case TokenType::LOOP:
                return LoopParser::parse(p);

            case TokenType::STOP:
                p.advance();
                return simple(StmtKind::StopStmt, tok.location);

            case TokenType::SKIP:
                p.advance();
                return simple(StmtKind::SkipStmt, tok.location);

            case TokenType::RETURN:
            {
                const SourceLocation loc = tok.location;
                p.advance();
                if (p.atStatementEnd())
                    return std::make_unique<Stmt>(StmtKind::ReturnStmt, ReturnStmt(nullptr, loc));
                auto val = ExpressionParser::parse(p);
                return std::make_unique<Stmt>(StmtKind::ReturnStmt, ReturnStmt(std::move(val), loc));
            }

            case TokenType::FAIL:
            {
                const SourceLocation loc = tok.location;
                p.advance();
                if (p.atStatementEnd())
                    p.failUnexpected("expected an error after 'fail': a message or an Error");
                auto val = ExpressionParser::parse(p);
                return std::make_unique<Stmt>(StmtKind::FailStmt, FailStmt(std::move(val), loc));
            }

            case TokenType::AWAIT:
            {
                const SourceLocation awaitLoc = tok.location;
                p.advance();
                auto awaitExpr = ExpressionParser::makeAwait(p, awaitLoc);
                return std::make_unique<Stmt>(StmtKind::AwaitStmt, AwaitStmt(std::move(awaitExpr), awaitLoc));
            }

            case TokenType::USE:
                return ImportParser::parse(p);

            case TokenType::ADD:
                return CollectionOpParser::parseAdd(p);

            case TokenType::REMOVE:
                return CollectionOpParser::parseRemove(p);

            default:
                break;
            }

            DeclarationParser::rejectOldForms(p);
            if (DeclarationParser::atDeclaration(p))
                return DeclarationParser::parseDeclaration(p);
            if (DeclarationParser::atAssignment(p))
                return DeclarationParser::parseAssign(p);

            auto expr = ExpressionParser::parse(p);
            return std::make_unique<Stmt>(StmtKind::ExprStmt, ExprStmt(std::move(expr), tok.location));
        }
    };

    inline std::vector<std::unique_ptr<Stmt>> BodyParser::parseLines(ParserCore &p)
    {
        std::vector<std::unique_ptr<Stmt>> body;

        while (!p.atEnd())
        {
            p.skipBodyBreaks();
            if (p.atEnd())
                break;

            if (p.check(TokenType::END) || p.check(TokenType::ELSE))
                break;

            auto stmt = StatementParser::parseStatement(p);
            if (stmt)
                body.push_back(std::move(stmt));

            p.skipBodyBreaks();
        }

        return body;
    }

    inline std::vector<std::unique_ptr<Stmt>> BodyParser::parse(ParserCore &p)
    {
        NameScope names(p.declared);
        if (p.check(TokenType::NEWLINE))
        {
            p.openBody();
            return parseLines(p);
        }

        std::vector<std::unique_ptr<Stmt>> body;
        auto stmt = StatementParser::parseStatement(p);
        if (stmt)
            body.push_back(std::move(stmt));
        return body;
    }

}
