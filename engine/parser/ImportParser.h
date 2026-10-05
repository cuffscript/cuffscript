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

}
