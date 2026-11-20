#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "../common/Token.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/SourceText.h"
#include "../lexer/Lexer.h"
#include "../lexer/Tokenizer.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_set>

namespace cuff
{

    // ---- bodies ---------------------------------------------------------------------------------------------------

    // The statements between `do:` and `end` (or `else`): a block on the following lines, or a single statement on
    // the same line. Defined after StatementParser, below, which every construct that has a body is parsed through.
    class BodyParser
    {
    public:
        static std::vector<std::unique_ptr<Stmt>> parse(ParserCore &p);

        // The statements of a multi-line block, up to (not including) its `end` or `else`.
        static std::vector<std::unique_ptr<Stmt>> parseLines(ParserCore &p);
    };

    // Raises the error for a block whose body is open: `do:` followed by `end` needs at least ... nothing; an empty
    // body is allowed. Kept here so that every construct reports a missing `end` the same way.
    inline void closeBlock(ParserCore &p, const char *what, const SourceLocation &openedAt)
    {
        p.consumeEnd(std::string("expected 'end' to close ") + what, openedAt);
        p.match(TokenType::DEDENT);
    }

    // ---- declarations, assignments and places ---------------------------------------------------------------------

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

    // ---- functions ------------------------------------------------------------------------------------------------

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
            FunctionNames names(p.declared, p.earlierDeclared);
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
        // `TYPE NAME [= expression]`
        static void parseParam(ParserCore &p, FunctionDecl &decl)
        {
            const Token typeTok = p.current();
            TypeSpec type = DeclarationParser::parseDeclaredType(p);
            DeclarationParser::requireName(p, typeTok, "any " + typeTok.value);
            if (type.kind != TypeKind::Any)
                decl.hasTypedParams = true;

            const Token nameTok = p.consumeName("expected parameter name");
            for (const auto &existing : decl.params)
                if (existing == nameTok.value)
                    throw SyntaxError(ErrorCode::UnexpectedToken, "duplicate parameter name '" + nameTok.value + "'",
                                      nameTok.location);
            p.declareName(nameTok);

            std::unique_ptr<Expr> fallback;
            if (p.match(TokenType::ASSIGN))
                fallback = ExpressionParser::parse(p);
            else if (!decl.paramDefaults.empty() && decl.paramDefaults.back())
                throw SyntaxError(ErrorCode::UnexpectedToken,
                                  "parameter '" + nameTok.value + "' needs a default value: it follows a parameter that has one",
                                  nameTok.location, "write `" + type.name() + " " + nameTok.value + " = value`, or move it before the others");

            decl.params.push_back(nameTok.value);
            decl.paramIds.push_back(internName(nameTok.value));
            decl.paramTypes.push_back(std::move(type));
            if (!fallback)
                decl.requiredParams = decl.params.size();
            decl.paramDefaults.push_back(std::move(fallback));
        }
    };

    // ---- classes --------------------------------------------------------------------------------------------------

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

            if (p.check(TokenType::NEWLINE))
                p.openBody();

            ++p.ctx.blockDepth;
            while (true)
            {
                p.skipBodyBreaks();
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

            if (FunctionParser::atFunctionStart(p))
            {
                FunctionParser::Owner owner;
                owner.isMethod = true;
                owner.isPrivate = isPrivate;
                owner.classHasParent = !decl.parentName.empty();
                decl.methods.push_back(FunctionParser::parse(p, owner));
            }
            else if (DeclarationParser::atDeclaration(p))
            {
                decl.fields.push_back(parseField(p, isPrivate));
            }
            else
            {
                p.failUnexpected("a class body can only contain fields (TYPE NAME = value) and methods ('func')");
            }

            if (!p.atStatementEnd())
                p.failUnexpected("expected the end of the line after a class member");
        }

        // `[constant] TYPE NAME = expression`; the default is computed for every new object, and cannot use self.
        static FieldDecl parseField(ParserCore &p, bool isPrivate)
        {
            FieldDecl field;
            field.loc = p.current().location;
            field.isPrivate = isPrivate;
            if (p.match(TokenType::CONSTANT))
                field.isConstant = true;
            field.type = DeclarationParser::parseDeclaredType(p);
            const Token nameTok = p.consumeName("expected a field name after the type");
            field.name = nameTok.value;
            field.nameId = internName(field.name);
            p.consume(TokenType::ASSIGN, "expected '=' and a default value in a field declaration");
            ContextGuard guard(p);
            p.ctx.inMethod = false;
            p.ctx.hasParent = false;
            p.ctx.withDepth = 0;
            field.defaultValue = ExpressionParser::parse(p);
            return field;
        }
    };

    // ---- if and with ----------------------------------------------------------------------------------------------

    class ControlFlowParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::IF, "expected 'if'");

            auto condition = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "expected 'do' after if condition");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            IfStmt ifStmt;
            ifStmt.loc = loc;

            ++p.ctx.blockDepth;

            IfStmt::Branch firstBranch;
            firstBranch.condition = std::move(condition);
            firstBranch.body = BodyParser::parse(p);
            ifStmt.branches.push_back(std::move(firstBranch));

            while (p.check(TokenType::ELSE))
            {
                p.advance();

                if (p.check(TokenType::IF))
                {
                    p.advance();
                    auto elseCond = ExpressionParser::parse(p);
                    p.consume(TokenType::DO, "expected 'do' after else-if condition");
                    p.consume(TokenType::COLON, "expected ':' after 'do'");

                    IfStmt::Branch elseIfBranch;
                    elseIfBranch.condition = std::move(elseCond);
                    elseIfBranch.body = BodyParser::parse(p);
                    ifStmt.branches.push_back(std::move(elseIfBranch));
                }
                else
                {
                    p.consume(TokenType::DO, "expected 'do' after else");
                    p.consume(TokenType::COLON, "expected ':' after 'do'");

                    IfStmt::Branch elseBranch;
                    elseBranch.condition = nullptr;
                    elseBranch.body = BodyParser::parse(p);
                    ifStmt.branches.push_back(std::move(elseBranch));
                    break;
                }
            }

            --p.ctx.blockDepth;
            closeBlock(p, "if statement", loc);

            return std::make_unique<Stmt>(StmtKind::IfStmt, std::move(ifStmt));
        }

        // `with expression do: body end`
        static std::unique_ptr<Stmt> parseWith(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::WITH, "expected 'with'");

            WithStmt with;
            with.loc = loc;
            with.object = ExpressionParser::parse(p);

            p.consume(TokenType::DO, "expected 'do' after the object of 'with'");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            ++p.ctx.withDepth;
            with.body = BodyParser::parse(p);
            --p.ctx.withDepth;
            --p.ctx.blockDepth;

            closeBlock(p, "with block", loc);
            return std::make_unique<Stmt>(StmtKind::WithStmt, std::move(with));
        }
    };

    // ---- loops ----------------------------------------------------------------------------------------------------

    class LoopParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::LOOP, "expected 'loop'");

            LoopStmt loop;
            loop.loc = loc;
            NameScope names(p.declared);

            if (p.match(TokenType::REPEAT))
            {
                loop.kind = LoopStmt::LoopKind::Repeat;
                parseVariable(p, loop);

                p.consume(TokenType::ASSIGN, "expected '=' and the first number of the range after the loop variable");

                loop.repeatStart = ExpressionParser::parse(p);
                p.consume(TokenType::TILDE, "expected '~' for repeat range");
                loop.repeatEnd = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for repeat loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else if (p.match(TokenType::WHILE))
            {
                loop.kind = LoopStmt::LoopKind::While;
                loop.condition = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for while loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else if (p.match(TokenType::EACH))
            {
                loop.kind = LoopStmt::LoopKind::Each;
                parseVariable(p, loop);

                p.consume(TokenType::IN, "expected 'in' in each loop");
                loop.collection = ExpressionParser::parse(p);

                p.consume(TokenType::DO, "expected 'do' for each loop");
                p.consume(TokenType::COLON, "expected ':' after 'do'");
            }
            else
            {
                p.failUnexpected("expected 'repeat', 'while' or 'each' after 'loop'");
            }

            ++p.ctx.blockDepth;
            loop.body = BodyParser::parse(p);
            --p.ctx.blockDepth;

            closeBlock(p, "loop", loc);

            return std::make_unique<Stmt>(StmtKind::LoopStmt, std::move(loop));
        }

    private:
        // `TYPE NAME`: the loop variable is declared by the loop, in the same words as any other variable.
        static void parseVariable(ParserCore &p, LoopStmt &loop)
        {
            const Token typeTok = p.current();
            loop.typeLoc = typeTok.location;
            loop.varType = DeclarationParser::parseDeclaredType(p);
            DeclarationParser::requireName(p, typeTok, loop.kind == LoopStmt::LoopKind::Repeat ? "loop repeat number i = 1 ~ 3 do:" : "loop each str item in items do:");
            const Token var = p.consumeName("expected the name of the loop variable after its type");
            p.declareName(var);
            loop.repeatVar = var.value;
            loop.repeatVarId = internName(loop.repeatVar);
        }
    };

    // ---- add and remove -------------------------------------------------------------------------------------------

    class CollectionOpParser
    {
    public:
        // `add expression to place`
        static std::unique_ptr<Stmt> parseAdd(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::ADD, "expected 'add'");

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Add;
            stmt.value = ExpressionParser::parse(p);
            p.consume(TokenType::TO, "expected 'to' in 'add' statement");
            stmt.target = PlaceParser::parse(p, "add to", "'add'");
            stmt.loc = loc;
            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }

        // `remove expression from place`, or `remove at position from place`
        static std::unique_ptr<Stmt> parseRemove(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::REMOVE, "expected 'remove'");

            CollectionOpStmt stmt;
            stmt.opKind = CollectionOpStmt::OpKind::Remove;
            stmt.atPosition = p.match(TokenType::AT);
            stmt.value = ExpressionParser::parse(p);
            p.consume(TokenType::FROM, "expected 'from' in 'remove' statement");
            stmt.target = PlaceParser::parse(p, "remove from", "'remove'");
            stmt.loc = loc;
            return std::make_unique<Stmt>(StmtKind::CollectionOp, std::move(stmt));
        }
    };

    // ---- use ------------------------------------------------------------------------------------------------------

    class ImportParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::USE, "expected 'use'");

            if (isWordLikeToken(p.current()) && p.current().value == "DLC" && p.peek(1).is(TokenType::COLON))
            {
                UseStmt use;
                use.isDLC = true;
                use.loc = loc;
                while (true)
                {
                    SourceLocation nameLoc = p.current().location;
                    p.advance();
                    p.consume(TokenType::COLON, "expected ':' after DLC");

                    if (!isWordLikeToken(p.current()))
                        throw SyntaxError("expected library name after 'DLC:'", p.current().location);
                    use.dlcs.push_back({p.current().value, nameLoc});
                    p.advance();

                    const SourceLocation commaLoc = p.current().location;
                    if (!p.match(TokenType::COMMA))
                        break;
                    if (!(isWordLikeToken(p.current()) && p.current().value == "DLC" && p.peek(1).is(TokenType::COLON)))
                        throw SyntaxError("expected 'DLC:name' after ',' in 'use' (every library in the list needs its own 'DLC:' prefix)",
                                          commaLoc);
                }

                // Catches a forgotten comma; otherwise the second `DLC` would parse as a new statement.
                if (isWordLikeToken(p.current()) && p.current().value == "DLC" && p.peek(1).is(TokenType::COLON))
                    throw SyntaxError("expected ',' between libraries: use DLC:name, DLC:name", p.current().location);

                return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
            }

            std::string moduleName;
            if (isWordLikeToken(p.current()))
            {
                moduleName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("expected module name after 'use'", p.current().location);
            }

            p.consume(TokenType::FROM, "expected 'from' in custom import");

            std::string path;
            while (!p.check(TokenType::NEWLINE) && !p.check(TokenType::EOF_TOKEN) && !p.check(TokenType::END) && !p.check(TokenType::DEDENT))
            {
                const Token &t = p.current();
                if (t.is(TokenType::DOT))
                {
                    path += ".";
                }
                else if (t.is(TokenType::SLASH))
                {
                    path += "/";
                }
                else if (t.is(TokenType::MINUS))
                {
                    path += "-";
                }
                else if (t.is(TokenType::NUMBER))
                {
                    path += t.value;
                }
                else if (isWordLikeToken(t))
                {
                    path += t.value;
                }
                else
                {
                    break;
                }
                p.advance();
            }

            if (path.empty())
            {
                throw SyntaxError("expected path after 'from'", loc);
            }

            UseStmt use;
            use.isDLC = false;
            use.name = moduleName;
            use.path = path;
            use.loc = loc;

            return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
        }
    };

    // ---- or_else --------------------------------------------------------------------------------------------------

    // `statement or_else [as TYPE NAME] do: fallback end`. `as` is a keyword only here, right after `or_else`.
    class OrElseParser
    {
    public:
        static std::unique_ptr<Stmt> wrap(ParserCore &p, std::unique_ptr<Stmt> primaryStmt)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::OR_ELSE, "Expected 'or_else'");

            OrElseStmt orElse;
            NameScope names(p.declared);
            if (p.check(TokenType::IDENTIFIER) && p.current().value == "as")
            {
                p.advance();
                const Token typeTok = p.current();
                orElse.typeLoc = typeTok.location;
                orElse.errorType = DeclarationParser::parseDeclaredType(p);
                DeclarationParser::requireName(p, typeTok, "or_else as Error e do:");
                const Token name = p.consumeName("expected a name for the error after its type in 'or_else as'");
                p.declareName(name);
                orElse.errorName = name.value;
                orElse.errorNameId = internName(orElse.errorName);
            }

            p.consume(TokenType::DO, "expected 'do' after 'or_else'");
            p.consume(TokenType::COLON, "expected ':' after 'do'");

            ++p.ctx.blockDepth;
            orElse.fallbackBody = BodyParser::parse(p);
            --p.ctx.blockDepth;

            closeBlock(p, "or_else block", loc);

            orElse.primaryStmt = std::move(primaryStmt);
            orElse.loc = loc;

            return std::make_unique<Stmt>(StmtKind::OrElse, std::move(orElse));
        }
    };

    // ---- statements -----------------------------------------------------------------------------------------------

    class StatementParser
    {
    public:
        // One statement, including a trailing `or_else`. A statement ends at the end of its line, or where the
        // enclosing one-line construct ends (`end`, `else`, `or_else`); anything else after it is an error.
        static std::unique_ptr<Stmt> parseStatement(ParserCore &p)
        {
            p.skipNewlines();
            if (p.atEnd())
                return nullptr;

            const Token &tok = p.current();
            ParseDepthScope depth(tok.location);
            std::unique_ptr<Stmt> stmt = parseOne(p, tok);

            if (stmt && p.check(TokenType::OR_ELSE))
                stmt = OrElseParser::wrap(p, std::move(stmt));

            if (!p.atStatementEnd())
                p.failUnexpected("expected the end of the line after the statement");

            return stmt;
        }

    private:
        static std::unique_ptr<Stmt> simple(StmtKind kind, const SourceLocation &loc)
        {
            if (kind == StmtKind::StopStmt)
                return std::make_unique<Stmt>(kind, StopStmt(loc));
            return std::make_unique<Stmt>(kind, SkipStmt(loc));
        }

        static std::unique_ptr<Stmt> parseOne(ParserCore &p, const Token &tok)
        {
            switch (tok.type)
            {
            case TokenType::CONSTANT:
                return DeclarationParser::parseDeclaration(p);

            case TokenType::GLOBAL:
                if (isNameToken(p.peek(1)))
                    return DeclarationParser::parseGlobal(p);
                break;

            case TokenType::FUNCTION:
            case TokenType::ASYNC:
            case TokenType::PURE:
                return FunctionParser::parse(p);

            case TokenType::CLASS:
                return ClassParser::parse(p);

            case TokenType::PRIVATE:
                throw SyntaxError(ErrorCode::UnexpectedToken, "'private' can only be used on a member of a class", tok.location,
                                  "put the field or method inside a class body");

            case TokenType::IF:
                return ControlFlowParser::parse(p);

            case TokenType::WITH:
                return ControlFlowParser::parseWith(p);

            case TokenType::LOOP:
                return LoopParser::parse(p);

            case TokenType::STOP:
                p.advance();
                return simple(StmtKind::StopStmt, tok.location);

            case TokenType::SKIP:
                p.advance();
                return simple(StmtKind::SkipStmt, tok.location);

            case TokenType::RETURN:
            {
                const SourceLocation loc = tok.location;
                p.advance();
                if (p.atStatementEnd())
                    return std::make_unique<Stmt>(StmtKind::ReturnStmt, ReturnStmt(nullptr, loc));
                auto val = ExpressionParser::parse(p);
                return std::make_unique<Stmt>(StmtKind::ReturnStmt, ReturnStmt(std::move(val), loc));
            }

            case TokenType::FAIL:
            {
                const SourceLocation loc = tok.location;
                p.advance();
                if (p.atStatementEnd())
                    p.failUnexpected("expected an error after 'fail': a message or an Error");
                auto val = ExpressionParser::parse(p);
                return std::make_unique<Stmt>(StmtKind::FailStmt, FailStmt(std::move(val), loc));
            }

            case TokenType::AWAIT:
            {
                const SourceLocation awaitLoc = tok.location;
                p.advance();
                auto awaitExpr = ExpressionParser::makeAwait(p, awaitLoc);
                return std::make_unique<Stmt>(StmtKind::AwaitStmt, AwaitStmt(std::move(awaitExpr), awaitLoc));
            }

            case TokenType::USE:
                return ImportParser::parse(p);

            case TokenType::ADD:
                return CollectionOpParser::parseAdd(p);

            case TokenType::REMOVE:
                return CollectionOpParser::parseRemove(p);

            default:
                break;
            }

            DeclarationParser::rejectOldForms(p);
            if (DeclarationParser::atDeclaration(p))
                return DeclarationParser::parseDeclaration(p);
            if (DeclarationParser::atAssignment(p))
                return DeclarationParser::parseAssign(p);

            auto expr = ExpressionParser::parse(p);
            return std::make_unique<Stmt>(StmtKind::ExprStmt, ExprStmt(std::move(expr), tok.location));
        }
    };

    inline std::vector<std::unique_ptr<Stmt>> BodyParser::parseLines(ParserCore &p)
    {
        std::vector<std::unique_ptr<Stmt>> body;

        while (!p.atEnd())
        {
            p.skipBodyBreaks();
            if (p.atEnd())
                break;

            if (p.check(TokenType::END) || p.check(TokenType::ELSE))
                break;

            auto stmt = StatementParser::parseStatement(p);
            if (stmt)
                body.push_back(std::move(stmt));

            p.skipBodyBreaks();
        }

        return body;
    }

    inline std::vector<std::unique_ptr<Stmt>> BodyParser::parse(ParserCore &p)
    {
        NameScope names(p.declared);
        if (p.check(TokenType::NEWLINE))
        {
            p.openBody();
            return parseLines(p);
        }

        // A body on its header's line is one statement, or nothing: `func later() do: end`.
        std::vector<std::unique_ptr<Stmt>> body;
        if (p.check(TokenType::END) || p.check(TokenType::ELSE))
            return body;
        auto stmt = StatementParser::parseStatement(p);
        if (stmt)
            body.push_back(std::move(stmt));
        return body;
    }

    // ---- a program ------------------------------------------------------------------------------------------------

    // What texts that an engine ran before this one declared at their top level and still has: an engine runs its
    // texts as one program, so a later text may use these names but not declare them again.
    struct EarlierNames
    {
        std::vector<uint32_t> variables;
        std::vector<uint32_t> functions;
    };

    class Parser
    {
    public:
        // `source` is the text the tokens were scanned from; it lets an error found later, while a function runs,
        // be drawn against the right text (see SourceText.h).
        explicit Parser(std::vector<Token> tokens, SourceRef source = nullptr, const EarlierNames *earlier = nullptr)
            : core_(std::move(tokens), std::move(source))
        {
            if (earlier)
            {
                core_.declared = earlier->variables;
                core_.earlierDeclared = core_.declared.size();
                earlierFunctions_.insert(earlier->functions.begin(), earlier->functions.end());
            }
        }

        std::unique_ptr<Program> parse()
        {
            ParseStackFloorScope stackGuard;
            auto program = std::make_unique<Program>();
            program->source = core_.source;

            while (!core_.atEnd())
            {
                core_.skipNewlines();
                if (core_.atEnd())
                    break;

                auto stmt = StatementParser::parseStatement(core_);
                if (stmt)
                {
                    if (stmt->kind == StmtKind::FunctionDecl)
                        declareFunction(*std::get_if<FunctionDecl>(&stmt->data));
                    program->statements.push_back(std::move(stmt));
                }

                core_.skipNewlines();
            }

            program->declaredNames.assign(core_.declared.begin() + static_cast<std::ptrdiff_t>(core_.earlierDeclared), core_.declared.end());
            return program;
        }

        ParserCore &core() { return core_; }

    private:
        // The functions at the top level of one text are known before the text runs, so two of one name cannot both
        // be meant; nor can one that an earlier text of the program declared.
        void declareFunction(const FunctionDecl &decl)
        {
            if (earlierFunctions_.count(decl.nameId))
                throw SyntaxError(ErrorCode::DuplicateDeclaration, "function '" + decl.name + "' is already declared", decl.loc,
                                  "give the new function another name");
            if (!functions_.insert(decl.nameId).second)
                throw SyntaxError(ErrorCode::DuplicateDeclaration, "function '" + decl.name + "' is already declared in this file",
                                  decl.loc, "give each function its own name, or remove one of the two");
        }

        ParserCore core_;
        std::unordered_set<uint32_t> functions_;
        std::unordered_set<uint32_t> earlierFunctions_;
    };

    // ---- source text to a program ---------------------------------------------------------------------------------

    // Source text to Program: the one place the front-end stages are chained (tokenize, lex, parse). The
    // engine entry points, the interactive shell and the module loader all go through it, so a change to the
    // pipeline lands everywhere at once. `rawTokens` / `lexedTokens`, when given, receive the token streams
    // for the --ast dump.
    inline std::unique_ptr<Program> parseProgram(const SourceRef &source, std::vector<Token> *rawTokens = nullptr,
                                                 std::vector<Token> *lexedTokens = nullptr, const EarlierNames *earlier = nullptr)
    {
        if (source->text.size() > limits::kMaxSourceBytes)
        {
            throw SyntaxError(ErrorCode::SourceTooLarge,
                              "source is larger than the limit of " + limits::formatBytes(limits::kMaxSourceBytes),
                              SourceLocation());
        }
        Tokenizer tokenizer(source->text);
        std::vector<Token> raw = tokenizer.tokenize();
        if (rawTokens)
            *rawTokens = raw;
        Lexer lexer(std::move(raw));
        std::vector<Token> lexed = lexer.lex();
        if (lexedTokens)
            *lexedTokens = lexed;
        Parser parser(std::move(lexed), source, earlier);
        return parser.parse();
    }

}
