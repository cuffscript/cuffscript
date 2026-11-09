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

    inline const char *kTypeNamesText = "number, str, boolean, list, map, match, pattern, object, empty, any or a class name";

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

    // The target of an assignment, add and remove: a variable, `self` or a member of the `with` object, followed by any
    // number of [index] and .field steps.
    class PlaceParser
    {
    public:
        static Place parse(ParserCore &p, const char *verb, const char *target)
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
                                          std::string("a slice cannot be the target of ") + target, p.current().location,
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
                                      std::string("the result of a call cannot be the target of ") + target,
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
                                  std::string("'self' cannot be the target of ") + target, place.loc,
                                  "name a field of it: self.name");
            return place;
        }
    };

    class DeclarationParser
    {
    public:
        // Whether the statement starts with a type, a name and `=`: `number x = 1`. A type word is a type only here, so
        // `number = 1` assigns to a variable that happens to be called number.
        static bool atDeclaration(const ParserCore &p)
        {
            return p.check(TokenType::CONSTANT) ||
                   (TypeParser::atType(p) && p.peek(1).is(TokenType::IDENTIFIER) && p.peek(2).is(TokenType::ASSIGN));
        }

        // Whether the statement is an assignment: a place followed by `=` somewhere before the end of the statement.
        static bool atAssignment(const ParserCore &p)
        {
            if (!(p.check(TokenType::IDENTIFIER) || p.check(TokenType::SELF) || p.check(TokenType::DOT)))
                return false;
            int depth = 0;
            for (int i = 0;; ++i)
            {
                switch (p.peek(i).type)
                {
                case TokenType::LPAREN:
                case TokenType::LBRACKET:
                case TokenType::LBRACE:
                    ++depth;
                    break;
                case TokenType::RPAREN:
                case TokenType::RBRACKET:
                case TokenType::RBRACE:
                    --depth;
                    break;
                case TokenType::ASSIGN:
                    if (depth == 0)
                        return true;
                    break;
                case TokenType::NEWLINE:
                    if (depth > 0)
                        break;
                    [[fallthrough]];
                case TokenType::EOF_TOKEN:
                case TokenType::END:
                case TokenType::ELSE:
                case TokenType::OR_ELSE:
                case TokenType::DEDENT:
                    return false;
                default:
                    break;
                }
            }
        }

        // `[constant] TYPE NAME = expression`. Also used for the fields of a class (which are not variables, so their
        // names are not recorded as declared).
        static std::unique_ptr<Stmt> parseDeclaration(ParserCore &p, bool isField = false)
        {
            SourceLocation loc = p.current().location;

            bool isConst = false;
            if (p.match(TokenType::CONSTANT))
                isConst = true;

            const SourceLocation typeLoc = p.current().location;
            TypeSpec type = parseDeclaredType(p);
            const Token nameTok = p.consumeName("expected a name after the type in a declaration");
            if (!isField)
                p.declareName(nameTok);
            p.consume(TokenType::ASSIGN, "expected '=' and a value after the name in a declaration");
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

        // The type of a declaration, a parameter, a loop variable or the error of a handler.
        static TypeSpec parseDeclaredType(ParserCore &p)
        {
            if (!TypeParser::atType(p))
                p.failUnexpected(std::string("expected a type (") + kTypeNamesText + ")");
            return TypeParser::parse(p);
        }

        // A type is always followed by the name it declares. A name written alone (`loop each x in xs`, `f(a)`) is read as a
        // type, so say what to write.
        static void requireName(const ParserCore &p, const Token &typeTok, const std::string &example)
        {
            if (!isNameToken(p.current()))
                throw SyntaxError(ErrorCode::UnexpectedToken,
                                  "'" + typeTok.value + "' is read as a type here, and no name follows it (got " +
                                      describeToken(p.current()) + ")",
                                  typeTok.location, "write the type and then the name, for example: " + example);
        }

        // `set` and `change` were the words of declarations and assignments before `=`: name what to write instead.
        static void rejectOldForms(ParserCore &p)
        {
            const Token &t = p.current();
            if (!t.is(TokenType::IDENTIFIER) || (t.value != "set" && t.value != "change"))
                return;
            const Token &next = p.peek(1);
            const bool function = next.is(TokenType::FUNCTION) || next.is(TokenType::ASYNC) || next.is(TokenType::PURE) ||
                                  (next.is(TokenType::IDENTIFIER) && next.value == "returnable");
            if (t.value == "set" && function)
                throw SyntaxError(ErrorCode::UnexpectedToken, "a function declaration does not start with 'set'", t.location,
                                  "write `func name(...) do:` without 'set'; return works in every function");
            if (!(next.is(TokenType::IDENTIFIER) || next.is(TokenType::SELF) || next.is(TokenType::DOT) ||
                  next.is(TokenType::CONSTANT) || next.is(TokenType::EMPTY)))
                return;
            if (t.value == "set")
                throw SyntaxError(ErrorCode::UnexpectedToken, "a declaration does not start with 'set'", t.location,
                                  "write the type, the name, '=' and the value, for example: number x = 1");
            throw SyntaxError(ErrorCode::UnexpectedToken, "an assignment does not start with 'change'", t.location,
                              "write the name, '=' and the value, for example: x = 1");
        }

        static std::unique_ptr<Stmt> parseAssign(ParserCore &p)
        {
            SourceLocation loc = p.current().location;

            Place place = PlaceParser::parse(p, "assign to", "an assignment");
            p.consume(TokenType::ASSIGN, "expected '=' in an assignment");

            AssignStmt assign;
            assign.target = std::move(place);
            assign.value = ExpressionParser::parse(p);
            assign.loc = loc;
            return std::make_unique<Stmt>(StmtKind::Assign, std::move(assign));
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
    };

}
