#pragma once

#include "common/CuffError.h"
#include "common/Token.h"
#include "tokenizer/Tokenizer.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "parser/ASTNodes.h"
#include "debug/ASTPrinter.h"
#include "interpreter/Interpreter.h"
#include "common/Limits.h"
#include <cstdint>
#include <new>
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

        // Sandbox and resource settings for execute(). All limits default to
        // "off"; the structural limits in common/Limits.h always apply.
        struct Options
        {
            std::string rootDir;    // modules must stay inside this directory (default: the script's directory)
            uint64_t maxSteps = 0;  // loop iterations + user-function calls; 0 = unlimited
            uint32_t timeoutMs = 0; // wall-clock budget; 0 = unlimited
            size_t stackBudgetBytes = 0; // native stack the evaluator may use; 0 = derive from the real stack size
            bool networkEnabled = true;  // 'use DLC:network' works at all; false suits multi-tenant/untrusted hosting
            bool allowPrivateNetworkTargets = false; // let DLC:network reach loopback/private/link-local addresses (see SECURITY.md)
        };

        // `keepTokens` retains the raw/lexed token streams in the result (only
        // the --ast debug dump needs them); without it the tokens are moved
        // through the pipeline instead of copied.
        static Result run(const std::string &source, bool keepTokens = true)
        {
            Result result;

            try
            {
                if (source.size() > limits::kMaxSourceBytes)
                {
                    throw SyntaxError(ErrorCode::SourceTooLarge,
                                      "source is larger than the " + std::to_string(limits::kMaxSourceBytes / 1024) + " KiB limit",
                                      SourceLocation());
                }

                // Stage 1: Tokenize
                Tokenizer tokenizer(source);
                std::vector<Token> raw = tokenizer.tokenize();

                // Stage 2: Lex (classify + validate)
                std::vector<Token> lexed;
                if (keepTokens)
                {
                    result.rawTokens = raw;
                    Lexer lexer(std::move(raw));
                    lexed = lexer.lex();
                    result.lexedTokens = lexed;
                }
                else
                {
                    Lexer lexer(std::move(raw));
                    lexed = lexer.lex();
                }

                // Stage 3: Parse
                Parser parser(std::move(lexed));
                result.ast = parser.parse();

                result.success = true;
            }
            catch (const CuffError &e)
            {
                result.error = e.what();
            }
            catch (const std::bad_alloc &)
            {
                result.error = outOfMemoryMessage();
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
            return execute(source, scriptDir, Options());
        }

        static Result execute(const std::string &source, const std::string &scriptDir, const Options &options)
        {
            Result result = run(source, false);
            if (!result.success)
                return result;

            try
            {
                Interpreter::Config config;
                config.rootDir = options.rootDir;
                config.maxSteps = options.maxSteps;
                config.timeoutMs = options.timeoutMs;
                config.stackBudgetBytes = options.stackBudgetBytes;
                config.networkEnabled = options.networkEnabled;
                config.allowPrivateNetworkTargets = options.allowPrivateNetworkTargets;
                Interpreter interp(std::move(config));
                interp.run(*result.ast, scriptDir);
            }
            catch (const CuffError &e)
            {
                result.success = false;
                result.error = e.what();
            }
            catch (const std::bad_alloc &)
            {
                result.success = false;
                result.error = outOfMemoryMessage();
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

    private:
        static std::string outOfMemoryMessage()
        {
            return "[" + errorCodeTag(ErrorCode::OutOfMemory) + "] " + errorCategoryName(ErrorCode::OutOfMemory) +
                   ": the program ran out of memory";
        }
    };

} // namespace cuff

