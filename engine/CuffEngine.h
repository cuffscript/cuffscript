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

    inline std::string buildCaretSnippet(const std::string &source, const SourceLocation &loc)
    {
        if (source.empty() || loc.offset < 0)
            return "";
        size_t offset = std::min(static_cast<size_t>(loc.offset), source.size());
        size_t lineStart = 0;
        if (offset > 0)
        {
            size_t prevNewline = source.rfind('\n', offset - 1);
            if (prevNewline != std::string::npos)
                lineStart = prevNewline + 1;
        }
        size_t lineEnd = source.find('\n', offset);
        if (lineEnd == std::string::npos)
            lineEnd = source.size();
        if (lineStart > lineEnd)
            return "";
        std::string lineText = source.substr(lineStart, lineEnd - lineStart);
        if (!lineText.empty() && lineText.back() == '\r')
            lineText.pop_back();
        size_t prefixBytes = std::min(offset - lineStart, lineText.size());
        size_t caretCol = utf8::length(lineText.substr(0, prefixBytes));
        return "    " + lineText + "\n    " + std::string(caretCol, ' ') + "^";
    }

    inline std::string renderErrorWithSnippet(const CuffError &e, const std::string &source)
    {
        std::string out = "[" + errorCodeTag(e.code) + "] " + e.category +
                           " at " + e.location.toString() + ": " + e.message;
        std::string snippet = buildCaretSnippet(source, e.location);
        if (!snippet.empty())
            out += "\n" + snippet;
        if (!e.hint.empty())
            out += "\n    hint: " + e.hint;
        return out;
    }

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

        struct Options
        {
            std::string rootDir;  // modules must stay inside this directory (default: the script's directory)
            uint64_t maxSteps = 0;  // loop iterations + user-function calls; 0 = unlimited
            uint32_t timeoutMs = 0;  // wall-clock budget; 0 = unlimited
            size_t stackBudgetBytes = 0;  // native stack the evaluator may use; 0 = derive from the real stack size
            bool networkEnabled = true;  // 'use DLC:network' works at all; false suits multi-tenant/untrusted hosting
            bool allowPrivateNetworkTargets = false;  // let DLC:network reach loopback/private/link-local addresses (see SECURITY.md)
            bool filesystemEnabled = true;  // 'use DLC:filesystem' works at all; false suits multi-tenant/untrusted hosting
        };

        // `keepTokens` keeps the raw/lexed token streams in the result (only --ast needs them); otherwise they are
        // moved.
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

                Tokenizer tokenizer(source);
                std::vector<Token> raw = tokenizer.tokenize();

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

                Parser parser(std::move(lexed));
                result.ast = parser.parse();

                result.success = true;
            }
            catch (const CuffError &e)
            {
                result.error = renderErrorWithSnippet(e, source);
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
                config.filesystemEnabled = options.filesystemEnabled;
                Interpreter interp(std::move(config));
                interp.run(*result.ast, scriptDir);
            }
            catch (const CuffError &e)
            {
                result.success = false;
                result.error = renderErrorWithSnippet(e, source);
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

}

