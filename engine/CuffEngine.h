#pragma once

#include "common/CuffError.h"
#include "common/Token.h"
#include "tokenizer/Tokenizer.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "parser/ASTNodes.h"
#include "debug/ASTPrinter.h"
#include "interpreter/Interpreter.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>

namespace cuff
{

    // CuffScript engine entry point.
    // Runs the full pipeline: source -> tokenize -> lex -> parse -> AST -> (optionally) execute
    class CuffEngine
    {
    public:
        struct Result
        {
            bool success = false;
            std::string error;
            std::vector<Token> rawTokens;
            std::vector<Token> lexedTokens;
            std::unique_ptr<Program> ast;
        };

        static Result run(const std::string &source)
        {
            Result result;

            try
            {
                // Stage 1: Tokenize
                Tokenizer tokenizer(source);
                result.rawTokens = tokenizer.tokenize();

                // Stage 2: Lex (classify + validate)
                Lexer lexer(result.rawTokens);
                result.lexedTokens = lexer.lex();

                // Stage 3: Parse
                Parser parser(result.lexedTokens);
                result.ast = parser.parse();

                result.success = true;
            }
            catch (const CuffError &e)
            {
                result.error = e.what();
            }
            catch (const std::exception &e)
            {
                result.error = std::string("Internal error: ") + e.what();
            }

            return result;
        }

        // Parses AND executes `source`. `scriptDir` resolves relative
        // `use ... from ...` module paths. Returns exit-code-style success;
        // on failure, an error message (already formatted by the systematic
        // CuffError hierarchy — see engine/common/CuffError.h) is in `error`.
        static Result execute(const std::string &source, const std::string &scriptDir)
        {
            Result result = run(source);
            if (!result.success)
                return result;

            try
            {
                Interpreter interp;
                interp.run(*result.ast, scriptDir);
            }
            catch (const CuffError &e)
            {
                result.success = false;
                result.error = e.what();
            }
            catch (const std::exception &e)
            {
                result.success = false;
                result.error = std::string("Internal error: ") + e.what();
            }

            return result;
        }

        static void debugDump(const Result &result)
        {
            if (!result.success)
            {
                std::cerr << "ERROR: " << result.error << "\n\n";
                return;
            }

            std::cout << "===== TOKENIZER OUTPUT =====\n";
            std::cout << ASTPrinter::printTokens(result.rawTokens);

            std::cout << "\n===== LEXER OUTPUT =====\n";
            std::cout << ASTPrinter::printTokens(result.lexedTokens);

            std::cout << "\n===== AST =====\n";
            if (result.ast)
            {
                std::cout << ASTPrinter::print(*result.ast);
            }
            std::cout << "\n===== PARSE SUCCESS =====\n";
        }
    };

} // namespace cuff

