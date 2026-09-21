#pragma once

#include "Value.h"
#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/Utf8.h"
#include "Environment.h"
#include "Signals.h"
#include "NativeFunctions.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include "../parser/ASTNodes.h"
#include "../parser/Parser.h"
#include "../tokenizer/Tokenizer.h"
#include "../lexer/Lexer.h"
#include "../regex/RegexEngine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

namespace cuff
{

    // =========================================================================
    // Interpreter — tree-walking evaluator for a parsed CuffScript Program.
    //
    // Design choices worth knowing when extending this file:
    //
    //  - Functions are NOT first-class values (the spec explicitly rules out
    //    closures/nested functions), so they live in their own registry
    //    (userFunctions_) rather than as a Value variant, and are looked up
    //    by name at call time.
    //  - Lists/Maps are reference types (Value.h, shared_ptr-backed); scalars
    //    are copied by value. `set` always declares into the *current* scope;
    //    `change` mutates an existing binding wherever it's found (see
    //    Environment.h for the exact scope-walking rules).
    //  - `async`/`await`: the language spec explicitly defers the async
    //    execution model to "a separate implementation spec" that doesn't
    //    exist yet. This engine's model: calling an async function *with*
    //    `await` runs it immediately and returns its value, exactly like a
    //    normal call (and requires the target to actually be declared
    //    `async`, so `await` still functions as useful documentation).
    //    Calling an async function *without* `await` does NOT run it
    //    immediately — it's queued (see taskQueue_) and runs after the
    //    entire top-level script finishes, which is what actually makes
    //    it "asynchronous": the rest of the program visibly runs first.
    //    There is no real concurrency (single-threaded interpreter, no
    //    thread-safety story for shared state), so this is cooperative
    //    deferral to "the end", not parallelism — see
    //    docs/IMPLEMENTATION_NOTES.md for the exact rules.
    //  - Control flow for `return`/`stop` is NOT implemented with C++
    //    exceptions — see Signals.h for why (performance) and what bug that
    //    used to cause (stop leaking through function-call boundaries).
    //    execStatement/execBlock/execIf/execLoop all return an ExecOutcome
    //    that must be propagated (or consumed) by every caller; exceptions
    //    are reserved for genuine CuffError conditions.
    //  - Adding a new expression/statement kind: add the case to evalExpr /
    //    execStatement below (the switch is exhaustive and -Werror=switch
    //    will fail the build if a case is missed, which is intentional).
    // =========================================================================
    class Interpreter
    {
    public:
        struct Config
        {
            std::string rootDir;    // modules must resolve inside this directory (default: the script's directory)
            uint64_t maxSteps = 0;  // loop iterations + user-function calls; 0 = unlimited
            uint32_t timeoutMs = 0; // wall-clock budget for the whole run; 0 = unlimited
            size_t stackBudgetBytes = 0; // native stack the evaluator may use; 0 = derive from the real stack size
        };

        Interpreter() { registerBuiltins(natives_); }
        explicit Interpreter(Config config) : config_(std::move(config)) { registerBuiltins(natives_); }

        // Entry point for the top-level script. `scriptDir` is used to
        // resolve relative `use ... from ...` paths; the caller must keep
        // `program` alive for the interpreter's whole lifetime (userById_
        // stores raw pointers into it).
        void run(const Program &program, const std::string &scriptDir)
        {
            scriptDir_ = scriptDir.empty() ? std::string(".") : scriptDir;
            initModuleRoot();
            const uintptr_t sp = stackPointer();
            size_t softBudget = config_.stackBudgetBytes ? config_.stackBudgetBytes : limits::kDefaultStackBudget;
            size_t hardBudget = softBudget + 3 * 1024 * 1024;
            if (size_t avail = availableStackBytes(); avail && !config_.stackBudgetBytes)
            {
                // Real stack size is known: interpreter recursion may use nearly
                // all of it, keeping a reserve below for native helpers (regex
                // matching also checks the hard floor and fails cleanly).
                constexpr size_t kMargin = 256 * 1024;
                constexpr size_t kLeafReserve = 768 * 1024;
                hardBudget = avail > kMargin ? avail - kMargin : avail / 2;
                softBudget = hardBudget - std::min(kLeafReserve, hardBudget / 3);
                softBudget = std::min(softBudget, limits::kMaxStackBudget);
            }
            stackLimit_ = sp > softBudget ? sp - softBudget : 0;
            StackFloorScope floorScope(sp > hardBudget ? sp - hardBudget : 0);
            stepsLeft_ = config_.maxSteps ? config_.maxSteps : UINT64_MAX;
            timed_ = config_.timeoutMs != 0;
            if (timed_)
                deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.timeoutMs);
            execProgram(program, globalEnv_);
        }

    private:
        Config config_;
        Environment globalEnv_;
        std::unordered_map<std::string, NativeFn> natives_;
        std::vector<const FunctionDecl *> userById_;  // indexed by interned function name id
        std::vector<const NativeFn *> nativeById_;    // lazily filled cache into natives_ (node-stable)
        std::vector<std::unique_ptr<Program>> loadedModules_; // keeps imported-module ASTs alive
        std::unordered_set<std::string> importedPaths_;
        std::string scriptDir_ = ".";
        std::filesystem::path moduleRoot_;
        int moduleDepth_ = 0;
        regex::RegexEngine regexEngine_;

        // An async function called without `await` doesn't run immediately —
        // it's appended here and drained (FIFO) after the entire top-level
        // script finishes (see execProgram/drainTaskQueue). Its result is
        // discarded either way: without `await` there is no expression to
        // receive a value.
        struct QueuedTask
        {
            const FunctionDecl *decl;
            std::vector<Value> args;
        };
        std::deque<QueuedTask> taskQueue_;

        int callDepth_ = 0;
        bool inFunctionBody_ = false;
        bool currentFunctionReturnable_ = false;
        static constexpr int kMaxCallDepth = limits::kMaxCallDepth;

        // Native-stack safety net. The call-depth counter above bounds
        // recursion in the common case, but a function body with deeply nested
        // blocks/expressions uses far more stack per call, so every evaluation
        // step also compares the real stack pointer against a fixed budget.
        uintptr_t stackLimit_ = 0;
        uint64_t stepsLeft_ = UINT64_MAX;
        uint32_t tickCount_ = 0;
        bool timed_ = false;
        std::chrono::steady_clock::time_point deadline_;

        struct StackFloorScope
        {
            explicit StackFloorScope(uintptr_t floor) { stackFloor() = floor; }
            ~StackFloorScope() { stackFloor() = 0; }
        };

        // RAII guard for entering/leaving a user function call — keeps the
        // call-depth counter and the two "current frame" flags correct even
        // when a Return outcome or an error unwinds through callUserFunction.
        struct FrameGuard
        {
            Interpreter *interp;
            bool prevInFunc;
            bool prevReturnable;
            FrameGuard(Interpreter *i, bool returnable) : interp(i)
            {
                prevInFunc = i->inFunctionBody_;
                prevReturnable = i->currentFunctionReturnable_;
                i->inFunctionBody_ = true;
                i->currentFunctionReturnable_ = returnable;
                ++i->callDepth_;
            }
            ~FrameGuard()
            {
                interp->inFunctionBody_ = prevInFunc;
                interp->currentFunctionReturnable_ = prevReturnable;
                --interp->callDepth_;
            }
        };

        // ==== Resource guards ===================================================

        template <typename Node>
        CUFF_ALWAYS_INLINE void guardStack(const Node &node)
        {
            if (stackPointer() < stackLimit_)
                stackExhausted(node);
        }

        [[noreturn]] CUFF_COLD void stackExhausted(const Expr &e) const
        {
            throwStackExhausted(std::visit([](const auto &n) { return n.loc; }, e.data));
        }
        [[noreturn]] CUFF_COLD void stackExhausted(const Stmt &s) const
        {
            throwStackExhausted(std::visit([](const auto &n) { return n.loc; }, s.data));
        }
        [[noreturn]] CUFF_COLD static void throwStackExhausted(const SourceLocation &loc)
        {
            throw StackOverflowError("native stack budget exhausted — calls and blocks are nested too deeply", loc);
        }

        // Charged once per loop iteration and per user-function call, which is
        // enough to bound any non-terminating program: expression trees are
        // finite and regex work has its own limits.
        CUFF_ALWAYS_INLINE void tick(const SourceLocation &loc)
        {
            if (--stepsLeft_ == 0)
                budgetExceeded(ErrorCode::ExecutionStepLimit,
                               "execution step limit (" + std::to_string(config_.maxSteps) + ") exceeded", loc);
            if (timed_ && (++tickCount_ & 0x3FF) == 0 && std::chrono::steady_clock::now() > deadline_)
                budgetExceeded(ErrorCode::ExecutionTimeout,
                               "execution time limit (" + std::to_string(config_.timeoutMs) + " ms) exceeded", loc);
        }

        [[noreturn]] CUFF_COLD static void budgetExceeded(ErrorCode code, const std::string &msg, const SourceLocation &loc)
        {
            throw CuffRuntimeError(code, msg, loc);
        }

        [[noreturn]] CUFF_COLD static void throwSizeLimit(const char *what, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::SizeLimitExceeded,
                                   std::string(what) + " exceeds the maximum allowed size", loc);
        }

        static void checkStringSize(size_t n, const SourceLocation &loc)
        {
            if (n > limits::kMaxStringBytes)
                throwSizeLimit("string", loc);
        }

        static void checkCollectionSize(size_t n, const SourceLocation &loc)
        {
            if (n > limits::kMaxCollectionItems)
                throwSizeLimit("collection", loc);
        }

        // ==== Program / statement execution ====================================

        void execProgram(const Program &program, Environment &env)
        {
            // Hoist top-level function declarations so call order in the
            // source doesn't matter (a function may be used before its
            // textual definition, as long as both are top-level).
            for (auto &s : program.statements)
                if (s->kind == StmtKind::FunctionDecl)
                    registerFunction(std::get<FunctionDecl>(s->data));

            for (auto &s : program.statements)
            {
                if (s->kind == StmtKind::FunctionDecl)
                    continue; // already registered above

                ExecOutcome outcome = execStatement(*s, env);

                if (outcome.result == ExecResult::Return)
                    throw CuffRuntimeError(ErrorCode::ReturnOutsideFunction,
                                           "'return' cannot be used outside of a function", outcome.loc);
                if (outcome.result == ExecResult::Stop)
                    throw CuffRuntimeError(ErrorCode::StopOutsideLoop,
                                           "'stop' cannot be used outside of a loop", outcome.loc);
            }

            // All synchronous top-level code has now run. Anything that was
            // queued along the way (an async function called anywhere,
            // without await) runs now, in the order it was queued — this is
            // what makes "called without await" visibly asynchronous: it
            // always happens after the rest of the script, not inline.
            drainTaskQueue();
        }

        // Runs any async calls that were queued (by evalExpr's FunctionCall
        // case, anywhere in the program) but haven't executed yet, once the
        // whole top-level script has finished. A task can itself queue more
        // tasks (by calling another async function without await); those
        // are processed too, in the order queued, before this returns.
        void drainTaskQueue()
        {
            while (!taskQueue_.empty())
            {
                QueuedTask task = std::move(taskQueue_.front());
                taskQueue_.pop_front();
                callUserFunction(*task.decl, task.args, task.decl->loc);
            }
        }

        ExecOutcome execBlock(const std::vector<std::unique_ptr<Stmt>> &body, Environment &env)
        {
            for (auto &s : body)
            {
                ExecOutcome outcome = execStatement(*s, env);
                if (outcome.result != ExecResult::Normal)
                    return outcome;
            }
            return ExecOutcome::normal();
        }

        void registerFunction(const FunctionDecl &decl)
        {
            if (decl.nameId >= userById_.size())
                userById_.resize(decl.nameId + 1, nullptr);
            userById_[decl.nameId] = &decl;
        }

        const FunctionDecl *findUser(uint32_t id) const
        {
            return id < userById_.size() ? userById_[id] : nullptr;
        }

        const NativeFn *findNative(const FunctionCall &fc)
        {
            const uint32_t id = fc.functionNameId;
            if (id < nativeById_.size() && nativeById_[id])
                return nativeById_[id];
            auto it = natives_.find(fc.functionName);
            if (it == natives_.end())
                return nullptr;
            if (id >= nativeById_.size())
                nativeById_.resize(id + 1, nullptr);
            nativeById_[id] = &it->second;
            return &it->second;
        }

        [[noreturn]] CUFF_COLD static void throwNestedFunction(const FunctionDecl &decl)
        {
            throw CuffRuntimeError(ErrorCode::NestedFunctionNotSupported,
                                   "nested function definitions are not supported ('" + decl.name + "' is defined inside another function)",
                                   decl.loc, "move '" + decl.name + "' to the top level");
        }

        [[noreturn]] CUFF_COLD static void throwReturnInVoidFunction(const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::UnsupportedOperation,
                                   "cannot return a value from a non-returnable function",
                                   loc, "declare it with 'set returnable function' to allow returning a value");
        }

        ExecOutcome execStatement(const Stmt &stmt, Environment &env)
        {
            guardStack(stmt);
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
                execDeclaration(std::get<DeclarationStmt>(stmt.data), env);
                return ExecOutcome::normal();
            case StmtKind::Change:
                execChange(std::get<ChangeStmt>(stmt.data), env);
                return ExecOutcome::normal();
            case StmtKind::FunctionDecl:
            {
                const auto &decl = std::get<FunctionDecl>(stmt.data);
                if (inFunctionBody_)
                    throwNestedFunction(decl);
                registerFunction(decl); // reached for functions nested in top-level if/loop bodies
                return ExecOutcome::normal();
            }
            case StmtKind::IfStmt:
                return execIf(std::get<IfStmt>(stmt.data), env);
            case StmtKind::LoopStmt:
                return execLoop(std::get<LoopStmt>(stmt.data), env);
            case StmtKind::StopStmt:
                return ExecOutcome::makeStop(std::get<StopStmt>(stmt.data).loc);
            case StmtKind::ReturnStmt:
            {
                const auto &r = std::get<ReturnStmt>(stmt.data);
                Value v = r.value ? evalExpr(*r.value, env) : Value::makeEmpty();
                if (r.value && !currentFunctionReturnable_)
                    throwReturnInVoidFunction(r.loc);
                return ExecOutcome::makeReturn(std::move(v), r.loc);
            }
            case StmtKind::AwaitStmt:
            {
                const auto &aw = std::get<AwaitStmt>(stmt.data);
                invokeAwaited(*aw.expr->call, env, aw.loc); // result intentionally discarded
                return ExecOutcome::normal();
            }
            case StmtKind::UseStmt:
                execUse(std::get<UseStmt>(stmt.data), env);
                return ExecOutcome::normal();
            case StmtKind::ExprStmt:
                evalExpr(*std::get<ExprStmt>(stmt.data).expr, env);
                return ExecOutcome::normal();
            case StmtKind::CollectionOp:
                execCollectionOp(std::get<CollectionOpStmt>(stmt.data), env);
                return ExecOutcome::normal();
            case StmtKind::OrElse:
                return execOrElse(std::get<OrElseStmt>(stmt.data), env);
            }
            throw InternalEngineError("unhandled statement kind");
        }

        // ---- Declarations & assignment ----

        static bool isValidConstantName(const std::string &name)
        {
            bool hasUpper = false;
            for (char c : name)
            {
                if (c >= 'a' && c <= 'z')
                    return false;
                if (c >= 'A' && c <= 'Z')
                    hasUpper = true;
            }
            return hasUpper;
        }

        [[noreturn]] CUFF_COLD static void throwDeclarationMismatch(const std::string &varType, const Value &v,
                                                                       const std::string &name, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::DeclarationTypeMismatch,
                                   "cannot assign a " + valueTypeName(v.type()) + " value to " + varType + " variable '" + name + "'",
                                   loc, "the declared type (" + varType + ") and the assigned value's type must match at 'set'");
        }

        static void checkDeclaredType(const std::string &varType, const Value &v, const std::string &name, const SourceLocation &loc)
        {
            // `empty` is a universal "no value" sentinel — any declared type
            // may hold it (this is what lets `find`/`match`/map lookups that
            // come up empty be stored directly in a typed variable, to then
            // be handled with `or_else` or an `is empty` check).
            if (v.isEmpty() || varType.empty())
                return;

            bool ok;
            switch (varType[0])
            {
            case 'n':
                ok = v.isNumber();
                break;
            case 's':
                ok = v.isStr();
                break;
            case 'l':
                ok = v.isList();
                break;
            case 'b':
                ok = v.isBool();
                break;
            case 'm':
                ok = varType.size() == 3 ? v.isMap() : v.isMatch(); // "map" / "match"
                break;
            case 'e':
                ok = false; // a non-empty value can never satisfy `empty`
                break;
            default:
                ok = true;
            }
            if (!ok)
                throwDeclarationMismatch(varType, v, name, loc);
        }

        static std::string upperAscii(const std::string &s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                            { return std::toupper(c); });
            return out;
        }

        void execDeclaration(const DeclarationStmt &decl, Environment &env)
        {
            Value v = evalExpr(*decl.value, env);

            if (decl.isConstant && !isValidConstantName(decl.name))
            {
                throw ConstantError(ErrorCode::InvalidConstantName,
                                    "constant name '" + decl.name + "' must be written in ALL CAPS (uppercase letters, digits, underscores only)",
                                    decl.loc, "for example: " + upperAscii(decl.name));
            }

            checkDeclaredType(decl.varType, v, decl.name, decl.loc);
            env.declare(decl.nameId, std::move(v), decl.isConstant);
        }

        void execChange(const ChangeStmt &c, Environment &env)
        {
            if (c.toGlobal)
            {
                if (!inFunctionBody_)
                {
                    throw CuffRuntimeError(ErrorCode::InvalidGlobalDeclaration,
                                           "'change " + c.name + " to global' can only be used inside a function body",
                                           c.loc, "at the top level, every variable is already global");
                }
                env.declareGlobal(c.nameId);
                return;
            }

            auto look = env.resolve(c.nameId);
            if (!look.value)
            {
                throw UndefinedVariableError("cannot change undefined variable '" + c.name + "'", c.loc,
                                             "declare it first with 'set', or bridge a global with 'change " + c.name + " to global'");
            }
            if (env.isConstantIn(look.owner, c.nameId))
            {
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot change constant '" + c.name + "'", c.loc);
            }

            if (c.indices.empty())
            {
                Value v = evalExpr(*c.value, env);
                *look.value = std::move(v);
                return;
            }

            std::vector<Value> indexVals;
            indexVals.reserve(c.indices.size());
            for (auto &idxExpr : c.indices)
                indexVals.push_back(evalExpr(*idxExpr, env));
            Value newVal = evalExpr(*c.value, env);
            ContainerSlot slot = resolveContainerSlot(*look.value, indexVals, c.loc);
            slot.write(std::move(newVal));
        }

        void assignLoopVar(const LoopStmt &loop, Value v, Environment &env)
        {
            if (!env.declareLoopVar(loop.repeatVarId, std::move(v)))
            {
                throw ConstantError(ErrorCode::ConstantReassignment,
                                    "cannot use constant '" + loop.repeatVar + "' as a loop variable", loop.loc);
            }
        }

        // ---- Control flow ----

        ExecOutcome execIf(const IfStmt &ifs, Environment &env)
        {
            for (auto &branch : ifs.branches)
            {
                if (!branch.condition || evalExpr(*branch.condition, env).truthy())
                {
                    return execBlock(branch.body, env);
                }
            }
            return ExecOutcome::normal();
        }

        ExecOutcome execLoop(const LoopStmt &loop, Environment &env)
        {
            if (loop.kind == LoopStmt::LoopKind::Repeat)
            {
                Value startV = evalExpr(*loop.repeatStart, env);
                Value endV = evalExpr(*loop.repeatEnd, env);
                if (!startV.isNumber() || !endV.isNumber())
                    throw TypeError("'loop repeat' bounds must be numbers", loop.loc);
                long long start = expectWholeNumber(startV.asNumber(), "'loop repeat' start bound", loop.loc);
                long long end = expectWholeNumber(endV.asNumber(), "'loop repeat' end bound", loop.loc);
                if (start <= end)
                {
                    for (long long i = start; i <= end; ++i)
                    {
                        tick(loop.loc);
                        assignLoopVar(loop, Value::makeNumber(static_cast<double>(i)), env);
                        ExecOutcome outcome = execBlock(loop.body, env);
                        if (outcome.result == ExecResult::Stop)
                            return ExecOutcome::normal(); // consumed here — loop ends normally
                        if (outcome.result == ExecResult::Return)
                            return outcome; // propagate up to the enclosing function
                    }
                }
                else
                {
                    for (long long i = start; i >= end; --i)
                    {
                        tick(loop.loc);
                        assignLoopVar(loop, Value::makeNumber(static_cast<double>(i)), env);
                        ExecOutcome outcome = execBlock(loop.body, env);
                        if (outcome.result == ExecResult::Stop)
                            return ExecOutcome::normal();
                        if (outcome.result == ExecResult::Return)
                            return outcome;
                    }
                }
            }
            else
            {
                // While and Match both re-check a boolean condition every
                // iteration (see LoopParser.h for why the two share this
                // implementation).
                while (evalExpr(*loop.condition, env).truthy())
                {
                    tick(loop.loc);
                    ExecOutcome outcome = execBlock(loop.body, env);
                    if (outcome.result == ExecResult::Stop)
                        return ExecOutcome::normal();
                    if (outcome.result == ExecResult::Return)
                        return outcome;
                }
            }
            return ExecOutcome::normal();
        }

        ExecOutcome execOrElse(const OrElseStmt &oe, Environment &env)
        {
            try
            {
                return execStatement(*oe.primaryStmt, env);
            }
            catch (CuffError &e)
            {
                if (!e.recoverable)
                    throw;

                // If the primary was a declaration that never completed (its
                // value expression threw before `env.declare` ran), make sure
                // the name still exists — as `empty` — so the fallback body's
                // `change` can find and fix it up.
                if (oe.primaryStmt->kind == StmtKind::Declaration)
                {
                    const auto &decl = std::get<DeclarationStmt>(oe.primaryStmt->data);
                    if (!env.isDeclaredHere(decl.nameId))
                        env.declare(decl.nameId, Value::makeEmpty(), false);
                }

                Environment blockEnv(Environment::Kind::BlockScope, env);
                return execBlock(oe.fallbackBody, blockEnv);
            }
        }

        // ---- Collections ----

        void execCollectionOp(const CollectionOpStmt &co, Environment &env)
        {
            auto look = env.resolve(co.collectionNameId);
            if (!look.value)
                throw UndefinedVariableError("undefined collection '" + co.collectionName + "'", co.loc);
            if (env.isConstantIn(look.owner, co.collectionNameId))
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot modify constant collection '" + co.collectionName + "'", co.loc);

            Value &target = *look.value;

            switch (co.opKind)
            {
            case CollectionOpStmt::OpKind::Add:
            {
                if (!target.isList())
                    throw TypeError("'add ... to' requires a list, got " + valueTypeName(target.type()), co.loc);
                Value v = evalExpr(*co.addValue, env);
                checkCollectionSize(target.asList()->items.size() + 1, co.loc);
                target.asList()->items.push_back(std::move(v));
                break;
            }
            case CollectionOpStmt::OpKind::Replace:
            {
                Value idxKey = evalExpr(*co.indexOrKey, env);
                Value newVal = evalExpr(*co.newValue, env);
                std::vector<Value> idxVals{std::move(idxKey)};
                ContainerSlot slot = resolveContainerSlot(target, idxVals, co.loc);
                slot.write(std::move(newVal));
                break;
            }
            case CollectionOpStmt::OpKind::Remove:
            {
                Value rv = evalExpr(*co.removeValue, env);
                if (target.isList())
                {
                    auto &items = target.asList()->items;
                    if (rv.isNumber())
                    {
                        size_t real = resolveIndex1Based(expectWholeNumber(rv.asNumber(), "list index", co.loc), items.size(), co.loc);
                        items.erase(items.begin() + static_cast<long>(real));
                    }
                    else
                    {
                        auto it = std::find_if(items.begin(), items.end(), [&](const Value &v)
                                                { return valuesEqual(v, rv, co.loc); });
                        if (it == items.end())
                            throw ElementNotFoundError("value not found in list — nothing to remove", co.loc);
                        items.erase(it);
                    }
                }
                else if (target.isMap())
                {
                    if (!rv.isStr())
                        throw TypeError("map keys are strings; cannot remove using a " + valueTypeName(rv.type()), co.loc);
                    target.asMap()->remove(rv.asStr()); // no-op if the key doesn't exist
                }
                else
                {
                    throw TypeError("'add'/'remove'/'replace' require a list or map, got " + valueTypeName(target.type()), co.loc);
                }
                break;
            }
            }
        }

        // ---- Indexing / slicing ----

        struct ContainerSlot
        {
            enum class Kind
            {
                ListIndex,
                MapKey
            } kind;
            ValueList *list = nullptr;
            size_t index = 0;
            ValueMap *map = nullptr;
            std::string key;
            SourceLocation loc;

            void write(Value v)
            {
                if (kind == Kind::ListIndex)
                {
                    list->items[index] = std::move(v);
                    return;
                }
                if (map->size() >= limits::kMaxCollectionItems && !map->has(key))
                    throwSizeLimit("collection", loc);
                map->set(key, std::move(v));
            }

            static ContainerSlot forList(ValueList *l, size_t i, const SourceLocation &at)
            {
                ContainerSlot s;
                s.kind = Kind::ListIndex;
                s.list = l;
                s.index = i;
                s.loc = at;
                return s;
            }
            static ContainerSlot forMap(ValueMap *m, std::string k, const SourceLocation &at)
            {
                ContainerSlot s;
                s.kind = Kind::MapKey;
                s.map = m;
                s.key = std::move(k);
                s.loc = at;
                return s;
            }
        };

        // Indices and range bounds must be whole numbers. Silently rounding a
        // fractional value (the previous behavior) hides real bugs — a
        // computed index like `total / 2` landing on 2.5 almost always means
        // the calculation is wrong, not that element 2 or 3 was intended.
        static long long expectWholeNumber(double d, const char *what, const SourceLocation &loc)
        {
            constexpr double kExactLimit = 9007199254740992.0; // 2^53: every whole number up to here is exactly representable
            if (!std::isfinite(d) || d != std::floor(d))
            {
                throw CuffRuntimeError(ErrorCode::FractionalIndex,
                                       std::string(what) + " must be a whole number, got " + formatCuffNumber(d),
                                       loc,
                                       "round it explicitly first (DLC:math's round/floor/ceil)");
            }
            if (std::fabs(d) > kExactLimit)
            {
                throw CuffRuntimeError(ErrorCode::InvalidArgumentValue,
                                       std::string(what) + " is outside the supported range (+/-9007199254740992), got " + formatCuffNumber(d),
                                       loc);
            }
            return static_cast<long long>(d);
        }

        static size_t resolveIndex1Based(long long idx, size_t length, const SourceLocation &loc)
        {
            if (idx == 0)
                throw IndexError(ErrorCode::ZeroIndexAccess,
                                 "index 0 does not exist — CuffScript indexing starts at 1", loc,
                                 "use 1 for the first element, or -1 for the last");
            long long real = (idx > 0) ? (idx - 1) : (static_cast<long long>(length) + idx);
            if (real < 0 || static_cast<size_t>(real) >= length)
            {
                throw IndexError(ErrorCode::IndexOutOfRange,
                                 "index " + std::to_string(idx) + " is out of range (length " + std::to_string(length) + ")", loc);
            }
            return static_cast<size_t>(real);
        }

        Value indexInto(const Value &target, const Value &indexVal, const SourceLocation &loc)
        {
            if (target.isList())
            {
                if (!indexVal.isNumber())
                    throw TypeError("list index must be a number (1-based)", loc);
                size_t real = resolveIndex1Based(expectWholeNumber(indexVal.asNumber(), "list index", loc), target.asList()->items.size(), loc);
                return target.asList()->items[real];
            }
            if (target.isStr())
            {
                if (!indexVal.isNumber())
                    throw TypeError("str index must be a number (1-based)", loc);
                const StrData &sd = target.asStrData();
                size_t cp = resolveIndex1Based(expectWholeNumber(indexVal.asNumber(), "str index", loc), sd.codepoints(), loc);
                if (sd.ascii())
                    return Value::makeChar(static_cast<unsigned char>(sd.text()[cp]));
                size_t from = sd.offsetOf(cp);
                size_t to = sd.offsetOf(cp + 1);
                return Value::makeStr(sd.text().substr(from, to - from));
            }
            if (target.isMap())
            {
                if (!indexVal.isStr())
                    throw TypeError("map keys are strings — cannot index with a " + valueTypeName(indexVal.type()), loc);
                const Value *v = target.asMap()->get(indexVal.asStr());
                if (!v)
                    throw KeyError("key \"" + indexVal.asStr() + "\" does not exist in this map", loc);
                return *v;
            }
            if (target.isMatch())
            {
                const auto &m = target.asMatch();
                if (indexVal.isNumber())
                {
                    size_t real = resolveIndex1Based(expectWholeNumber(indexVal.asNumber(), "capture index", loc), m->positional.size(), loc);
                    return Value::makeStr(m->positional[real]);
                }
                if (indexVal.isStr())
                {
                    auto it = m->named.find(indexVal.asStr());
                    if (it == m->named.end())
                        throw KeyError("named capture \"" + indexVal.asStr() + "\" does not exist in this match result", loc);
                    return Value::makeStr(it->second);
                }
                throw TypeError("a match result's index must be a number (positional) or str (named capture)", loc);
            }
            throw TypeError("cannot index into a " + valueTypeName(target.type()) + " value", loc);
        }

        Value sliceInto(const Value &target, const Value &startVal, const Value &endVal, const SourceLocation &loc)
        {
            if (!startVal.isNumber() || !endVal.isNumber())
                throw TypeError("slice bounds must be numbers", loc);
            long long s = expectWholeNumber(startVal.asNumber(), "slice start", loc);
            long long e = expectWholeNumber(endVal.asNumber(), "slice end", loc);

            if (target.isList())
            {
                size_t n = target.asList()->items.size();
                size_t rs = resolveIndex1Based(s, n, loc);
                size_t re = resolveIndex1Based(e, n, loc);
                auto result = std::make_shared<ValueList>();
                if (rs <= re)
                {
                    const auto &src = target.asList()->items;
                    result->items.assign(src.begin() + static_cast<long>(rs), src.begin() + static_cast<long>(re) + 1);
                }
                return Value::makeList(result);
            }
            if (target.isStr())
            {
                const StrData &sd = target.asStrData();
                size_t n = sd.codepoints();
                size_t rs = resolveIndex1Based(s, n, loc);
                size_t re = resolveIndex1Based(e, n, loc);
                if (rs > re)
                    return Value::makeStr(std::string());
                size_t from = sd.offsetOf(rs);
                size_t to = sd.offsetOf(re + 1);
                return Value::makeStr(sd.text().substr(from, to - from));
            }
            throw TypeError("cannot slice a " + valueTypeName(target.type()) + " value", loc);
        }

        ContainerSlot resolveContainerSlot(const Value &baseValue, const std::vector<Value> &indexValues, const SourceLocation &loc)
        {
            Value current = baseValue; // shallow copy — List/Map alias the same underlying storage
            for (size_t i = 0; i + 1 < indexValues.size(); ++i)
                current = indexInto(current, indexValues[i], loc);

            const Value &finalIdx = indexValues.back();
            if (current.isList())
            {
                if (!finalIdx.isNumber())
                    throw TypeError("list index must be a number (1-based)", loc);
                size_t real = resolveIndex1Based(expectWholeNumber(finalIdx.asNumber(), "list index", loc), current.asList()->items.size(), loc);
                return ContainerSlot::forList(current.asList().get(), real, loc);
            }
            if (current.isMap())
            {
                if (!finalIdx.isStr())
                    throw TypeError("map keys are strings — cannot assign with a " + valueTypeName(finalIdx.type()), loc);
                return ContainerSlot::forMap(current.asMap().get(), finalIdx.asStr(), loc);
            }
            throw TypeError("cannot index-assign into a " + valueTypeName(current.type()) + " value", loc);
        }

        // ==== Expression evaluation =============================================

        [[noreturn]] CUFF_COLD static void throwUndefinedVariable(const std::string &name, const SourceLocation &loc)
        {
            throw UndefinedVariableError("undefined variable '" + name + "'", loc);
        }

        [[noreturn]] CUFF_COLD static void throwUndefinedFunction(const std::string &name, const SourceLocation &loc)
        {
            throw UndefinedFunctionError("undefined function '" + name + "'", loc,
                                         "check the spelling, or make sure it's declared before this point");
        }

        [[noreturn]] CUFF_COLD static void throwDivisionByZero(const SourceLocation &loc)
        {
            throw DivisionByZeroError("division by zero", loc);
        }

        Value evalExpr(const Expr &expr, Environment &env)
        {
            guardStack(expr);
            switch (expr.kind)
            {
            case ExprKind::Number:
                return Value::makeNumber(std::get<NumberLiteral>(expr.data).value);
            case ExprKind::String:
                return Value::makeStr(std::get<StringLiteral>(expr.data).shared);
            case ExprKind::Bool:
                return Value::makeBool(std::get<BoolLiteral>(expr.data).value);
            case ExprKind::Empty:
                return Value::makeEmpty();
            case ExprKind::Identifier:
            {
                const auto &id = std::get<IdentifierExpr>(expr.data);
                auto look = env.resolve(id.nameId);
                if (!look.value)
                    throwUndefinedVariable(id.name, id.loc);
                return *look.value;
            }
            case ExprKind::List:
                return evalList(std::get<ListLiteral>(expr.data), env);
            case ExprKind::Map:
                return evalMap(std::get<MapLiteral>(expr.data), env);
            case ExprKind::FString:
                return evalFString(std::get<FStringExpr>(expr.data), env);
            case ExprKind::BinaryOp:
                return evalBinaryOp(std::get<BinaryOp>(expr.data), env);
            case ExprKind::UnaryOp:
                return evalUnaryOp(std::get<UnaryOp>(expr.data), env);
            case ExprKind::IndexAccess:
                return evalIndexAccess(std::get<IndexAccess>(expr.data), env);
            case ExprKind::SliceAccess:
                return evalSliceAccess(std::get<SliceAccess>(expr.data), env);
            case ExprKind::FunctionCall:
                return evalCall(std::get<FunctionCall>(expr.data), env);
            case ExprKind::Await:
            {
                const auto &aw = std::get<AwaitExpr>(expr.data);
                return invokeAwaited(*aw.call, env, aw.loc);
            }
            case ExprKind::RegexMatch:
                return evalRegexMatch(std::get<RegexMatchExpr>(expr.data), env);
            case ExprKind::MatchFrom:
                return evalMatchFrom(std::get<MatchFromExpr>(expr.data), env);
            case ExprKind::Find:
                return evalFind(std::get<FindExpr>(expr.data), env);
            case ExprKind::PatternReplace:
                return evalPatternReplace(std::get<PatternReplaceExpr>(expr.data), env);
            case ExprKind::Split:
                return evalSplit(std::get<SplitExpr>(expr.data), env);
            case ExprKind::Count:
                return evalCount(std::get<CountExpr>(expr.data), env);
            }
            throw InternalEngineError("unhandled expression kind");
        }

        CUFF_NOINLINE Value evalList(const ListLiteral &list, Environment &env)
        {
            checkCollectionSize(list.elements.size(), list.loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(list.elements.size());
            for (auto &e : list.elements)
                out->items.push_back(evalExpr(*e, env));
            return Value::makeList(std::move(out));
        }

        CUFF_NOINLINE Value evalMap(const MapLiteral &map, Environment &env)
        {
            auto out = std::make_shared<ValueMap>();
            out->reserve(map.pairs.size());
            for (auto &pair : map.pairs)
            {
                Value k = evalExpr(*pair.key, env);
                if (!k.isStr())
                    throw TypeError("map keys must be strings", map.loc);
                Value v = evalExpr(*pair.value, env);
                out->set(k.asStr(), std::move(v));
            }
            return Value::makeMap(std::move(out));
        }

        CUFF_NOINLINE Value evalFString(const FStringExpr &fs, Environment &env)
        {
            std::string out;
            for (auto &seg : fs.segments)
            {
                if (seg.isExpression)
                    evalExpr(*seg.expr, env).appendDisplay(out);
                else
                    out += seg.text;
                checkStringSize(out.size(), fs.loc);
            }
            return Value::makeStr(std::move(out));
        }

        CUFF_NOINLINE Value evalIndexAccess(const IndexAccess &idx, Environment &env)
        {
            Value t = evalExpr(*idx.target, env);
            Value i = evalExpr(*idx.index, env);
            return indexInto(t, i, idx.loc);
        }

        CUFF_NOINLINE Value evalSliceAccess(const SliceAccess &sl, Environment &env)
        {
            Value t = evalExpr(*sl.target, env);
            Value s = evalExpr(*sl.start, env);
            Value e = evalExpr(*sl.end, env);
            return sliceInto(t, s, e, sl.loc);
        }

        Value evalUnaryOp(const UnaryOp &u, Environment &env)
        {
            Value operand = evalExpr(*u.operand, env);
            if (u.op == UnOp::Not)
                return Value::makeBool(!operand.truthy());
            if (!operand.isNumber())
                throw TypeError("unary '-' requires a number, got " + valueTypeName(operand.type()), u.loc);
            return Value::makeNumber(-operand.asNumber());
        }

        Value evalBinaryOp(const BinaryOp &b, Environment &env)
        {
            Value l = evalExpr(*b.left, env);
            Value r = evalExpr(*b.right, env);

            // Fast path: both operands numeric, which is the overwhelmingly
            // common case for arithmetic and ordering.
            if (l.isNumber() && r.isNumber())
            {
                const double x = l.asNumber(), y = r.asNumber();
                switch (b.op)
                {
                case BinOp::Add:
                    return Value::makeNumber(x + y);
                case BinOp::Sub:
                    return Value::makeNumber(x - y);
                case BinOp::Mul:
                    return Value::makeNumber(x * y);
                case BinOp::Div:
                    if (y == 0.0)
                        throwDivisionByZero(b.loc);
                    return Value::makeNumber(x / y);
                case BinOp::Is:
                case BinOp::IsCase:
                    return Value::makeBool(x == y);
                case BinOp::IsNot:
                case BinOp::IsNotCase:
                    return Value::makeBool(x != y);
                case BinOp::Greater:
                    return Value::makeBool(x > y);
                case BinOp::Less:
                    return Value::makeBool(x < y);
                case BinOp::GreaterEq:
                    return Value::makeBool(x >= y);
                case BinOp::LessEq:
                    return Value::makeBool(x <= y);
                }
            }
            return evalBinaryOpSlow(b, l, r);
        }

        CUFF_NOINLINE Value evalBinaryOpSlow(const BinaryOp &b, const Value &l, const Value &r)
        {
            switch (b.op)
            {
            case BinOp::Add:
                if (l.isStr() && r.isStr())
                {
                    const std::string &a = l.asStr();
                    const std::string &c = r.asStr();
                    checkStringSize(a.size() + c.size(), b.loc);
                    if (c.empty())
                        return l;
                    if (a.empty())
                        return r;
                    std::string out;
                    out.reserve(a.size() + c.size());
                    out += a;
                    out += c;
                    return Value::makeStr(std::move(out));
                }
                if (l.isList() && r.isList())
                {
                    const auto &la = l.asList()->items;
                    const auto &lb = r.asList()->items;
                    checkCollectionSize(la.size() + lb.size(), b.loc);
                    auto out = std::make_shared<ValueList>();
                    out->items.reserve(la.size() + lb.size());
                    out->items.insert(out->items.end(), la.begin(), la.end());
                    out->items.insert(out->items.end(), lb.begin(), lb.end());
                    return Value::makeList(std::move(out));
                }
                throw TypeError("cannot add " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc,
                                "'+' works on number+number, str+str, or list+list");

            case BinOp::Sub:
                throw TypeError("'-' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);
            case BinOp::Mul:
                throw TypeError("'*' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);
            case BinOp::Div:
                throw TypeError("'/' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);

            case BinOp::Is:
                return Value::makeBool(valuesEqual(l, r, b.loc));
            case BinOp::IsNot:
                return Value::makeBool(!valuesEqual(l, r, b.loc));
            case BinOp::IsCase:
                return Value::makeBool(caseInsensitiveEquals(l, r, b.loc));
            case BinOp::IsNotCase:
                return Value::makeBool(!caseInsensitiveEquals(l, r, b.loc));

            case BinOp::Greater:
            case BinOp::Less:
            case BinOp::GreaterEq:
            case BinOp::LessEq:
                return compareOrdered(b.op, l, r, b.loc);
            }
            throw InternalEngineError("unknown binary operator");
        }

        static bool caseInsensitiveEquals(const Value &l, const Value &r, const SourceLocation &loc)
        {
            if (l.isStr() && r.isStr())
            {
                const std::string &a = l.asStr();
                const std::string &c = r.asStr();
                if (a.size() != c.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(c[i])))
                        return false;
                return true;
            }
            return valuesEqual(l, r, loc);
        }

        static Value compareOrdered(BinOp op, const Value &l, const Value &r, const SourceLocation &loc)
        {
            if (l.isNumber() && r.isNumber())
            {
                double a = l.asNumber(), c = r.asNumber();
                switch (op)
                {
                case BinOp::Greater: return Value::makeBool(a > c);
                case BinOp::Less: return Value::makeBool(a < c);
                case BinOp::GreaterEq: return Value::makeBool(a >= c);
                default: return Value::makeBool(a <= c);
                }
            }
            if (l.isStr() && r.isStr())
            {
                const std::string &a = l.asStr();
                const std::string &c = r.asStr();
                switch (op)
                {
                case BinOp::Greater: return Value::makeBool(a > c);
                case BinOp::Less: return Value::makeBool(a < c);
                case BinOp::GreaterEq: return Value::makeBool(a >= c);
                default: return Value::makeBool(a <= c);
                }
            }
            throw TypeError(std::string("cannot compare ") + valueTypeName(l.type()) + " and " +
                                valueTypeName(r.type()) + " with '" + binOpName(op) + "'",
                            loc, "comparisons work on two numbers or two strings");
        }

        // ---- Function calls ----

        [[noreturn]] CUFF_COLD static void throwAwaitOnNonAsync(const std::string &name, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::AwaitOnNonAsync,
                                   "'await' can only be used with an async function; '" + name + "' is not declared async", loc,
                                   "declare it with 'set async function " + name + "(...) do:', or call it without 'await'");
        }

        [[noreturn]] CUFF_COLD static void throwArgumentCount(const FunctionDecl &decl, size_t got, const SourceLocation &loc)
        {
            throw ArgumentError(decl.name + "() expects " + std::to_string(decl.params.size()) +
                                    " argument(s), got " + std::to_string(got),
                                loc);
        }

        [[noreturn]] CUFF_COLD static void throwCallDepth(const FunctionDecl &decl, const SourceLocation &loc)
        {
            throw StackOverflowError("maximum call depth (" + std::to_string(kMaxCallDepth) +
                                         ") exceeded while calling '" + decl.name + "' — check for infinite recursion",
                                     loc);
        }

        void queueTask(const FunctionDecl &decl, std::vector<Value> &&args, const SourceLocation &loc)
        {
            if (taskQueue_.size() >= limits::kMaxQueuedTasks)
                throwSizeLimit("async task queue", loc);
            taskQueue_.push_back(QueuedTask{&decl, std::move(args)});
        }

        Value evalCall(const FunctionCall &fc, Environment &env)
        {
            std::vector<Value> args;
            args.reserve(fc.args.size());
            for (auto &a : fc.args)
                args.push_back(evalExpr(*a, env));

            // User-defined functions are checked first: it's the hot path for
            // any recursive/heavily-called script function, and the same
            // lookup answers the async-deferral question.
            if (const FunctionDecl *user = findUser(fc.functionNameId))
            {
                if (user->isAsync)
                {
                    // Called without await: doesn't run now — see the
                    // class-level comment on taskQueue_ above.
                    queueTask(*user, std::move(args), fc.loc);
                    return Value::makeEmpty();
                }
                return callUserFunction(*user, args, fc.loc);
            }
            if (const NativeFn *native = findNative(fc))
                return (*native)(args, fc.loc);
            throwUndefinedFunction(fc.functionName, fc.loc);
        }

        // `await f(...)`: runs the call immediately. A user function must be
        // declared async; native/DLC functions aren't classified async/sync,
        // so awaiting one just calls it normally.
        CUFF_NOINLINE Value invokeAwaited(const FunctionCall &call, Environment &env, const SourceLocation &loc)
        {
            const FunctionDecl *user = findUser(call.functionNameId);
            if (user && !user->isAsync)
                throwAwaitOnNonAsync(call.functionName, loc);
            std::vector<Value> args;
            args.reserve(call.args.size());
            for (auto &a : call.args)
                args.push_back(evalExpr(*a, env));
            if (user)
                return callUserFunction(*user, args, loc);
            if (const NativeFn *native = findNative(call))
                return (*native)(args, loc);
            throwUndefinedFunction(call.functionName, loc);
        }

        Value callUserFunction(const FunctionDecl &decl, std::vector<Value> &args, const SourceLocation &loc)
        {
            if (args.size() != decl.params.size())
                throwArgumentCount(decl, args.size(), loc);
            if (callDepth_ >= kMaxCallDepth)
                throwCallDepth(decl, loc);
            tick(loc);

            FrameGuard guard(this, decl.isReturnable);

            Environment funcEnv(Environment::Kind::FunctionScope, globalEnv_);
            funcEnv.reserve(decl.params.size());
            for (size_t i = 0; i < decl.paramIds.size(); ++i)
                funcEnv.declare(decl.paramIds[i], std::move(args[i]), false);

            ExecOutcome outcome = execBlock(decl.body, funcEnv);
            if (outcome.result == ExecResult::Return)
                return std::move(outcome.returnValue);
            if (outcome.result == ExecResult::Stop)
                throw CuffRuntimeError(ErrorCode::StopOutsideLoop,
                                       "'stop' cannot be used outside of a loop", outcome.loc);
            return Value::makeEmpty();
        }

        // ---- Pattern-matching commands (docs/REGEX.md) ----

        std::shared_ptr<regex::CompiledPattern> compilePatternArg(const PatternArg &pat, Environment &env, const SourceLocation &loc)
        {
            if (pat.isLiteral)
                return regexEngine_.compile(pat.literalPattern, loc); // already validated at parse time; hits cache
            Value v = evalExpr(*pat.dynamicExpr, env);
            if (!v.isStr())
                throw TypeError("a pattern must be a str, got " + valueTypeName(v.type()), loc);
            return regexEngine_.compile(v.asStr(), loc); // may throw RegexSyntaxError here (dynamic pattern)
        }

        Value evalRegexMatch(const RegexMatchExpr &rm, Environment &env)
        {
            Value targetVal = evalExpr(*rm.target, env);
            if (!targetVal.isStr())
                throw TypeError("'is'/'IS' pattern matching requires a str on the left-hand side, got " + valueTypeName(targetVal.type()), rm.loc);
            auto compiled = regexEngine_.compile(rm.pattern, rm.loc);
            regex::MatchOutcome out;
            bool matched = regexEngine_.fullMatch(compiled, targetVal.asStr(), rm.caseInsensitive, rm.loc, out);
            return Value::makeBool(matched);
        }

        Value evalMatchFrom(const MatchFromExpr &m, Environment &env)
        {
            Value targetVal = evalExpr(*m.target, env);
            if (!targetVal.isStr())
                throw TypeError("'match ... from' requires a str target, got " + valueTypeName(targetVal.type()), m.loc);
            auto compiled = compilePatternArg(m.pattern, env, m.loc);
            regex::Flags flags = regex::Flags::parse(m.flags);
            regex::MatchOutcome out;
            if (!regexEngine_.search(compiled, targetVal.asStr(), 0, flags, m.loc, out))
                return Value::makeEmpty();

            auto mr = std::make_shared<MatchResult>();
            mr->matched = true;
            mr->wholeMatch = targetVal.asStr().substr(out.start, out.end - out.start);
            mr->positional = out.positional;
            mr->named = out.named;
            return Value::makeMatch(mr);
        }

        Value evalFind(const FindExpr &f, Environment &env)
        {
            auto compiled = compilePatternArg(f.pattern, env, f.loc);
            Value targetVal = evalExpr(*f.target, env);
            if (!targetVal.isStr())
                throw TypeError("'find ... from' requires a str target, got " + valueTypeName(targetVal.type()), f.loc);
            regex::Flags flags = regex::Flags::parse(f.flags);

            if (flags.global)
            {
                auto spans = regexEngine_.searchAllSpans(compiled, targetVal.asStr(), flags, f.loc);
                auto list = std::make_shared<ValueList>();
                list->items.reserve(spans.size());
                for (auto &m : spans)
                    list->items.push_back(Value::makeStr(targetVal.asStr().substr(m.first, m.second - m.first)));
                return Value::makeList(std::move(list));
            }
            regex::MatchOutcome out;
            if (!regexEngine_.search(compiled, targetVal.asStr(), 0, flags, f.loc, out))
                return Value::makeEmpty();
            return Value::makeStr(targetVal.asStr().substr(out.start, out.end - out.start));
        }

        Value evalPatternReplace(const PatternReplaceExpr &r, Environment &env)
        {
            auto compiled = compilePatternArg(r.pattern, env, r.loc);
            Value targetVal = evalExpr(*r.target, env);
            if (!targetVal.isStr())
                throw TypeError("'replace ... in' requires a str target, got " + valueTypeName(targetVal.type()), r.loc);
            Value replVal = evalExpr(*r.replacement, env);
            if (!replVal.isStr())
                throw TypeError("the replacement for 'replace' must be a str, got " + valueTypeName(replVal.type()), r.loc);
            regex::Flags flags = regex::Flags::parse(r.flags);
            return Value::makeStr(regexEngine_.replace(compiled, targetVal.asStr(), replVal.asStr(), flags, r.loc));
        }

        Value evalSplit(const SplitExpr &s, Environment &env)
        {
            Value targetVal = evalExpr(*s.target, env);
            if (!targetVal.isStr())
                throw TypeError("'split ... by' requires a str target, got " + valueTypeName(targetVal.type()), s.loc);
            auto compiled = compilePatternArg(s.pattern, env, s.loc);
            auto parts = regexEngine_.split(compiled, targetVal.asStr(), s.loc);
            auto list = std::make_shared<ValueList>();
            for (auto &p : parts)
                list->items.push_back(Value::makeStr(p));
            return Value::makeList(list);
        }

        Value evalCount(const CountExpr &c, Environment &env)
        {
            auto compiled = compilePatternArg(c.pattern, env, c.loc);
            Value targetVal = evalExpr(*c.target, env);
            if (!targetVal.isStr())
                throw TypeError("'count ... in' requires a str target, got " + valueTypeName(targetVal.type()), c.loc);
            regex::Flags flags = regex::Flags::parse(c.flags);
            size_t n = regexEngine_.count(compiled, targetVal.asStr(), flags, c.loc);
            return Value::makeNumber(static_cast<double>(n));
        }

        // ---- Modules (use / from / DLC) ----

        void execUse(const UseStmt &use, Environment &env)
        {
            if (use.isDLC)
            {
                registerDLC(use.name, natives_, use.loc);
                return;
            }
            loadCustomModule(use.name, use.path, use.loc, env);
        }

        void initModuleRoot()
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path root = config_.rootDir.empty() ? fs::path(scriptDir_) : fs::path(config_.rootDir);
            fs::path canon = fs::weakly_canonical(root, ec);
            moduleRoot_ = ec ? root.lexically_normal() : canon;
        }

        static bool isInsideRoot(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            auto r = root.begin();
            auto p = path.begin();
            for (; r != root.end(); ++r, ++p)
            {
                if (r->empty())
                    break;
                if (p == path.end() || *r != *p)
                    return false;
            }
            return true;
        }

        struct ImportMark
        {
            std::unordered_set<std::string> &set;
            std::string key;
            bool committed = false;
            ~ImportMark()
            {
                if (!committed)
                    set.erase(key);
            }
        };

        struct ImportDepthGuard
        {
            int &depth;
            explicit ImportDepthGuard(int &d) : depth(d) { ++depth; }
            ~ImportDepthGuard() { --depth; }
        };

        void loadCustomModule(const std::string &moduleName, const std::string &relPath, const SourceLocation &loc, Environment &env)
        {
            namespace fs = std::filesystem;
            const std::string shown = (relPath.empty() ? std::string() : relPath + "/") + moduleName + ".cuff";

            fs::path rel(relPath);
            if (rel.has_root_name() || rel.has_root_directory())
                throw ModuleError(ErrorCode::ModuleAccessDenied,
                                  "absolute module paths are not allowed ('use " + moduleName + " from " + relPath + "')", loc,
                                  "use a path relative to the running script, e.g. ./lib");

            fs::path full = fs::path(scriptDir_) / rel / (moduleName + ".cuff");
            std::error_code ec;
            fs::path canon = fs::weakly_canonical(full, ec);
            if (ec)
                canon = full.lexically_normal();

            if (!isInsideRoot(canon, moduleRoot_))
                throw ModuleError(ErrorCode::ModuleAccessDenied,
                                  "module '" + shown + "' resolves outside the allowed directory", loc,
                                  "keep imported modules inside the script's directory, or run with --root <dir> to widen it");

            const std::string key = canon.string();

            // Idempotent: a module already loaded (including the diamond- or
            // circular-import case) is treated as a no-op rather than
            // re-parsed/re-executed or flagged as an error.
            if (importedPaths_.count(key))
                return;

            if (moduleDepth_ >= limits::kMaxImportDepth)
                throw ModuleError(ErrorCode::ModuleLimitExceeded,
                                  "modules are imported too deeply (maximum is " + std::to_string(limits::kMaxImportDepth) + " levels)", loc);

            if (!fs::exists(canon, ec))
                throw ModuleError(ErrorCode::ModuleNotFound,
                                  "could not find module file '" + shown + "' for 'use " + moduleName + " from " + relPath + "'",
                                  loc, "paths are resolved relative to the running script's directory");
            if (!fs::is_regular_file(canon, ec))
                throw ModuleError(ErrorCode::ModuleAccessDenied, "module '" + shown + "' is not a regular file", loc);
            const auto size = fs::file_size(canon, ec);
            if (ec || size > limits::kMaxSourceBytes)
                throw ModuleError(ErrorCode::ModuleLimitExceeded,
                                  "module '" + shown + "' is larger than the " + std::to_string(limits::kMaxSourceBytes / 1024) + " KiB source limit", loc);

            std::ifstream file(canon, std::ios::binary);
            if (!file)
                throw ModuleError(ErrorCode::ModuleNotFound,
                                  "could not read module file '" + shown + "' for 'use " + moduleName + " from " + relPath + "'", loc);
            std::string source(static_cast<size_t>(size), '\0');
            file.read(source.data(), static_cast<std::streamsize>(size));
            source.resize(static_cast<size_t>(file.gcount()));

            importedPaths_.insert(key); // mark before parsing to make self-cycles a safe no-op too
            ImportMark mark{importedPaths_, key};

            std::unique_ptr<Program> modProgram;
            try
            {
                Tokenizer tokenizer(source);
                auto rawTokens = tokenizer.tokenize();
                Lexer lexer(std::move(rawTokens));
                auto tokens = lexer.lex();
                Parser parser(std::move(tokens));
                modProgram = parser.parse();
            }
            catch (const CuffError &e)
            {
                throw ModuleError(ErrorCode::ModuleParseFailed,
                                  "failed to parse module '" + shown + "': " + e.message, loc);
            }

            // The module's AST must outlive its functions, which stay
            // registered in userById_ even if the module body fails midway.
            loadedModules_.push_back(std::move(modProgram));
            const Program &program = *loadedModules_.back();

            // Execute the module's top level into its own environment, then
            // merge its top-level variables into the importer's current
            // scope. Function declarations are automatically visible to the
            // importer too, since userById_ is a single registry shared
            // by the whole interpreter (execProgram registers them there).
            ImportDepthGuard depthGuard(moduleDepth_);
            Environment moduleEnv;
            execProgram(program, moduleEnv);
            for (const auto &kv : moduleEnv.localVars())
                env.declare(kv.first, kv.second, moduleEnv.isConstantHere(kv.first));
            mark.committed = true;
        }
    };

} // namespace cuff
