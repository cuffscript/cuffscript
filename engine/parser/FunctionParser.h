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

    class FunctionParser
    {
    private:
        static ParamType paramTypeFromToken(TokenType t)
        {
            switch (t)
            {
            case TokenType::NUMBER_TYPE:
                return ParamType::Number;
            case TokenType::STR_TYPE:
                return ParamType::Str;
            case TokenType::BOOLEAN_TYPE:
                return ParamType::Boolean;
            case TokenType::LIST_TYPE:
                return ParamType::List;
            case TokenType::MAP_TYPE:
                return ParamType::Map;
            case TokenType::MATCH:
                return ParamType::Match;
            default:
                return ParamType::Any;
            }
        }

    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "expected 'set'");

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
            if (isWordLikeToken(p.current()))
            {
                name = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected function name after 'func'", p.current().location);
            }

            p.consume(TokenType::LPAREN, "expected '(' for function parameters");

            std::vector<std::string> params;
            std::vector<ParamType> paramTypes;
            bool anyTyped = false;
            auto parseParam = [&](const char *missingNameMsg)
            {
                ParamType type = ParamType::Any;
                ParamType fromKeyword = paramTypeFromToken(p.current().type);
                if (fromKeyword != ParamType::Any && isWordLikeToken(p.peek(1)))
                {
                    type = fromKeyword;
                    anyTyped = true;
                    p.advance();
                }
                if (!isWordLikeToken(p.current()))
                    throw SyntaxError(missingNameMsg, p.current().location);
                params.push_back(p.current().value);
                paramTypes.push_back(type);
                p.advance();
            };
            if (!p.check(TokenType::RPAREN))
            {
                parseParam("expected parameter name");
                while (p.match(TokenType::COMMA))
                    parseParam("expected parameter name after ','");
            }
            p.consume(TokenType::RPAREN, "expected ')' to close parameter list");

            p.consume(TokenType::DO, "expected 'do' keyword for function body");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            if (!p.check(TokenType::NEWLINE) && !p.check(TokenType::EOF_TOKEN))
            {
                throw SyntaxError("function body must start on a new line — one-line shorthand is forbidden for functions",
                                  p.current().location);
            }

            p.skipNewlines();
            if (p.check(TokenType::INDENT))
            {
                p.advance();
            }

            auto body = parseBlockBody(p);

            p.consume(TokenType::END, "expected 'end' to close function");
            p.match(TokenType::DEDENT);

            FunctionDecl decl;
            decl.isAsync = isAsync;
            decl.isReturnable = isReturnable;
            decl.isPure = isPure;
            decl.name = name;
            decl.nameId = internName(name);
            decl.params = std::move(params);
            decl.paramTypes = std::move(paramTypes);
            decl.hasTypedParams = anyTyped;
            for (const auto &pn : decl.params)
                decl.paramIds.push_back(internName(pn));
            decl.body = std::move(body);
            decl.loc = loc;

            return std::make_unique<Stmt>(StmtKind::FunctionDecl, std::move(decl));
        }

        static std::vector<std::unique_ptr<Stmt>> parseBlockBody(ParserCore &p);
    };

}
