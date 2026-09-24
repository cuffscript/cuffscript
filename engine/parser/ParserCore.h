#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include "../common/Attributes.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "ASTNodes.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include <string>

namespace cuff
{

    // Lowest real stack address the parser's own recursion may reach; 0 =
    // no floor (unknown/not yet primed). Kept separate from the
    // interpreter's stackFloor() — they guard different, non-overlapping
    // phases (see ParseStackFloorScope) and must never be confused.
    inline uintptr_t &parseStackFloor()
    {
        static thread_local uintptr_t floor = 0;
        return floor;
    }

    // Primes parseStackFloor() from the real stack available *right now* —
    // called once at the top of Parser::parse(), so a module parsed deep
    // inside a running script's own recursion (via `use ... from`) gets a
    // floor based on however much stack is actually left at that point, not
    // a stale budget computed back when the top-level script started.
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

    // Counts native recursion depth across every parser (including the nested
    // parsers used for f-string expressions) so hostile nesting fails with a
    // syntax error instead of exhausting the C++ stack. Backed by two
    // independent checks: a level counter (kMaxParseDepth) and, since a
    // single level can cost several KB of real stack, a stack-pointer floor
    // primed by ParseStackFloorScope — either one alone can miss a case the
    // other catches.
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

    // Core parser state — token cursor + utility helpers.
    // Shared by all sub-parsers.
    class ParserCore
    {
    public:
        std::vector<Token> tokens;
        size_t pos = 0;

        explicit ParserCore(std::vector<Token> t) : tokens(std::move(t)) {}

        // ---- Token cursor helpers ----

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

        Token consume(TokenType t, const std::string &errMsg)
        {
            if (!check(t))
            {
                throw SyntaxError(errMsg + " (got '" + current().value + "')", current().location);
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

        // Skip NEWLINE tokens (and INDENT/DEDENT for structural flexibility)
        void skipNewlines()
        {
            while (check(TokenType::NEWLINE) || check(TokenType::INDENT) || check(TokenType::DEDENT))
            {
                advance();
            }
        }

        // Skip only NEWLINE tokens
        void skipNewlinesOnly()
        {
            while (check(TokenType::NEWLINE))
                advance();
        }

        // Check if the next non-trivial token (skipping NEWLINE/INDENT/DEDENT) is a terminator
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

        // Get current indentation level (from the most recent INDENT/DEDENT or current token)
        int currentIndent() const
        {
            // Walk backwards to find the last INDENT/DEDENT
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

} // namespace cuff
