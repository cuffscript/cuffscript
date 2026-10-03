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
    // isWordLikeToken() now lives in ParserCore.h, shared by every parser
    // that needs to accept a keyword-shaped name (declarations, references,
    // DLC/module names here).

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
            p.consume(TokenType::USE, "expected 'use'");

            // Pattern 1: use DLC:name  (DLC is a word, then COLON, then name)
            if (isWordLikeToken(p.current()) && p.current().value == "DLC" && p.peek(1).is(TokenType::COLON))
            {
                p.advance(); // DLC
                p.consume(TokenType::COLON, "expected ':' after DLC");

                std::string libName;
                if (isWordLikeToken(p.current()))
                {
                    libName = p.current().value;
                    p.advance();
                }
                else
                {
                    throw SyntaxError("expected library name after 'DLC:'", p.current().location);
                }

                UseStmt use;
                use.isDLC = true;
                use.name = libName;
                use.dlcNames.push_back(std::move(libName));
                while (p.match(TokenType::COMMA))
                {
                    if (!isWordLikeToken(p.current()) || p.current().value != "DLC" ||
                        !p.peek(1).is(TokenType::COLON))
                        throw SyntaxError("expected 'DLC:<name>' after ',' in DLC import", p.current().location);
                    p.advance();
                    p.advance();
                    if (!isWordLikeToken(p.current()))
                        throw SyntaxError("expected library name after 'DLC:'", p.current().location);
                    use.dlcNames.push_back(p.current().value);
                    p.advance();
                }
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
                throw SyntaxError("expected module name after 'use'", p.current().location);
            }

            p.consume(TokenType::FROM, "expected 'from' in custom import");

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
                else if (t.is(TokenType::BACKSLASH))
                {
                    path += "/";
                }
                else if (t.is(TokenType::COLON))
                {
                    path += ":";
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

} // namespace cuff
