#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "DeclarationParser.h"
#include "FunctionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>

namespace cuff
{

    // `class NAME [extends NAME] do: { [private] field | [private] method } end`
    class ClassParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            if (p.ctx.blockDepth > 0)
                throw SyntaxError(ErrorCode::ClassNotTopLevel, "a class can only be declared at the top level of a file", loc,
                                  "move the class out of the block or function that contains it");
            p.consume(TokenType::CLASS, "expected 'class'");

            ClassDecl decl;
            decl.loc = loc;
            decl.source = p.source;

            const Token nameTok = p.consumeName("expected a class name after 'class'");
            decl.name = nameTok.value;
            decl.nameId = internName(decl.name);

            if (p.check(TokenType::EXTENDS))
            {
                p.advance();
                const Token parent = p.consumeName("expected the name of the parent class after 'extends'");
                decl.parentName = parent.value;
                decl.parentNameId = internName(decl.parentName);
                decl.parentLoc = parent.location;
            }

            p.consume(TokenType::DO, "expected 'do' after the class header");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            while (true)
            {
                p.skipNewlines();
                if (p.atEnd() || p.check(TokenType::END))
                    break;
                parseMember(p, decl);
            }
            --p.ctx.blockDepth;

            closeBlock(p, "class", loc);
            return std::make_unique<Stmt>(StmtKind::ClassDecl, std::move(decl));
        }

    private:
        static void parseMember(ParserCore &p, ClassDecl &decl)
        {
            ParseDepthScope depth(p.current().location);
            bool isPrivate = false;
            if (p.check(TokenType::PRIVATE))
            {
                isPrivate = true;
                p.advance();
            }

            if (p.check(TokenType::SET))
            {
                decl.fields.push_back(parseField(p, isPrivate));
            }
            else if (FunctionParser::atFunctionStart(p))
            {
                FunctionParser::Owner owner;
                owner.isMethod = true;
                owner.isPrivate = isPrivate;
                owner.classHasParent = !decl.parentName.empty();
                decl.methods.push_back(FunctionParser::parse(p, owner));
            }
            else
            {
                p.failUnexpected("a class body can only contain fields ('set') and methods ('func')");
            }

            if (!p.atStatementEnd())
                p.failUnexpected("expected the end of the line after a class member");
        }

        // `set [constant] TYPE NAME to expression`; the default is computed for every new object, and cannot use self.
        static FieldDecl parseField(ParserCore &p, bool isPrivate)
        {
            FieldDecl field;
            field.loc = p.current().location;
            field.isPrivate = isPrivate;
            p.consume(TokenType::SET, "expected 'set'");
            if (p.match(TokenType::CONSTANT))
                field.isConstant = true;
            field.type = DeclarationParser::parseDeclaredType(p);
            const Token nameTok = p.consumeName("expected a field name after the type");
            field.name = nameTok.value;
            field.nameId = internName(field.name);
            p.consume(TokenType::TO, "expected 'to' and a default value in a field declaration");
            ContextGuard guard(p);
            p.ctx.inMethod = false;
            p.ctx.hasParent = false;
            p.ctx.withDepth = 0;
            field.defaultValue = ExpressionParser::parse(p);
            return field;
        }
    };

}
