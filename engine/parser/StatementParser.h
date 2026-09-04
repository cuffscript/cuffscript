#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "DeclarationParser.h"
#include "FunctionParser.h"
#include "ControlFlowParser.h"
#include "LoopParser.h"
#include "CollectionOpParser.h"
#include "ImportParser.h"
#include "OrElseParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff {

// Parses individual statements — dispatches to the appropriate sub-parser
// based on the leading keyword. Also handles or_else wrapping.
class StatementParser {
public:
    static std::unique_ptr<Stmt> parseStatement(ParserCore& p) {
        p.skipNewlines();
        if (p.atEnd()) return nullptr;

        const Token& tok = p.current();
        std::unique_ptr<Stmt> stmt;

        switch (tok.type) {
        case TokenType::SET:
            if (DeclarationParser::isFunctionDecl(p)) {
                stmt = FunctionParser::parse(p);
            } else {
                stmt = DeclarationParser::parseSet(p);
            }
            break;

        case TokenType::CHANGE:
            stmt = DeclarationParser::parseChange(p);
            break;

        case TokenType::IF:
            stmt = ControlFlowParser::parse(p);
            break;

        case TokenType::LOOP:
            stmt = LoopParser::parse(p);
            break;

        case TokenType::STOP:
            p.advance();
            stmt = std::make_unique<Stmt>(StmtKind::StopStmt, StopStmt(tok.location));
            break;

        case TokenType::RETURN: {
            p.advance();
            if (p.check(TokenType::NEWLINE) || p.check(TokenType::EOF_TOKEN)
                || p.check(TokenType::END) || p.check(TokenType::DEDENT)) {
                stmt = std::make_unique<Stmt>(StmtKind::ReturnStmt,
                    ReturnStmt(nullptr, tok.location));
            } else {
                auto val = ExpressionParser::parse(p);
                stmt = std::make_unique<Stmt>(StmtKind::ReturnStmt,
                    ReturnStmt(std::move(val), tok.location));
            }
            break;
        }

        case TokenType::AWAIT: {
            SourceLocation awaitLoc = tok.location;
            p.advance();
            auto expr = ExpressionParser::parse(p);
            if (expr->kind != ExprKind::FunctionCall) {
                throw SyntaxError("Expected function call after 'await'", awaitLoc);
            }
            FunctionCall fc = std::get<FunctionCall>(expr->data);
            auto callPtr = std::make_unique<FunctionCall>(std::move(fc));
            auto awaitExpr = std::make_unique<AwaitExpr>(std::move(callPtr), awaitLoc);
            stmt = std::make_unique<Stmt>(StmtKind::AwaitStmt,
                AwaitStmt(std::move(awaitExpr), awaitLoc));
            break;
        }

        case TokenType::USE:
            stmt = ImportParser::parse(p);
            break;

        case TokenType::ADD:
            stmt = CollectionOpParser::parseAdd(p);
            break;

        case TokenType::REMOVE:
            stmt = CollectionOpParser::parseRemove(p);
            break;

        case TokenType::REPLACE:
            stmt = CollectionOpParser::parseReplace(p);
            break;

        default: {
            // Expression statement (e.g. print(...), function call)
            auto expr = ExpressionParser::parse(p);
            stmt = std::make_unique<Stmt>(StmtKind::ExprStmt,
                ExprStmt(std::move(expr), tok.location));
            break;
        }
        }

        // Check for trailing or_else
        if (stmt && p.check(TokenType::OR_ELSE)) {
            stmt = OrElseParser::wrap(p, std::move(stmt));
        }

        return stmt;
    }
};

// ---- FunctionParser::parseBlockBody implementation (depends on StatementParser) ----
inline std::vector<std::unique_ptr<Stmt>> FunctionParser::parseBlockBody(ParserCore& p) {
    std::vector<std::unique_ptr<Stmt>> body;

    while (!p.atEnd()) {
        p.skipNewlines();
        if (p.atEnd()) break;

        // Check for terminators
        if (p.check(TokenType::END) || p.check(TokenType::ELSE) || p.check(TokenType::DEDENT)) {
            break;
        }

        auto stmt = StatementParser::parseStatement(p);
        if (stmt) {
            body.push_back(std::move(stmt));
        }

        p.skipNewlines();
    }

    return body;
}

} // namespace cuff
