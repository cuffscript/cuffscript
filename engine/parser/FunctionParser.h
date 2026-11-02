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

    // What a function declaration belongs to: nothing (a function of the program) or a class.
    struct FunctionOwner
    {
        bool isMethod = false;
        bool isPrivate = false;
        bool classHasParent = false;
    };

    // `{ async | pure } func NAME ( parameters ) do: body end`, as a function of the program or a method of a class.
    class FunctionParser
    {
    public:
        using Owner = FunctionOwner;

        static bool atFunctionStart(const ParserCore &p)
        {
            return p.check(TokenType::FUNCTION) || p.check(TokenType::ASYNC) || p.check(TokenType::PURE);
        }

        static std::unique_ptr<Stmt> parse(ParserCore &p, const FunctionOwner &owner = FunctionOwner())
        {
            SourceLocation loc = p.current().location;

            bool isAsync = false;
            bool isPure = false;
            while (true)
            {
                const Token &t = p.current();
                if (t.is(TokenType::IDENTIFIER) && t.value == "returnable")
                    throw SyntaxError(ErrorCode::UnexpectedToken, "'returnable' is not a modifier", t.location,
                                      "write `func`; return works in every function");
                if (t.is(TokenType::ASYNC) || t.is(TokenType::PURE))
                {
                    bool &flag = t.is(TokenType::ASYNC) ? isAsync : isPure;
                    if (flag)
                        throw SyntaxError(ErrorCode::UnexpectedToken, "duplicate '" + t.value + "' modifier", t.location);
                    flag = true;
                    p.advance();
                    continue;
                }
                break;
            }

            p.consume(TokenType::FUNCTION, "expected 'func' keyword");

            const Token nameTok = p.consumeName("expected function name after 'func'");
            if (owner.isMethod && nameTok.value == "init" && isAsync)
                throw SyntaxError(ErrorCode::UnexpectedToken, "a constructor cannot be 'async'", nameTok.location,
                                  "the object has to be complete when the call returns");

            FunctionDecl decl;
            decl.isAsync = isAsync;
            decl.isPure = isPure;
            decl.isPrivate = owner.isPrivate;
            decl.isMethod = owner.isMethod;
            decl.name = nameTok.value;
            decl.nameId = internName(decl.name);
            decl.loc = loc;
            decl.source = p.source;

            ContextGuard guard(p);
            p.ctx.inMethod = owner.isMethod;
            p.ctx.hasParent = owner.isMethod && owner.classHasParent;
            p.ctx.withDepth = 0;

            p.consume(TokenType::LPAREN, "expected '(' for function parameters");
            if (!p.check(TokenType::RPAREN))
            {
                parseParam(p, decl);
                while (p.match(TokenType::COMMA))
                    parseParam(p, decl);
            }
            p.consume(TokenType::RPAREN, "expected ')' to close parameter list");

            p.consume(TokenType::DO, "expected 'do' keyword for function body");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            decl.body = BodyParser::parse(p);
            --p.ctx.blockDepth;

            closeBlock(p, "function", loc);

            return std::make_unique<Stmt>(StmtKind::FunctionDecl, std::move(decl));
        }

    private:
        // `[TYPE] NAME [to expression]`. A word followed by a name is a type; a word alone is the name.
        static void parseParam(ParserCore &p, FunctionDecl &decl)
        {
            TypeSpec type;
            if (p.check(TokenType::EMPTY))
                throw SyntaxError(ErrorCode::UnexpectedToken, "'empty' cannot be the type of a parameter", p.current().location,
                                  "leave the type out to accept any value");
            if (p.check(TokenType::IDENTIFIER) && isNameToken(p.peek(1)) && !p.peek(1).is(TokenType::TO))
            {
                type = TypeParser::parse(p);
                decl.hasTypedParams = true;
            }

            const Token nameTok = p.consumeName("expected parameter name");
            for (const auto &existing : decl.params)
                if (existing == nameTok.value)
                    throw SyntaxError(ErrorCode::UnexpectedToken, "duplicate parameter name '" + nameTok.value + "'",
                                      nameTok.location);

            std::unique_ptr<Expr> fallback;
            if (p.match(TokenType::TO))
                fallback = ExpressionParser::parse(p);
            else if (!decl.paramDefaults.empty() && decl.paramDefaults.back())
                throw SyntaxError(ErrorCode::UnexpectedToken,
                                  "parameter '" + nameTok.value + "' needs a default value: it follows a parameter that has one",
                                  nameTok.location, "write `" + nameTok.value + " to value`, or move it before the others");

            decl.params.push_back(nameTok.value);
            decl.paramIds.push_back(internName(nameTok.value));
            decl.paramTypes.push_back(std::move(type));
            if (!fallback)
                decl.requiredParams = decl.params.size();
            decl.paramDefaults.push_back(std::move(fallback));
        }
    };

}
