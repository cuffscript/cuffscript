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

    // Parses function declarations:
    //   set function name(params) do: <newline> ... end
    //   set returnable function name(params) do: <newline> ... end
    //   set async function name(params) do: <newline> ... end
    //
    // Enforces: NO one-line shorthand — body must start on a new line after do:.
    // Function bodies require indentation (enforced by INDENT token).
    class FunctionParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "Expected 'set'");

            FunctionDecl::FuncKind kind = FunctionDecl::FuncKind::Normal;
            if (p.match(TokenType::RETURNABLE))
            {
                kind = FunctionDecl::FuncKind::Returnable;
            }
            else if (p.match(TokenType::ASYNC))
            {
                kind = FunctionDecl::FuncKind::Async;
            }

            p.consume(TokenType::FUNCTION, "Expected 'function' keyword");

            std::string name;
            if (p.check(TokenType::IDENTIFIER))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected function name after 'function'", p.current().location);
            }

            // Parse parameter list
            p.consume(TokenType::LPAREN, "Expected '(' for function parameters");

            std::vector<std::string> params;
            if (!p.check(TokenType::RPAREN))
            {
                if (p.check(TokenType::IDENTIFIER))
                {
                    params.push_back(p.current().value);
                    p.advance();
                }
                else
                {
                    throw SyntaxError("Expected parameter name", p.current().location);
                }
                while (p.match(TokenType::COMMA))
                {
                    if (p.check(TokenType::IDENTIFIER))
                    {
                        params.push_back(p.current().value);
                        p.advance();
                    }
                    else
                    {
                        throw SyntaxError("Expected parameter name after ','", p.current().location);
                    }
                }
            }
            p.consume(TokenType::RPAREN, "Expected ')' to close parameter list");

            // Parse do: — must be followed by newline (one-line shorthand forbidden for functions)
            p.consume(TokenType::DO, "Expected 'do' keyword for function body");
            p.consume(TokenType::COLON, "Expected ':' after 'do'");

            // Enforce: function body must start on a new line
            if (!p.check(TokenType::NEWLINE) && !p.check(TokenType::EOF_TOKEN))
            {
                throw SyntaxError("Function body must start on a new line — one-line shorthand is forbidden for functions",
                                  p.current().location);
            }

            // Parse function body — skip newlines and INDENT, then parse until DEDENT/END
            p.skipNewlines();
            // Expect INDENT for block body
            if (p.check(TokenType::INDENT))
            {
                p.advance(); // consume INDENT
            }

            auto body = parseBlockBody(p);

            p.consume(TokenType::END, "Expected 'end' to close function");
            // Skip DEDENT after end
            p.match(TokenType::DEDENT);

            FunctionDecl decl;
            decl.funcKind = kind;
            decl.name = name;
            decl.params = std::move(params);
            decl.body = std::move(body);
            decl.loc = loc;

            return std::make_unique<Stmt>(StmtKind::FunctionDecl, std::move(decl));
        }

        // Parse a block of statements until we hit 'end', 'else', or DEDENT.
        // Shared with ControlFlowParser and LoopParser.
        static std::vector<std::unique_ptr<Stmt>> parseBlockBody(ParserCore &p);
    };

} // namespace cuff
