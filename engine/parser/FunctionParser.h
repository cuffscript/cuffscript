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
    //   set func name(params) do: <newline> ... end
    //   set returnable func name(params) do: <newline> ... end
    //   set async func name(params) do: <newline> ... end
    //   set pure func name(params) do: <newline> ... end   (body cannot touch global scope)
    //
    // Enforces: NO one-line shorthand — body must start on a new line after do:.
    // Function bodies require indentation (enforced by INDENT token).
    class FunctionParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "expected 'set'");

            // `async`, `returnable` and `pure` are independent modifiers and
            // may appear together, in any order (e.g. `set async returnable
            // pure func ...` or `set pure func ...`).
            bool isAsync = false;
            bool isReturnable = false;
            bool isPure = false;
            while (true)
            {
                if (p.match(TokenType::RETURNABLE))
                {
                    isReturnable = true;
                }
                else if (p.match(TokenType::ASYNC))
                {
                    isAsync = true;
                }
                else if (p.match(TokenType::PURE))
                {
                    isPure = true;
                }
                else
                {
                    break;
                }
            }

            p.consume(TokenType::FUNCTION, "expected 'func' keyword");

            std::string name;
            if (p.check(TokenType::IDENTIFIER))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected function name after 'func'", p.current().location);
            }

            // Parse parameter list
            p.consume(TokenType::LPAREN, "expected '(' for function parameters");

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
                    throw SyntaxError("expected parameter name", p.current().location);
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
                        throw SyntaxError("expected parameter name after ','", p.current().location);
                    }
                }
            }
            p.consume(TokenType::RPAREN, "expected ')' to close parameter list");

            // Parse do: — must be followed by newline (one-line shorthand forbidden for functions)
            p.consume(TokenType::DO, "expected 'do' keyword for function body");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            // Enforce: function body must start on a new line
            if (!p.check(TokenType::NEWLINE) && !p.check(TokenType::EOF_TOKEN))
            {
                throw SyntaxError("function body must start on a new line — one-line shorthand is forbidden for functions",
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

            p.consume(TokenType::END, "expected 'end' to close function");
            // Skip DEDENT after end
            p.match(TokenType::DEDENT);

            FunctionDecl decl;
            decl.isAsync = isAsync;
            decl.isReturnable = isReturnable;
            decl.isPure = isPure;
            decl.name = name;
            decl.nameId = internName(name);
            decl.params = std::move(params);
            for (const auto &pn : decl.params)
                decl.paramIds.push_back(internName(pn));
            decl.body = std::move(body);
            decl.loc = loc;

            return std::make_unique<Stmt>(StmtKind::FunctionDecl, std::move(decl));
        }

        // Parse a block of statements until we hit 'end', 'else', or DEDENT.
        // Shared with ControlFlowParser and LoopParser.
        static std::vector<std::unique_ptr<Stmt>> parseBlockBody(ParserCore &p);
    };

} // namespace cuff
