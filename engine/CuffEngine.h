#pragma once

#include "ParseProgram.h"
#include "Session.h"
#include "common/CuffError.h"
#include "common/ErrorFormat.h"
#include "common/Limits.h"
#include "common/Token.h"
#include "debug/ASTPrinter.h"
#include "parser/ASTNodes.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace cuff
{

    // The embedding API: run a whole program in one call. For a program that arrives in pieces (a shell), keep
    // a Session instead; this class is a thin convenience over the same machinery.
    class CuffEngine
    {
    public:
        struct Result
        {
            bool success = false;
            std::string error; // the full report (see common/ErrorFormat.h), empty on success
            std::vector<Token> rawTokens;
            std::vector<Token> lexedTokens;
            std::unique_ptr<Program> ast; // set by run() only
        };

        // The settings of a run: root directory for modules, step and time budgets, stack budget, and which
        // DLCs may touch the network or file system. See Interpreter::Config for each field.
        using Options = EngineOptions;

        // Parses without running. `keepTokens` keeps the raw/lexed token streams in the result (only --ast needs
        // them); otherwise they are discarded.
        static Result run(const std::string &source, bool keepTokens = true)
        {
            Result result;
            const SourceRef text = makeSourceRef(source);
            try
            {
                result.ast = keepTokens ? parseProgram(text, &result.rawTokens, &result.lexedTokens) : parseProgram(text);
                result.success = true;
            }
            catch (...)
            {
                result.error = renderCurrentException(text);
            }
            return result;
        }

        static Result execute(const std::string &source, const std::string &scriptDir)
        {
            return execute(source, scriptDir, Options());
        }

        // Parses and runs `source`. `scriptDir` is the directory `use ... from` resolves against.
        static Result execute(const std::string &source, const std::string &scriptDir, const Options &options)
        {
            Session session(options);
            return toResult(session.run(source, scriptDir));
        }

        // The same, taking the program text instead of copying it.
        static Result execute(std::string &&source, const std::string &scriptDir, const Options &options)
        {
            Session session(options);
            return toResult(session.run(std::move(source), scriptDir));
        }

    private:
        static Result toResult(Session::Outcome &&outcome)
        {
            Result result;
            result.success = outcome.success;
            result.error = std::move(outcome.error);
            return result;
        }

    public:
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

}
