#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include "../common/Attributes.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/SourceText.h"
#include "ASTNodes.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>
#include <string>

namespace cuff
{

    inline bool isWordLikeToken(const Token &t)
    {
        if (t.is(TokenType::STRING) || t.is(TokenType::FSTRING) || t.is(TokenType::NUMBER) || t.is(TokenType::PATTERN))
            return false;
        if (t.value.empty())
            return false;
        if (!(std::isalpha(static_cast<unsigned char>(t.value[0])) || t.value[0] == '_'))
            return false;
        for (char c : t.value)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                return false;
        return true;
    }

    // A token that can be used as a name: a reserved word never can.
    inline bool isNameToken(const Token &t)
    {
        return t.is(TokenType::IDENTIFIER);
    }

    // Where the parser is in the program, as far as the rules of `self`, `super`, `.name` and `class` are concerned.
    // An f-string's expression is parsed by a parser of its own that starts from a copy of the outer context.
    struct ParseContext
    {
        int blockDepth = 0;     // bodies of if, loop, with, func, class and or_else that enclose the statement
        bool inMethod = false;  // `self` is available
        bool hasParent = false; // `super` is available
        int withDepth = 0;      // `with` blocks enclosing the statement within the same function
    };

    // How a token reads in an error message: "end of line" rather than a raw '\n'.
    inline std::string describeToken(const Token &t)
    {
        switch (t.type)
        {
        case TokenType::NEWLINE:
            return "end of line";
        case TokenType::EOF_TOKEN:
            return "end of file";
        case TokenType::INDENT:
        case TokenType::DEDENT:
            return "a change of indentation";
        case TokenType::PATTERN:
            return "a pattern literal";
        default:
            return "'" + t.value + "'";
        }
    }

    // Lowest stack address the parser's recursion may reach; 0 = no floor.
    inline uintptr_t &parseStackFloor()
    {
        static thread_local uintptr_t floor = 0;
        return floor;
    }

    struct ParseStackFloorScope
    {
        uintptr_t previous;
        ParseStackFloorScope()
        {
            previous = parseStackFloor();
            const uintptr_t sp = stackPointer();
            size_t avail = availableStackBytes();
            size_t budget = avail
                                ? (avail > limits::kParseStackMargin ? avail - limits::kParseStackMargin : avail / 2)
                                : limits::kParseStackBudget;
            budget = std::min(budget, limits::kParseStackBudget);
            parseStackFloor() = sp > budget ? sp - budget : 0;
#ifdef CUFF_DEBUG_STACK
            std::fprintf(stderr, "[parse-stack] sp=%zu avail=%zu budget=%zu floor=%zu\n",
                         (size_t)sp, avail, budget, (size_t)parseStackFloor());
#endif
        }
        ~ParseStackFloorScope() { parseStackFloor() = previous; }
    };

    // Counts native recursion across all parsers (f-string sub-parsers included) so hostile nesting fails cleanly.
    class ParseDepthScope
    {
    public:
        explicit ParseDepthScope(const SourceLocation &loc)
        {
            const uintptr_t floor = parseStackFloor();
            if (++depth() > limits::kMaxParseDepth || (floor != 0 && stackPointer() < floor))
            {
                --depth();
                throw SyntaxError(ErrorCode::NestingTooDeep,
                                  "code is nested too deeply (maximum nesting depth is " +
                                      std::to_string(limits::kMaxParseDepth) + ")",
                                  loc);
            }
        }
        ~ParseDepthScope() { --depth(); }
        ParseDepthScope(const ParseDepthScope &) = delete;
        ParseDepthScope &operator=(const ParseDepthScope &) = delete;

    private:
        static int &depth()
        {
            static thread_local int d = 0;
            return d;
        }
    };

    class ParserCore;

    // The names a block declares are forgotten when the block ends: a sibling block may use them again.
    struct NameScope
    {
        std::vector<uint32_t> &names;
        size_t mark;
        explicit NameScope(std::vector<uint32_t> &n) : names(n), mark(n.size()) {}
        ~NameScope() { names.resize(mark); }
        NameScope(const NameScope &) = delete;
        NameScope &operator=(const NameScope &) = delete;
    };

    // A function starts with no names declared: it cannot see the variables of the code around it.
    struct FunctionNames
    {
        std::vector<uint32_t> &names;
        std::vector<uint32_t> saved;
        explicit FunctionNames(std::vector<uint32_t> &n) : names(n), saved(std::move(n)) { names.clear(); }
        ~FunctionNames() { names = std::move(saved); }
        FunctionNames(const FunctionNames &) = delete;
        FunctionNames &operator=(const FunctionNames &) = delete;
    };

    // Restores the parse context when a construct that changes it is left (also when parsing it fails).
    struct ContextGuard
    {
        ParserCore &core;
        ParseContext saved;
        explicit ContextGuard(ParserCore &c);
        ~ContextGuard();
        ContextGuard(const ContextGuard &) = delete;
        ContextGuard &operator=(const ContextGuard &) = delete;
    };

    class ParserCore
    {
    public:
        std::vector<Token> tokens;
        size_t pos = 0;
        SourceRef source; // the text the tokens came from; stamped onto every function declared (see SourceText.h)
        ParseContext ctx;
        std::vector<uint32_t> declared; // the names that the blocks around the statement being parsed have declared

        explicit ParserCore(std::vector<Token> t, SourceRef src = nullptr) : tokens(std::move(t)), source(std::move(src)) {}

        const Token &current() const { return tokens[pos]; }

        const Token &peek(int ahead = 0) const
        {
            size_t idx = pos + ahead;
            if (idx >= tokens.size())
                return tokens.back();
            return tokens[idx];
        }

        bool atEnd() const
        {
            return current().is(TokenType::EOF_TOKEN);
        }

        bool check(TokenType t) const
        {
            return current().is(t);
        }

        bool checkAny(std::initializer_list<TokenType> types) const
        {
            for (TokenType t : types)
            {
                if (current().is(t))
                    return true;
            }
            return false;
        }

        const Token &advance()
        {
            if (!atEnd())
                ++pos;
            return tokens[pos - 1];
        }

        // A hint for a token that was once part of the language and now only stands where something else is
        // expected: the old spelling of an operation, with what to write instead. Empty when there is none.
        std::string hintForUnexpected() const
        {
            const Token &t = current();
            if (t.is(TokenType::IDENTIFIER) && t.value == "IS")
            {
                if (peek(1).is(TokenType::STRING))
                    return "use `matches p\"" + peek(1).value + "\"i` for a case-insensitive pattern test";
                return "use `matches p\"...\"i` for a case-insensitive pattern test";
            }
            if (t.is(TokenType::ASSIGN))
                return "`=` stores a value in a variable; to compare two values write `is`";
            const Token *before = pos > 0 ? &tokens[pos - 1] : nullptr;
            const bool afterWord = before && (before->is(TokenType::IDENTIFIER) || before->is(TokenType::FUNCTION));
            switch (t.type)
            {
            case TokenType::ADD:
            case TokenType::TO:
            case TokenType::IN:
            case TokenType::GLOBAL:
            case TokenType::NOT:
            case TokenType::FROM:
            case TokenType::AT:
                if (afterWord)
                    return "'" + t.value + "' is a reserved word, so it cannot be used as a name";
                break;
            case TokenType::IDENTIFIER:
                if (before && before->is(TokenType::IDENTIFIER) && !peek(1).is(TokenType::ASSIGN))
                    return "to declare '" + t.value + "' write `" + before->value + " " + t.value + " = value`";
                break;
            default:
                break;
            }
            return "";
        }

        [[noreturn]] void failUnexpected(const std::string &errMsg) const
        {
            throw SyntaxError(ErrorCode::UnexpectedToken, errMsg + " (got " + describeToken(current()) + ")",
                              current().location, hintForUnexpected());
        }

        Token consume(TokenType t, const std::string &errMsg)
        {
            if (!check(t))
                failUnexpected(errMsg);
            return advance();
        }

        // The closing 'end' of a block. When it is missing, say where the block was opened: the mistake is
        // rarely where the text runs out.
        Token consumeEnd(const std::string &errMsg, const SourceLocation &openedAt)
        {
            if (!check(TokenType::END))
            {
                std::string hint = hintForUnexpected();
                if (hint.empty())
                    hint = "the block opened at " + openedAt.toString() + " has no matching 'end'";
                throw SyntaxError(ErrorCode::UnexpectedToken, errMsg + " (got " + describeToken(current()) + ")",
                                  current().location, hint);
            }
            return advance();
        }

        bool match(TokenType t)
        {
            if (check(t))
            {
                advance();
                return true;
            }
            return false;
        }

        void skipNewlines()
        {
            while (check(TokenType::NEWLINE) || check(TokenType::INDENT) || check(TokenType::DEDENT))
            {
                advance();
            }
        }

        void skipNewlinesOnly()
        {
            while (check(TokenType::NEWLINE))
                advance();
        }

        bool peekTerminator(std::initializer_list<TokenType> terminators) const
        {
            size_t idx = pos;
            while (idx < tokens.size())
            {
                TokenType tt = tokens[idx].type;
                if (tt == TokenType::NEWLINE || tt == TokenType::INDENT || tt == TokenType::DEDENT)
                {
                    ++idx;
                    continue;
                }
                for (TokenType term : terminators)
                {
                    if (tt == term)
                        return true;
                }
                return false;
            }
            return false;
        }

        // The start of a block whose `do:` ends its line. The lines of the body are indented one level deeper than the
        // header; a body with nothing in it (`end`, `else` or the end of the text comes next) needs no indentation.
        void openBody()
        {
            skipNewlinesOnly();
            if (check(TokenType::INDENT))
            {
                advance();
                return;
            }
            if (peekTerminator({TokenType::END, TokenType::ELSE, TokenType::EOF_TOKEN}))
                return;
            size_t idx = pos;
            while (tokens[idx].is(TokenType::DEDENT))
                ++idx;
            throw SyntaxError(ErrorCode::UnexpectedToken, "expected the lines of the block to be indented (got " + describeToken(tokens[idx]) + ")",
                              tokens[idx].location, "indent the body one level deeper than the line that ends with 'do:'");
        }

        // Between the lines of a block body: line breaks, and the end of a deeper level. A line indented deeper than the
        // one before it, where no block was opened, does not belong to the body.
        void skipBodyBreaks()
        {
            while (check(TokenType::NEWLINE) || check(TokenType::DEDENT))
                advance();
            if (check(TokenType::INDENT))
                throw SyntaxError(ErrorCode::UnexpectedToken, "unexpected indentation", current().location,
                                  "every line of a block is indented the same, one level deeper than the line that ends with 'do:'");
        }

        // Whether the statement just parsed is complete: a statement ends at the end of its line, or where the
        // construct around a one-line form ends.
        bool atStatementEnd() const
        {
            switch (current().type)
            {
            case TokenType::NEWLINE:
            case TokenType::EOF_TOKEN:
            case TokenType::END:
            case TokenType::ELSE:
            case TokenType::DEDENT:
            case TokenType::OR_ELSE:
                return true;
            default:
                return false;
            }
        }

        // A name: a variable, parameter, member or class. `what` completes "expected ... name".
        Token consumeName(const std::string &errMsg)
        {
            if (!isNameToken(current()))
                failUnexpected(errMsg);
            return advance();
        }

        // A name given to a new variable. Within one function (or the top level of a program) a name is declared once,
        // wherever the block that declares it sits inside the ones around it.
        void declareName(const Token &name)
        {
            const uint32_t id = internName(name.value);
            if (std::find(declared.begin(), declared.end(), id) != declared.end())
                throw SyntaxError(ErrorCode::DuplicateDeclaration, "'" + name.value + "' is already declared here",
                                  name.location,
                                  "give it a new value with `" + name.value + " = ...`, or choose another name");
            declared.push_back(id);
        }

        // The name after a '.': any word, even one that is a keyword elsewhere (`text.repeat(3)`), because a dot leaves
        // no doubt about what the word is.
        Token consumeMemberName(const std::string &errMsg)
        {
            if (!isWordLikeToken(current()))
                failUnexpected(errMsg);
            return advance();
        }

        int currentIndent() const
        {
            for (int i = static_cast<int>(pos) - 1; i >= 0; --i)
            {
                if (tokens[i].is(TokenType::INDENT))
                    return tokens[i].indentLevel;
                if (tokens[i].is(TokenType::DEDENT))
                    return tokens[i].indentLevel;
            }
            return 0;
        }
    };

    inline ContextGuard::ContextGuard(ParserCore &c) : core(c), saved(c.ctx) {}
    inline ContextGuard::~ContextGuard() { core.ctx = saved; }

    inline void requireSelfContext(const ParserCore &p, const Token &tok)
    {
        if (!p.ctx.inMethod)
            throw SyntaxError(ErrorCode::UnexpectedToken, "'self' can only be used inside a method", tok.location,
                              "a method is a func declared in a class body");
    }

    inline void requireWithContext(const ParserCore &p, const Token &tok)
    {
        if (p.ctx.withDepth == 0)
            throw SyntaxError(ErrorCode::UnexpectedToken, "'.name' can only be used inside a 'with' block", tok.location,
                              "write object.name to reach a member of an object");
    }

}
