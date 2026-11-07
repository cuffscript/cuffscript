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

    inline const char *kTypeNamesText = "number, str, boolean, list, map, match, pattern, object, empty or a class name";

    // A type, where one is expected: the name of a built-in type, `empty`, or the name of a class. These words are
    // types only in these positions; anywhere else they are ordinary names.
    class TypeParser
    {
    public:
        static bool atType(const ParserCore &p)
        {
            return p.check(TokenType::IDENTIFIER) || p.check(TokenType::EMPTY);
        }

        static TypeSpec parse(ParserCore &p)
        {
            const Token &tok = p.current();
            TypeSpec spec;
            if (tok.is(TokenType::EMPTY))
            {
                spec.kind = TypeKind::Empty;
            }
            else if (!builtinTypeFromName(tok.value, spec.kind))
            {
                spec.kind = TypeKind::Class;
                spec.className = tok.value;
                spec.classNameId = internName(spec.className);
            }
            p.advance();
            return spec;
        }
    };

    // The target of change, add and remove: a variable, `self` or a member of the `with` object, followed by any
    // number of [index] and .field steps.
    class PlaceParser
    {
    public:
        static Place parse(ParserCore &p, const char *verb)
        {
            Place place;
            place.loc = p.current().location;
            const Token &first = p.current();

            if (first.is(TokenType::SELF))
            {
                requireSelfContext(p, first);
                place.root = Place::Root::Self;
                p.advance();
            }
            else if (first.is(TokenType::DOT))
            {
                requireWithContext(p, first);
                p.advance();
                const Token name = p.consumeMemberName("expected a member name after '.'");
                place.root = Place::Root::With;
                place.name = name.value;
                place.nameId = internName(place.name);
            }
            else if (isNameToken(first))
            {
                place.root = Place::Root::Name;
                place.name = first.value;
                place.nameId = internName(place.name);
                place.inWith = p.ctx.withDepth > 0;
                p.advance();
            }
            else
            {
                p.failUnexpected(std::string("expected a variable, a field or an item to ") + verb);
            }

            while (true)
            {
                if (p.check(TokenType::LBRACKET))
                {
                    PlaceStep step;
                    step.isIndex = true;
                    step.loc = p.current().location;
                    p.advance();
                    step.index = ExpressionParser::parse(p);
                    if (p.check(TokenType::TILDE))
                        throw SyntaxError(ErrorCode::InvalidAssignmentTarget,
                                          std::string("a slice cannot be the target of '") + verb + "'", p.current().location,
                                          "slices produce a new list or text; use an index to reach one item");
                    p.consume(TokenType::RBRACKET, "expected ']' to close the index");
                    place.steps.push_back(std::move(step));
                }
                else if (p.check(TokenType::DOT))
                {
                    PlaceStep step;
                    p.advance();
                    const Token name = p.consumeMemberName("expected a field name after '.'");
                    step.loc = name.location;
                    step.name = name.value;
                    step.nameId = internName(step.name);
                    place.steps.push_back(std::move(step));
                }
                else if (p.check(TokenType::LPAREN))
                {
                    throw SyntaxError(ErrorCode::InvalidAssignmentTarget,
                                      std::string("the result of a call cannot be the target of '") + verb + "'",
                                      p.current().location,
                                      "store the result in a variable first");
                }
                else
                {
                    break;
                }
            }

            if (place.root == Place::Root::Self && place.steps.empty())
                throw SyntaxError(ErrorCode::InvalidAssignmentTarget,
                                  std::string("'self' cannot be the target of '") + verb + "'", place.loc,
                                  "name a field of it: self.name");
            return place;
        }
    };

    class DeclarationParser
    {
    public:
        // `set [constant] TYPE NAME to expression`. Also used for the fields of a class.
        static std::unique_ptr<Stmt> parseSet(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::SET, "expected 'set'");

            rejectOldFunctionForms(p);

            bool isConst = false;
            if (p.match(TokenType::CONSTANT))
                isConst = true;

            const SourceLocation typeLoc = p.current().location;
            TypeSpec type = parseDeclaredType(p);
            const Token nameTok = p.consumeName("expected a variable name after the type in 'set' declaration");
            p.consume(TokenType::TO, "expected 'to' in 'set' declaration (CuffScript uses 'to', not '=')");
            auto value = ExpressionParser::parse(p);

            DeclarationStmt decl;
            decl.type = std::move(type);
            decl.name = nameTok.value;
            decl.nameId = internName(decl.name);
            decl.isConstant = isConst;
            decl.value = std::move(value);
            decl.loc = loc;
            decl.typeLoc = typeLoc;
            decl.nameLoc = nameTok.location;

            return std::make_unique<Stmt>(StmtKind::Declaration, std::move(decl));
        }

        // The type after `set [constant]`. A word that is not a built-in type is taken as a class name, except
        // that `set x to 5` (a name where the type belongs) is reported as a missing type.
        static TypeSpec parseDeclaredType(ParserCore &p)
        {
            if (!TypeParser::atType(p))
                p.failUnexpected(std::string("expected a type (") + kTypeNamesText + ") after 'set'");
            TypeKind builtin;
            if (p.check(TokenType::IDENTIFIER) && !builtinTypeFromName(p.current().value, builtin) &&
                p.peek(1).is(TokenType::TO) && !p.peek(2).is(TokenType::TO))
            {
                throw SyntaxError(ErrorCode::UnexpectedToken,
                                  "expected a type before '" + p.current().value + "' in 'set' declaration",
                                  p.current().location,
                                  std::string("write the type first, for example: set number ") + p.current().value + " to ...");
            }
            return TypeParser::parse(p);
        }

        static std::unique_ptr<Stmt> parseChange(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::CHANGE, "expected 'change'");

            Place place = PlaceParser::parse(p, "change");
            p.consume(TokenType::TO, "expected 'to' in 'change' statement");

            if (p.check(TokenType::GLOBAL) && isStatementEndAfter(p, 1))
            {
                const std::string name = (place.root == Place::Root::Name && place.steps.empty()) ? place.name : "NAME";
                throw SyntaxError(ErrorCode::UnexpectedToken, "'change ... to global' is not a statement",
                                  p.current().location, "write `global " + name + "` on its own line");
            }

            ChangeStmt change;
            change.target = std::move(place);
            change.value = ExpressionParser::parse(p);
            change.loc = loc;
            return std::make_unique<Stmt>(StmtKind::Change, std::move(change));
        }

        // `global NAME { , NAME }`
        static std::unique_ptr<Stmt> parseGlobal(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::GLOBAL, "expected 'global'");

            GlobalStmt global;
            global.loc = loc;
            while (true)
            {
                const Token name = p.consumeName("expected a variable name after 'global'");
                GlobalStmt::Name n;
                n.name = name.value;
                n.nameId = internName(n.name);
                n.loc = name.location;
                global.names.push_back(std::move(n));
                if (!p.match(TokenType::COMMA))
                    break;
            }
            return std::make_unique<Stmt>(StmtKind::Global, std::move(global));
        }

        static bool isStatementEndAfter(const ParserCore &p, int ahead)
        {
            switch (p.peek(ahead).type)
            {
            case TokenType::NEWLINE:
            case TokenType::EOF_TOKEN:
            case TokenType::END:
            case TokenType::DEDENT:
            case TokenType::OR_ELSE:
            case TokenType::ELSE:
                return true;
            default:
                return false;
            }
        }

    private:
        // `set func`, `set async func`, `set returnable func`: functions are declared without `set`.
        static void rejectOldFunctionForms(ParserCore &p)
        {
            const Token &t = p.current();
            const bool returnable = t.is(TokenType::IDENTIFIER) && t.value == "returnable";
            if (t.is(TokenType::FUNCTION) || t.is(TokenType::ASYNC) || t.is(TokenType::PURE) || returnable)
            {
                throw SyntaxError(ErrorCode::UnexpectedToken, "a function declaration does not start with 'set'", t.location,
                                  returnable ? "write `func`; return works in every function"
                                             : "write `func name(...) do:` without 'set'");
            }
        }
    };

}
