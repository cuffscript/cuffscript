#pragma once

#include "../common/Token.h"
#include "../common/TokenTypes.h"
#include "../common/Attributes.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
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
        if (t.is(TokenType::STRING) || t.is(TokenType::FSTRING) || t.is(TokenType::NUMBER))
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

    class ParserCore
    {
    public:
        std::vector<Token> tokens;
        size_t pos = 0;

        explicit ParserCore(std::vector<Token> t) : tokens(std::move(t)) {}

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

}
