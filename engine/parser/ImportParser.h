#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "ExpressionParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <string>
#include <cctype>

namespace cuff
{

    // True for tokens that carry a plain "word" as their raw text —
    // IDENTIFIER, plus any reserved keyword (which is lexically still just a
    // word; it only means something special elsewhere in the grammar).
    // Needed so a DLC/module name or path segment that happens to collide
    // with a keyword (`use DLC:list`, `use count from ./x`) still parses,
    // instead of confusingly rejecting a perfectly reasonable name. String
    // and number literals are excluded explicitly since their `.value` could
    // coincidentally look word-shaped (e.g. the string literal "list").
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

    // Parses import statements:
    //   use DLC:[name]          — built-in library load
    //   use [name] from [path]  — custom module load
    //
    // Enforces: must be a single line (no newlines within the statement).
    class ImportParser
    {
    public:
        static std::unique_ptr<Stmt> parse(ParserCore &p)
        {
            SourceLocation loc = p.current().location;
            p.consume(TokenType::USE, "Expected 'use'");

            // Pattern 1: use DLC:name  (DLC is a word, then COLON, then name)
            if (isWordLikeToken(p.current()) && p.current().value == "DLC" && p.peek(1).is(TokenType::COLON))
            {
                p.advance(); // DLC
                p.consume(TokenType::COLON, "Expected ':' after DLC");

                std::string libName;
                if (isWordLikeToken(p.current()))
                {
                    libName = p.current().value;
                    p.advance();
                }
                else
                {
                    throw SyntaxError("Expected library name after 'DLC:'", p.current().location);
                }

                UseStmt use;
                use.isDLC = true;
                use.name = libName;
                use.loc = loc;

                return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
            }

            // Pattern 2: use [name] from [path]
            std::string moduleName;
            if (isWordLikeToken(p.current()))
            {
                moduleName = p.current().value;
                p.advance();
            }
            else
            {
                throw SyntaxError("Expected module name after 'use'", p.current().location);
            }

            p.consume(TokenType::FROM, "Expected 'from' in custom import");

            // Path: reconstruct from tokens (./maps/core_engine -> DOT, WORD, SLASH, WORD, SLASH, WORD)
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
                throw SyntaxError("Expected path after 'from'", loc);
            }

            UseStmt use;
            use.isDLC = false;
            use.name = moduleName;
            use.path = path;
            use.loc = loc;

            return std::make_unique<Stmt>(StmtKind::UseStmt, std::move(use));
        }
    };

} // namespace cuff
