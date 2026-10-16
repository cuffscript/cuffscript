#pragma once

#include "ParseProgram.h"
#include "common/ErrorFormat.h"
#include "interpreter/Interpreter.h"
#include <memory>
#include <string>
#include <vector>

namespace cuff
{

    // The options that configure a run. The interpreter is configured by exactly this, so a new option is
    // added in one place (Interpreter::Config) and every front end can use it.
    using EngineOptions = Interpreter::Config;

    // A program that runs in pieces: each run() parses and executes one piece of source against the same
    // interpreter, so what an earlier piece declared (variables, functions, loaded modules) is still there
    // for the next. The interactive shell is a long-lived Session; a one-shot run is a Session used once
    // (CuffEngine::execute).
    //
    // Everything a front end needs from a failed run is in Outcome::error, already in the engine's report
    // format (see common/ErrorFormat.h), so a front end prints it and never builds an error message of its
    // own. That is what keeps the shell and `cuffc` in step when the report changes.
    class Session
    {
    public:
        struct Outcome
        {
            bool success = true;
            std::string error;
        };

        explicit Session(EngineOptions options = EngineOptions())
            : options_(std::move(options)), interp_(std::make_unique<Interpreter>(options_)) {}

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        // Parses and runs `source`. `scriptDir` is the directory `use ... from` resolves against. Never throws.
        Outcome run(const std::string &source, const std::string &scriptDir = ".")
        {
            return run(makeSourceRef(source), scriptDir);
        }

        // As above, handing the text over instead of copying it (a program can be up to 16 MiB).
        Outcome run(std::string &&source, const std::string &scriptDir = ".")
        {
            return run(makeSourceRef(std::move(source)), scriptDir);
        }

        // Runs text that is already shared, e.g. an entry the caller also keeps for its own use.
        Outcome run(const SourceRef &text, const std::string &scriptDir)
        {
            Outcome outcome;
            try
            {
                programs_.push_back(parseProgram(text));
                interp_->run(*programs_.back(), scriptDir);
            }
            catch (...)
            {
                outcome.success = false;
                outcome.error = renderCurrentException(text);
            }
            return outcome;
        }

        // Forgets everything the session declared and starts again with the same options.
        void reset()
        {
            interp_.reset();
            programs_.clear();
            interp_ = std::make_unique<Interpreter>(options_);
        }

        const EngineOptions &options() const { return options_; }

    private:
        EngineOptions options_;
        // Every program that was run stays alive: the interpreter keeps raw pointers to the functions inside
        // them, at any nesting depth, for as long as the session lasts. Declared before interp_ so it is
        // destroyed after it.
        std::vector<std::unique_ptr<Program>> programs_;
        std::unique_ptr<Interpreter> interp_;
    };

}
