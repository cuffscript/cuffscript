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

    class DeclarationParser
    {
    public:
        // Function declarations are recognized here so the caller can delegate
        // them to FunctionParser.
        static bool isFunctionDecl(ParserCore &p)
        {
            if (!p.check(TokenType::SET))
                return false;
            return p.peek(1).is(TokenType::FUNCTION) || p.peek(1).is(TokenType::RETURNABLE) ||
                   p.peek(1).is(TokenType::ASYNC) || p.peek(1).is(TokenType::PURE);
        }

        static std::unique_ptr<Stmt> parseSet(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "expected 'set'");

            bool isConst = false;
            if (p.match(TokenType::CONSTANT))
            {
                isConst = true;
            }

            std::string varType;
            if (p.check(TokenType::NUMBER_TYPE))
            {
                varType = "number";
                p.advance();
            }
            else if (p.check(TokenType::STR_TYPE))
            {
                varType = "str";
                p.advance();
            }
            else if (p.check(TokenType::LIST_TYPE))
            {
                varType = "list";
                p.advance();
            }
            else if (p.check(TokenType::MAP_TYPE))
            {
                varType = "map";
                p.advance();
            }
            else if (p.check(TokenType::BOOLEAN_TYPE))
            {
                varType = "boolean";
                p.advance();
            }
            else if (p.check(TokenType::EMPTY))
            {
                varType = "empty";
                p.advance();
            }
            else if (p.check(TokenType::MATCH))
            {
                varType = "match";
                p.advance();
            }
            else
            {
                throw SyntaxError("expected a type (number, str, list, map, boolean, empty, match) after 'set'",
                                  p.current().location);
            }

            std::string name;
            if (isWordLikeToken(p.current()))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected variable name after type in 'set' declaration",
                                  p.current().location);
            }

            p.consume(TokenType::TO, "expected 'to' in 'set' declaration (CuffScript uses 'to', not '=')");
            auto value = ExpressionParser::parse(p);

            DeclarationStmt decl;
            decl.varType = varType;
            decl.name = name;
            decl.nameId = internName(name);
            decl.isConstant = isConst;
            decl.value = std::move(value);
            decl.loc = loc;

            return std::make_unique<Stmt>(StmtKind::Declaration, std::move(decl));
        }

        static std::unique_ptr<Stmt> parseChange(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::CHANGE, "expected 'change'");

            std::string name;
            if (isWordLikeToken(p.current()))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected variable name after 'change'", p.current().location);
            }

            std::vector<std::unique_ptr<Expr>> indices;
            while (p.check(TokenType::LBRACKET))
            {
                p.advance();
                indices.push_back(ExpressionParser::parse(p));
                p.consume(TokenType::RBRACKET, "expected ']' to close index in 'change' statement");
            }

            p.consume(TokenType::TO, "expected 'to' in 'change' statement");

            ChangeStmt change;
            change.name = name;
            change.nameId = internName(name);
            change.loc = loc;

            if (indices.empty() && p.check(TokenType::GLOBAL) &&
                (p.peek(1).is(TokenType::NEWLINE) || p.peek(1).is(TokenType::EOF_TOKEN) ||
                 p.peek(1).is(TokenType::END) || p.peek(1).is(TokenType::DEDENT) ||
                 p.peek(1).is(TokenType::OR_ELSE)))
            {
                p.advance();
                change.toGlobal = true;
                return std::make_unique<Stmt>(StmtKind::Change, std::move(change));
            }

            auto value = ExpressionParser::parse(p);
            change.indices = std::move(indices);
            change.value = std::move(value);

            return std::make_unique<Stmt>(StmtKind::Change, std::move(change));
        }
    };

}
