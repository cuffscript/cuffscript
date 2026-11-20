#pragma once

#include "common/CuffError.h"
#include "common/ErrorFormat.h"
#include "common/Limits.h"
#include "common/SourceText.h"
#include "common/Token.h"
#include "debug/ASTPrinter.h"
#include "dlc/DLCCommon.h"
#include "interpreter/Interpreter.h"
#include "parser/ASTNodes.h"
#include "parser/Parser.h"
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace cuff
{

    // The options that configure an engine. The interpreter is configured by exactly this, so a new option is added
    // in one place (Interpreter::Config) and every front end can use it.
    using EngineOptions = Interpreter::Config;

    // The arguments of a call to a function the host program added (Engine::addFunction). The checked readers raise
    // the errors a library function raises, naming the function and the argument, so a host function reports a
    // wrong call the way a built-in does.
    class Args
    {
    public:
        Args(const std::string &function, std::vector<Value> &values, const SourceLocation &loc)
            : function_(function), values_(values), loc_(loc) {}

        size_t size() const { return values_.size(); }
        const Value &operator[](size_t i) const { return values_[i]; }

        // The call must have exactly `n` arguments, or between `lo` and `hi` (E4-010).
        void expectCount(size_t n) const { expectArgCount(function_.c_str(), values_, n, loc_); }
        void expectCount(size_t lo, size_t hi) const { expectArgRange(function_.c_str(), values_, lo, hi, loc_); }

        // Argument `i` (from 0) as the type asked for; E4-010 when the call has no such argument, E4-005 when it
        // is of another type.
        double number(size_t i) const { return expectNumber(function_.c_str(), values_, need(i), loc_); }
        const std::string &str(size_t i) const { return expectStr(function_.c_str(), values_, need(i), loc_); }
        const ValueList &list(size_t i) const { return expectList(function_.c_str(), values_, need(i), loc_); }
        const ValueMap &map(size_t i) const { return expectMap(function_.c_str(), values_, need(i), loc_); }
        bool boolean(size_t i) const
        {
            if (!values_[need(i)].isBool())
                throw TypeError(function_ + "() expects argument " + std::to_string(i + 1) + " to be a boolean, got " +
                                    valueTypeName(values_[i].type()),
                                loc_);
            return values_[i].asBool();
        }

        // Ends the call with an error the script can catch with `or_else` (E4-042), reported at the call.
        [[noreturn]] void fail(const std::string &message) const
        {
            throw CuffRuntimeError(ErrorCode::HostFunctionFailed, function_ + "() failed: " + message, loc_);
        }

    private:
        const std::string &function_;
        std::vector<Value> &values_;
        const SourceLocation &loc_;

        size_t need(size_t i) const
        {
            if (i >= values_.size())
                throw ArgumentError(function_ + "() expects at least " + std::to_string(i + 1) + " argument(s), got " +
                                        std::to_string(values_.size()),
                                    loc_);
            return i;
        }
    };

    // A function of the host program that scripts can call. It returns the call's value (Value() is `empty`).
    using HostFunction = std::function<Value(Args &)>;

    // The engine as a host program uses it: one Engine is one program that can be run in pieces. Each run() parses
    // and executes a piece of source against the same interpreter, so what an earlier piece declared (variables,
    // functions, classes, loaded modules) is there for the next; the interactive shell is one long-lived Engine, and
    // a script runner is an Engine used once (execute).
    //
    // Nothing here throws for something a script did. A failure comes back as Outcome::error, already in the
    // engine's report format (common/ErrorFormat.h), so a front end prints it and never builds an error message of
    // its own. That is what keeps every front end in step when the report changes.
    class Engine
    {
    public:
        using Options = EngineOptions;

        struct Outcome
        {
            bool success = true;
            std::string error; // the full report, empty on success
            Value value;       // what call() returned; `empty` otherwise
        };
        using Result = Outcome; // the name this had in 5.x

        explicit Engine(Options options = Options()) : options_(std::move(options)) { start(); }

        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;

        // Parses and runs `source`. `scriptDir` is the directory `use ... from` resolves against.
        Outcome run(const std::string &source, const std::string &scriptDir = ".")
        {
            return run(makeSourceRef(source), scriptDir);
        }

        // As above, handing the text over instead of copying it (a program can be as large as the source limit).
        Outcome run(std::string &&source, const std::string &scriptDir = ".")
        {
            return run(makeSourceRef(std::move(source)), scriptDir);
        }

        // Runs text that is already shared, e.g. an entry the caller also keeps for its own use.
        //
        // The texts one engine runs are one program: a later text uses what the earlier ones declared, and declaring
        // one of their functions or top-level variables again is E2-010, as it would be further down one file.
        Outcome run(const SourceRef &text, const std::string &scriptDir)
        {
            return guarded(text, [&](Outcome &)
                           {
                               if (programs_.empty())
                                   programs_.push_back(parseProgram(text));
                               else
                               {
                                   const EarlierNames earlier = earlierNames();
                                   programs_.push_back(parseProgram(text, nullptr, nullptr, &earlier));
                               }
                               const Program &program = *programs_.back();
                               topVariables_.insert(program.declaredNames.begin(), program.declaredNames.end());
                               interp_->run(program, scriptDir);
                           });
        }

        // Frees, now, the lists, maps and objects that only refer to each other (two objects that hold each other, a
        // list that contains itself); how many of them that was. The engine does this by itself while a program runs,
        // so a host needs it only to have the memory back at a moment of its own choosing.
        size_t collectCycles() { return interp_->collectCycles(); }

        // Calls a function that an earlier run() declared, with these arguments, and gives its value.
        Outcome call(const std::string &function, std::vector<Value> arguments = {})
        {
            return guarded(nullptr, [&](Outcome &outcome) { outcome.value = interp_->call(function, std::move(arguments)); });
        }

        // The value of a variable of the program's top level; nothing when the program declares no such variable.
        // A list, map or object is shared with the program, as it is between two variables of a script.
        std::optional<Value> get(const std::string &variable) const
        {
            Value v;
            if (!interp_->readGlobal(variable, v))
                return std::nullopt;
            return v;
        }

        // Assigns to a variable of the top level as a script would (the variable's type and `constant` are
        // respected), or declares it as `any` when the program has no such variable yet.
        Outcome set(const std::string &variable, Value value)
        {
            return guarded(nullptr, [&](Outcome &) { interp_->writeGlobal(variable, std::move(value)); });
        }

        // Makes `function` callable from scripts as `name(...)`, for as long as the engine lasts (reset() keeps it).
        // Add a function before the code that calls it runs. What the function throws ends the call with E4-042,
        // which a script can catch.
        void addFunction(const std::string &name, HostFunction function)
        {
            hostFunctions_.emplace_back(name, std::move(function));
            install(hostFunctions_.back().first, hostFunctions_.back().second);
        }

        // Forgets everything the program declared and starts again with the same options and host functions.
        void reset()
        {
            interp_.reset();
            programs_.clear();
            topVariables_.clear();
            start();
        }

        const Options &options() const { return options_; }

        // Runs a whole program once: an Engine made for the call and discarded after it.
        static Outcome execute(const std::string &source, const std::string &scriptDir = ".", const Options &options = Options())
        {
            return execute(std::string(source), scriptDir, options);
        }

        static Outcome execute(std::string &&source, const std::string &scriptDir, const Options &options = Options())
        {
            // Making the engine can fail too (no memory), and the caller is promised an Outcome.
            try
            {
                Engine engine(options);
                return engine.run(std::move(source), scriptDir);
            }
            catch (...)
            {
                Outcome outcome;
                outcome.success = false;
                outcome.error = renderCurrentExceptionSafely(nullptr);
                return outcome;
            }
        }

        // ---- for tools ---------------------------------------------------------------------------------------------

        struct Parsed
        {
            bool success = false;
            std::string error;
            std::vector<Token> rawTokens;
            std::vector<Token> lexedTokens;
            std::unique_ptr<Program> ast;
        };

        // Parses without running. `keepTokens` keeps the token streams before and after the lexer in the result.
        static Parsed parse(const std::string &source, bool keepTokens = true)
        {
            Parsed parsed;
            const SourceRef text = makeSourceRef(source);
            try
            {
                parsed.ast = keepTokens ? parseProgram(text, &parsed.rawTokens, &parsed.lexedTokens) : parseProgram(text);
                parsed.success = true;
            }
            catch (...)
            {
                parsed.error = renderCurrentExceptionSafely(text);
            }
            return parsed;
        }

        // What `cuffc --ast` prints: the token streams and the syntax tree on standard output, or the error on
        // standard error.
        static void dump(const Parsed &parsed)
        {
            if (!parsed.success)
            {
                std::cerr << "ERROR: " << parsed.error << "\n\n";
                return;
            }
            std::cout << "===== TOKENIZER OUTPUT =====\n";
            std::cout << ASTPrinter::printTokens(parsed.rawTokens);
            std::cout << "\n===== LEXER OUTPUT =====\n";
            std::cout << ASTPrinter::printTokens(parsed.lexedTokens);
            std::cout << "\n===== AST =====\n";
            if (parsed.ast)
                std::cout << ASTPrinter::print(*parsed.ast);
            std::cout << "\n===== PARSE SUCCESS =====\n";
        }

    private:
        Options options_;
        std::vector<std::pair<std::string, HostFunction>> hostFunctions_;
        // Every program that was run stays alive: the interpreter keeps raw pointers to the functions inside them, at
        // any nesting depth, for as long as the engine lasts. Declared before interp_ so it is destroyed after it.
        std::vector<std::unique_ptr<Program>> programs_;
        // The variables those programs declared at their top level, outside any block.
        std::unordered_set<uint32_t> topVariables_;
        std::unique_ptr<Interpreter> interp_;
        // Memory that is given back when a run ends because memory ran out, so that the report of that can be
        // written. (What the program had built is still there at that moment: its variables outlive the run.)
        static constexpr size_t kReserveBytes = 32 * 1024;
        std::unique_ptr<char[]> reserve_{new (std::nothrow) char[kReserveBytes]};

        // What the earlier programs declared and the program has: their functions (a text whose functions could not
        // all be declared declared none), and the variables of their top levels whose declarations ran (a run that
        // failed first leaves the name free for a later text).
        EarlierNames earlierNames() const
        {
            EarlierNames names;
            names.functions = interp_->programFunctions();
            for (uint32_t id : topVariables_)
                if (interp_->declaresVariable(id))
                    names.variables.push_back(id);
            return names;
        }

        void start()
        {
            interp_ = std::make_unique<Interpreter>(options_);
            for (const auto &entry : hostFunctions_)
                install(entry.first, entry.second);
        }

        void install(const std::string &name, const HostFunction &function)
        {
            interp_->addNative(name, [name, function](std::vector<Value> &values, const SourceLocation &loc) -> Value
                               {
                                   Args args(name, values, loc);
                                   try
                                   {
                                       return function(args);
                                   }
                                   catch (const CuffError &)
                                   {
                                       throw;
                                   }
                                   catch (const std::bad_alloc &)
                                   {
                                       throw;
                                   }
                                   catch (const std::exception &e)
                                   {
                                       throw CuffRuntimeError(ErrorCode::HostFunctionFailed, name + "() failed: " + e.what(), loc);
                                   }
                               });
        }

        // Runs `body` and turns whatever it throws into the report. `text` is the source the run was given, which an
        // error that names no text of its own is drawn against.
        template <typename Body>
        Outcome guarded(const SourceRef &text, Body &&body)
        {
            Outcome outcome;
            try
            {
                body(outcome);
            }
            catch (...)
            {
                outcome.success = false;
                outcome.value = Value();
                outcome.error = renderCurrentExceptionSafely(text, &reserve_);
            }
            if (!reserve_)
                reserve_.reset(new (std::nothrow) char[kReserveBytes]);
            return outcome;
        }
    };

    // The names this class had in 5.x. `CuffEngine::run(source)`, which only parsed, is Engine::parse.
    using Session = Engine;
    using CuffEngine = Engine;

}
