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

    // Parses variable declarations (set) and value changes (change).
    //
    //   set [type] [name] to [value]
    //   set constant [type] [name] to [value]
    //   change [name] to [value]
    //
    // Function declarations (set function / set returnable function / set async function)
    // are detected here but handled by FunctionParser.
    class DeclarationParser
    {
    public:
        // Check if this is a function declaration: set function / set returnable function / set async function
        static bool isFunctionDecl(ParserCore &p)
        {
            if (!p.check(TokenType::SET))
                return false;
            return p.peek(1).is(TokenType::FUNCTION) || p.peek(1).is(TokenType::RETURNABLE) || p.peek(1).is(TokenType::ASYNC);
        }

        // Parse a set declaration (non-function). Caller should check isFunctionDecl first.
        static std::unique_ptr<Stmt> parseSet(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "Expected 'set'");

            bool isConst = false;
            if (p.match(TokenType::CONSTANT))
            {
                isConst = true;
            }

            // Parse type keyword
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
            else
            {
                throw SyntaxError("Expected a type (number, str, list, map, boolean, empty) after 'set'",
                                  p.current().location);
            }

            // Parse variable name
            std::string name;
            if (p.check(TokenType::IDENTIFIER))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected variable name after type in 'set' declaration",
                                  p.current().location);
            }

            // Parse 'to' keyword and value
            p.consume(TokenType::TO, "Expected 'to' in 'set' declaration (CuffScript uses 'to', not '=')");
            auto value = ExpressionParser::parse(p);

            DeclarationStmt decl;
            decl.varType = varType;
            decl.name = name;
            decl.isConstant = isConst;
            decl.value = std::move(value);
            decl.loc = loc;

            return std::make_unique<Stmt>(StmtKind::Declaration, std::move(decl));
        }

        // Parse a change statement
        static std::unique_ptr<Stmt> parseChange(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::CHANGE, "Expected 'change'");

            std::string name;
            if (p.check(TokenType::IDENTIFIER))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected variable name after 'change'", p.current().location);
            }

            p.consume(TokenType::TO, "Expected 'to' in 'change' statement");
            auto value = ExpressionParser::parse(p);

            ChangeStmt change;
            change.name = name;
            change.value = std::move(value);
            change.loc = loc;

            return std::make_unique<Stmt>(StmtKind::Change, std::move(change));
        }
    };

} // namespace cuff
