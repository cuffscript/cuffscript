#pragma once

#include "Value.h"
#include "../common/Arithmetic.h"
#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/Utf8.h"
#include "Environment.h"
#include "Signals.h"
#include "NativeFunctions.h"
#include "ProgramError.h"
#include "../common/CuffError.h"
#include "../common/ErrorFormat.h"
#include "../common/SourceText.h"
#include "../common/SourceLocation.h"
#include "../common/PathSandbox.h"
#include "../parser/ASTNodes.h"
#include "../ParseProgram.h"
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

    class Interpreter : private ObjectDisplayHook
    {
    public:
        struct Config
        {
            std::string rootDir; // modules must resolve inside this directory (default: the script's directory)
            uint64_t maxSteps = 0;
            uint32_t timeoutMs = 0;
            size_t stackBudgetBytes = 0; // native stack the evaluator may use; 0 = derive from the real stack size
            bool networkEnabled = true;
            bool allowPrivateNetworkTargets = false; // let DLC:network reach loopback/private/link-local addresses (see SECURITY.md)
            bool filesystemEnabled = true;
        };

        Interpreter() { initialize(); }
        explicit Interpreter(Config config) : config_(std::move(config)) { initialize(); }

        Interpreter(const Interpreter &) = delete;
        Interpreter &operator=(const Interpreter &) = delete;

        // How much native stack the evaluator may use. `soft` is where evalExpr/execStatement give up with the
        // catchable E4-017; `hard` is the floor below which even leaf work such as regex matching refuses to
        // start, so something deep inside a program that is already near `soft` still fails with an error
        // rather than overflowing.
        struct StackPlan
        {
            size_t soft;
            size_t hard;
        };

        // `configured` is Config::stackBudgetBytes (0 = derive it); `available` is the stack the thread really
        // has below the caller (0 = unknown). A configured budget is honoured only as far as the thread can
        // back it: asking for more than exists must not turn a runaway recursion into a crash.
        static StackPlan planStack(size_t configured, size_t available)
        {
            constexpr size_t kMargin = 256 * 1024;
            constexpr size_t kLeafReserve = 768 * 1024;
            constexpr size_t kHardExtra = 3 * 1024 * 1024;

            StackPlan plan;
            plan.soft = std::min(configured ? configured : limits::kDefaultStackBudget, limits::kMaxStackBudget);
            plan.hard = plan.soft + kHardExtra;
            if (available)
            {
                const size_t usable = available > kMargin ? available - kMargin : available / 2;
                if (!configured || plan.hard > usable)
                    plan.hard = usable;
                plan.soft = std::min(configured ? plan.soft : limits::kMaxStackBudget,
                                     plan.hard - std::min(kLeafReserve, plan.hard / 3));
            }
            return plan;
        }


        void run(const Program &program, const std::string &scriptDir)
        {
            scriptDir_ = scriptDir.empty() ? std::string(".") : scriptDir;
            initModuleRoot();
            const uintptr_t sp = stackPointer();
            const StackPlan plan = planStack(config_.stackBudgetBytes, availableStackBytes());
            stackLimit_ = sp > plan.soft ? sp - plan.soft : 0;
            StackFloorScope floorScope(sp > plan.hard ? sp - plan.hard : 0);
            DisplayHookScope displayScope(this);
            ++runEpoch_;
            stepsLeft_ = config_.maxSteps ? config_.maxSteps : UINT64_MAX;
            timed_ = config_.timeoutMs != 0;
            if (timed_)
                deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.timeoutMs);
            try
            {
                execProgram(program, globalEnv_);
            }
            catch (...)
            {
                // An error ends the run: queued async jobs that have not started must not run later. A
                // one-shot interpreter is simply discarded, but cuffsh keeps one alive across entries.
                taskQueue_.clear();
                throw;
            }
        }

    private:
        Config config_;
        Environment globalEnv_;
        std::unordered_map<std::string, NativeFn> natives_;
        std::vector<const FunctionDecl *> userById_;
        static constexpr size_t kCallPoolCap = 256;
        std::vector<std::vector<Value>> argsPool_;
        std::vector<std::vector<std::pair<uint32_t, Value>>> envVarsPool_;

        CUFF_ALWAYS_INLINE std::vector<Value> takeArgsBuffer()
        {
            if (argsPool_.empty())
                return {};
            std::vector<Value> v = std::move(argsPool_.back());
            argsPool_.pop_back();
            v.clear();
            return v;
        }
        CUFF_ALWAYS_INLINE void returnArgsBuffer(std::vector<Value> &&v)
        {
            if (argsPool_.size() < kCallPoolCap)
            {
                v.clear();
                argsPool_.push_back(std::move(v));
            }
        }

        // The argument list of one call, borrowed from the pool for as long as the call needs it.
        struct PooledArgs
        {
            Interpreter *interp;
            std::vector<Value> v;
            explicit PooledArgs(Interpreter *i) : interp(i), v(i->takeArgsBuffer()) {}
            PooledArgs(const PooledArgs &) = delete;
            PooledArgs &operator=(const PooledArgs &) = delete;
            CUFF_ALWAYS_INLINE ~PooledArgs() { interp->returnArgsBuffer(std::move(v)); }
        };

        struct EnvPoolReturner
        {
            Interpreter *interp;
            Environment *env;
            ~EnvPoolReturner()
            {
                if (interp->envVarsPool_.size() < kCallPoolCap)
                    interp->envVarsPool_.push_back(env->releaseStorage());
            }
        };

        // While a run is in progress, printing an object (print, f-strings, to_str) may have to run its `to_str`
        // method, which only this interpreter can do.
        struct DisplayHookScope
        {
            ObjectDisplayHook *previous;
            explicit DisplayHookScope(ObjectDisplayHook *hook) : previous(activeObjectDisplay()) { activeObjectDisplay() = hook; }
            ~DisplayHookScope() { activeObjectDisplay() = previous; }
        };

        std::vector<const NativeFn *> nativeById_;
        std::vector<std::unique_ptr<Program>> loadedModules_;
        std::unordered_set<std::string> importedPaths_;
        std::string scriptDir_ = ".";
        std::filesystem::path moduleRoot_;
        int moduleDepth_ = 0;

        // ---- classes -------------------------------------------------------------------------------------------
        // The registry belongs to this interpreter (there is no process-wide class table), indexed by name id like
        // userById_. A class declared again by a later run (a shell entry) replaces the earlier one; objects that
        // already exist keep the class they were made from.
        std::vector<std::shared_ptr<ClassInfo>> classById_;
        std::vector<uint64_t> classEpoch_;
        uint64_t runEpoch_ = 0;
        size_t classCount_ = 0;
        std::shared_ptr<ClassInfo> errorClass_;
        std::unique_ptr<Program> preludeProgram_;
        uint32_t initId_ = 0, toStrId_ = 0;

        // The built-in methods of non-object values, by method name (see dlc/MethodTable.h).
        struct BoundMethod
        {
            const MethodEntry *entry;
            uint32_t functionId;
        };
        std::unordered_map<uint32_t, std::vector<BoundMethod>> builtinMethods_;
        size_t errMessageIdx_ = 0, errCodeIdx_ = 0, errLineIdx_ = 0, errColumnIdx_ = 0, errHintIdx_ = 0;

        struct QueuedTask
        {
            const FunctionDecl *decl;
            std::vector<Value> args;
            Value self;                  // the receiver of an async method
            const ClassInfo *owner = nullptr;
        };
        std::deque<QueuedTask> taskQueue_;

        int callDepth_ = 0;
        bool inFunctionBody_ = false;
        bool currentFunctionPure_ = false;
        const Value *currentSelf_ = nullptr;       // the receiver of the method that is running
        const ClassInfo *currentOwner_ = nullptr;  // the class that wrote the method that is running
        static constexpr int kMaxCallDepth = limits::kMaxCallDepth;

        // Deeply nested blocks use more stack per call than the depth cap assumes, so each step also checks the real
        // stack.
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


        struct FrameGuard
        {
            Interpreter *interp;
            bool prevInFunc;
            bool prevPure;
            const Value *prevSelf;
            const ClassInfo *prevOwner;
            FrameGuard(Interpreter *i, bool pure, const Value *self, const ClassInfo *owner) : interp(i)
            {
                prevInFunc = i->inFunctionBody_;
                prevPure = i->currentFunctionPure_;
                prevSelf = i->currentSelf_;
                prevOwner = i->currentOwner_;
                i->inFunctionBody_ = true;
                i->currentFunctionPure_ = pure;
                i->currentSelf_ = self;
                i->currentOwner_ = owner;
                ++i->callDepth_;
            }
            ~FrameGuard()
            {
                interp->inFunctionBody_ = prevInFunc;
                interp->currentFunctionPure_ = prevPure;
                interp->currentSelf_ = prevSelf;
                interp->currentOwner_ = prevOwner;
                --interp->callDepth_;
            }
        };

        // A field's default value is computed outside any method: no `self`, no private access, and no variables of
        // the code that happens to be creating the object.
        struct DefaultValueScope
        {
            Interpreter *interp;
            bool prevInFunc;
            const Value *prevSelf;
            const ClassInfo *prevOwner;
            explicit DefaultValueScope(Interpreter *i)
                : interp(i), prevInFunc(i->inFunctionBody_), prevSelf(i->currentSelf_), prevOwner(i->currentOwner_)
            {
                i->inFunctionBody_ = false;
                i->currentSelf_ = nullptr;
                i->currentOwner_ = nullptr;
            }
            ~DefaultValueScope()
            {
                interp->inFunctionBody_ = prevInFunc;
                interp->currentSelf_ = prevSelf;
                interp->currentOwner_ = prevOwner;
            }
        };

        // The object whose `init` is running may have its constant fields given their value.
        struct ConstructingScope
        {
            ObjectData &object;
            explicit ConstructingScope(ObjectData &o) : object(o) { object.constructing = true; }
            ~ConstructingScope() { object.constructing = false; }
        };

        template <typename Node>
        CUFF_ALWAYS_INLINE void guardStack(const Node &node)
        {
            if (stackPointer() < stackLimit_)
                stackExhausted(node);
        }

        [[noreturn]] CUFF_COLD void stackExhausted(const Expr &e) const
        {
            throwStackExhausted(std::visit([](const auto &n)
                                           { return n.loc; }, e.data));
        }
        [[noreturn]] CUFF_COLD void stackExhausted(const Stmt &s) const
        {
            throwStackExhausted(std::visit([](const auto &n)
                                           { return n.loc; }, s.data));
        }
        [[noreturn]] CUFF_COLD static void throwStackExhausted(const SourceLocation &loc)
        {
            throw StackOverflowError("native stack budget exhausted — calls and blocks are nested too deeply", loc);
        }

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


        void initialize()
        {
            registerBuiltins(natives_);
            initialIdsAndPrelude();
        }

        // ---- the built-in Error class ---------------------------------------------------------------------------
        // Written in the language itself and registered like any class of a program, so a program can build,
        // extend and raise it with nothing special in the evaluator but `fail` and `or_else as`.
        static const char *preludeSource()
        {
            return "class Error do:\n"
                   "    set str message to \"\"\n"
                   "    set str code to \"E7-001\"\n"
                   "    set number line to 0\n"
                   "    set number column to 0\n"
                   "    set str hint to \"\"\n"
                   "\n"
                   "    func init(str message to \"\") do:\n"
                   "        change self.message to message\n"
                   "    end\n"
                   "\n"
                   "    func to_str() do:\n"
                   "        return f\"[{self.code}] {self.message}\"\n"
                   "    end\n"
                   "end\n";
        }

        void initialIdsAndPrelude()
        {
            initId_ = internName("init");
            toStrId_ = internName("to_str");
            for (const MethodEntry &e : methodTable())
                builtinMethods_[internName(e.method)].push_back({&e, internName(e.function)});

            preludeProgram_ = parseProgram(makeSourceRef(std::string(preludeSource()), "<built-in>"));
            registerClasses(*preludeProgram_);
            errorClass_ = classById_[internName("Error")];
            errorClass_->isBuiltin = true;
            classCount_ = 0; // the limit is on the classes a program declares
            errMessageIdx_ = errorClass_->fieldIndex.at(internName("message"));
            errCodeIdx_ = errorClass_->fieldIndex.at(internName("code"));
            errLineIdx_ = errorClass_->fieldIndex.at(internName("line"));
            errColumnIdx_ = errorClass_->fieldIndex.at(internName("column"));
            errHintIdx_ = errorClass_->fieldIndex.at(internName("hint"));
        }

        ClassInfo *findClass(uint32_t id) const
        {
            return id < classById_.size() ? classById_[id].get() : nullptr;
        }

        const std::shared_ptr<ClassInfo> *classHandle(uint32_t id) const
        {
            if (id < classById_.size() && classById_[id])
                return &classById_[id];
            return nullptr;
        }

        [[noreturn]] CUFF_COLD static void throwClassError(ErrorCode code, const std::string &msg, const SourceLocation &loc,
                                                           const std::string &hint = "")
        {
            throw CuffRuntimeError(code, msg, loc, hint);
        }

        [[noreturn]] CUFF_COLD static void throwUnknownClass(const std::string &name, const SourceLocation &loc)
        {
            throwClassError(ErrorCode::UnknownClass, "unknown class '" + name + "'", loc,
                            "declare it with `class " + name + " do: ... end` at the top level of a file, and check the spelling");
        }

        // Declares every class of `program` before anything runs, so a class can be used above where it is written
        // and `extends` can name a class that comes later in the file.
        void registerClasses(const Program &program)
        {
            std::vector<const ClassDecl *> decls;
            for (const auto &s : program.statements)
                if (s->kind == StmtKind::ClassDecl)
                    decls.push_back(std::get_if<ClassDecl>(&s->data));
            if (decls.empty())
                return;

            size_t added = 0;
            for (const ClassDecl *d : decls)
                if (!classHandle(d->nameId))
                    ++added;
            if (classCount_ + added > limits::kMaxClasses)
                throwClassError(ErrorCode::SizeLimitExceeded,
                                "too many classes (a program can declare at most " + std::to_string(limits::kMaxClasses) + ")",
                                decls.front()->loc);

            std::unordered_map<uint32_t, size_t> inBatch;
            for (size_t i = 0; i < decls.size(); ++i)
            {
                const ClassDecl &d = *decls[i];
                checkClassName(d);
                if (!inBatch.emplace(d.nameId, i).second)
                    throwClassError(ErrorCode::ClassNameConflict, "class '" + d.name + "' is declared more than once", d.loc,
                                    "give each class its own name");
                checkClassNameFree(d);
            }

            std::vector<uint8_t> state(decls.size(), 0);
            for (size_t i = 0; i < decls.size(); ++i)
                buildClass(decls, inBatch, state, i);
        }

        void checkClassName(const ClassDecl &d) const
        {
            if (d.name.empty() || d.name[0] < 'A' || d.name[0] > 'Z')
                throwClassError(ErrorCode::InvalidClassName, "class name '" + d.name + "' must start with an uppercase letter", d.loc,
                                "for example: " + capitalizeAscii(d.name));
        }

        static std::string capitalizeAscii(std::string s)
        {
            if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
                s[0] = static_cast<char>(s[0] - 'a' + 'A');
            return s;
        }

        void checkClassNameFree(const ClassDecl &d) const
        {
            if (const ClassInfo *existing = findClass(d.nameId))
            {
                if (existing->isBuiltin)
                    throwClassError(ErrorCode::ClassNameConflict, "'" + d.name + "' is a built-in class and cannot be declared again",
                                    d.loc, "choose another name, or extend it: class My" + d.name + " extends " + d.name + " do: ... end");
                if (classEpoch_[d.nameId] == runEpoch_)
                    throwClassError(ErrorCode::ClassNameConflict, "class '" + d.name + "' is declared more than once", d.loc,
                                    "give each class its own name");
            }
            if (findUser(d.nameId) || natives_.count(d.name))
                throwClassError(ErrorCode::ClassNameConflict,
                                "class '" + d.name + "' has the same name as a function", d.loc,
                                "classes and functions share one namespace; rename one of them");
        }

        void buildClass(const std::vector<const ClassDecl *> &decls, const std::unordered_map<uint32_t, size_t> &inBatch,
                        std::vector<uint8_t> &state, size_t index)
        {
            if (state[index] == 2)
                return;
            const ClassDecl &d = *decls[index];
            if (state[index] == 1)
                throwClassError(ErrorCode::InvalidInheritance,
                                "class '" + d.name + "' inherits from itself through its ancestors", d.loc,
                                "follow the 'extends' chain: it must end at a class that has no parent");
            state[index] = 1;

            std::shared_ptr<ClassInfo> parent;
            if (!d.parentName.empty())
            {
                if (d.parentNameId == d.nameId)
                    throwClassError(ErrorCode::InvalidInheritance, "class '" + d.name + "' cannot extend itself", d.parentLoc);
                auto it = inBatch.find(d.parentNameId);
                if (it != inBatch.end())
                {
                    buildClass(decls, inBatch, state, it->second);
                    parent = classById_[d.parentNameId];
                }
                else if (const auto *known = classHandle(d.parentNameId))
                {
                    parent = *known;
                }
                else if (findUser(d.parentNameId) || natives_.count(d.parentName))
                {
                    throwClassError(ErrorCode::InvalidInheritance, "'" + d.parentName + "' is a function, not a class", d.parentLoc,
                                    "only a class can be extended");
                }
                else
                {
                    throwUnknownClass(d.parentName, d.parentLoc);
                }
            }

            auto info = std::make_shared<ClassInfo>();
            info->name = d.name;
            info->nameId = d.nameId;
            info->parent = parent;
            info->depth = parent ? parent->depth + 1 : 0;
            if (info->depth > limits::kMaxInheritanceDepth)
                throwClassError(ErrorCode::SizeLimitExceeded,
                                "class '" + d.name + "' is nested too deeply (a class may have at most " +
                                    std::to_string(limits::kMaxInheritanceDepth) + " ancestors)",
                                d.loc);
            if (parent)
            {
                info->fields = parent->fields;
                info->fieldIndex = parent->fieldIndex;
                info->methods = parent->methods;
            }

            std::unordered_set<uint32_t> own;
            for (const FieldDecl &f : d.fields)
            {
                if (own.count(f.nameId))
                    throwClassError(ErrorCode::DuplicateMember, "'" + f.name + "' is declared twice in class '" + d.name + "'", f.loc);
                if (const auto *inherited = info->findField(f.nameId))
                    throwClassError(ErrorCode::DuplicateMember,
                                    "field '" + f.name + "' is already declared by parent class '" + inherited->declaredIn->name + "'",
                                    f.loc, "a child class inherits its parent's fields; use that one");
                if (f.isConstant && !isValidConstantName(f.name))
                    throw ConstantError(ErrorCode::InvalidConstantName,
                                        "constant name '" + f.name + "' must be written in ALL CAPS (uppercase letters, digits, underscores only)",
                                        f.loc, "for example: " + upperAscii(f.name));
                own.insert(f.nameId);
                ClassInfo::Field field;
                field.name = f.name;
                field.nameId = f.nameId;
                field.isPrivate = f.isPrivate;
                field.isConstant = f.isConstant;
                field.declaredIn = info.get();
                field.decl = &f;
                info->fieldIndex[f.nameId] = static_cast<uint32_t>(info->fields.size());
                info->fields.push_back(std::move(field));
            }
            for (const auto &m : d.methods)
            {
                const FunctionDecl &decl = *std::get_if<FunctionDecl>(&m->data);
                if (own.count(decl.nameId))
                    throwClassError(ErrorCode::DuplicateMember, "'" + decl.name + "' is declared twice in class '" + d.name + "'", decl.loc);
                if (info->findField(decl.nameId))
                    throwClassError(ErrorCode::DuplicateMember,
                                    "method '" + decl.name + "' has the same name as a field of class '" + d.name + "'", decl.loc,
                                    "a field and a method cannot share a name");
                own.insert(decl.nameId);
                info->methods[decl.nameId] = ClassInfo::Method{&decl, info.get()};
            }
            if (info->fields.size() + info->methods.size() > limits::kMaxClassMembers)
                throwClassError(ErrorCode::SizeLimitExceeded,
                                "class '" + d.name + "' has too many members (the most a class can have, counting inherited ones, is " +
                                    std::to_string(limits::kMaxClassMembers) + ")",
                                d.loc);
            info->init = info->findMethod(initId_);
            info->toStr = info->findMethod(toStrId_);

            if (d.nameId >= classById_.size())
            {
                classById_.resize(d.nameId + 1);
                classEpoch_.resize(d.nameId + 1, 0);
            }
            if (!classById_[d.nameId])
                ++classCount_;
            classById_[d.nameId] = std::move(info);
            classEpoch_[d.nameId] = runEpoch_;
            state[index] = 2;
        }


        template <typename T>
        static const T &stmtAs(const Stmt &s) { return *std::get_if<T>(&s.data); }
        template <typename T>
        static const T &exprAs(const Expr &e) { return *std::get_if<T>(&e.data); }

        void execProgram(const Program &program, Environment &env)
        {
            for (auto &s : program.statements)
                if (s->kind == StmtKind::FunctionDecl)
                    registerFunction(stmtAs<FunctionDecl>(*s));
            registerClasses(program);

            for (auto &s : program.statements)
            {
                if (s->kind == StmtKind::FunctionDecl || s->kind == StmtKind::ClassDecl)
                    continue;

                ExecOutcome outcome = execStatement(*s, env);
                if (outcome.result != ExecResult::Normal)
                    throwLoopControlOutside(outcome);
            }

            // Un-awaited async calls run FIFO after top-level synchronous execution ends.
            drainTaskQueue();
        }

        // `return`, `stop` or `skip` that reached the top of a program or a function without a function or loop to
        // take it.
        [[noreturn]] CUFF_COLD static void throwLoopControlOutside(const ExecOutcome &outcome)
        {
            if (outcome.result == ExecResult::Return)
                throw CuffRuntimeError(ErrorCode::ReturnOutsideFunction,
                                       "'return' cannot be used outside of a function", outcome.loc);
            const char *word = outcome.result == ExecResult::Skip ? "skip" : "stop";
            throw CuffRuntimeError(ErrorCode::StopOutsideLoop,
                                   std::string("'") + word + "' cannot be used outside of a loop", outcome.loc);
        }

        void drainTaskQueue()
        {
            while (!taskQueue_.empty())
            {
                QueuedTask task = std::move(taskQueue_.front());
                taskQueue_.pop_front();
                callUserFunction(*task.decl, task.args, task.decl->loc, task.decl->isMethod ? &task.self : nullptr, task.owner);
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
            if (findClass(decl.nameId))
                throwClassError(ErrorCode::ClassNameConflict, "function '" + decl.name + "' has the same name as a class", decl.loc,
                                "classes and functions share one namespace; rename one of them");
            if (decl.nameId >= userById_.size())
                userById_.resize(decl.nameId + 1, nullptr);
            userById_[decl.nameId] = &decl;
        }

        const FunctionDecl *findUser(uint32_t id) const
        {
            return id < userById_.size() ? userById_[id] : nullptr;
        }

        const NativeFn *findNative(const std::string &name, uint32_t id)
        {
            if (id < nativeById_.size() && nativeById_[id])
                return nativeById_[id];
            auto it = natives_.find(name);
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

        [[noreturn]] CUFF_COLD static void throwReturnValueOutsideFunction(const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::UnsupportedOperation,
                                   "'return' with a value can only be used inside a function",
                                   loc, "at the top level, show a value with print(...)");
        }

        ExecOutcome execStatement(const Stmt &stmt, Environment &env)
        {
            guardStack(stmt);
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
                execDeclaration(stmtAs<DeclarationStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::Change:
                execChange(stmtAs<ChangeStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::Global:
                execGlobal(stmtAs<GlobalStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::FunctionDecl:
            {
                const auto &decl = stmtAs<FunctionDecl>(stmt);
                if (inFunctionBody_)
                    throwNestedFunction(decl);
                registerFunction(decl);
                return ExecOutcome::normal();
            }
            case StmtKind::ClassDecl:
                return ExecOutcome::normal(); // declared when its program started
            case StmtKind::IfStmt:
                return execIf(stmtAs<IfStmt>(stmt), env);
            case StmtKind::LoopStmt:
                return execLoop(stmtAs<LoopStmt>(stmt), env);
            case StmtKind::WithStmt:
                return execWith(stmtAs<WithStmt>(stmt), env);
            case StmtKind::StopStmt:
                return ExecOutcome::makeStop(stmtAs<StopStmt>(stmt).loc);
            case StmtKind::SkipStmt:
                return ExecOutcome::makeSkip(stmtAs<SkipStmt>(stmt).loc);
            case StmtKind::ReturnStmt:
            {
                const auto &r = stmtAs<ReturnStmt>(stmt);
                if (r.value && !inFunctionBody_)
                    throwReturnValueOutsideFunction(r.loc);
                Value v = r.value ? evalExpr(*r.value, env) : Value::makeEmpty();
                return ExecOutcome::makeReturn(std::move(v), r.loc);
            }
            case StmtKind::FailStmt:
                execFail(stmtAs<FailStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::AwaitStmt:
            {
                const auto &aw = stmtAs<AwaitStmt>(stmt);
                invokeAwaited(*aw.expr->call, env, aw.loc);
                return ExecOutcome::normal();
            }
            case StmtKind::UseStmt:
                execUse(stmtAs<UseStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::ExprStmt:
                evalExpr(*stmtAs<ExprStmt>(stmt).expr, env);
                return ExecOutcome::normal();
            case StmtKind::CollectionOp:
                execCollectionOp(stmtAs<CollectionOpStmt>(stmt), env);
                return ExecOutcome::normal();
            case StmtKind::OrElse:
                return execOrElse(stmtAs<OrElseStmt>(stmt), env);
            }
            throw InternalEngineError("unhandled statement kind");
        }

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


        static std::string typeNameOf(const Value &v)
        {
            return v.isObject() ? v.asObject()->cls->name : valueTypeName(v.type());
        }

        [[noreturn]] CUFF_COLD static void throwDeclarationMismatch(const std::string &typeName, const Value &v, const std::string &name,
                                                                    const SourceLocation &loc, const char *what)
        {
            throw CuffRuntimeError(ErrorCode::DeclarationTypeMismatch,
                                   "cannot assign a " + typeNameOf(v) + " value to " + typeName + " " + what + " '" + name + "'",
                                   loc, "the declared type (" + typeName + ") and the assigned value's type must match at 'set'");
        }

        // Whether a value that is not `empty` satisfies a declared type. A class accepts its own objects and those of
        // its subclasses.
        bool typeAccepts(const TypeSpec &type, const Value &v, const SourceLocation &loc)
        {
            switch (type.kind)
            {
            case TypeKind::Any:
                return true;
            case TypeKind::Empty:
                return false; // a non-empty value can never satisfy `empty`
            case TypeKind::Number:
                return v.isNumber();
            case TypeKind::Str:
                return v.isStr();
            case TypeKind::Boolean:
                return v.isBool();
            case TypeKind::List:
                return v.isList();
            case TypeKind::Map:
                return v.isMap();
            case TypeKind::Match:
                return v.isMatch();
            case TypeKind::Pattern:
                return v.isPattern();
            case TypeKind::Object:
                return v.isObject();
            case TypeKind::Class:
            {
                const ClassInfo *cls = findClass(type.classNameId);
                if (!cls)
                    throwUnknownClass(type.className, loc);
                return v.isObject() && v.asObject()->cls->isSubclassOf(cls);
            }
            }
            return true;
        }

        void checkDeclaredType(const TypeSpec &type, const Value &v, const std::string &name, const SourceLocation &loc,
                               const char *what = "variable")
        {
            if (type.kind == TypeKind::Any)
                return;
            if (type.kind == TypeKind::Class && !findClass(type.classNameId))
                throwUnknownClass(type.className, loc);
            if (v.isEmpty())
                return;
            if (!typeAccepts(type, v, loc))
                throwDeclarationMismatch(type.name(), v, name, loc, what);
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

            checkDeclaredType(decl.type, v, decl.name, decl.loc);
            if (decl.isConstant && v.isList())
                v = freezeList(v);
            env.declare(decl.nameId, std::move(v), decl.isConstant);
        }

        void execGlobal(const GlobalStmt &g, Environment &env)
        {
            if (!inFunctionBody_)
            {
                throw CuffRuntimeError(ErrorCode::InvalidGlobalDeclaration,
                                       "'global' can only be used inside a function body",
                                       g.loc, "at the top level, every variable is already global");
            }
            if (currentFunctionPure_)
                throwPureGlobalAccess(g.names.front().name, g.names.front().loc);
            for (const auto &n : g.names)
                env.declareGlobal(n.nameId);
        }

        static std::string upperAscii(const std::string &s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                           { return std::toupper(c); });
            return out;
        }

        // Freeze a shallow copy: only its top-level slots are immutable; nested containers keep their own mutability.
        static Value freezeList(const Value &v)
        {
            auto frozen = std::make_shared<ValueList>();
            frozen->items = v.asList()->items;
            frozen->isConstant = true;
            return Value::makeList(std::move(frozen));
        }

        [[noreturn]] CUFF_COLD static void throwConstantListMutation(const SourceLocation &loc)
        {
            throw ConstantError(ErrorCode::ConstantReassignment,
                                "cannot modify a constant list — it was declared with 'constant' and its contents are read-only",
                                loc, "assigning it to another variable does not unfreeze it; build a new list instead, e.g. 'change x to y + []'");
        }

        [[noreturn]] CUFF_COLD void throwPureGlobalAccess(const std::string &name, const SourceLocation &loc) const
        {
            throw CuffRuntimeError(ErrorCode::PureFunctionGlobalAccess,
                                   "cannot access global variable '" + name + "' from inside a pure function", loc,
                                   "remove the 'pure' keyword to allow this");
        }


        [[noreturn]] CUFF_COLD void throwPureFieldWrite(const SourceLocation &loc) const
        {
            throw CuffRuntimeError(ErrorCode::PureFunctionFieldWrite,
                                   "a pure function cannot change a field", loc,
                                   "remove the 'pure' keyword to allow this");
        }

        // ---- objects: members, privacy, with ------------------------------------------------------------------------

        [[noreturn]] CUFF_COLD static void throwNotAnObject(const std::string &what, const Value &v, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::NotAnObject,
                                   "cannot " + what + " a " + typeNameOf(v) + " value: it is not an object", loc,
                                   "only objects made from a class have fields and methods");
        }

        // The public members of a class, for the hint of an unknown-member error.
        static std::string publicMembers(const ClassInfo &cls)
        {
            std::string fields, methods;
            for (const auto &f : cls.fields)
                if (!f.isPrivate)
                    fields += (fields.empty() ? "" : ", ") + f.name;
            std::vector<std::string> names;
            for (const auto &kv : cls.methods)
                if (!kv.second.decl->isPrivate && kv.second.decl->name != "init")
                    names.push_back(kv.second.decl->name);
            std::sort(names.begin(), names.end());
            for (const auto &n : names)
                methods += (methods.empty() ? "" : ", ") + n + "()";
            std::string out;
            if (!fields.empty())
                out += "fields: " + fields;
            if (!methods.empty())
                out += std::string(out.empty() ? "" : "; ") + "methods: " + methods;
            return out.empty() ? "the class has no public members" : out;
        }

        // `obj.name` where the class has no such field (or the name is a method).
        [[noreturn]] CUFF_COLD static void throwNoField(const ClassInfo &cls, const std::string &name, uint32_t id,
                                                        const SourceLocation &loc)
        {
            if (cls.findMethod(id))
                throw CuffRuntimeError(ErrorCode::MethodNotCalled,
                                       "'" + name + "' is a method of '" + cls.name + "', not a field", loc,
                                       "call it with parentheses: " + name + "(...)");
            throw CuffRuntimeError(ErrorCode::UnknownMember, "class '" + cls.name + "' has no field '" + name + "'", loc,
                                   publicMembers(cls));
        }

        // `obj.name(...)` where the class has no such method (or the name is a field).
        [[noreturn]] CUFF_COLD static void throwNoMethod(const ClassInfo &cls, const std::string &name, uint32_t id,
                                                         const SourceLocation &loc)
        {
            if (cls.findField(id))
                throw CuffRuntimeError(ErrorCode::MethodNotCalled,
                                       "'" + name + "' is a field of '" + cls.name + "', not a method", loc,
                                       "read it without parentheses: obj." + name);
            throw CuffRuntimeError(ErrorCode::UnknownMember, "class '" + cls.name + "' has no method '" + name + "'", loc,
                                   publicMembers(cls));
        }

        [[noreturn]] CUFF_COLD static void throwPrivate(const ClassInfo &declaredIn, const std::string &name, const char *kind,
                                                        const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::PrivateMemberAccess,
                                   std::string("'") + name + "' is a private " + kind + " of '" + declaredIn.name + "'", loc,
                                   "it can only be used inside the methods of " + declaredIn.name + " itself");
        }

        void checkFieldAccess(const ClassInfo::Field &f, const SourceLocation &loc) const
        {
            if (f.isPrivate && currentOwner_ != f.declaredIn)
                throwPrivate(*f.declaredIn, f.name, "field", loc);
        }

        const Value &selfValue(const SourceLocation &loc) const
        {
            if (!currentSelf_)
                throw InternalEngineError("'self' used outside a method", loc);
            return *currentSelf_;
        }

        // The object of the innermost `with` block of the running function.
        const Value &withObjectOf(Environment &env, const SourceLocation &loc) const
        {
            for (Environment *e = &env; e; e = e->parentScope())
            {
                if (e->hasWithObject())
                    return e->withObject();
                if (e->isFunctionScope())
                    break;
            }
            throw InternalEngineError("'.name' used outside a 'with' block", loc);
        }

        // The `with` object, innermost first, whose class has a method called `id`.
        const Value *findWithMethodHolder(Environment &env, uint32_t id) const
        {
            for (Environment *e = &env; e; e = e->parentScope())
            {
                if (e->hasWithObject() && e->withObject().asObject()->cls->findMethod(id))
                    return &e->withObject();
                if (e->isFunctionScope())
                    break;
            }
            return nullptr;
        }

        // A name written inside a `with` block: each scope is searched from the inside out, its own variables first and then
        // the members of its object, and the search ends with the ordinary rules.
        struct Located
        {
            Value *var = nullptr;
            Environment *owner = nullptr;
            Value holder;                // a member of a `with` object: the object
            uint32_t fieldIndex = 0;
            bool isMember = false;
        };

        Located locateWith(uint32_t id, Environment &env)
        {
            Located found;
            for (Environment *e = &env; e; e = e->parentScope())
            {
                if (Value *v = e->localValue(id))
                {
                    found.var = v;
                    found.owner = e;
                    return found;
                }
                if (e->hasWithObject())
                {
                    const ObjectData &o = *e->withObject().asObject();
                    auto it = o.cls->fieldIndex.find(id);
                    if (it != o.cls->fieldIndex.end())
                    {
                        found.holder = e->withObject();
                        found.fieldIndex = it->second;
                        found.isMember = true;
                        return found;
                    }
                }
                if (e->isFunctionScope())
                {
                    Environment::Lookup look = e->resolve(id);
                    found.var = look.value;
                    found.owner = look.owner;
                    return found;
                }
            }
            return found;
        }

        Value readField(const ObjectData &o, size_t index, const SourceLocation &loc) const
        {
            checkFieldAccess(o.cls->fields[index], loc);
            return o.fields[index];
        }

        size_t fieldIndexOrThrow(const ObjectData &o, uint32_t id, const std::string &name, const SourceLocation &loc) const
        {
            auto it = o.cls->fieldIndex.find(id);
            if (it == o.cls->fieldIndex.end())
                throwNoField(*o.cls, name, id, loc);
            return it->second;
        }

        ObjectData &expectObject(const Value &v, const std::string &name, const SourceLocation &loc) const
        {
            if (!v.isObject())
                throwNotAnObject("read '." + name + "' of", v, loc);
            return *v.asObject();
        }

        void writeField(ObjectData &o, size_t index, Value v, const SourceLocation &loc) const
        {
            const ClassInfo::Field &f = o.cls->fields[index];
            checkFieldAccess(f, loc);
            if (f.isConstant)
            {
                if (!o.constructing)
                    throw ConstantError(ErrorCode::ConstantReassignment,
                                        "cannot change constant field '" + f.name + "'", loc,
                                        "a constant field gets its value while the object is being made, and keeps it");
                if (v.isList())
                    v = freezeList(v);
            }
            o.fields[index] = std::move(v);
        }

        // ---- places: where `change`, `add` and `remove` write -------------------------------------------------------

        struct PlaceOp
        {
            bool isIndex;
            const Value *index;
            uint32_t nameId;
            const std::string *name;
            SourceLocation loc;
        };

        // Where a place starts: a variable, or an object whose field comes first.
        struct PlaceRoot
        {
            Value *var = nullptr;
            Environment *owner = nullptr;
            bool constant = false;
            Value object;            // `self`, a `.name`, or a member found through `with`
            bool synthetic = false;  // the first field is the place's own name, not one of its steps
        };

        // The steps of a place in order: a leading field when the place starts at a member, then its own steps.
        struct OpIter
        {
            const Place &place;
            bool synthetic;
            const std::vector<Value> &indexes;
            size_t total;
            size_t pos = 0;
            size_t indexPos = 0;

            OpIter(const Place &p, bool synth, const std::vector<Value> &idx)
                : place(p), synthetic(synth), indexes(idx), total(p.steps.size() + (synth ? 1 : 0)) {}

            PlaceOp next()
            {
                if (synthetic && pos == 0)
                {
                    ++pos;
                    return PlaceOp{false, nullptr, place.nameId, &place.name, place.loc};
                }
                const PlaceStep &s = place.steps[pos - (synthetic ? 1 : 0)];
                ++pos;
                if (s.isIndex)
                    return PlaceOp{true, &indexes[indexPos++], 0, nullptr, s.loc};
                return PlaceOp{false, nullptr, s.nameId, &s.name, s.loc};
            }
        };

        enum class PlaceUse
        {
            Change,
            Collection
        };

        PlaceRoot resolveRoot(const Place &pl, Environment &env, PlaceUse use, const SourceLocation &loc)
        {
            PlaceRoot r;
            switch (pl.root)
            {
            case Place::Root::Self:
                r.object = selfValue(loc);
                return r;
            case Place::Root::With:
                r.object = withObjectOf(env, loc);
                r.synthetic = true;
                return r;
            case Place::Root::Name:
                break;
            }

            if (pl.inWith)
            {
                Located l = locateWith(pl.nameId, env);
                if (l.isMember)
                {
                    r.object = std::move(l.holder);
                    r.synthetic = true;
                    return r;
                }
                r.var = l.var;
                r.owner = l.owner;
            }
            else
            {
                Environment::Lookup look = env.resolve(pl.nameId);
                r.var = look.value;
                r.owner = look.owner;
            }
            if (!r.var)
            {
                if (use == PlaceUse::Change)
                    throwUnresolved(pl.nameId, pl.name, "cannot change undefined variable '" + pl.name + "'", loc,
                                    "declare it first with 'set', or bridge a global with 'global " + pl.name + "'");
                throwUnresolved(pl.nameId, pl.name, "undefined collection '" + pl.name + "'", loc);
            }
            r.constant = env.isConstantIn(r.owner, pl.nameId);
            return r;
        }

        static bool touchesField(const Place &pl, const PlaceRoot &root)
        {
            if (!root.var)
                return true;
            for (const auto &s : pl.steps)
                if (!s.isIndex)
                    return true;
            return false;
        }

        std::vector<Value> evalPlaceIndexes(const Place &pl, Environment &env)
        {
            std::vector<Value> out;
            size_t n = 0;
            for (const auto &s : pl.steps)
                if (s.isIndex)
                    ++n;
            out.reserve(n);
            for (const auto &s : pl.steps)
                if (s.isIndex)
                    out.push_back(evalExpr(*s.index, env));
            return out;
        }

        // The value a place has reached so far, and whether a constant slot was crossed on the way: a constant field
        // that holds something other than a list protects whatever lies below it.
        struct Walk
        {
            Value current;
            bool barrier = false;
            const std::string *barrierName = nullptr;
        };

        void readOp(Walk &w, const PlaceOp &op)
        {
            if (op.isIndex)
            {
                w.current = indexInto(w.current, *op.index, op.loc);
                return;
            }
            ObjectData &o = expectObject(w.current, *op.name, op.loc);
            const size_t index = fieldIndexOrThrow(o, op.nameId, *op.name, op.loc);
            const ClassInfo::Field &f = o.cls->fields[index];
            checkFieldAccess(f, op.loc);
            Value next = o.fields[index];
            if (f.isConstant && !o.constructing && !next.isList())
            {
                w.barrier = true;
                w.barrierName = &f.name;
            }
            w.current = std::move(next);
        }

        [[noreturn]] CUFF_COLD static void throwConstantBarrier(const std::string &name, const char *what, const SourceLocation &loc)
        {
            throw ConstantError(ErrorCode::ConstantReassignment, std::string("cannot ") + what + " '" + name + "' — it is constant", loc);
        }

        // Stores `newVal` where the last step of a place points.
        void writeLast(Walk &w, const PlaceOp &op, Value newVal)
        {
            if (!op.isIndex)
            {
                ObjectData &o = expectObject(w.current, *op.name, op.loc);
                const size_t index = fieldIndexOrThrow(o, op.nameId, *op.name, op.loc);
                if (w.barrier)
                    throwConstantBarrier(*w.barrierName, "change", op.loc);
                writeField(o, index, std::move(newVal), op.loc);
                return;
            }

            if (w.barrier)
                throwConstantBarrier(*w.barrierName, "change", op.loc);
            const Value &container = w.current;
            if (container.isList())
            {
                if (container.asList()->isConstant)
                    throwConstantListMutation(op.loc);
                if (!op.index->isNumber())
                    throw TypeError("list index must be a number (1-based)", op.loc);
                const size_t real = resolveIndex1Based(expectWholeNumber(op.index->asNumber(), "list index", op.loc),
                                                       container.asList()->items.size(), op.loc);
                container.asList()->items[real] = std::move(newVal);
                return;
            }
            if (container.isMap())
            {
                if (!op.index->isStr())
                    throw TypeError("map keys are strings — cannot assign with a " + valueTypeName(op.index->type()), op.loc);
                ValueMap &map = *container.asMap();
                if (map.size() >= limits::kMaxCollectionItems && !map.has(op.index->asStr()))
                    throwSizeLimit("collection", op.loc);
                map.set(op.index->asStr(), std::move(newVal));
                return;
            }
            throw TypeError("cannot index-assign into a " + typeNameOf(container) + " value", op.loc);
        }

        CUFF_ALWAYS_INLINE void execChange(const ChangeStmt &c, Environment &env)
        {
            const Place &pl = c.target;

            // The common case, a plain variable, needs no place walk. A missing or constant variable falls through
            // to the general path, which reports it.
            if (pl.steps.empty() && pl.root == Place::Root::Name && !pl.inWith)
            {
                const Environment::Lookup look = env.resolve(pl.nameId);
                if (look.value && !env.isConstantIn(look.owner, pl.nameId))
                {
                    Value v = evalExpr(*c.value, env);
                    *look.value = std::move(v);
                    return;
                }
            }
            execChangePlace(c, env);
        }

        CUFF_NOINLINE void execChangePlace(const ChangeStmt &c, Environment &env)
        {
            const Place &pl = c.target;
            PlaceRoot root = resolveRoot(pl, env, PlaceUse::Change, c.loc);

            if (root.var)
            {
                if (pl.steps.empty())
                {
                    if (root.constant)
                        throw ConstantError(ErrorCode::ConstantReassignment, "cannot change constant '" + pl.name + "'", c.loc);
                    // Evaluating an expression never adds a variable to the scope it runs in, so the slot found
                    // above is still where the variable lives.
                    Value v = evalExpr(*c.value, env);
                    *root.var = std::move(v);
                    return;
                }
                if (root.constant && !root.var->isList())
                    throw ConstantError(ErrorCode::ConstantReassignment, "cannot change constant '" + pl.name + "'", c.loc);
            }
            if (currentFunctionPure_ && touchesField(pl, root))
                throwPureFieldWrite(c.loc);

            std::vector<Value> indexes = evalPlaceIndexes(pl, env);
            Value newVal = evalExpr(*c.value, env);

            Walk w;
            w.current = root.var ? *root.var : std::move(root.object);
            OpIter ops(pl, root.synthetic, indexes);
            for (size_t k = 0; k + 1 < ops.total; ++k)
                readOp(w, ops.next());
            writeLast(w, ops.next(), std::move(newVal));
        }

        void execCollectionOp(const CollectionOpStmt &co, Environment &env)
        {
            const Place &pl = co.target;
            PlaceRoot root = resolveRoot(pl, env, PlaceUse::Collection, co.loc);
            if (root.var && pl.steps.empty() && root.constant)
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot modify constant collection '" + pl.name + "'", co.loc);
            if (currentFunctionPure_ && touchesField(pl, root))
                throwPureFieldWrite(co.loc);

            std::vector<Value> indexes = evalPlaceIndexes(pl, env);
            Value operand = evalExpr(*co.value, env);

            Walk w;
            w.current = root.var ? *root.var : std::move(root.object);
            OpIter ops(pl, root.synthetic, indexes);
            while (ops.pos < ops.total)
                readOp(w, ops.next());
            if (w.barrier)
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot modify constant collection '" + *w.barrierName + "'", co.loc);
            Value &target = w.current;

            if (co.opKind == CollectionOpStmt::OpKind::Add)
            {
                if (!target.isList())
                    throw TypeError("'add ... to' requires a list, got " + typeNameOf(target), co.loc);
                if (target.asList()->isConstant)
                    throwConstantListMutation(co.loc);
                checkCollectionSize(target.asList()->items.size() + 1, co.loc);
                target.asList()->items.push_back(std::move(operand));
                return;
            }

            if (target.isList())
            {
                if (target.asList()->isConstant)
                    throwConstantListMutation(co.loc);
                auto &items = target.asList()->items;
                if (operand.isNumber())
                {
                    size_t real = resolveIndex1Based(expectWholeNumber(operand.asNumber(), "list index", co.loc), items.size(), co.loc);
                    items.erase(items.begin() + static_cast<long>(real));
                }
                else
                {
                    auto it = std::find_if(items.begin(), items.end(), [&](const Value &v)
                                           { return valuesEqual(v, operand, co.loc); });
                    if (it == items.end())
                        throw ElementNotFoundError("value not found in list — nothing to remove", co.loc);
                    items.erase(it);
                }
            }
            else if (target.isMap())
            {
                if (!operand.isStr())
                    throw TypeError("map keys are strings; cannot remove using a " + typeNameOf(operand), co.loc);
                target.asMap()->remove(operand.asStr());
            }
            else
            {
                throw TypeError("'add' and 'remove' require a list or map, got " + typeNameOf(target), co.loc);
            }
        }


        void assignLoopVar(const LoopStmt &loop, Value v, Environment &env)
        {
            if (!env.declareLoopVar(loop.repeatVarId, std::move(v)))
            {
                throw ConstantError(ErrorCode::ConstantReassignment,
                                    "cannot use constant '" + loop.repeatVar + "' as a loop variable", loop.loc);
            }
        }

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

        // Runs one pass of a loop body. Returns true when the loop is over (`stop`, or `return`), with what the loop
        // gives back in `result`; `skip` only ends the pass.
        CUFF_ALWAYS_INLINE bool runLoopBody(const LoopStmt &loop, Environment &env, ExecOutcome &result)
        {
            ExecOutcome outcome = execBlock(loop.body, env);
            if (outcome.result == ExecResult::Normal || outcome.result == ExecResult::Skip)
                return false;
            result = outcome.result == ExecResult::Stop ? ExecOutcome::normal() : std::move(outcome);
            return true;
        }

        ExecOutcome execLoop(const LoopStmt &loop, Environment &env)
        {
            ExecOutcome result;
            switch (loop.kind)
            {
            case LoopStmt::LoopKind::Repeat:
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
                        if (runLoopBody(loop, env, result))
                            return result;
                    }
                }
                else
                {
                    for (long long i = start; i >= end; --i)
                    {
                        tick(loop.loc);
                        assignLoopVar(loop, Value::makeNumber(static_cast<double>(i)), env);
                        if (runLoopBody(loop, env, result))
                            return result;
                    }
                }
                break;
            }
            case LoopStmt::LoopKind::While:
                while (evalExpr(*loop.condition, env).truthy())
                {
                    tick(loop.loc);
                    if (runLoopBody(loop, env, result))
                        return result;
                }
                break;
            case LoopStmt::LoopKind::Each:
                return execLoopEach(loop, env);
            }
            return ExecOutcome::normal();
        }

        // `loop each`: a list gives its items, a map its keys (in insertion order) and a str its characters. The loop
        // visits what the collection held when it started, so changing it from inside the body is safe.
        ExecOutcome execLoopEach(const LoopStmt &loop, Environment &env)
        {
            Value collection = evalExpr(*loop.collection, env);
            ExecOutcome result;
            if (collection.isList())
            {
                std::vector<Value> items = collection.asList()->items;
                for (Value &item : items)
                {
                    tick(loop.loc);
                    assignLoopVar(loop, std::move(item), env);
                    if (runLoopBody(loop, env, result))
                        return result;
                }
            }
            else if (collection.isMap())
            {
                std::vector<std::string> keys = collection.asMap()->keys();
                for (std::string &key : keys)
                {
                    tick(loop.loc);
                    assignLoopVar(loop, Value::makeStr(std::move(key)), env);
                    if (runLoopBody(loop, env, result))
                        return result;
                }
            }
            else if (collection.isStr())
            {
                const StrData &sd = collection.asStrData();
                const size_t n = sd.codepoints();
                for (size_t i = 0; i < n; ++i)
                {
                    tick(loop.loc);
                    Value ch = sd.ascii() ? Value::makeChar(static_cast<unsigned char>(sd.text()[i]))
                                          : Value::makeStr(sd.text().substr(sd.offsetOf(i), sd.offsetOf(i + 1) - sd.offsetOf(i)));
                    assignLoopVar(loop, std::move(ch), env);
                    if (runLoopBody(loop, env, result))
                        return result;
                }
            }
            else
            {
                throw TypeError("'loop each' needs a list, map or str, got " + typeNameOf(collection), loop.loc,
                                "use 'loop repeat' to count through a range of numbers");
            }
            return ExecOutcome::normal();
        }

        // `with obj do: ... end`: the body is a scope of its own that also sees the members of `obj`.
        ExecOutcome execWith(const WithStmt &w, Environment &env)
        {
            Value object = evalExpr(*w.object, env);
            if (!object.isObject())
                throw CuffRuntimeError(ErrorCode::NotAnObject,
                                       "'with' needs an object, got a " + typeNameOf(object) + " value", w.loc,
                                       "only objects made from a class can be used with 'with'");
            Environment withEnv(Environment::Kind::BlockScope, env);
            withEnv.setWithObject(std::move(object));
            return execBlock(w.body, withEnv);
        }

        // ---- errors as objects ---------------------------------------------------------------------------------------

        static std::string displayText(const Value &v) { return v.isStr() ? v.asStr() : v.toDisplayString(); }

        // The Error object a handler receives for an error that was raised while the program ran. An error the program
        // raised itself with `fail` is handed back as the very object that was raised.
        Value makeErrorValue(const CuffError &e)
        {
            if (const auto *raised = dynamic_cast<const ProgramError *>(&e))
                return raised->payload;
            auto obj = std::make_shared<ObjectData>();
            obj->cls = errorClass_;
            obj->fields.resize(errorClass_->fields.size());
            obj->fields[errMessageIdx_] = Value::makeStr(e.message);
            obj->fields[errCodeIdx_] = Value::makeStr(errorCodeTag(e.code));
            obj->fields[errLineIdx_] = Value::makeNumber(e.location.line);
            obj->fields[errColumnIdx_] = Value::makeNumber(e.location.column);
            obj->fields[errHintIdx_] = Value::makeStr(e.hint);
            obj->origin = std::make_shared<CuffError>(e);
            return Value::makeObject(std::move(obj));
        }

        Value newErrorValue(const std::string &message)
        {
            auto obj = std::make_shared<ObjectData>();
            obj->cls = errorClass_;
            obj->fields.resize(errorClass_->fields.size());
            obj->fields[errMessageIdx_] = Value::makeStr(message);
            obj->fields[errCodeIdx_] = Value::makeStr("E7-001");
            obj->fields[errLineIdx_] = Value::makeNumber(0);
            obj->fields[errColumnIdx_] = Value::makeNumber(0);
            obj->fields[errHintIdx_] = Value::makeStr(std::string());
            return Value::makeObject(std::move(obj));
        }

        void execFail(const FailStmt &f, Environment &env)
        {
            Value v = evalExpr(*f.value, env);
            Value error;
            if (v.isStr())
                error = newErrorValue(v.asStr());
            else if (v.isObject() && v.asObject()->cls->isSubclassOf(errorClass_.get()))
                error = std::move(v);
            else
                throw TypeError("'fail' needs an Error or a str, got " + typeNameOf(v), f.loc,
                                "write fail \"message\", or fail Error(\"message\")");

            ObjectData &o = *error.asObject();
            const std::string message = displayText(o.fields[errMessageIdx_]);
            const std::string hint = displayText(o.fields[errHintIdx_]);
            const std::string code = displayText(o.fields[errCodeIdx_]);

            // An error that was caught and is raised again unchanged is the original error.
            if (o.origin)
            {
                const CuffError &original = *static_cast<const CuffError *>(o.origin.get());
                if (original.message == message && original.hint == hint && errorCodeTag(original.code) == code)
                    throw CuffError(original);
            }

            // An error that has not been raised before records where it was.
            const Value &line = o.fields[errLineIdx_];
            if (!line.isNumber() || line.asNumber() == 0)
            {
                o.fields[errLineIdx_] = Value::makeNumber(f.loc.line);
                o.fields[errColumnIdx_] = Value::makeNumber(f.loc.column);
            }
            const std::string shown = o.cls.get() == errorClass_.get() ? message : o.cls->name + ": " + message;
            throw ProgramError(shown, f.loc, std::move(error), hint);
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

                // If a declaration threw before declare ran, bind the name to `empty` so the fallback can `change` it.
                if (oe.primaryStmt->kind == StmtKind::Declaration)
                {
                    const auto &decl = stmtAs<DeclarationStmt>(*oe.primaryStmt);
                    if (!env.isDeclaredHere(decl.nameId))
                        env.declare(decl.nameId, Value::makeEmpty(), false);
                }

                Environment blockEnv(Environment::Kind::BlockScope, env);
                if (!oe.errorName.empty())
                    blockEnv.declare(oe.errorNameId, makeErrorValue(e), false);
                return execBlock(oe.fallbackBody, blockEnv);
            }
        }

        // A global that isn't bridged into the running function resolves to nothing; say why instead of "undefined".
        [[noreturn]] CUFF_COLD void throwUnresolved(uint32_t nameId, const std::string &name, const std::string &message,
                                                    const SourceLocation &loc, const std::string &hint = "") const
        {
            if (inFunctionBody_ && globalEnv_.isDeclaredHere(nameId))
            {
                if (currentFunctionPure_)
                    throwPureGlobalAccess(name, loc);
                throw UndefinedVariableError("global variable '" + name + "' is not visible inside a function unless it is bridged",
                                             loc, "add 'global " + name + "' before using it");
            }
            throw UndefinedVariableError(message, loc, hint);
        }

        [[noreturn]] CUFF_COLD static void throwUndefinedFunction(const std::string &name, const SourceLocation &loc)
        {
            throw UndefinedFunctionError("undefined function '" + name + "'", loc,
                                         "check the spelling, or make sure it's declared before this point");
        }

        [[noreturn]] CUFF_COLD static void throwNotCallable(const std::string &name, const Value &v, const SourceLocation &loc)
        {
            throw UndefinedFunctionError("'" + name + "' is a " + typeNameOf(v) + ", not a function",
                                         loc, "did you mean to call a different name, or use '" + name + "' as a value instead?");
        }

        [[noreturn]] CUFF_COLD static void throwDivisionByZero(const SourceLocation &loc)
        {
            throw DivisionByZeroError("division by zero", loc);
        }

        [[noreturn]] CUFF_COLD static void throwModuloByZero(const SourceLocation &loc)
        {
            throw DivisionByZeroError("modulo by zero", loc);
        }

        static long long expectWholeNumber(double d, const char *what, const SourceLocation &loc)
        {
            constexpr double kExactLimit = 9007199254740992.0;
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


        Value evalExpr(const Expr &expr, Environment &env)
        {
            guardStack(expr);
            switch (expr.kind)
            {
            case ExprKind::Number:
                return Value::makeNumber(exprAs<NumberLiteral>(expr).value);
            case ExprKind::String:
                return Value::makeStr(exprAs<StringLiteral>(expr).shared);
            case ExprKind::Bool:
                return Value::makeBool(exprAs<BoolLiteral>(expr).value);
            case ExprKind::Empty:
                return Value::makeEmpty();
            case ExprKind::Pattern:
                return Value::makePattern(exprAs<PatternLiteral>(expr).value);
            case ExprKind::Identifier:
            {
                const auto &id = exprAs<IdentifierExpr>(expr);
                if (id.inWith)
                    return evalWithIdentifier(id, env);
                auto look = env.resolve(id.nameId);
                if (!look.value)
                    throwUnresolved(id.nameId, id.name, "undefined variable '" + id.name + "'", id.loc);
                return *look.value;
            }
            case ExprKind::Self:
                return selfValue(exprAs<SelfExpr>(expr).loc);
            case ExprKind::List:
                return evalList(exprAs<ListLiteral>(expr), env);
            case ExprKind::Map:
                return evalMap(exprAs<MapLiteral>(expr), env);
            case ExprKind::FString:
                return evalFString(exprAs<FStringExpr>(expr), env);
            case ExprKind::BinaryOp:
                if (expr.height > kSpineHeight)
                    return evalSpine(expr, env);
                return evalBinaryOp(exprAs<BinaryOp>(expr), env);
            case ExprKind::UnaryOp:
                return evalUnaryOp(exprAs<UnaryOp>(expr), env);
            case ExprKind::IndexAccess:
                if (expr.height > kSpineHeight)
                    return evalSpine(expr, env);
                return evalIndexAccess(exprAs<IndexAccess>(expr), env);
            case ExprKind::SliceAccess:
                if (expr.height > kSpineHeight)
                    return evalSpine(expr, env);
                return evalSliceAccess(exprAs<SliceAccess>(expr), env);
            case ExprKind::FunctionCall:
                return evalCall(exprAs<FunctionCall>(expr), env);
            case ExprKind::Member:
                if (expr.height > kSpineHeight && spineOperand(expr))
                    return evalSpine(expr, env);
                return evalMember(exprAs<MemberAccess>(expr), env);
            case ExprKind::MethodCall:
                if (expr.height > kSpineHeight && spineOperand(expr))
                    return evalSpine(expr, env);
                return evalMethodCall(exprAs<MethodCall>(expr), env, false);
            case ExprKind::SuperCall:
                return evalSuperCall(exprAs<SuperCall>(expr), env, false);
            case ExprKind::Isa:
                if (expr.height > kSpineHeight)
                    return evalSpine(expr, env);
                return evalIsa(exprAs<IsaExpr>(expr), env);
            case ExprKind::Await:
            {
                const auto &aw = exprAs<AwaitExpr>(expr);
                return invokeAwaited(*aw.call, env, aw.loc);
            }
            }
            throw InternalEngineError("unhandled expression kind");
        }

        CUFF_NOINLINE Value evalWithIdentifier(const IdentifierExpr &id, Environment &env)
        {
            Located l = locateWith(id.nameId, env);
            if (l.isMember)
                return readField(*l.holder.asObject(), l.fieldIndex, id.loc);
            if (!l.var)
                throwUnresolved(id.nameId, id.name, "undefined variable '" + id.name + "'", id.loc);
            return *l.var;
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


        // Chains of binary operators, indexing, slicing and member access (a + b + c + ..., x[1][2][3]..., a.b.c.d...)
        // lean to the left: each node's first operand is the next node of the chain. Recursing down such a chain would
        // use stack in proportion to its length, so a long one the parser accepts could still fail to run. Above this
        // height the chain is walked iteratively instead (see evalSpine); below it the recursive form is
        // cheaper, since it needs no list of nodes.
        static constexpr uint32_t kSpineHeight = 48;

        static const Expr *spineOperand(const Expr &e)
        {
            switch (e.kind)
            {
            case ExprKind::BinaryOp:
                return std::get_if<BinaryOp>(&e.data)->left.get();
            case ExprKind::IndexAccess:
                return std::get_if<IndexAccess>(&e.data)->target.get();
            case ExprKind::SliceAccess:
                return std::get_if<SliceAccess>(&e.data)->target.get();
            case ExprKind::Member:
                return std::get_if<MemberAccess>(&e.data)->target.get();
            case ExprKind::MethodCall:
                return std::get_if<MethodCall>(&e.data)->target.get();
            case ExprKind::Isa:
                return std::get_if<IsaExpr>(&e.data)->target.get();
            default:
                return nullptr;
            }
        }

        // Evaluates the chain from its innermost operand outwards. Operands are evaluated in the same
        // left-to-right order as the recursive form, so results, errors and their locations are identical.
        CUFF_NOINLINE Value evalSpine(const Expr &top, Environment &env)
        {
            std::vector<const Expr *> chain;
            const Expr *node = &top;
            for (;;)
            {
                chain.push_back(node);
                const Expr *next = spineOperand(*node);
                if (!next || spineOperand(*next) == nullptr)
                {
                    // `next` is the chain's first operand (or `node` has none); either way the walk ends here.
                    break;
                }
                node = next;
            }
            Value acc = evalExpr(*spineOperand(*chain.back()), env);
            for (size_t k = chain.size(); k-- > 0;)
            {
                const Expr &e = *chain[k];
                switch (e.kind)
                {
                case ExprKind::BinaryOp:
                {
                    const BinaryOp &b = *std::get_if<BinaryOp>(&e.data);
                    if (isLogical(b.op))
                    {
                        acc = evalLogical(b, acc, env);
                        break;
                    }
                    Value r = evalExpr(*b.right, env);
                    acc = applyBinaryOp(b, acc, r);
                    break;
                }
                case ExprKind::IndexAccess:
                {
                    const IndexAccess &idx = *std::get_if<IndexAccess>(&e.data);
                    Value i = evalExpr(*idx.index, env);
                    acc = indexInto(acc, i, idx.loc);
                    break;
                }
                case ExprKind::SliceAccess:
                {
                    const SliceAccess &sl = *std::get_if<SliceAccess>(&e.data);
                    Value s = evalExpr(*sl.start, env);
                    Value en = evalExpr(*sl.end, env);
                    acc = sliceInto(acc, s, en, sl.loc);
                    break;
                }
                case ExprKind::Member:
                    acc = memberOf(acc, *std::get_if<MemberAccess>(&e.data));
                    break;
                case ExprKind::MethodCall:
                    acc = callMethodOn(std::move(acc), *std::get_if<MethodCall>(&e.data), env, false);
                    break;
                default:
                    acc = Value::makeBool(isaCheck(acc, *std::get_if<IsaExpr>(&e.data)));
                    break;
                }
            }
            return acc;
        }

        CUFF_ALWAYS_INLINE Value evalBinaryOp(const BinaryOp &b, Environment &env)
        {
            Value l = evalExpr(*b.left, env);
            if (isLogical(b.op))
                return evalLogical(b, l, env);
            Value r = evalExpr(*b.right, env);
            return applyBinaryOp(b, l, r);
        }

        static bool isLogical(BinOp op) { return op == BinOp::And || op == BinOp::Or; }

        // `and` / `or` take any value as an operand and give a boolean. The right operand is evaluated only when the
        // left one does not already decide the result (false for `and`, true for `or`).
        Value evalLogical(const BinaryOp &b, const Value &left, Environment &env)
        {
            const bool l = left.truthy();
            if (l == (b.op == BinOp::Or))
                return Value::makeBool(l);
            return Value::makeBool(evalExpr(*b.right, env).truthy());
        }

        CUFF_ALWAYS_INLINE Value applyBinaryOp(const BinaryOp &b, const Value &l, const Value &r)
        {
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
                case BinOp::Mod:
                    return Value::makeNumber(moduloOf(x, y, b.loc));
                case BinOp::Pow:
                    return Value::makeNumber(powerOf(x, y, b.loc));
                case BinOp::And:
                case BinOp::Or:
                case BinOp::Matches:
                    break;
                case BinOp::Is:
                    return Value::makeBool(x == y);
                case BinOp::IsNot:
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
                throw TypeError("cannot add " + typeNameOf(l) + " and " + typeNameOf(r), b.loc,
                                "'+' works on number+number, str+str, or list+list");

            case BinOp::Sub:
                throw TypeError("'-' requires two numbers, got " + typeNameOf(l) + " and " + typeNameOf(r), b.loc);
            case BinOp::Mul:
                throw TypeError("'*' requires two numbers, got " + typeNameOf(l) + " and " + typeNameOf(r), b.loc);
            case BinOp::Div:
                throw TypeError("'/' requires two numbers, got " + typeNameOf(l) + " and " + typeNameOf(r), b.loc);
            case BinOp::Mod:
                throw TypeError("'%' requires two numbers, got " + typeNameOf(l) + " and " + typeNameOf(r), b.loc);
            case BinOp::Pow:
                throw TypeError("'**' requires two numbers, got " + typeNameOf(l) + " and " + typeNameOf(r), b.loc);
            case BinOp::And:
            case BinOp::Or:
                break;

            case BinOp::Is:
                return Value::makeBool(valuesEqual(l, r, b.loc));
            case BinOp::IsNot:
                return Value::makeBool(!valuesEqual(l, r, b.loc));
            case BinOp::Matches:
                return matchesOp(b, l, r);

            case BinOp::Greater:
            case BinOp::Less:
            case BinOp::GreaterEq:
            case BinOp::LessEq:
                return compareOrdered(b.op, l, r, b.loc);
            }
            throw InternalEngineError("unknown binary operator");
        }

        // `text matches p"..."`: does the pattern match the whole text?
        static Value matchesOp(const BinaryOp &b, const Value &l, const Value &r)
        {
            if (!l.isStr())
                throw TypeError("'matches' needs a str on the left, got " + typeNameOf(l), b.loc);
            if (!r.isPattern())
                throw TypeError("'matches' needs a pattern on the right, got " + typeNameOf(r), b.loc,
                                "write the pattern as p\"...\", or build one from a str with pattern(text)");
            const PatternData &pat = *r.asPattern();
            regex::MatchOutcome out;
            return Value::makeBool(regex::RegexEngine::fullMatch(pat.compiled, l.asStr(), pat.flags(), b.loc, out));
        }

        CUFF_NOINLINE static double moduloOf(double x, double y, const SourceLocation &loc)
        {
            if (y == 0.0)
                throwModuloByZero(loc);
            const double r = arith::floorMod(x, y);
            if (!std::isfinite(r))
                throw ValueError("'%' result is not a finite number", loc);
            return r;
        }

        CUFF_NOINLINE static double powerOf(double x, double y, const SourceLocation &loc)
        {
            double r = 0.0;
            switch (arith::power(x, y, r))
            {
            case arith::PowStatus::ZeroToNegative:
                throw DivisionByZeroError("'**' cannot raise 0 to a negative power (division by zero)", loc);
            case arith::PowStatus::NegativeToFraction:
                throw ValueError("'**' cannot raise a negative number to a fractional power", loc);
            case arith::PowStatus::TooLarge:
                throw ValueError("'**' result is too large to represent", loc);
            case arith::PowStatus::Ok:
                break;
            }
            return r;
        }

        static Value compareOrdered(BinOp op, const Value &l, const Value &r, const SourceLocation &loc)
        {
            if (l.isNumber() && r.isNumber())
            {
                double a = l.asNumber(), c = r.asNumber();
                switch (op)
                {
                case BinOp::Greater:
                    return Value::makeBool(a > c);
                case BinOp::Less:
                    return Value::makeBool(a < c);
                case BinOp::GreaterEq:
                    return Value::makeBool(a >= c);
                default:
                    return Value::makeBool(a <= c);
                }
            }
            if (l.isStr() && r.isStr())
            {
                const std::string &a = l.asStr();
                const std::string &c = r.asStr();
                switch (op)
                {
                case BinOp::Greater:
                    return Value::makeBool(a > c);
                case BinOp::Less:
                    return Value::makeBool(a < c);
                case BinOp::GreaterEq:
                    return Value::makeBool(a >= c);
                default:
                    return Value::makeBool(a <= c);
                }
            }
            throw TypeError(std::string("cannot compare ") + valueTypeName(l.type()) + " and " +
                                valueTypeName(r.type()) + " with '" + binOpName(op) + "'",
                            loc, "comparisons work on two numbers or two strings");
        }


        // ---- objects in expressions ----------------------------------------------------------------------------------

        Value memberOf(const Value &target, const MemberAccess &ma)
        {
            if (!target.isObject())
                throwNotAnObject("read '." + ma.name + "' of", target, ma.loc);
            const ObjectData &o = *target.asObject();
            return readField(o, fieldIndexOrThrow(o, ma.nameId, ma.name, ma.loc), ma.loc);
        }

        CUFF_NOINLINE Value evalMember(const MemberAccess &ma, Environment &env)
        {
            if (!ma.target)
                return memberOf(withObjectOf(env, ma.loc), ma);
            if (ma.target->kind == ExprKind::Self)
                return memberOf(selfValue(ma.loc), ma);
            return memberOf(evalExpr(*ma.target, env), ma);
        }

        bool isaCheck(const Value &v, const IsaExpr &e)
        {
            const ClassInfo *cls = findClass(e.classNameId);
            if (!cls)
                throwUnknownClass(e.className, e.loc);
            return v.isObject() && v.asObject()->cls->isSubclassOf(cls);
        }

        CUFF_NOINLINE Value evalIsa(const IsaExpr &e, Environment &env)
        {
            Value v = evalExpr(*e.target, env);
            return Value::makeBool(isaCheck(v, e));
        }

        [[noreturn]] CUFF_COLD static void throwAwaitOnNonAsync(const std::string &name, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::AwaitOnNonAsync,
                                   "'await' can only be used with an async function; '" + name + "' is not declared async", loc,
                                   "declare it with 'async func " + name + "(...) do:', or call it without 'await'");
        }

        static std::string arityText(const FunctionDecl &decl)
        {
            if (decl.requiredParams == decl.params.size())
                return std::to_string(decl.params.size());
            return std::to_string(decl.requiredParams) + "-" + std::to_string(decl.params.size());
        }

        [[noreturn]] CUFF_COLD static void throwArgumentCount(const std::string &name, const FunctionDecl &decl, size_t got,
                                                              const SourceLocation &loc)
        {
            throw ArgumentError(name + "() expects " + arityText(decl) + " argument(s), got " + std::to_string(got), loc);
        }

        static void checkArity(const std::string &name, const FunctionDecl &decl, size_t got, const SourceLocation &loc)
        {
            if (got < decl.requiredParams || got > decl.params.size())
                throwArgumentCount(name, decl, got, loc);
        }

        [[noreturn]] CUFF_COLD static void throwParamTypeMismatch(const std::string &name, const FunctionDecl &decl, size_t i,
                                                                  const Value &v, const SourceLocation &loc)
        {
            const std::string typeName = decl.paramTypes[i].name();
            throw CuffRuntimeError(ErrorCode::ParameterTypeMismatch,
                                   name + "(): parameter '" + decl.params[i] + "' expects " + typeName +
                                       ", got " + typeNameOf(v),
                                   loc, "declared as '" + typeName + " " + decl.params[i] + "' in the parameter list");
        }

        // Only called for functions with a typed parameter, so untyped calls pay nothing.
        void checkParamTypes(const std::string &name, const FunctionDecl &decl, const std::vector<Value> &args,
                             const SourceLocation &loc)
        {
            const size_t n = std::min(args.size(), decl.paramTypes.size());
            for (size_t i = 0; i < n; ++i)
            {
                const Value &v = args[i];
                if (v.isEmpty())
                    continue;
                if (!typeAccepts(decl.paramTypes[i], v, loc))
                    throwParamTypeMismatch(name, decl, i, v, loc);
            }
        }

        [[noreturn]] CUFF_COLD static void throwCallDepth(const FunctionDecl &decl, const SourceLocation &loc)
        {
            throw StackOverflowError("maximum call depth (" + std::to_string(kMaxCallDepth) +
                                         ") exceeded while calling '" + decl.name + "' — check for infinite recursion",
                                     loc);
        }

        void queueTask(const FunctionDecl &decl, std::vector<Value> &&args, const SourceLocation &loc,
                       const Value *self, const ClassInfo *owner)
        {
            if (taskQueue_.size() >= limits::kMaxQueuedTasks)
                throwSizeLimit("async task queue", loc);
            QueuedTask task;
            task.decl = &decl;
            task.args = std::move(args);
            if (self)
                task.self = *self;
            task.owner = owner;
            taskQueue_.push_back(std::move(task));
        }

        // A pure function may call only itself, pure functions and natives; any other helper would bypass `pure`.
        [[noreturn]] CUFF_COLD static void throwPureImpureCall(const std::string &calleeName, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::PureFunctionImpureCall,
                                   "a pure function cannot call non-pure function '" + calleeName + "'", loc,
                                   "mark '" + calleeName + "' as pure, or remove 'pure' from the caller");
        }

        void checkPureCallAllowed(bool calleeIsPure, const std::string &calleeName, const SourceLocation &loc)
        {
            if (currentFunctionPure_ && !calleeIsPure)
                throwPureImpureCall(calleeName, loc);
        }

        // ---- calls ---------------------------------------------------------------------------------------------------

        CUFF_NOINLINE Value evalCall(const FunctionCall &fc, Environment &env)
        {
            PooledArgs pooled(this);
            std::vector<Value> &args = pooled.v;
            args.reserve(fc.args.size());
            for (auto &a : fc.args)
                args.push_back(evalExpr(*a, env));

            if (fc.inWith)
            {
                if (const Value *holder = findWithMethodHolder(env, fc.functionNameId))
                {
                    Value receiver = *holder;
                    return dispatchMethod(receiver, fc.functionNameId, fc.functionName, args, fc.loc, false);
                }
            }

            // User functions are looked up first: it's the hot path, and it answers the async question.
            if (const FunctionDecl *user = findUser(fc.functionNameId))
            {
                checkPureCallAllowed(user->isPure, fc.functionName, fc.loc);
                if (user->hasTypedParams)
                    checkParamTypes(fc.functionName, *user, args, fc.loc);
                if (user->isAsync)
                {
                    checkArity(fc.functionName, *user, args.size(), fc.loc);
                    queueTask(*user, std::move(args), fc.loc, nullptr, nullptr);
                    return Value::makeEmpty();
                }
                return callUserFunction(*user, args, fc.loc);
            }
            if (const std::shared_ptr<ClassInfo> *cls = classHandle(fc.functionNameId))
                return instantiate(*cls, args, fc.loc);
            if (const NativeFn *native = findNative(fc.functionName, fc.functionNameId))
                return (*native)(args, fc.loc);
            if (Environment::Lookup notFn = env.resolve(fc.functionNameId); notFn.value)
                throwNotCallable(fc.functionName, *notFn.value, fc.loc);
            throwUndefinedFunction(fc.functionName, fc.loc);
        }

        // `await call`: the call runs now, and its value is the result.
        CUFF_NOINLINE Value invokeAwaited(const Expr &call, Environment &env, const SourceLocation &loc)
        {
            switch (call.kind)
            {
            case ExprKind::MethodCall:
                return evalMethodCall(exprAs<MethodCall>(call), env, true);
            case ExprKind::SuperCall:
                return evalSuperCall(exprAs<SuperCall>(call), env, true);
            case ExprKind::FunctionCall:
                return invokeAwaitedFunction(exprAs<FunctionCall>(call), env, loc);
            default:
                throw InternalEngineError("'await' needs a call", loc);
            }
        }

        Value invokeAwaitedFunction(const FunctionCall &call, Environment &env, const SourceLocation &loc)
        {
            const Value *withHolder = call.inWith ? findWithMethodHolder(env, call.functionNameId) : nullptr;
            if (withHolder)
            {
                Value receiver = *withHolder;
                const ClassInfo::Method *m = receiver.asObject()->cls->findMethod(call.functionNameId);
                if (!m->decl->isAsync)
                    throwAwaitOnNonAsync(call.functionName, loc);
                PooledArgs pooled(this);
                for (auto &a : call.args)
                    pooled.v.push_back(evalExpr(*a, env));
                return dispatchMethod(receiver, call.functionNameId, call.functionName, pooled.v, loc, true);
            }
            const FunctionDecl *user = findUser(call.functionNameId);
            if (user && !user->isAsync)
                throwAwaitOnNonAsync(call.functionName, loc);
            if (!user && classHandle(call.functionNameId))
                throwAwaitOnNonAsync(call.functionName, loc);
            if (user)
                checkPureCallAllowed(user->isPure, call.functionName, loc);
            PooledArgs pooled(this);
            std::vector<Value> &args = pooled.v;
            args.reserve(call.args.size());
            for (auto &a : call.args)
                args.push_back(evalExpr(*a, env));
            if (user)
            {
                if (user->hasTypedParams)
                    checkParamTypes(call.functionName, *user, args, loc);
                return callUserFunction(*user, args, loc);
            }
            if (const NativeFn *native = findNative(call.functionName, call.functionNameId))
                return (*native)(args, loc);
            if (Environment::Lookup notFn = env.resolve(call.functionNameId); notFn.value)
                throwNotCallable(call.functionName, *notFn.value, loc);
            throwUndefinedFunction(call.functionName, loc);
        }

        // ---- method calls --------------------------------------------------------------------------------------------

        CUFF_NOINLINE Value evalMethodCall(const MethodCall &mc, Environment &env, bool awaited)
        {
            Value receiver;
            if (!mc.target)
                receiver = withObjectOf(env, mc.loc);
            else if (mc.target->kind == ExprKind::Self)
                receiver = selfValue(mc.loc);
            else
                receiver = evalExpr(*mc.target, env);
            return callMethodOn(std::move(receiver), mc, env, awaited);
        }

        // The call part of `receiver.name(args)`, once the receiver is known.
        Value callMethodOn(Value receiver, const MethodCall &mc, Environment &env, bool awaited)
        {
            if (awaited)
            {
                const ClassInfo::Method *m = receiver.isObject() ? receiver.asObject()->cls->findMethod(mc.nameId) : nullptr;
                if (!m || !m->decl->isAsync)
                    throwAwaitOnNonAsync(mc.name, mc.loc);
            }
            PooledArgs pooled(this);
            std::vector<Value> &args = pooled.v;
            args.reserve(mc.args.size());
            for (auto &a : mc.args)
                args.push_back(evalExpr(*a, env));
            return dispatchMethod(receiver, mc.nameId, mc.name, args, mc.loc, awaited);
        }

        CUFF_NOINLINE Value evalSuperCall(const SuperCall &sc, Environment &env, bool awaited)
        {
            if (!currentOwner_ || !currentOwner_->parent || !currentSelf_)
                throw InternalEngineError("'super' used outside a method of a class with a parent", sc.loc);
            const ClassInfo &parent = *currentOwner_->parent;
            const ClassInfo::Method *m = parent.findMethod(sc.nameId);
            if (m && awaited && !m->decl->isAsync)
                throwAwaitOnNonAsync(sc.name, sc.loc);
            PooledArgs pooled(this);
            std::vector<Value> &args = pooled.v;
            args.reserve(sc.args.size());
            for (auto &a : sc.args)
                args.push_back(evalExpr(*a, env));
            if (!m)
                throwNoMethod(parent, sc.name, sc.nameId, sc.loc);
            if (m->decl->isPrivate && m->owner != currentOwner_)
                throwPrivate(*m->owner, sc.name, "method", sc.loc);
            Value receiver = *currentSelf_;
            return invokeMethod(receiver, *m, args, sc.name, sc.loc, awaited);
        }

        // Calls method `nameId` of `receiver`: a method of its class when it is an object, otherwise (or when the
        // class has no such method) a built-in one.
        Value dispatchMethod(const Value &receiver, uint32_t nameId, const std::string &name, std::vector<Value> &args,
                             const SourceLocation &loc, bool awaited)
        {
            const ClassInfo *cls = nullptr;
            if (receiver.isObject())
            {
                cls = receiver.asObject()->cls.get();
                if (const ClassInfo::Method *m = cls->findMethod(nameId))
                {
                    if (m->decl->isPrivate && m->owner != currentOwner_)
                        throwPrivate(*m->owner, name, "method", loc);
                    return invokeMethod(receiver, *m, args, name, loc, awaited);
                }
            }
            auto it = builtinMethods_.find(nameId);
            if (it != builtinMethods_.end())
            {
                const unsigned bit = receiverBit(receiver.type());
                for (const BoundMethod &b : it->second)
                {
                    if (!(b.entry->receivers & bit))
                        continue;
                    const NativeFn *fn = findNative(b.entry->function, b.functionId);
                    if (!fn)
                        throwMethodNeedsLibrary(receiver, name, *b.entry, loc);
                    args.insert(args.begin(), receiver);
                    return (*fn)(args, loc);
                }
            }
            if (cls)
                throwNoMethod(*cls, name, nameId, loc);
            throwNoBuiltinMethod(receiver, name, loc);
        }

        [[noreturn]] CUFF_COLD static void throwMethodNeedsLibrary(const Value &receiver, const std::string &name,
                                                                   const MethodEntry &entry, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::UnknownMember,
                                   "undefined method '" + name + "' for " + valueTypeName(receiver.type()), loc,
                                   "it comes from a library: add 'use DLC:" + std::string(entry.library) + "'");
        }

        [[noreturn]] CUFF_COLD static void throwNoBuiltinMethod(const Value &receiver, const std::string &name,
                                                                const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::UnknownMember, valueTypeName(receiver.type()) + " has no method '" + name + "'", loc,
                                   "a built-in method is a library function that takes the value first, "
                                   "such as text.upper() for str_upper(text)");
        }

        Value invokeMethod(const Value &receiver, const ClassInfo::Method &m, std::vector<Value> &args, const std::string &name,
                           const SourceLocation &loc, bool awaited)
        {
            const FunctionDecl &decl = *m.decl;
            checkPureCallAllowed(decl.isPure, name, loc);
            if (decl.hasTypedParams)
                checkParamTypes(name, decl, args, loc);
            if (decl.isAsync && !awaited)
            {
                checkArity(name, decl, args.size(), loc);
                queueTask(decl, std::move(args), loc, &receiver, m.owner);
                return Value::makeEmpty();
            }
            return callUserFunction(decl, args, loc, &receiver, m.owner);
        }

        // ---- running a function --------------------------------------------------------------------------------------

        // Runs a function's body. An error that leaves it is stamped with the text the function was written
        // in, so it is drawn against that text even when the caller is in another file or an earlier entry.
        CUFF_ALWAYS_INLINE ExecOutcome execFunctionBody(const FunctionDecl &decl, Environment &funcEnv)
        {
            try
            {
                return execBlock(decl.body, funcEnv);
            }
            catch (CuffError &e)
            {
                e.attachSource(decl.source);
                throw;
            }
        }

        // `self` and `owner` are set for a method: the receiver, and the class that wrote the method.
        Value callUserFunction(const FunctionDecl &decl, std::vector<Value> &args, const SourceLocation &loc,
                               const Value *self = nullptr, const ClassInfo *owner = nullptr)
        {
            if (args.size() < decl.requiredParams || args.size() > decl.params.size())
                throwArgumentCount(decl.name, decl, args.size(), loc);
            if (callDepth_ >= kMaxCallDepth)
                throwCallDepth(decl, loc);
            tick(loc);

            FrameGuard guard(this, decl.isPure, self, owner);

            Environment funcEnv(Environment::Kind::FunctionScope, globalEnv_);
            EnvPoolReturner poolReturn{this, &funcEnv}; // must be declared after funcEnv (see its comment)
            if (!envVarsPool_.empty())
            {
                funcEnv.adoptStorage(std::move(envVarsPool_.back()));
                envVarsPool_.pop_back();
            }
            funcEnv.reserve(decl.params.size());
            const size_t given = args.size();
            for (size_t i = 0; i < given; ++i)
                funcEnv.declare(decl.paramIds[i], std::move(args[i]), false);
            if (given < decl.params.size())
                fillDefaultArgs(decl, funcEnv, given, loc);

            ExecOutcome outcome = execFunctionBody(decl, funcEnv);
            if (outcome.result == ExecResult::Return)
                return std::move(outcome.returnValue);
            if (outcome.result != ExecResult::Normal)
                throwLoopControlOutside(outcome);
            return Value::makeEmpty();
        }

        // A default is worked out at every call that leaves its parameter out, and can use the parameters before it.
        CUFF_NOINLINE void fillDefaultArgs(const FunctionDecl &decl, Environment &funcEnv, size_t given, const SourceLocation &loc)
        {
            for (size_t i = given; i < decl.params.size(); ++i)
            {
                Value fallback = evalExpr(*decl.paramDefaults[i], funcEnv);
                if (decl.hasTypedParams && !fallback.isEmpty() && !typeAccepts(decl.paramTypes[i], fallback, loc))
                    throwParamTypeMismatch(decl.name, decl, i, fallback, loc);
                funcEnv.declare(decl.paramIds[i], std::move(fallback), false);
            }
        }

        // `ClassName(args)`: make the object, work out the field defaults from the oldest class down, then run the
        // nearest `init` with the arguments. The object only exists for the program if `init` finishes.
        Value instantiate(const std::shared_ptr<ClassInfo> &cls, std::vector<Value> &args, const SourceLocation &loc)
        {
            tick(loc);
            auto obj = std::make_shared<ObjectData>();
            obj->cls = cls;
            obj->fields.resize(cls->fields.size());
            Value self = Value::makeObject(obj);

            {
                DefaultValueScope scope(this);
                Environment defaultsEnv(Environment::Kind::FunctionScope, globalEnv_);
                for (size_t i = 0; i < cls->fields.size(); ++i)
                {
                    const ClassInfo::Field &f = cls->fields[i];
                    Value v = evalExpr(*f.decl->defaultValue, defaultsEnv);
                    checkDeclaredType(f.decl->type, v, f.name, f.decl->loc, "field");
                    if (f.isConstant && v.isList())
                        v = freezeList(v);
                    obj->fields[i] = std::move(v);
                }
            }

            if (const ClassInfo::Method *init = cls->init)
            {
                const FunctionDecl &decl = *init->decl;
                if (decl.isPrivate && init->owner != currentOwner_)
                    throwPrivate(*init->owner, "init", "constructor", loc);
                checkPureCallAllowed(decl.isPure, cls->name, loc);
                if (decl.hasTypedParams)
                    checkParamTypes(cls->name, decl, args, loc);
                checkArity(cls->name, decl, args.size(), loc);
                ConstructingScope constructing(*obj);
                callUserFunction(decl, args, loc, &self, init->owner);
            }
            else if (!args.empty())
            {
                throw ArgumentError(cls->name + "() expects 0 argument(s), got " + std::to_string(args.size()), loc,
                                    "the class has no 'init' method to take arguments");
            }
            return self;
        }

        // Printing an object (print, an f-string, to_str) uses its `to_str` method when the class has one.
        void display(const Value &object, std::string &out) override
        {
            const ObjectData &o = *object.asObject();
            if (const ClassInfo::Method *m = o.cls->toStr)
            {
                std::vector<Value> noArgs;
                Value text = callUserFunction(*m->decl, noArgs, m->decl->loc, &object, m->owner);
                if (!text.isStr())
                    throw TypeError("to_str() of class '" + o.cls->name + "' must return a str, got " + typeNameOf(text),
                                    m->decl->loc);
                out += text.asStr();
                return;
            }
            out += '<';
            out += o.cls->name;
            out += '>';
        }

        void execUse(const UseStmt &use, Environment &env)
        {
            if (use.isDLC)
            {
                DLCOptions dlcOpts;
                dlcOpts.network.enabled = config_.networkEnabled;
                dlcOpts.network.allowPrivateTargets = config_.allowPrivateNetworkTargets;
                dlcOpts.filesystem.enabled = config_.filesystemEnabled;
                dlcOpts.filesystem.root = moduleRoot_;
                for (const auto &lib : use.dlcs)
                    registerDLC(lib.name, natives_, lib.loc, dlcOpts);
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

        // Shifts every line of `text` right by four columns (the first line is placed by the caller).
        static std::string indentLines(const std::string &text)
        {
            std::string out;
            for (char c : text)
            {
                out += c;
                if (c == '\n')
                    out += "    ";
            }
            return out;
        }

        void loadCustomModule(const std::string &moduleName, const std::string &relPath, const SourceLocation &loc, Environment &env)
        {
            namespace fs = std::filesystem;
            const std::string shown = (relPath.empty() ? std::string() : relPath + "/") + moduleName + ".cuff";

            fs::path rel = fs::u8path(relPath);  // plain construction would read it as ANSI on Windows
            if (rel.has_root_name() || rel.has_root_directory())
                throw ModuleError(ErrorCode::ModuleAccessDenied,
                                  "absolute module paths are not allowed ('use " + moduleName + " from " + relPath + "')", loc,
                                  "use a path relative to the running script, e.g. ./lib");

            fs::path full = fs::path(scriptDir_) / rel / fs::u8path(moduleName + ".cuff");
            std::error_code ec;
            fs::path canon = fs::weakly_canonical(full, ec);
            if (ec)
                canon = full.lexically_normal();

            if (!isInsideRoot(canon, moduleRoot_))
                throw ModuleError(ErrorCode::ModuleAccessDenied,
                                  "module '" + shown + "' resolves outside the allowed directory", loc,
                                  "keep imported modules inside the script's directory, or run with --root <dir> to widen it");

            const std::string key = canon.string();

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
                                  "module '" + shown + "' is larger than the source limit of " + limits::formatBytes(limits::kMaxSourceBytes), loc);

            std::ifstream file(canon, std::ios::binary);
            if (!file)
                throw ModuleError(ErrorCode::ModuleNotFound,
                                  "could not read module file '" + shown + "' for 'use " + moduleName + " from " + relPath + "'", loc);
            std::string text(static_cast<size_t>(size), '\0');
            file.read(text.data(), static_cast<std::streamsize>(size));
            text.resize(static_cast<size_t>(file.gcount()));
            const SourceRef source = makeSourceRef(std::move(text), shown);

            importedPaths_.insert(key);
            ImportMark mark{importedPaths_, key};

            std::unique_ptr<Program> modProgram;
            try
            {
                modProgram = parseProgram(source);
            }
            catch (const CuffError &e)
            {
                // Reported at the `use` statement, with the module's own line shown beneath it.
                ModuleError err(ErrorCode::ModuleParseFailed, "failed to parse module '" + shown + "': " + e.message, loc);
                err.related = "    in '" + shown + "' [" + errorCodeTag(e.code) + "] at " + e.location.toString() + ":";
                const std::string snippet = buildCaretSnippet(source->text, e.location);
                if (!snippet.empty())
                    err.related += "\n    " + indentLines(snippet);
                throw err;
            }

            // The module's AST must outlive its functions, which stay registered even if the module body fails.
            loadedModules_.push_back(std::move(modProgram));
            const Program &program = *loadedModules_.back();

            ImportDepthGuard depthGuard(moduleDepth_);
            // Only the module's top level runs here; its functions later run against globalEnv_, so they reach
            // these names through `global x` after the copy below.
            Environment moduleEnv;
            try
            {
                execProgram(program, moduleEnv);
            }
            catch (CuffError &e)
            {
                e.attachSource(program.source);
                throw;
            }
            for (const auto &kv : moduleEnv.localVars())
                env.declare(kv.first, kv.second, moduleEnv.isConstantHere(kv.first));
            mark.committed = true;
        }

    };

}
