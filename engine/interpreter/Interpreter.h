#pragma once

#include "Value.h"
#include "../bytecode/Bytecode.h"
#include "../bytecode/Compiler.h"
#include "../common/Arithmetic.h"
#include "../common/Ascii.h"
#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/Utf8.h"
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
#include <chrono>
#include <cstdint>

// The machine jumps from one instruction to the next through a table of labels where the compiler has them
// (GCC, Clang) and through a switch elsewhere; -DCUFF_VM_SWITCH asks for the switch everywhere.
#if !defined(CUFF_VM_SWITCH) && (defined(__GNUC__) || defined(__clang__))
#define CUFF_VM_LABELS 1
#else
#define CUFF_VM_LABELS 0
#endif

namespace cuff
{

    // Runs a program: compiles each function, and each statement of the top level, to bytecode (bytecode/Compiler.h)
    // the first time it is needed, and executes that in one loop (runLoop). A call from the program to one of its own
    // functions is a new frame in the same loop, so it uses none of the thread's stack.
    class Interpreter final : private ObjectDisplayHook
    {
    public:
        struct Config
        {
            std::string rootDir; // modules must resolve inside this directory (default: the script's directory)
            uint64_t maxSteps = 0;
            uint32_t timeoutMs = 0;
            size_t stackBudgetBytes = 0; // native stack the engine may use; 0 = derive from the real stack size
            bool networkEnabled = true;
            bool allowPrivateNetworkTargets = false; // let DLC:network reach loopback/private/link-local addresses (see SECURITY.md)
            bool filesystemEnabled = true;
            // Called with each piece of code the compiler makes, as it is made (`cuffc --bytecode` prints them).
            std::function<void(const bc::Proto &)> onCompiled;
        };

        Interpreter() { initialize(); }
        explicit Interpreter(Config config) : config_(std::move(config)) { initialize(); }

        Interpreter(const Interpreter &) = delete;
        Interpreter &operator=(const Interpreter &) = delete;

        // How much native stack the engine may use. A program's own calls use none; what does is work that nests
        // inside the machine: the compiler following nested expressions, an object's `to_str` run from inside
        // print, a field's default value that makes another object, a module that loads a module. `soft` is where
        // that work gives up with the catchable E4-017; `hard` is the floor below which even leaf work such as
        // regex matching refuses to start, so something deep inside a program that is already near `soft` still
        // fails with an error rather than overflowing.
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
            compiler_.setStackLimit(stackLimit_);
            StackFloorScope floorScope(sp > plan.hard ? sp - plan.hard : 0);
            DisplayHookScope displayScope(this);
            ++runEpoch_;
            stepsLeft_ = config_.maxSteps ? config_.maxSteps : UINT64_MAX;
            timed_ = config_.timeoutMs != 0;
            if (timed_)
                deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.timeoutMs);
            try
            {
                execProgram(program, globals_, false, false, nullptr);
            }
            catch (...)
            {
                // An error ends the run: queued async jobs that have not started must not run later, and whatever
                // an error that is not the program's own (out of memory) left on the machine is cleared. A one-shot
                // interpreter is simply discarded, but cuffsh keeps one alive across entries.
                taskQueue_.clear();
                unwindTo(0);
                throw;
            }
        }

    private:
        using Reg = bc::Reg;
        using Proto = bc::Proto;
        using Instr = bc::Instr;
        using Op = bc::Op;

        // One running function, top-level statement or default value.
        struct Frame
        {
            const Instr *ip;             // where it continues: saved when it calls, read when an error unwinds
            Reg *regs;
            Proto *proto;
            Value *ret;                  // where its result goes
            const ClassInfo *owner;      // the class that wrote the running method: what `private` and `super` refer to
            ObjectData *constructing;    // the object whose `init` this is; the call's result is that object
            bc::VarTable *vars;          // a top-level statement: the variables of its program
            const SourceLocation *callLoc; // a call made by the engine itself; a call from bytecode has its instruction
            uint32_t argc;
            uint32_t segment;
            uint32_t dynBase;
            bool isPure;
            bool inFunction;
            bool counted;                // a function call: one of the limits::kMaxCallDepth
            bool noStamp;                // the error leaving it points into the caller's text, not its own
        };

        struct UserFn
        {
            const FunctionDecl *decl = nullptr;
            Proto *proto = nullptr;
        };

        // A variable a module's `use` put into a scope whose text does not declare it.
        struct DynVar
        {
            uint32_t nameId;
            int32_t scope;
            Reg cell;
        };

        struct Segment
        {
            std::unique_ptr<Reg[]> cells;
            size_t size = 0;
        };

        // How a field gets its value when an object is made. Most defaults are a literal, which needs no code run.
        struct FieldInit
        {
            enum Kind : uint8_t
            {
                Constant,
                NewList,
                NewMap,
                Thunk
            } kind = Constant;
            bool fits = false; // the value is known to be of the field's type
            Value constant;
            Proto *thunk = nullptr;
        };

        // What the machine keeps for a class once it has been used: how to fill a new object, and the compiled `init`.
        struct ClassRuntime
        {
            std::vector<FieldInit> inits;
            std::vector<Value> defaults; // every default is a literal of its field's type: a new object starts as a copy of these
            bool plain = false;
            Proto *init = nullptr;
        };

        Config config_;
        bc::VarTable globals_;
        bc::Compiler compiler_{globals_};
        std::unordered_map<std::string, NativeFn> natives_;
        std::vector<UserFn> userById_;
        std::unordered_map<const void *, std::unique_ptr<Proto>> protos_; // by FunctionDecl, or FieldDecl for a default
        std::vector<std::unique_ptr<Proto>> retired_;
        std::vector<Proto *> snapshotProtos_;
        static constexpr size_t kCallPoolCap = 256;
        std::vector<std::vector<Value>> argsPool_;

        // The machine's stack. Frames and registers are kept in blocks that never move, so a pointer to either stays
        // good while something else (a native function that prints an object, a module being loaded) runs the loop
        // again on top of them.
        static constexpr size_t kFrameBlock = 128;
        static constexpr size_t kFirstSegmentCells = 1024;
        static constexpr size_t kMaxSegmentCells = 64 * 1024;
        static constexpr size_t kNoFrame = static_cast<size_t>(-1);
        std::vector<std::unique_ptr<Frame[]>> frameBlocks_;
        size_t frameCount_ = 0;
        Frame *top_ = nullptr; // the running frame: frameAt(frameCount_ - 1)
        std::vector<Segment> segments_;
        uint32_t curSeg_ = 0;
        Reg *segEnd_ = nullptr;
        std::vector<DynVar> dynVars_;
        size_t uncatchable_ = kNoFrame; // the frame whose own `or_else` must not catch the error in flight

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
        uint32_t classSerial_ = 0;
        std::shared_ptr<ClassInfo> errorClass_;
        std::unique_ptr<Program> preludeProgram_;
        uint32_t initId_ = 0, toStrId_ = 0;

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
        static constexpr int kMaxCallDepth = limits::kMaxCallDepth;

        uintptr_t stackLimit_ = 0;
        uint64_t stepsLeft_ = UINT64_MAX;
        uint32_t tickCount_ = 0;
        bool timed_ = false;
        std::chrono::steady_clock::time_point deadline_;

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

        // While a run is in progress, printing an object (print, f-strings, to_str) may have to run its `to_str`
        // method, which only this interpreter can do.
        struct DisplayHookScope
        {
            ObjectDisplayHook *previous;
            explicit DisplayHookScope(ObjectDisplayHook *hook) : previous(activeObjectDisplay()) { activeObjectDisplay() = hook; }
            ~DisplayHookScope() { activeObjectDisplay() = previous; }
        };

        struct StackFloorScope
        {
            explicit StackFloorScope(uintptr_t floor) { stackFloor() = floor; }
            ~StackFloorScope() { stackFloor() = 0; }
        };

        // ---- budgets ---------------------------------------------------------------------------------------------

        // Checked wherever the engine is about to nest: before it runs the loop again from inside a native function,
        // a default value or a module.
        CUFF_ALWAYS_INLINE void guardNative(const SourceLocation &loc) const
        {
            if (stackPointer() < stackLimit_)
                throwStackExhausted(loc);
        }

        [[noreturn]] CUFF_COLD static void throwStackExhausted(const SourceLocation &loc)
        {
            throw StackOverflowError("native stack budget exhausted — calls and blocks are nested too deeply", loc);
        }

        // The same check for work whose text is not that of the code that is running.
        void guardNative(const SourceLocation &loc, const SourceRef &source) const
        {
            if (stackPointer() >= stackLimit_)
                return;
            StackOverflowError e("native stack budget exhausted — calls and blocks are nested too deeply", loc);
            e.attachSource(source);
            throw e;
        }

        // One step of the budget: a pass of a loop, a call, an object made.
        CUFF_ALWAYS_INLINE void tick(const SourceLocation &loc)
        {
            if (CUFF_UNLIKELY(--stepsLeft_ == 0))
                stepsExhausted(loc);
            if (CUFF_UNLIKELY(timed_) && (++tickCount_ & 0x3FF) == 0)
                checkDeadline(loc);
        }

        [[noreturn]] CUFF_COLD void stepsExhausted(const SourceLocation &loc) const
        {
            budgetExceeded(ErrorCode::ExecutionStepLimit, "execution step limit (" + std::to_string(config_.maxSteps) + ") exceeded", loc);
        }

        CUFF_NOINLINE void checkDeadline(const SourceLocation &loc) const
        {
            if (std::chrono::steady_clock::now() > deadline_)
                budgetExceeded(ErrorCode::ExecutionTimeout, "execution time limit (" + std::to_string(config_.timeoutMs) + " ms) exceeded",
                               loc);
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
            segmentFor(0, kFirstSegmentCells);
            segEnd_ = segments_[0].cells.get() + segments_[0].size;
            initialIdsAndPrelude();
        }

        // ---- the built-in Error class ---------------------------------------------------------------------------
        // Written in the language itself and registered like any class of a program, so a program can build,
        // extend and raise it with nothing special in the evaluator but `fail` and `or_else as`.
        static const char *preludeSource()
        {
            return "class Error do:\n"
                   "    str message = \"\"\n"
                   "    str code = \"E7-001\"\n"
                   "    number line = 0\n"
                   "    number column = 0\n"
                   "    str hint = \"\"\n"
                   "\n"
                   "    func init(str message = \"\") do:\n"
                   "        self.message = message\n"
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
            info->serial = ++classSerial_;
            info->source = d.source;
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
                field.typeKind = static_cast<uint8_t>(f.type.kind);
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

        // ---- functions -------------------------------------------------------------------------------------------

        void registerFunction(const FunctionDecl &decl)
        {
            if (findClass(decl.nameId))
                throwClassError(ErrorCode::ClassNameConflict, "function '" + decl.name + "' has the same name as a class", decl.loc,
                                "classes and functions share one namespace; rename one of them");
            if (decl.nameId >= userById_.size())
                userById_.resize(decl.nameId + 1);
            userById_[decl.nameId] = UserFn{&decl, nullptr};
        }

        const FunctionDecl *findUser(uint32_t id) const
        {
            return id < userById_.size() ? userById_[id].decl : nullptr;
        }

        // The compiled form of a function or method, made the first time it is called.
        CUFF_NOINLINE Proto *protoOf(const FunctionDecl &decl)
        {
            std::unique_ptr<Proto> &slot = protos_[&decl];
            if (!slot)
                slot = std::make_unique<Proto>();
            if (!slot->ready)
            {
                compiler_.compileFunction(decl, *slot);
                if (CUFF_UNLIKELY(config_.onCompiled != nullptr))
                    config_.onCompiled(*slot);
                if (slot->usesSnapshot)
                    snapshotProtos_.push_back(slot.get());
            }
            return slot.get();
        }

        // A function that bridges a variable of the program is compiled against what that variable is declared as
        // (Compiler::globalHere). When the top level declares such a variable again as something else, or for the
        // first time, those functions are compiled again at their next call.
        CUFF_NOINLINE void snapshotsChanged()
        {
            for (Proto *p : snapshotProtos_)
            {
                p->ready = false;
                if (callDepth_ != 0)
                    retireIfRunning(p);
            }
            snapshotProtos_.clear();
        }

        // Only the top level declares the program's variables, and it does so while no function is running, so this
        // finds nothing. It is here so that code which is running is never compiled over.
        void retireIfRunning(Proto *p)
        {
            for (size_t k = 0; k < frameCount_; ++k)
            {
                if (frameAt(k)->proto != p)
                    continue;
                auto it = protos_.find(p->decl);
                if (it != protos_.end() && it->second.get() == p)
                {
                    retired_.push_back(std::move(it->second));
                    protos_.erase(it);
                }
                return;
            }
        }

        // Declares a variable of a top level, or declares it again.
        CUFF_ALWAYS_INLINE void setSlotMeta(Reg &slot, uint32_t meta)
        {
            const uint32_t old = slot.meta;
            if (CUFF_UNLIKELY(old & bc::kMetaWatched) && ((old ^ meta) & bc::kMetaAttrs))
            {
                slot.meta = meta;
                snapshotsChanged();
                return;
            }
            slot.meta = meta | (old & bc::kMetaWatched);
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
                                   loc, "a variable keeps the type it was declared with (" + typeName + "); use 'any' for a variable that holds different types");
        }

        // Whether a value satisfies a type: a built-in kind, or the class named by `classNameId`, whose objects and those
        // of its subclasses it accepts. `empty` satisfies `empty` and `any` only.
        bool acceptsType(TypeKind kind, uint32_t classNameId, const Value &v)
        {
            switch (kind)
            {
            case TypeKind::Any:
                return true;
            case TypeKind::Empty:
                return v.isEmpty();
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
                const ClassInfo *cls = findClass(classNameId);
                return cls && v.isObject() && v.asObject()->cls->isSubclassOf(cls);
            }
            }
            return true;
        }

        bool typeAccepts(const TypeSpec &type, const Value &v, const SourceLocation &loc)
        {
            if (type.kind == TypeKind::Class && !findClass(type.classNameId))
                throwUnknownClass(type.className, loc);
            return acceptsType(type.kind, type.classNameId, v);
        }

        void checkDeclaredType(const TypeSpec &type, const Value &v, const std::string &name, const SourceLocation &loc,
                               const char *what = "variable")
        {
            if (type.kind == TypeKind::Any)
                return;
            if (!typeAccepts(type, v, loc))
                throwDeclarationMismatch(type.name(), v, name, loc, what);
        }

        static std::string upperAscii(const std::string &s)
        {
            std::string out = s;
            for (char &c : out)
                c = ascii::upper(c);
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
                                loc, "assigning it to another variable does not unfreeze it; build a new list instead, e.g. 'x = y + []'");
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

        // A built-in type's TypeKind is one more than the ValueType of the values it accepts, so the test for one is
        // a comparison.
        static_assert(static_cast<int>(TypeKind::Empty) == static_cast<int>(ValueType::Empty) + 1 &&
                          static_cast<int>(TypeKind::Number) == static_cast<int>(ValueType::Number) + 1 &&
                          static_cast<int>(TypeKind::Str) == static_cast<int>(ValueType::Str) + 1 &&
                          static_cast<int>(TypeKind::Boolean) == static_cast<int>(ValueType::Boolean) + 1 &&
                          static_cast<int>(TypeKind::List) == static_cast<int>(ValueType::List) + 1 &&
                          static_cast<int>(TypeKind::Map) == static_cast<int>(ValueType::Map) + 1 &&
                          static_cast<int>(TypeKind::Match) == static_cast<int>(ValueType::Match) + 1 &&
                          static_cast<int>(TypeKind::Pattern) == static_cast<int>(ValueType::Pattern) + 1 &&
                          static_cast<int>(TypeKind::Object) == static_cast<int>(ValueType::Object) + 1 &&
                          static_cast<uint32_t>(TypeKind::Class) < bc::kClassTypeBase,
                      "TypeKind and ValueType must stay in step");

        CUFF_ALWAYS_INLINE static bool isKind(const Value &v, uint32_t kind)
        {
            return static_cast<uint32_t>(v.type()) + 1 == kind;
        }

        // Whether `v` is an object of the very class that a name stands for at this moment: how a class type is
        // usually met, and settled without walking the object's ancestors.
        CUFF_ALWAYS_INLINE bool isExactly(const Value &v, uint32_t classNameId) const
        {
            return v.isObject() && classNameId < classById_.size() && classById_[classNameId].get() == v.asObject()->cls.get();
        }

        CUFF_NOINLINE bool fitsClass(uint32_t classNameId, const Value &v)
        {
            return isExactly(v, classNameId) || acceptsType(TypeKind::Class, classNameId, v);
        }

        // `type` is a declared type as a variable's meta holds it: 0 for `any`, a TypeKind, or a class.
        CUFF_ALWAYS_INLINE bool fits(uint32_t type, const Value &v)
        {
            if (type == 0 || isKind(v, type))
                return true;
            return type >= bc::kClassTypeBase && fitsClass(type - bc::kClassTypeBase, v);
        }

        // An assignment keeps a variable to the type it was declared with.
        CUFF_ALWAYS_INLINE void checkAssigned(uint32_t type, const Value &v, const std::string &name, const SourceLocation &loc)
        {
            if (CUFF_UNLIKELY(!fits(type, v)))
                throwAssignedMismatch(type, v, name, loc);
        }

        [[noreturn]] CUFF_NOINLINE void throwAssignedMismatch(uint32_t type, const Value &v, const std::string &name,
                                                              const SourceLocation &loc)
        {
            TypeSpec spec;
            if (type >= bc::kClassTypeBase)
            {
                spec.kind = TypeKind::Class;
                const ClassInfo *cls = findClass(type - bc::kClassTypeBase);
                spec.className = cls ? cls->name : "?";
            }
            else
            {
                spec.kind = static_cast<TypeKind>(type);
            }
            throwDeclarationMismatch(spec.name(), v, name, loc, "variable");
        }

        [[noreturn]] CUFF_COLD static void throwConstantName(const std::string &name, const SourceLocation &loc)
        {
            throw ConstantError(ErrorCode::InvalidConstantName,
                                "constant name '" + name + "' must be written in ALL CAPS (uppercase letters, digits, underscores only)", loc,
                                "for example: " + upperAscii(name));
        }

        // The checks of a declaration that could not be settled when it was compiled.
        CUFF_NOINLINE void checkDeclaration(const DeclarationStmt &decl, const Value &v)
        {
            if (decl.type.kind == TypeKind::Class)
            {
                if (isExactly(v, decl.type.classNameId))
                    return;
                if (!findClass(decl.type.classNameId))
                    throwUnknownClass(decl.type.className, decl.typeLoc);
            }
            checkDeclaredType(decl.type, v, decl.name, decl.nameLoc);
        }

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

        [[noreturn]] CUFF_COLD static void throwModuloNotFinite(const SourceLocation &loc)
        {
            throw ValueError("'%' result is not a finite number", loc);
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

        bool isaCheck(const Value &v, const IsaExpr &e)
        {
            const ClassInfo *cls = findClass(e.classNameId);
            if (!cls)
                throwUnknownClass(e.className, e.loc);
            return v.isObject() && v.asObject()->cls->isSubclassOf(cls);
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

        [[noreturn]] CUFF_COLD static void throwParamTypeMismatch(const std::string &name, const FunctionDecl &decl, size_t i,
                                                                  const Value &v, const SourceLocation &loc)
        {
            const std::string typeName = decl.paramTypes[i].name();
            throw CuffRuntimeError(ErrorCode::ParameterTypeMismatch,
                                   name + "(): parameter '" + decl.params[i] + "' expects " + typeName +
                                       ", got " + typeNameOf(v),
                                   loc, "declared as '" + typeName + " " + decl.params[i] + "' in the parameter list");
        }

        [[noreturn]] CUFF_COLD static void throwCallDepth(const FunctionDecl &decl, const SourceLocation &loc)
        {
            throw StackOverflowError("maximum call depth (" + std::to_string(kMaxCallDepth) +
                                         ") exceeded while calling '" + decl.name + "' — check for infinite recursion",
                                     loc);
        }

        // A pure function may call only itself, pure functions and natives; any other helper would bypass `pure`.
        [[noreturn]] CUFF_COLD static void throwPureImpureCall(const std::string &calleeName, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::PureFunctionImpureCall,
                                   "a pure function cannot call non-pure function '" + calleeName + "'", loc,
                                   "mark '" + calleeName + "' as pure, or remove 'pure' from the caller");
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

        void checkFieldAccess(const ClassInfo::Field &f, const ClassInfo *owner, const SourceLocation &loc) const
        {
            if (f.isPrivate && owner != f.declaredIn)
                throwPrivate(*f.declaredIn, f.name, "field", loc);
        }

        Value readField(const ObjectData &o, size_t index, const ClassInfo *owner, const SourceLocation &loc) const
        {
            checkFieldAccess(o.cls->fields[index], owner, loc);
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

        void writeField(ObjectData &o, size_t index, Value v, const ClassInfo *owner, const SourceLocation &loc)
        {
            const ClassInfo::Field &f = o.cls->fields[index];
            checkFieldAccess(f, owner, loc);
            checkDeclaredType(f.decl->type, v, f.name, loc, "field");
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

        // target.name, where the instruction's own record of the class did not answer.
        CUFF_NOINLINE void getFieldSlow(Value &out, const Value &target, const MemberAccess &ma, const ClassInfo *owner,
                                        bc::FieldCache &cache)
        {
            if (!target.isObject())
                throwNotAnObject("read '." + ma.name + "' of", target, ma.loc);
            const ObjectData &o = *target.asObject();
            const size_t index = fieldIndexOrThrow(o, ma.nameId, ma.name, ma.loc);
            cache.serial = o.cls->serial;
            cache.index = static_cast<uint32_t>(index);
            Value v = readField(o, index, owner, ma.loc);
            out = std::move(v);
        }

        CUFF_NOINLINE void setFieldSlow(const Value &holder, Value v, const PlaceStep &s, const ClassInfo *owner, bc::FieldCache &cache)
        {
            ObjectData &o = expectObject(holder, s.name, s.loc);
            const size_t index = fieldIndexOrThrow(o, s.nameId, s.name, s.loc);
            cache.serial = o.cls->serial;
            cache.index = static_cast<uint32_t>(index);
            writeField(o, index, std::move(v), owner, s.loc);
        }

        // ---- names the compiler left to the machine ----------------------------------------------------------------

        static Reg &cellAt(const Frame &fr, int32_t offset)
        {
            return *reinterpret_cast<Reg *>(reinterpret_cast<char *>(fr.regs) + offset);
        }

        // What a name stands for: a variable, or a field of a `with` object.
        struct Located
        {
            Reg *var = nullptr;
            const Value *holder = nullptr;
            uint32_t fieldIndex = 0;
        };

        // Each scope is searched from the inside out, its own variables first and then the members of its `with`
        // object; the function's scope ends the search with a bridged global.
        Located locate(Frame &fr, const bc::NameRec &rec)
        {
            Located found;
            Proto &p = *fr.proto;
            const bc::NameStep *step = p.steps.data() + rec.firstStep;
            for (uint32_t k = 0; k < rec.stepCount; ++k, ++step)
            {
                switch (step->kind)
                {
                case bc::NameStep::Local:
                {
                    Reg &r = cellAt(fr, step->a);
                    if (r.meta & bc::kMetaDeclared)
                    {
                        found.var = &r;
                        return found;
                    }
                    break;
                }
                case bc::NameStep::LocalSure:
                    found.var = &cellAt(fr, step->a);
                    return found;
                case bc::NameStep::With:
                {
                    const Value &holder = cellAt(fr, step->a).v;
                    const ClassInfo &cls = *holder.asObject()->cls;
                    bc::WithCache &cache = p.withCaches[static_cast<size_t>(step->b)];
                    if (cache.serial != cls.serial)
                    {
                        auto it = cls.fieldIndex.find(rec.nameId);
                        cache.index = it == cls.fieldIndex.end() ? -1 : static_cast<int32_t>(it->second);
                        cache.serial = cls.serial;
                    }
                    if (cache.index >= 0)
                    {
                        found.holder = &holder;
                        found.fieldIndex = static_cast<uint32_t>(cache.index);
                        return found;
                    }
                    break;
                }
                case bc::NameStep::Dynamic:
                    for (size_t d = fr.dynBase; d < dynVars_.size(); ++d)
                        if (dynVars_[d].nameId == rec.nameId && dynVars_[d].scope == step->a)
                        {
                            found.var = &dynVars_[d].cell;
                            return found;
                        }
                    break;
                case bc::NameStep::Bridge:
                    if (!cellAt(fr, step->a).meta)
                        break;
                    [[fallthrough]];
                case bc::NameStep::BridgeSure:
                case bc::NameStep::Global:
                {
                    Reg *g = p.gcells[static_cast<size_t>(step->b)];
                    if (g->meta & bc::kMetaDeclared)
                        found.var = g;
                    return found;
                }
                }
            }
            return found;
        }

        // A global that isn't bridged into the running function resolves to nothing; say why instead of "undefined".
        [[noreturn]] CUFF_COLD void throwUnresolved(const Frame &fr, uint32_t nameId, const std::string &name, const std::string &message,
                                                    const SourceLocation &loc, const std::string &hint = "")
        {
            if (fr.inFunction && globals_.isDeclared(nameId))
            {
                if (fr.isPure)
                    throwPureGlobalAccess(name, loc);
                throw UndefinedVariableError("global variable '" + name + "' is not visible inside a function unless it is bridged",
                                             loc, "add 'global " + name + "' before using it");
            }
            throw UndefinedVariableError(message, loc, hint);
        }

        [[noreturn]] CUFF_COLD void throwUndefinedVariable(const Frame &fr, const IdentifierExpr &id)
        {
            throwUnresolved(fr, id.nameId, id.name, "undefined variable '" + id.name + "'", id.loc);
        }

        CUFF_NOINLINE void getName(Frame &fr, const bc::NameRec &rec, Value &out, const IdentifierExpr &id)
        {
            const Located l = locate(fr, rec);
            if (l.holder)
            {
                Value v = readField(*l.holder->asObject(), l.fieldIndex, fr.owner, id.loc);
                out = std::move(v);
            }
            else if (l.var)
                out = l.var->v;
            else
                throwUndefinedVariable(fr, id);
        }

        // The variable a call's name stands for, when it is no function: the call then says what it is instead.
        const Reg *variableNamed(Frame &fr, const Instr &ins, uint32_t nameId)
        {
            const Proto &p = *fr.proto;
            auto it = p.callRecs.find(static_cast<uint32_t>(&ins - p.code.data()));
            if (it != p.callRecs.end())
            {
                const Located l = locate(fr, p.names[it->second]);
                if (l.var)
                    return l.var;
            }
            if (fr.vars)
            {
                const Reg *slot = fr.vars->find(nameId);
                if (slot && (slot->meta & bc::kMetaDeclared))
                    return slot;
            }
            return nullptr;
        }

        // ---- places: where an assignment, `add` and `remove` write -------------------------------------------------------
        //
        // A place is a root (a variable, `self`, or a member reached through `with`) followed by steps: `.name` and
        // `[index]`. The index values are worked out first, into consecutive registers.

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
            Reg *var = nullptr;
            const Value *object = nullptr; // `self`, a `.name`, or a member found through `with`
            bool synthetic = false;        // the first field is the place's own name, not one of its steps
        };

        // The steps of a place in order: a leading field when the place starts at a member, then its own steps.
        struct OpIter
        {
            const Place &place;
            bool synthetic;
            const Reg *indexes;
            size_t total;
            size_t pos = 0;
            size_t indexPos = 0;

            OpIter(const Place &p, bool synth, const Reg *idx)
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
                    return PlaceOp{true, &indexes[indexPos++].v, 0, nullptr, s.loc};
                return PlaceOp{false, nullptr, s.nameId, &s.name, s.loc};
            }
        };

        PlaceRoot placeRoot(Frame &fr, bc::RootKind kind, int32_t operand, const Place &pl, bool collection)
        {
            PlaceRoot r;
            switch (kind)
            {
            case bc::RootKind::Self:
                r.object = &fr.regs[0].v;
                return r;
            case bc::RootKind::WithObj:
                r.object = &cellAt(fr, operand).v;
                r.synthetic = true;
                return r;
            case bc::RootKind::Local:
                r.var = &cellAt(fr, operand);
                return r;
            case bc::RootKind::Global:
                r.var = fr.proto->gcells[static_cast<size_t>(operand)];
                if (!(r.var->meta & bc::kMetaDeclared))
                    r.var = nullptr;
                break;
            case bc::RootKind::Name:
            {
                const Located l = locate(fr, fr.proto->names[static_cast<size_t>(operand)]);
                if (l.holder)
                {
                    r.object = l.holder;
                    r.synthetic = true;
                    return r;
                }
                r.var = l.var;
                break;
            }
            }
            if (!r.var)
            {
                if (!collection)
                    throwUndefinedTarget(fr, pl);
                throwUnresolved(fr, pl.nameId, pl.name, "undefined collection '" + pl.name + "'", pl.loc);
            }
            return r;
        }

        [[noreturn]] CUFF_COLD void throwUndefinedTarget(Frame &fr, const Place &pl)
        {
            throwUnresolved(fr, pl.nameId, pl.name, "cannot assign to undefined variable '" + pl.name + "'", pl.loc,
                            "declare it first, for example 'number " + pl.name + " = 0', or bridge a global with 'global " + pl.name + "'");
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

        [[noreturn]] CUFF_COLD static void throwConstantAssign(const AssignStmt &c)
        {
            throw ConstantError(ErrorCode::ConstantReassignment, "cannot change constant '" + c.target.name + "'", c.loc);
        }

        [[noreturn]] CUFF_COLD static void throwConstantCollection(const std::string &name, const SourceLocation &loc)
        {
            throw ConstantError(ErrorCode::ConstantReassignment, "cannot modify constant collection '" + name + "'", loc);
        }

        // What must hold before the indexes and the value of a place statement are worked out: its first name exists,
        // a constant is not written, a pure function changes no field.
        CUFF_NOINLINE void placeCheck(Frame &fr, const Instr &ins, const void *node)
        {
            const bool collection = (ins.x & bc::kRootCollection) != 0;
            const bc::RootKind kind = static_cast<bc::RootKind>(ins.x & bc::kRootMask);
            const AssignStmt *assign = collection ? nullptr : static_cast<const AssignStmt *>(node);
            const CollectionOpStmt *op = collection ? static_cast<const CollectionOpStmt *>(node) : nullptr;
            const Place &pl = collection ? op->target : assign->target;
            const PlaceRoot root = placeRoot(fr, kind, ins.c, pl, collection);
            // What a variable of the running code was declared as is the compiler's to say; what one found by name, or
            // one of the program, was declared as is in its meta.
            const bool constant = kind == bc::RootKind::Local ? (ins.x & bc::kRootConstant) != 0
                                                              : root.var && (root.var->meta & bc::kMetaConst);
            if (collection)
            {
                if (constant && pl.steps.empty())
                    throwConstantCollection(pl.name, op->loc);
            }
            else if (constant && (pl.steps.empty() || !root.var->v.isList()))
            {
                throwConstantAssign(*assign);
            }
            if (fr.isPure && touchesField(pl, root))
                throwPureFieldWrite(collection ? op->loc : assign->loc);
        }

        // The value a place has reached so far, and whether a constant slot was crossed on the way: a constant field
        // that holds something other than a list protects whatever lies below it.
        struct Walk
        {
            Value current;
            bool barrier = false;
            const std::string *barrierName = nullptr;
            const ClassInfo *owner = nullptr;
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
            checkFieldAccess(f, w.owner, op.loc);
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

        // container[index] = newVal
        void writeIndex(const Value &container, const Value &index, Value newVal, const SourceLocation &loc)
        {
            if (container.isList())
            {
                if (container.asList()->isConstant)
                    throwConstantListMutation(loc);
                if (!index.isNumber())
                    throw TypeError("list index must be a number (1-based)", loc);
                const size_t real = resolveIndex1Based(expectWholeNumber(index.asNumber(), "list index", loc),
                                                       container.asList()->items.size(), loc);
                container.asList()->items[real] = std::move(newVal);
                return;
            }
            if (container.isMap())
            {
                if (!index.isStr())
                    throw TypeError("map keys are strings — cannot assign with a " + valueTypeName(index.type()), loc);
                ValueMap &map = *container.asMap();
                if (map.size() >= limits::kMaxCollectionItems && !map.has(index.asStr()))
                    throwSizeLimit("collection", loc);
                map.set(index.asStr(), std::move(newVal));
                return;
            }
            throw TypeError("cannot index-assign into a " + typeNameOf(container) + " value", loc);
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
                writeField(o, index, std::move(newVal), w.owner, op.loc);
                return;
            }
            if (w.barrier)
                throwConstantBarrier(*w.barrierName, "change", op.loc);
            writeIndex(w.current, *op.index, std::move(newVal), op.loc);
        }

        // place = value, for every place the compiler did not turn into a single instruction.
        CUFF_NOINLINE void placeSet(Frame &fr, const Instr &ins, const AssignStmt &c)
        {
            const Place &pl = c.target;
            const PlaceRoot root = placeRoot(fr, static_cast<bc::RootKind>(ins.x & bc::kRootMask), ins.c, pl, false);
            Value &source = cellAt(fr, ins.b).v;
            Value newVal = (ins.x & bc::kMoveSource) ? std::move(source) : source;

            if (root.var && pl.steps.empty())
            {
                checkAssigned(root.var->meta >> bc::kMetaTypeShift, newVal, pl.name, pl.loc);
                root.var->v = std::move(newVal);
                return;
            }
            Walk w;
            w.owner = fr.owner;
            w.current = root.var ? root.var->v : *root.object;
            OpIter ops(pl, root.synthetic, &cellAt(fr, ins.a));
            for (size_t k = 0; k + 1 < ops.total; ++k)
                readOp(w, ops.next());
            writeLast(w, ops.next(), std::move(newVal));
        }

        // `add value to target`, `remove value from target`, `remove at position from target`, once the target is known.
        void applyCollectionOp(const CollectionOpStmt &co, Value &target, Value operand)
        {
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
                if (co.atPosition)
                {
                    if (!operand.isNumber())
                        throw TypeError("'remove at' needs a position (a number), got " + typeNameOf(operand), co.loc,
                                        "to remove an element by its value write `remove value from list`");
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
                if (co.atPosition)
                    throw TypeError("'remove at' needs a list, got a map", co.loc, "remove a key with `remove \"key\" from map`");
                if (!operand.isStr())
                    throw TypeError("map keys are strings; cannot remove using a " + typeNameOf(operand), co.loc);
                target.asMap()->remove(operand.asStr());
            }
            else
            {
                throw TypeError("'add' and 'remove' require a list or map, got " + typeNameOf(target), co.loc);
            }
        }

        CUFF_NOINLINE void collectionOp(Frame &fr, const Instr &ins, const CollectionOpStmt &co)
        {
            const Place &pl = co.target;
            const PlaceRoot root = placeRoot(fr, static_cast<bc::RootKind>(ins.x & bc::kRootMask), ins.c, pl, true);
            Value &source = cellAt(fr, ins.b).v;
            Value operand = (ins.x & bc::kMoveSource) ? std::move(source) : source;

            Walk w;
            w.owner = fr.owner;
            w.current = root.var ? root.var->v : *root.object;
            OpIter ops(pl, root.synthetic, &cellAt(fr, ins.a));
            while (ops.pos < ops.total)
                readOp(w, ops.next());
            if (w.barrier)
                throwConstantCollection(*w.barrierName, co.loc);
            applyCollectionOp(co, w.current, std::move(operand));
        }

        // ---- loops ---------------------------------------------------------------------------------------------------

        [[noreturn]] CUFF_COLD static void throwLoopConstant(const LoopStmt &loop)
        {
            throw ConstantError(ErrorCode::ConstantReassignment, "cannot use constant '" + loop.repeatVar + "' as a loop variable", loop.loc);
        }

        CUFF_NOINLINE void checkLoopValue(const LoopStmt &loop, const Value &v)
        {
            checkDeclaredType(loop.varType, v, loop.repeatVar, loop.typeLoc, "loop variable");
        }

        // `loop repeat T name = start ~ end`: the bounds are in the first two of three registers; the third gets the
        // direction, and the first becomes the counter.
        CUFF_NOINLINE void forPrepare(Reg *base, const LoopStmt &loop)
        {
            const Value &startV = base[0].v;
            const Value &endV = base[1].v;
            if (!startV.isNumber() || !endV.isNumber())
                throw TypeError("'loop repeat' bounds must be numbers", loop.loc);
            const long long start = expectWholeNumber(startV.asNumber(), "'loop repeat' start bound", loop.loc);
            const long long end = expectWholeNumber(endV.asNumber(), "'loop repeat' end bound", loop.loc);
            checkDeclaredType(loop.varType, startV, loop.repeatVar, loop.typeLoc, "loop variable");
            base[2].v.setNumber(start <= end ? 1.0 : -1.0);
        }

        // `loop each`: a list gives its items, a map its keys (in insertion order) and a str its characters. The loop
        // visits what the collection held when it started, so changing it from inside the body is safe.
        CUFF_NOINLINE void eachPrepare(Reg *base, const Value &collection, const LoopStmt &loop)
        {
            if (collection.isList())
            {
                auto items = std::make_shared<ValueList>();
                items->items = collection.asList()->items;
                base[0].v = Value::makeList(std::move(items));
            }
            else if (collection.isMap())
            {
                auto keys = std::make_shared<ValueList>();
                std::vector<std::string> names = collection.asMap()->keys();
                keys->items.reserve(names.size());
                for (std::string &name : names)
                    keys->items.push_back(Value::makeStr(std::move(name)));
                base[0].v = Value::makeList(std::move(keys));
            }
            else if (collection.isStr())
            {
                base[0].v = collection;
            }
            else
            {
                throw TypeError("'loop each' needs a list, map or str, got " + typeNameOf(collection), loop.loc,
                                "use 'loop repeat' to count through a range of numbers");
            }
            base[1].v.setNumber(0);
        }

        // The next item of a `loop each` into `out`; false when there is none.
        CUFF_ALWAYS_INLINE bool eachNext(Reg *base, Value &out)
        {
            const size_t at = static_cast<size_t>(base[1].v.asNumber());
            const Value &from = base[0].v;
            if (CUFF_LIKELY(from.isList()))
            {
                auto &items = from.asList()->items;
                if (at >= items.size())
                    return false;
                out = std::move(items[at]);
            }
            else
            {
                const StrData &sd = from.asStrData();
                if (at >= sd.codepoints())
                    return false;
                out = sd.ascii() ? Value::makeChar(static_cast<unsigned char>(sd.text()[at]))
                                 : Value::makeStr(sd.text().substr(sd.offsetOf(at), sd.offsetOf(at + 1) - sd.offsetOf(at)));
            }
            base[1].v.setNumber(static_cast<double>(at + 1));
            return true;
        }

        // `fail value`: raises an Error object, or a str as the message of a new one.
        [[noreturn]] CUFF_NOINLINE void failWith(Value v, const FailStmt &f)
        {
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

        // ---- the machine's stack ---------------------------------------------------------------------------------

        Frame *frameAt(size_t index) const
        {
            return &frameBlocks_[index / kFrameBlock][index % kFrameBlock];
        }

        CUFF_NOINLINE Frame *frameBlock(size_t index)
        {
            if (index == frameBlocks_.size())
                frameBlocks_.push_back(std::make_unique<Frame[]>(kFrameBlock));
            return frameBlocks_[index].get();
        }

        CUFF_ALWAYS_INLINE Frame *pushFrame()
        {
            Frame *f = CUFF_LIKELY((frameCount_ & (kFrameBlock - 1)) != 0) ? top_ + 1 : frameBlock(frameCount_ / kFrameBlock);
            ++frameCount_;
            top_ = f;
            return f;
        }

        CUFF_ALWAYS_INLINE void popFrame()
        {
            --frameCount_;
            if (CUFF_LIKELY((frameCount_ & (kFrameBlock - 1)) != 0))
                --top_;
            else
                top_ = frameCount_ ? frameAt(frameCount_ - 1) : nullptr;
        }

        Reg *segmentFor(uint32_t index, size_t cells)
        {
            if (index == segments_.size())
                segments_.emplace_back();
            Segment &s = segments_[index];
            if (s.size < cells)
            {
                const size_t usual = std::min<size_t>(kMaxSegmentCells, kFirstSegmentCells << std::min<uint32_t>(index, 6));
                s.size = std::max(cells, usual);
                s.cells = std::make_unique<Reg[]>(s.size);
            }
            return s.cells.get();
        }

        // The frame being entered does not fit above its caller: it starts the next block of registers, and what the
        // caller put in the first `carry` of them (the receiver and the arguments) moves there.
        CUFF_NOINLINE Reg *startSegment(Reg *from, uint32_t carry, uint32_t cells)
        {
            const uint32_t seg = curSeg_ + 1;
            Reg *base = segmentFor(seg, cells);
            for (uint32_t k = 0; k < carry; ++k)
                base[k].v = std::move(from[k].v);
            curSeg_ = seg;
            segEnd_ = base + segments_[seg].size;
            return base;
        }

        // The first register above everything that is in use.
        Reg *freeCells() const
        {
            if (!frameCount_)
                return segments_[0].cells.get();
            return top_->regs + top_->proto->numCells;
        }

        CUFF_ALWAYS_INLINE Frame *openFrame(Proto *p, Reg *regs, Value *ret)
        {
            Frame *f = pushFrame();
            f->ip = p->code.data();
            f->regs = regs;
            f->proto = p;
            f->ret = ret;
            f->owner = nullptr;
            f->constructing = nullptr;
            f->vars = nullptr;
            f->callLoc = nullptr;
            f->argc = 0;
            f->segment = curSeg_;
            f->dynBase = static_cast<uint32_t>(dynVars_.size());
            f->isPure = false;
            f->inFunction = false;
            f->counted = false;
            f->noStamp = false;
            return f;
        }

        // A frame for code that takes no arguments: a statement of the top level, a default value.
        Frame *enterPlain(Proto *p, Value *ret)
        {
            Reg *regs = freeCells();
            if (regs + p->numCells > segEnd_)
                regs = startSegment(nullptr, 0, p->numCells);
            Frame *f = openFrame(p, regs, ret);
            if (p->usesMeta)
                for (uint32_t k = 0; k < p->namedEnd; ++k)
                    regs[k].meta = 0;
            return f;
        }

        // The parameters that were given are declared; every other variable of the function is not yet.
        void initMeta(Frame &f)
        {
            const Proto &p = *f.proto;
            Reg *cell = f.regs + p.paramBase;
            for (uint32_t k = 0; k < f.argc; ++k, ++cell)
                cell->meta = p.paramMeta[k];
            for (Reg *end = f.regs + p.namedEnd; cell < end; ++cell)
                cell->meta = 0;
        }

        static void releaseCells(Reg *cells, uint32_t n)
        {
            for (uint32_t k = 0; k < n; ++k)
                cells[k].v.clear();
        }

        // Leaves the top frame: its registers give up what they hold.
        void dropFrame(Frame &f)
        {
            if (f.constructing)
                f.constructing->constructing = false; // before the registers go: one of them may hold the object alone
            releaseCells(f.regs, f.proto->numCells);
            if (f.counted)
                --callDepth_;
            if (dynVars_.size() > f.dynBase)
                dynVars_.resize(f.dynBase);
            popFrame();
            const uint32_t seg = frameCount_ ? top_->segment : 0;
            if (seg != curSeg_)
            {
                curSeg_ = seg;
                segEnd_ = segments_[seg].cells.get() + segments_[seg].size;
            }
        }

        void unwindTo(size_t count)
        {
            while (frameCount_ > count)
                dropFrame(*top_);
            if (uncatchable_ != kNoFrame && uncatchable_ >= count)
                uncatchable_ = kNoFrame;
        }

        // Finds the `or_else` that takes an error among the frames one loop is running, leaving the frames above
        // it. False when none does: the loop's frames are all left, and the error goes on to whoever ran the loop.
        // A frame the error leaves, or is caught in, stamps it with the text its locations point into, unless
        // something nearer the error already has.
        bool handleError(CuffError &e, size_t stopCount)
        {
            while (frameCount_ > stopCount)
            {
                Frame &f = *top_;
                const Proto &p = *f.proto;
                const bool barred = uncatchable_ == frameCount_ - 1;
                if (e.recoverable && !barred)
                {
                    const uint32_t pc = static_cast<uint32_t>(f.ip - p.code.data()) - 1;
                    for (const bc::Handler &h : p.handlers)
                    {
                        if (pc < h.start || pc >= h.end)
                            continue;
                        if (!f.noStamp)
                            e.attachSource(p.source);
                        f.noStamp = false;
                        if (h.errCell != bc::kNoCell)
                            cellAt(f, h.errCell).v = makeErrorValue(e);
                        f.ip = p.code.data() + h.target;
                        return true;
                    }
                }
                if (barred)
                    uncatchable_ = kNoFrame;
                if (!f.noStamp)
                    e.attachSource(p.source);
                dropFrame(f);
            }
            return false;
        }

        // ---- calls -------------------------------------------------------------------------------------------------

        struct CallSite
        {
            const std::string *name;
            uint32_t nameId;
            const SourceLocation *loc;
        };

        // The name and the location a call instruction reports with. `await f()` reports at the `await`; a method or
        // `super` call always at its own name.
        static CallSite siteOf(const Proto &p, const Instr &ins)
        {
            const void *node = p.nodes[static_cast<size_t>(&ins - p.code.data())];
            const AwaitExpr *aw = (ins.x & bc::kCallAwaited) ? static_cast<const AwaitExpr *>(node) : nullptr;
            switch (static_cast<Op>(ins.op))
            {
            case Op::Call:
            case Op::CallW:
            {
                const FunctionCall &fc = aw ? *std::get_if<FunctionCall>(&aw->call->data) : *static_cast<const FunctionCall *>(node);
                return CallSite{&fc.functionName, fc.functionNameId, aw ? &aw->loc : &fc.loc};
            }
            case Op::CallM:
            {
                const MethodCall &mc = aw ? *std::get_if<MethodCall>(&aw->call->data) : *static_cast<const MethodCall *>(node);
                return CallSite{&mc.name, mc.nameId, &mc.loc};
            }
            default:
            {
                const SuperCall &sc = aw ? *std::get_if<SuperCall>(&aw->call->data) : *static_cast<const SuperCall *>(node);
                return CallSite{&sc.name, sc.nameId, &sc.loc};
            }
            }
        }

        // Where the call that made the top frame was written.
        const SourceLocation &callLocation(const Frame &f) const
        {
            if (f.callLoc)
                return *f.callLoc;
            const Frame &caller = *frameAt(frameCount_ - 2);
            return *siteOf(*caller.proto, caller.ip[-1]).loc;
        }

        CUFF_NOINLINE void paramTypeSlow(const Proto &p, uint32_t k, const Value &v, const std::string &name, const SourceLocation &loc)
        {
            const FunctionDecl &decl = *p.decl;
            if (!typeAccepts(decl.paramTypes[k], v, loc))
                throwParamTypeMismatch(name, decl, k, v, loc);
        }

        // Only run for functions with a parameter that is not `any`, so the others pay nothing.
        template <typename LocFn>
        CUFF_ALWAYS_INLINE void checkParamTypes(const Proto &p, const Reg *args, uint32_t argc, const std::string &name, LocFn &&loc)
        {
            const uint32_t n = std::min(argc, p.nparams);
            for (uint32_t k = 0; k < n; ++k)
            {
                const uint32_t type = p.paramMeta[k] >> bc::kMetaTypeShift;
                if (type != 0 && !isKind(args[k].v, type) &&
                    !(type >= bc::kClassTypeBase && isExactly(args[k].v, type - bc::kClassTypeBase)))
                    paramTypeSlow(p, k, args[k].v, name, loc());
            }
        }

        // An async function called without `await`: its receiver and arguments, from `base` on, wait in the queue.
        CUFF_NOINLINE void queueTask(const Proto &p, Reg *base, uint32_t argc, const SourceLocation &loc, const ClassInfo *owner)
        {
            if (taskQueue_.size() >= limits::kMaxQueuedTasks)
                throwSizeLimit("async task queue", loc);
            QueuedTask task;
            task.decl = p.decl;
            Reg *args = base + p.paramBase;
            task.args.reserve(argc);
            for (uint32_t k = 0; k < argc; ++k)
                task.args.push_back(std::move(args[k].v));
            if (p.isMethod)
                task.self = base->v;
            task.owner = owner;
            taskQueue_.push_back(std::move(task));
        }

        // Enters a function whose receiver (for a method) and arguments are in consecutive registers from `base`:
        // those registers become the first of the new frame, so nothing is copied.
        template <typename LocFn>
        CUFF_ALWAYS_INLINE Frame *enterCall(Proto *p, Reg *base, uint32_t argc, Value *ret, const ClassInfo *owner,
                                             const std::string &name, LocFn &&loc)
        {
            if (CUFF_UNLIKELY(argc < p->required || argc > p->nparams))
                throwArgumentCount(name, *p->decl, argc, loc());
            if (CUFF_UNLIKELY(callDepth_ >= kMaxCallDepth))
                throwCallDepth(*p->decl, loc());
            if (CUFF_UNLIKELY(--stepsLeft_ == 0))
                stepsExhausted(loc());
            if (CUFF_UNLIKELY(timed_) && (++tickCount_ & 0x3FF) == 0)
                checkDeadline(loc());
            Reg *regs = base;
            if (CUFF_UNLIKELY(base + p->numCells > segEnd_))
                regs = startSegment(base, p->paramBase + argc, p->numCells);
            Frame *f = openFrame(p, regs, ret);
            f->owner = owner;
            f->argc = argc;
            f->isPure = p->isPure;
            f->inFunction = true;
            f->counted = true;
            if (CUFF_UNLIKELY(p->usesMeta))
                initMeta(*f);
            ++callDepth_;
            return f;
        }

        // A call to a user function or method: checked, then entered. False when it was queued instead (an async
        // function that is not awaited); its result, `empty`, is then already in place.
        template <typename LocFn>
        CUFF_ALWAYS_INLINE bool invoke(Frame &fr, Proto *p, Reg *base, uint32_t argc, Value *ret, const ClassInfo *owner,
                                       const std::string &name, bool awaited, LocFn &&loc)
        {
            if (CUFF_UNLIKELY(fr.isPure && !p->isPure))
                throwPureImpureCall(name, loc());
            Reg *args = base + p->paramBase;
            if (p->typedParams)
                checkParamTypes(*p, args, argc, name, loc);
            if (CUFF_UNLIKELY(p->isAsync && !awaited))
            {
                if (argc < p->required || argc > p->nparams)
                    throwArgumentCount(name, *p->decl, argc, loc());
                queueTask(*p, base, argc, loc(), owner);
                ret->clear();
                return false;
            }
            enterCall(p, base, argc, ret, owner, p->decl->name, loc);
            return true;
        }

        Value callNative(const NativeFn &fn, Reg *args, uint32_t argc, const SourceLocation &loc)
        {
            PooledArgs pooled(this);
            pooled.v.reserve(argc);
            for (uint32_t k = 0; k < argc; ++k)
                pooled.v.push_back(std::move(args[k].v));
            return fn(pooled.v, loc);
        }

        // A call by a name that is no user function: a class, a native function, or nothing that can be called. True
        // when a frame was entered (a class with an `init`).
        CUFF_NOINLINE bool callOther(Frame &fr, const Instr &ins, uint32_t nameId, Reg *args, Value *ret)
        {
            const uint32_t argc = static_cast<uint32_t>(ins.b);
            if (const std::shared_ptr<ClassInfo> *handle = classHandle(nameId))
            {
                if (ins.x & bc::kCallAwaited)
                {
                    const CallSite site = siteOf(*fr.proto, ins);
                    throwAwaitOnNonAsync(*site.name, *site.loc);
                }
                // The object is the receiver of `init`, so it goes in the register in front of the arguments. A call
                // inside `with` kept one free; a plain call makes room (the frame has a spare register for it).
                Reg *self = args - 1;
                if (ret == &args->v)
                {
                    for (uint32_t k = argc; k > 0; --k)
                        args[k].v.take(args[k - 1].v);
                    self = args;
                }
                return instantiate(fr, ins, *handle, self, argc, ret);
            }
            const CallSite site = siteOf(*fr.proto, ins);
            const SourceLocation &loc = *site.loc;
            if (const NativeFn *native = findNative(*site.name, nameId))
            {
                Value out = callNative(*native, args, argc, loc);
                *ret = std::move(out);
                return false;
            }
            if (const Reg *var = variableNamed(fr, ins, nameId))
                throwNotCallable(*site.name, var->v, loc);
            throwUndefinedFunction(*site.name, loc);
        }

        // `await name(args)`: settled before the arguments are worked out.
        CUFF_NOINLINE void awaitCheck(Frame &fr, const Instr &ins, const AwaitExpr &aw)
        {
            const FunctionCall &call = *std::get_if<FunctionCall>(&aw.call->data);
            if (ins.x)
                for (int32_t holderCell : fr.proto->withLists[static_cast<size_t>(ins.c)])
                    if (const ClassInfo::Method *m = cellAt(fr, holderCell).v.asObject()->cls->findMethod(call.functionNameId))
                    {
                        if (!m->decl->isAsync)
                            throwAwaitOnNonAsync(call.functionName, aw.loc);
                        return;
                    }
            const FunctionDecl *user = findUser(call.functionNameId);
            if (user && !user->isAsync)
                throwAwaitOnNonAsync(call.functionName, aw.loc);
            if (!user && classHandle(call.functionNameId))
                throwAwaitOnNonAsync(call.functionName, aw.loc);
            if (user && fr.isPure && !user->isPure)
                throwPureImpureCall(call.functionName, aw.loc);
        }

        const ClassInfo &parentOf(const Frame &fr, const SourceLocation &loc) const
        {
            if (!fr.owner || !fr.owner->parent)
                throw InternalEngineError("'super' used outside a method of a class with a parent", loc);
            return *fr.owner->parent;
        }

        // name(args) inside a `with` block: a method of the nearest `with` object that has one of that name, else an
        // ordinary call. `slot` is the register in front of the arguments, where the receiver and the result go.
        CUFF_NOINLINE bool callWith(Frame &fr, const Instr &ins, Reg *slot, uint32_t argc)
        {
            const CallSite site = siteOf(*fr.proto, ins);
            const bool awaited = (ins.x & bc::kCallAwaited) != 0;
            auto at = [&]() -> const SourceLocation & { return *site.loc; };
            for (int32_t holderCell : fr.proto->withLists[static_cast<size_t>(ins.c)])
            {
                const Value &holder = cellAt(fr, holderCell).v;
                const ClassInfo::Method *m = holder.asObject()->cls->findMethod(site.nameId);
                if (!m)
                    continue;
                slot->v = holder;
                if (m->decl->isPrivate && m->owner != fr.owner)
                    throwPrivate(*m->owner, *site.name, "method", *site.loc);
                return invoke(fr, protoOf(*m->decl), slot, argc, &slot->v, m->owner, m->decl->name, awaited, at);
            }
            if (const FunctionDecl *user = findUser(site.nameId))
                return invoke(fr, protoOf(*user), slot + 1, argc, &slot->v, nullptr, user->name, awaited, at);
            return callOther(fr, ins, site.nameId, slot + 1, &slot->v);
        }

        // super.name(args): `self` is in `base`, the arguments after it.
        CUFF_NOINLINE bool callSuper(Frame &fr, const Instr &ins, Reg *base, uint32_t argc)
        {
            const CallSite site = siteOf(*fr.proto, ins);
            const ClassInfo &parent = parentOf(fr, *site.loc);
            const ClassInfo::Method *m = parent.findMethod(site.nameId);
            if (!m)
                throwNoMethod(parent, *site.name, site.nameId, *site.loc);
            if (m->decl->isPrivate && m->owner != fr.owner)
                throwPrivate(*m->owner, *site.name, "method", *site.loc);
            auto at = [&]() -> const SourceLocation & { return *site.loc; };
            return invoke(fr, protoOf(*m->decl), base, argc, &base->v, m->owner, m->decl->name, (ins.x & bc::kCallAwaited) != 0, at);
        }

        Proto *protoEntry(const FunctionDecl &decl)
        {
            std::unique_ptr<Proto> &slot = protos_[&decl];
            if (!slot)
                slot = std::make_unique<Proto>();
            return slot.get();
        }

        // receiver.name(args) for a receiver the instruction has no record of: a method of the object's class (which
        // is recorded and returned), or a built-in method (which is run here; null is returned).
        CUFF_NOINLINE const ClassInfo::Method *findMethodFor(Frame &fr, const Instr &ins, Reg *base, uint32_t argc,
                                                             bc::MethodCache &cache)
        {
            const CallSite site = siteOf(*fr.proto, ins);
            const Value &receiver = base->v;
            const ClassInfo *cls = nullptr;
            if (receiver.isObject())
            {
                cls = receiver.asObject()->cls.get();
                if (const ClassInfo::Method *m = cls->findMethod(site.nameId))
                {
                    cache.method = m;
                    cache.proto = protoEntry(*m->decl);
                    cache.serial = cls->serial;
                    return m;
                }
            }
            auto it = builtinMethods_.find(site.nameId);
            if (it != builtinMethods_.end())
            {
                const unsigned bit = receiverBit(receiver.type());
                for (const BoundMethod &b : it->second)
                {
                    if (!(b.entry->receivers & bit))
                        continue;
                    const NativeFn *fn = findNative(b.entry->function, b.functionId);
                    if (!fn)
                        throwMethodNeedsLibrary(receiver, *site.name, *b.entry, *site.loc);
                    Value out = callNative(*fn, base, argc + 1, *site.loc);
                    base->v = std::move(out);
                    return nullptr;
                }
            }
            if (cls)
                throwNoMethod(*cls, *site.name, site.nameId, *site.loc);
            throwNoBuiltinMethod(receiver, *site.name, *site.loc);
        }

        [[noreturn]] CUFF_COLD void throwPrivateMethod(const Frame &fr, const Instr &ins, const ClassInfo::Method &m)
        {
            const CallSite site = siteOf(*fr.proto, ins);
            throwPrivate(*m.owner, *site.name, "method", *site.loc);
        }

        // ---- making objects ----------------------------------------------------------------------------------------

        static bool literalFits(TypeKind declared, TypeKind actual)
        {
            return declared == TypeKind::Any || declared == actual;
        }

        // How each field of a class gets its first value, worked out the first time the class is used.
        CUFF_NOINLINE void buildRuntime(const ClassInfo &cls)
        {
            auto rt = std::make_shared<ClassRuntime>();
            rt->inits.resize(cls.fields.size());
            for (size_t k = 0; k < cls.fields.size(); ++k)
            {
                const ClassInfo::Field &f = cls.fields[k];
                const Expr &e = *f.decl->defaultValue;
                const TypeKind declared = f.decl->type.kind;
                FieldInit &init = rt->inits[k];
                switch (e.kind)
                {
                case ExprKind::Number:
                    init.constant = Value::makeNumber(std::get_if<NumberLiteral>(&e.data)->value);
                    init.fits = literalFits(declared, TypeKind::Number);
                    continue;
                case ExprKind::String:
                    init.constant = Value::makeStr(std::get_if<StringLiteral>(&e.data)->shared);
                    init.fits = literalFits(declared, TypeKind::Str);
                    continue;
                case ExprKind::Bool:
                    init.constant = Value::makeBool(std::get_if<BoolLiteral>(&e.data)->value);
                    init.fits = literalFits(declared, TypeKind::Boolean);
                    continue;
                case ExprKind::Empty:
                    init.fits = literalFits(declared, TypeKind::Empty);
                    continue;
                case ExprKind::List:
                    if (std::get_if<ListLiteral>(&e.data)->elements.empty())
                    {
                        init.kind = FieldInit::NewList;
                        init.fits = literalFits(declared, TypeKind::List);
                        continue;
                    }
                    break;
                case ExprKind::Map:
                    if (std::get_if<MapLiteral>(&e.data)->pairs.empty())
                    {
                        init.kind = FieldInit::NewMap;
                        init.fits = literalFits(declared, TypeKind::Map);
                        continue;
                    }
                    break;
                default:
                    break;
                }
                std::unique_ptr<Proto> &thunk = protos_[f.decl];
                if (!thunk)
                    thunk = std::make_unique<Proto>();
                if (!thunk->ready)
                {
                    compiler_.compileThunk(e, f.declaredIn->source, *thunk);
                    if (config_.onCompiled)
                        config_.onCompiled(*thunk);
                }
                init.kind = FieldInit::Thunk;
                init.thunk = thunk.get();
                init.fits = declared == TypeKind::Any;
            }
            rt->plain = std::all_of(rt->inits.begin(), rt->inits.end(),
                                    [](const FieldInit &init) { return init.kind == FieldInit::Constant && init.fits; });
            if (rt->plain)
                for (const FieldInit &init : rt->inits)
                    rt->defaults.push_back(init.constant);
            cls.runtime = std::move(rt);
        }

        // A field's default value is worked out outside any method: no `self`, no private access, and no variables of
        // the code that happens to be creating the object.
        Value runThunk(Frame &fr, Proto &thunk, const Expr &value)
        {
            guardNative(std::visit([](const auto &n) { return n.loc; }, value.data), thunk.source);
            Value out;
            Frame *f = enterPlain(&thunk, &out);
            f->isPure = fr.isPure;
            runLoop(frameCount_ - 1);
            return out;
        }

        static ClassRuntime &runtimeOf(const ClassInfo &cls)
        {
            return *static_cast<ClassRuntime *>(cls.runtime.get());
        }

        // Gives each field of a new object its default, the oldest class's fields first. The object is not reachable
        // from the program until this has finished, so a default never sees it half made.
        void fillDefaults(Frame &fr, const ClassInfo &cls, ObjectData &obj)
        {
            if (!cls.runtime)
                buildRuntime(cls);
            const ClassRuntime &rt = runtimeOf(cls);
            if (rt.plain)
            {
                obj.fields = rt.defaults;
                return;
            }
            obj.fields.reserve(rt.inits.size());
            for (size_t k = 0; k < rt.inits.size(); ++k)
            {
                const FieldInit &init = rt.inits[k];
                const ClassInfo::Field &f = cls.fields[k];
                Value v;
                switch (init.kind)
                {
                case FieldInit::Constant:
                    v = init.constant;
                    break;
                case FieldInit::NewList:
                    v = Value::makeList(std::make_shared<ValueList>());
                    break;
                case FieldInit::NewMap:
                    v = Value::makeMap(std::make_shared<ValueMap>());
                    break;
                case FieldInit::Thunk:
                    v = runThunk(fr, *init.thunk, *f.decl->defaultValue);
                    break;
                }
                if (!init.fits)
                    checkFieldDefault(f, v);
                if (f.isConstant && v.isList())
                    v = freezeList(v);
                obj.fields.push_back(std::move(v));
            }
        }

        CUFF_NOINLINE void checkFieldDefault(const ClassInfo::Field &f, const Value &v)
        {
            try
            {
                checkDeclaredType(f.decl->type, v, f.name, f.decl->loc, "field");
            }
            catch (CuffError &e)
            {
                e.attachSource(f.declaredIn->source);
                throw;
            }
        }

        // `ClassName(args)`: make the object, give each field its default from the oldest class down, then run the
        // nearest `init` with the arguments. `self` is the register in front of the arguments. True when `init` was
        // entered: the object is the call's result once it returns, and only then does it exist for the program.
        // False when there is no `init`, and the object is already in `*ret`.
        CUFF_NOINLINE bool instantiate(Frame &fr, const Instr &ins, const std::shared_ptr<ClassInfo> &handle, Reg *self, uint32_t argc,
                                       Value *ret)
        {
            auto at = [&]() -> const SourceLocation & { return *siteOf(*fr.proto, ins).loc; };
            if (CUFF_UNLIKELY(--stepsLeft_ == 0))
                stepsExhausted(at());
            if (CUFF_UNLIKELY(timed_) && (++tickCount_ & 0x3FF) == 0)
                checkDeadline(at());
            auto made = std::make_shared<ObjectData>();
            // The object keeps its class alive from here on, whatever a default value or `init` goes on to declare.
            made->cls = handle;
            ObjectData *obj = made.get();
            const ClassInfo &cls = *obj->cls;
            Value object = Value::makeObject(std::move(made));
            fillDefaults(fr, cls, *obj);

            const ClassInfo::Method *init = cls.init;
            if (!init)
            {
                if (argc != 0)
                    throw ArgumentError(cls.name + "() expects 0 argument(s), got " + std::to_string(argc), at(),
                                        "the class has no 'init' method to take arguments");
                *ret = std::move(object);
                return false;
            }
            const FunctionDecl &decl = *init->decl;
            if (decl.isPrivate && init->owner != fr.owner)
                throwPrivate(*init->owner, "init", "constructor", at());
            if (fr.isPure && !decl.isPure)
                throwPureImpureCall(cls.name, at());
            ClassRuntime &rt = runtimeOf(cls);
            Proto *p = rt.init;
            if (CUFF_UNLIKELY(!p || !p->ready))
                p = rt.init = protoOf(decl);
            if (p->typedParams)
                checkParamTypes(*p, self + 1, argc, cls.name, at);
            self->v.take(object);
            Frame *f = enterCall(p, self, argc, ret, init->owner, cls.name, at);
            f->constructing = obj;
            obj->constructing = true;
            return true;
        }

        // Calls a function from the engine itself (a queued async call, an object's `to_str`) and runs it to its end.
        void callFunction(const FunctionDecl &decl, std::vector<Value> *args, const Value *self, const ClassInfo *owner,
                          const SourceLocation &loc, Value &out)
        {
            guardNative(loc);
            Proto *p = protoOf(decl);
            const uint32_t argc = args ? static_cast<uint32_t>(args->size()) : 0;
            if (argc < p->required || argc > p->nparams)
                throwArgumentCount(decl.name, decl, argc, loc);
            if (callDepth_ >= kMaxCallDepth)
                throwCallDepth(decl, loc);
            tick(loc);
            Reg *regs = freeCells();
            if (regs + p->numCells > segEnd_)
                regs = startSegment(nullptr, 0, p->numCells);
            if (self)
                regs[0].v = *self;
            for (uint32_t k = 0; k < argc; ++k)
                regs[p->paramBase + k].v = std::move((*args)[k]);
            Frame *f = openFrame(p, regs, &out);
            f->owner = owner;
            f->callLoc = &loc;
            f->argc = argc;
            f->isPure = p->isPure;
            f->inFunction = true;
            f->counted = true;
            if (p->usesMeta)
                initMeta(*f);
            ++callDepth_;
            runLoop(frameCount_ - 1);
        }

        // Printing an object (print, an f-string, to_str) uses its `to_str` method when the class has one.
        void display(const Value &object, std::string &out) override
        {
            const ObjectData &o = *object.asObject();
            const ClassInfo::Method *m = o.cls->toStr;
            if (!m)
            {
                out += '<';
                out += o.cls->name;
                out += '>';
                return;
            }
            const FunctionDecl &decl = *m->decl;
            try
            {
                Value text;
                callFunction(decl, nullptr, &object, m->owner, decl.loc, text);
                if (!text.isStr())
                    throw TypeError("to_str() of class '" + o.cls->name + "' must return a str, got " + typeNameOf(text), decl.loc);
                out += text.asStr();
            }
            catch (CuffError &e)
            {
                e.attachSource(decl.source);
                throw;
            }
        }

        // Un-awaited async calls run in the order they were made, after the top level has finished.
        void drainTaskQueue()
        {
            while (!taskQueue_.empty())
            {
                QueuedTask task = std::move(taskQueue_.front());
                taskQueue_.pop_front();
                try
                {
                    Value ignored;
                    callFunction(*task.decl, &task.args, task.decl->isMethod ? &task.self : nullptr, task.owner, task.decl->loc, ignored);
                }
                catch (CuffError &e)
                {
                    e.attachSource(task.decl->source);
                    throw;
                }
            }
        }

        // ---- statements that are not worth an instruction of their own ----------------------------------------------

        [[noreturn]] CUFF_COLD void raise(Frame &fr, bc::RaiseKind kind, const void *node)
        {
            switch (kind)
            {
            case bc::RaiseKind::ConstantName:
            {
                const auto &d = *static_cast<const DeclarationStmt *>(node);
                throwConstantName(d.name, d.nameLoc);
            }
            case bc::RaiseKind::ConstantAssign:
                throwConstantAssign(*static_cast<const AssignStmt *>(node));
            case bc::RaiseKind::ConstantCollection:
            {
                const auto &co = *static_cast<const CollectionOpStmt *>(node);
                throwConstantCollection(co.target.name, co.loc);
            }
            case bc::RaiseKind::PureFieldWrite:
                throwPureFieldWrite(static_cast<const AssignStmt *>(node)->loc);
            case bc::RaiseKind::PureFieldWriteCollection:
                throwPureFieldWrite(static_cast<const CollectionOpStmt *>(node)->loc);
            case bc::RaiseKind::LoopConstant:
                throwLoopConstant(*static_cast<const LoopStmt *>(node));
            case bc::RaiseKind::StopOutsideLoop:
            case bc::RaiseKind::SkipOutsideLoop:
            case bc::RaiseKind::ReturnOutsideFunction:
                break;
            }
            // `return`, `stop` or `skip` with no function or loop to take it ends the function or program it is in:
            // an `or_else` of that same code does not catch it, one around the call does.
            uncatchable_ = frameCount_ - 1;
            (void)fr;
            if (kind == bc::RaiseKind::ReturnOutsideFunction)
                throw CuffRuntimeError(ErrorCode::ReturnOutsideFunction, "'return' cannot be used outside of a function",
                                       static_cast<const ReturnStmt *>(node)->loc);
            const bool skip = kind == bc::RaiseKind::SkipOutsideLoop;
            throw CuffRuntimeError(ErrorCode::StopOutsideLoop, std::string("'") + (skip ? "skip" : "stop") + "' cannot be used outside of a loop",
                                   skip ? static_cast<const SkipStmt *>(node)->loc : static_cast<const StopStmt *>(node)->loc);
        }

        CUFF_NOINLINE void globalCheck(const Frame &fr, const GlobalStmt &g)
        {
            if (!fr.inFunction)
                throw CuffRuntimeError(ErrorCode::InvalidGlobalDeclaration, "'global' can only be used inside a function body", g.loc,
                                       "at the top level, every variable is already global");
            if (fr.isPure)
                throwPureGlobalAccess(g.names.front().name, g.names.front().loc);
        }

        // A parameter's default value, once worked out, is checked against the parameter's type. The error is the
        // caller's: it is reported at the call.
        CUFF_NOINLINE void checkDefault(Frame &fr, uint32_t index, const Value &v)
        {
            const FunctionDecl &decl = *fr.proto->decl;
            fr.noStamp = true;
            const SourceLocation &loc = callLocation(fr);
            if (!typeAccepts(decl.paramTypes[index], v, loc))
                throwParamTypeMismatch(decl.name, decl, index, v, loc);
            fr.noStamp = false;
        }

        // ---- the loop ----------------------------------------------------------------------------------------------

        CUFF_ALWAYS_INLINE static bool truthy(const Value &v)
        {
            return v.isBool() ? v.asBool() : v.truthy();
        }

        CUFF_NOINLINE void binarySlow(Value &out, const BinaryOp &b, const Value &l, const Value &r)
        {
            Value v = evalBinaryOpSlow(b, l, r);
            out = std::move(v);
        }

        CUFF_NOINLINE static bool orderedSlow(const BinaryOp &b, const Value &l, const Value &r)
        {
            return compareOrdered(b.op, l, r, b.loc).asBool();
        }

        [[noreturn]] CUFF_COLD static void throwNegate(const Value &v, const UnaryOp &u)
        {
            throw TypeError("unary '-' requires a number, got " + valueTypeName(v.type()), u.loc);
        }

        [[noreturn]] CUFF_COLD static void throwWithNeedsObject(const Value &v, const WithStmt &w)
        {
            throw CuffRuntimeError(ErrorCode::NotAnObject, "'with' needs an object, got a " + typeNameOf(v) + " value", w.loc,
                                   "only objects made from a class can be used with 'with'");
        }

        // An f-string, once each of its expressions is a str in consecutive registers.
        CUFF_NOINLINE void concat(Value &out, const Reg *parts, const FStringExpr &fs)
        {
            // Checked after each piece: a sum of many large pieces could wrap around a 32-bit size_t.
            size_t total = 0;
            size_t k = 0;
            for (const auto &seg : fs.segments)
            {
                total += seg.isExpression ? parts[k++].v.asStr().size() : seg.text.size();
                checkStringSize(total, fs.loc);
            }
            std::string text;
            text.reserve(total);
            k = 0;
            for (const auto &seg : fs.segments)
                text += seg.isExpression ? parts[k++].v.asStr() : seg.text;
            out = Value::makeStr(std::move(text));
        }

        // What an f-string shows for a value: the value itself if it is a str.
        CUFF_NOINLINE void toStr(Value &v, const FStringExpr &fs)
        {
            std::string text;
            v.appendDisplay(text);
            checkStringSize(text.size(), fs.loc);
            v = Value::makeStr(std::move(text));
        }

        // Runs the top frame, and every frame it calls, until it returns: `stopCount` is the number of frames below
        // it. An error is offered to the `or_else` handlers of those frames (handleError); with none to take it, the
        // frames are left and the error goes to the caller.
        //
        // The catch blocks read nothing that the loop keeps in a local: where each frame stands is written to the
        // frame at every dispatch, and the running frame is `top_`. A local that a catch block read would have to live
        // in memory across every call that can throw, which is nearly every instruction; this way the instruction
        // pointer and the register base stay in machine registers.
        void runLoop(const size_t stopCount)
        {
            Frame *fr;
            const Instr *ip;
            Reg *regs;
            const Value *consts;
            Proto *P;
            const Instr *i = nullptr;

#define CUFF_LOAD_FRAME()                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        fr = top_;                                                                                                     \
        ip = fr->ip;                                                                                                   \
        regs = fr->regs;                                                                                               \
        P = fr->proto;                                                                                                 \
        consts = P->consts.data();                                                                                     \
    } while (0)
#define CUFF_REG(off) (*reinterpret_cast<Reg *>(reinterpret_cast<char *>(regs) + (off)))
#define CUFF_RA CUFF_REG(i->a)
#define CUFF_RB CUFF_REG(i->b)
#define CUFF_RC CUFF_REG(i->c)
#define CUFF_KB (*reinterpret_cast<const Value *>(reinterpret_cast<const char *>(consts) + i->b))
#define CUFF_KC (*reinterpret_cast<const Value *>(reinterpret_cast<const char *>(consts) + i->c))
#define CUFF_NODE(T) (static_cast<const T *>(P->nodes[static_cast<size_t>(i - P->code.data())]))
#define CUFF_SITE_LOC [&]() -> const SourceLocation & { return *siteOf(*P, *i).loc; }
#define CUFF_TICK(LOC)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (CUFF_UNLIKELY(--stepsLeft_ == 0))                                                                          \
            stepsExhausted(LOC);                                                                                       \
        if (CUFF_UNLIKELY(timed_) && (++tickCount_ & 0x3FF) == 0)                                                      \
            checkDeadline(LOC);                                                                                        \
    } while (0)
#define CUFF_TAKE(dst, src, moved)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (moved)                                                                                                     \
            (dst).take(src);                                                                                           \
        else                                                                                                           \
            (dst).put(src);                                                                                            \
    } while (0)

#if CUFF_VM_LABELS
            static const void *const kLabels[] = {
#define CUFF_OP_LABEL(name, operands) &&L_##name,
                CUFF_OPCODES(CUFF_OP_LABEL)
#undef CUFF_OP_LABEL
            };
// Each instruction's code finds its instruction just behind `ip`, so that `ip` is all that has to survive the jump.
// A jump through the table runs no destructor, so nothing that has one may still be in scope where CUFF_VM_NEXT is
// written: a Value or a shared_ptr an instruction needs lives in an inner block that ends before it. Clang refuses
// code that breaks this; GCC would compile it and skip the destructor.
#define CUFF_VM_CASE(name)                                                                                             \
    L_##name : i = ip - 1;
#define CUFF_VM_NEXT()                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        const void *const target = kLabels[ip->op];                                                                    \
        fr->ip = ++ip;                                                                                                 \
        goto *target;                                                                                                  \
    } while (0)
#else
#define CUFF_VM_CASE(name) case Op::name:
#define CUFF_VM_NEXT() goto dispatch
#endif

#define CUFF_ARITH(NAME, RIGHT, RIGHT_IS_NUMBER, RESULT)                                                               \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RB.v;                                                                                    \
        const Value &r = RIGHT;                                                                                        \
        if (CUFF_LIKELY(l.isNumber() && (RIGHT_IS_NUMBER)))                                                            \
        {                                                                                                              \
            const double x = l.asNumber(), y = r.asNumber();                                                           \
            CUFF_RA.v.setNumber(RESULT);                                                                               \
            CUFF_VM_NEXT();                                                                                            \
        }                                                                                                              \
        binarySlow(CUFF_RA.v, *CUFF_NODE(BinaryOp), l, r);                                                             \
        CUFF_VM_NEXT();                                                                                                \
    }
#define CUFF_DIVIDE(NAME, RIGHT, RIGHT_IS_NUMBER)                                                                      \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RB.v;                                                                                    \
        const Value &r = RIGHT;                                                                                        \
        if (CUFF_LIKELY(l.isNumber() && (RIGHT_IS_NUMBER)))                                                            \
        {                                                                                                              \
            const double x = l.asNumber(), y = r.asNumber();                                                           \
            if (CUFF_UNLIKELY(y == 0.0))                                                                               \
                throwDivisionByZero(CUFF_NODE(BinaryOp)->loc);                                                         \
            CUFF_RA.v.setNumber(x / y);                                                                                \
            CUFF_VM_NEXT();                                                                                            \
        }                                                                                                              \
        binarySlow(CUFF_RA.v, *CUFF_NODE(BinaryOp), l, r);                                                             \
        CUFF_VM_NEXT();                                                                                                \
    }
#define CUFF_MODULO(NAME, RIGHT, RIGHT_IS_NUMBER)                                                                      \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RB.v;                                                                                    \
        const Value &r = RIGHT;                                                                                        \
        if (CUFF_LIKELY(l.isNumber() && (RIGHT_IS_NUMBER)))                                                            \
        {                                                                                                              \
            const double x = l.asNumber(), y = r.asNumber();                                                           \
            if (CUFF_UNLIKELY(y == 0.0))                                                                               \
                throwModuloByZero(CUFF_NODE(BinaryOp)->loc);                                                           \
            const double m = arith::floorMod(x, y);                                                                    \
            if (CUFF_UNLIKELY(!std::isfinite(m)))                                                                      \
                throwModuloNotFinite(CUFF_NODE(BinaryOp)->loc);                                                        \
            CUFF_RA.v.setNumber(m);                                                                                    \
            CUFF_VM_NEXT();                                                                                            \
        }                                                                                                              \
        binarySlow(CUFF_RA.v, *CUFF_NODE(BinaryOp), l, r);                                                             \
        CUFF_VM_NEXT();                                                                                                \
    }
#define CUFF_COMPARE(NAME, OP)                                                                                         \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RB.v;                                                                                    \
        const Value &r = CUFF_RC.v;                                                                                    \
        const bool t = CUFF_LIKELY(l.isNumber() && r.isNumber()) ? l.asNumber() OP r.asNumber()                        \
                                                                 : orderedSlow(*CUFF_NODE(BinaryOp), l, r);            \
        CUFF_RA.v.setBool(t);                                                                                          \
        CUFF_VM_NEXT();                                                                                                \
    }
#define CUFF_JUMP_IF(TEST)                                                                                             \
    if ((TEST) == ((i->x & bc::kJumpIfTrue) != 0))                                                                     \
    {                                                                                                                  \
        if (i->x & bc::kJumpTicks)                                                                                     \
            CUFF_TICK(*P->tickLocs.at(static_cast<uint32_t>(i - P->code.data())));                                     \
        ip += i->c;                                                                                                    \
    }                                                                                                                  \
    CUFF_VM_NEXT();
#define CUFF_JUMP_ORDERED(NAME, RIGHT, RIGHT_IS_NUMBER, OP)                                                            \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RA.v;                                                                                    \
        const Value &r = RIGHT;                                                                                        \
        const bool t = CUFF_LIKELY(l.isNumber() && (RIGHT_IS_NUMBER)) ? l.asNumber() OP r.asNumber()                   \
                                                                 : orderedSlow(*CUFF_NODE(BinaryOp), l, r);            \
        CUFF_JUMP_IF(t)                                                                                                \
    }
#define CUFF_JUMP_EQUAL(NAME, RIGHT, RIGHT_IS_NUMBER)                                                                  \
    CUFF_VM_CASE(NAME)                                                                                                 \
    {                                                                                                                  \
        const Value &l = CUFF_RA.v;                                                                                    \
        const Value &r = RIGHT;                                                                                        \
        const bool t = CUFF_LIKELY(l.isNumber() && (RIGHT_IS_NUMBER)) ? l.asNumber() == r.asNumber() : l.strictEquals(r); \
        CUFF_JUMP_IF(t)                                                                                                \
    }

            CUFF_LOAD_FRAME();
            for (;;)
            {
                try
                {
#if CUFF_VM_LABELS
                    CUFF_VM_NEXT();
#else
                dispatch:
                    i = ip++;
                    fr->ip = ip;
                    switch (static_cast<Op>(i->op))
                    {
#endif
                    // ---- values ----
                    CUFF_VM_CASE(LoadK)
                    {
                        CUFF_RA.v.put(CUFF_KB);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(LoadNil)
                    {
                        CUFF_RA.v.clear();
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(LoadBool)
                    {
                        CUFF_RA.v.setBool(i->x != 0);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Move)
                    {
                        if (CUFF_LIKELY(i->a != i->b))
                            CUFF_RA.v.put(CUFF_RB.v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Take)
                    {
                        if (CUFF_LIKELY(i->a != i->b))
                            CUFF_RA.v.take(CUFF_RB.v);
                        CUFF_VM_NEXT();
                    }

                    // ---- variables of the top level ----
                    CUFF_VM_CASE(GetG)
                    {
                        CUFF_RA.v.put(P->gcells[static_cast<size_t>(i->b)]->v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(GetGC)
                    {
                        const Reg *g = P->gcells[static_cast<size_t>(i->b)];
                        if (CUFF_UNLIKELY(!(g->meta & bc::kMetaDeclared)))
                            throwUndefinedVariable(*fr, *CUFF_NODE(IdentifierExpr));
                        CUFF_RA.v.put(g->v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(SetG)
                    {
                        Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        Value &src = CUFF_RB.v;
                        const uint32_t type = g->meta >> bc::kMetaTypeShift;
                        if (CUFF_UNLIKELY(!fits(type, src)))
                        {
                            const Place &pl = CUFF_NODE(AssignStmt)->target;
                            throwAssignedMismatch(type, src, pl.name, pl.loc);
                        }
                        CUFF_TAKE(g->v, src, i->x & bc::kMoveSource);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(SetGU)
                    {
                        Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        CUFF_TAKE(g->v, CUFF_RB.v, i->x & bc::kMoveSource);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(CheckG)
                    {
                        const Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        const AssignStmt &c = *CUFF_NODE(AssignStmt);
                        if (!(g->meta & bc::kMetaDeclared))
                            throwUndefinedTarget(*fr, c.target);
                        if (g->meta & bc::kMetaConst)
                            throwConstantAssign(c);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(DeclG)
                    {
                        Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        CUFF_TAKE(g->v, CUFF_RB.v, i->x & bc::kMoveSource);
                        setSlotMeta(*g, static_cast<uint32_t>(i->c));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(PlaceholdG)
                    {
                        Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        if (!(g->meta & bc::kMetaDeclared))
                        {
                            g->v.clear();
                            setSlotMeta(*g, static_cast<uint32_t>(i->c));
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(GetName)
                    {
                        getName(*fr, P->names[static_cast<size_t>(i->b)], CUFF_RA.v, *CUFF_NODE(IdentifierExpr));
                        CUFF_VM_NEXT();
                    }

                    // ---- variables of a function ----
                    CUFF_VM_CASE(Store)
                    {
                        Value &src = CUFF_RB.v;
                        const uint32_t type = static_cast<uint32_t>(i->c);
                        if (CUFF_UNLIKELY(!isKind(src, i->x & bc::kKindMask)) && !fits(type, src))
                        {
                            const Place &pl = CUFF_NODE(AssignStmt)->target;
                            throwAssignedMismatch(type, src, pl.name, pl.loc);
                        }
                        CUFF_TAKE(CUFF_RA.v, src, i->x & bc::kMoveSource);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Decl)
                    {
                        Reg &dst = CUFF_RA;
                        CUFF_TAKE(dst.v, CUFF_RB.v, i->x & bc::kMoveSource);
                        dst.meta = static_cast<uint32_t>(i->c);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(SetMeta)
                    {
                        CUFF_RA.meta = static_cast<uint32_t>(i->c);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Undecl)
                    {
                        Reg *cell = &CUFF_RA;
                        for (int32_t k = 0; k < i->b; ++k)
                            cell[k].meta = 0;
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ChkDecl)
                    {
                        const Value &v = CUFF_RA.v;
                        if (CUFF_UNLIKELY(!isKind(v, i->x)))
                            checkDeclaration(*CUFF_NODE(DeclarationStmt), v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Freeze)
                    {
                        Value &v = CUFF_RA.v;
                        if (v.isList())
                            v = freezeList(v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(PlaceholdL)
                    {
                        Reg &cell = CUFF_RA;
                        if (!(cell.meta & bc::kMetaDeclared))
                        {
                            cell.v.clear();
                            cell.meta = static_cast<uint32_t>(i->c);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(BindL)
                    {
                        Reg &cell = CUFF_RA;
                        if (CUFF_UNLIKELY(cell.meta & bc::kMetaConst))
                            throwLoopConstant(*CUFF_NODE(LoopStmt));
                        cell.v.put(CUFF_RB.v);
                        cell.meta = static_cast<uint32_t>(i->c);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(BindG)
                    {
                        Reg *g = P->gcells[static_cast<size_t>(i->a)];
                        if (CUFF_UNLIKELY(g->meta & bc::kMetaConst))
                            throwLoopConstant(*CUFF_NODE(LoopStmt));
                        g->v.put(CUFF_RB.v);
                        setSlotMeta(*g, static_cast<uint32_t>(i->c));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ChkLoop)
                    {
                        const Value &v = CUFF_RA.v;
                        if (CUFF_UNLIKELY(!isKind(v, i->x)))
                            checkLoopValue(*CUFF_NODE(LoopStmt), v);
                        CUFF_VM_NEXT();
                    }

                    // ---- operators ----
                    CUFF_ARITH(Add, CUFF_RC.v, r.isNumber(), x + y)
                    CUFF_ARITH(Sub, CUFF_RC.v, r.isNumber(), x - y)
                    CUFF_ARITH(Mul, CUFF_RC.v, r.isNumber(), x * y)
                    CUFF_DIVIDE(Div, CUFF_RC.v, r.isNumber())
                    CUFF_MODULO(Mod, CUFF_RC.v, r.isNumber())
                    CUFF_ARITH(Pow, CUFF_RC.v, r.isNumber(), powerOf(x, y, CUFF_NODE(BinaryOp)->loc))
                    CUFF_ARITH(AddK, CUFF_KC, true, x + y)
                    CUFF_ARITH(SubK, CUFF_KC, true, x - y)
                    CUFF_ARITH(MulK, CUFF_KC, true, x * y)
                    CUFF_DIVIDE(DivK, CUFF_KC, true)
                    CUFF_MODULO(ModK, CUFF_KC, true)
                    CUFF_ARITH(PowK, CUFF_KC, true, powerOf(x, y, CUFF_NODE(BinaryOp)->loc))
                    CUFF_VM_CASE(Neg)
                    {
                        const Value &v = CUFF_RB.v;
                        if (CUFF_UNLIKELY(!v.isNumber()))
                            throwNegate(v, *CUFF_NODE(UnaryOp));
                        CUFF_RA.v.setNumber(-v.asNumber());
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Not)
                    {
                        CUFF_RA.v.setBool(!truthy(CUFF_RB.v));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ToBool)
                    {
                        CUFF_RA.v.setBool(truthy(CUFF_RB.v));
                        CUFF_VM_NEXT();
                    }
                    CUFF_COMPARE(Lt, <)
                    CUFF_COMPARE(Le, <=)
                    CUFF_COMPARE(Gt, >)
                    CUFF_COMPARE(Ge, >=)
                    CUFF_VM_CASE(Eq)
                    {
                        const Value &l = CUFF_RB.v;
                        const Value &r = CUFF_RC.v;
                        const bool t = CUFF_LIKELY(l.isNumber() && r.isNumber()) ? l.asNumber() == r.asNumber() : l.strictEquals(r);
                        CUFF_RA.v.setBool(t);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Ne)
                    {
                        const Value &l = CUFF_RB.v;
                        const Value &r = CUFF_RC.v;
                        const bool t = CUFF_LIKELY(l.isNumber() && r.isNumber()) ? l.asNumber() != r.asNumber() : !l.strictEquals(r);
                        CUFF_RA.v.setBool(t);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Matches)
                    {
                        {
                            Value v = matchesOp(*CUFF_NODE(BinaryOp), CUFF_RB.v, CUFF_RC.v);
                            CUFF_RA.v = std::move(v);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Isa)
                    {
                        const bool t = isaCheck(CUFF_RB.v, *CUFF_NODE(IsaExpr));
                        CUFF_RA.v.setBool(t);
                        CUFF_VM_NEXT();
                    }

                    // ---- jumps ----
                    CUFF_VM_CASE(Jmp)
                    {
                        ip += i->c;
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(JmpIf)
                    {
                        const bool t = truthy(CUFF_RA.v);
                        CUFF_JUMP_IF(t)
                    }
                    CUFF_JUMP_ORDERED(JLt, CUFF_RB.v, r.isNumber(), <)
                    CUFF_JUMP_ORDERED(JLe, CUFF_RB.v, r.isNumber(), <=)
                    CUFF_JUMP_ORDERED(JGt, CUFF_RB.v, r.isNumber(), >)
                    CUFF_JUMP_ORDERED(JGe, CUFF_RB.v, r.isNumber(), >=)
                    CUFF_JUMP_EQUAL(JEq, CUFF_RB.v, r.isNumber())
                    CUFF_JUMP_ORDERED(JLtK, CUFF_KB, true, <)
                    CUFF_JUMP_ORDERED(JLeK, CUFF_KB, true, <=)
                    CUFF_JUMP_ORDERED(JGtK, CUFF_KB, true, >)
                    CUFF_JUMP_ORDERED(JGeK, CUFF_KB, true, >=)
                    CUFF_JUMP_EQUAL(JEqK, CUFF_KB, true)
                    CUFF_VM_CASE(JAnd)
                    {
                        if (!truthy(CUFF_RB.v))
                        {
                            CUFF_RA.v.setBool(false);
                            ip += i->c;
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(JOr)
                    {
                        if (truthy(CUFF_RB.v))
                        {
                            CUFF_RA.v.setBool(true);
                            ip += i->c;
                        }
                        CUFF_VM_NEXT();
                    }

                    // ---- lists, maps, text ----
                    CUFF_VM_CASE(NewList)
                    {
                        {
                            auto list = std::make_shared<ValueList>();
                            Reg *from = &CUFF_RB;
                            list->items.reserve(static_cast<size_t>(i->c));
                            for (int32_t k = 0; k < i->c; ++k)
                                list->items.push_back(std::move(from[k].v));
                            CUFF_RA.v = Value::makeList(std::move(list));
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ListAdd)
                    {
                        auto &items = CUFF_RA.v.asList()->items;
                        if (i->x & bc::kMoveSource)
                            items.push_back(std::move(CUFF_RB.v));
                        else
                            items.push_back(CUFF_RB.v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(NewMap)
                    {
                        {
                            auto map = std::make_shared<ValueMap>();
                            map->reserve(static_cast<size_t>(i->c));
                            CUFF_RA.v = Value::makeMap(std::move(map));
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(MapSet)
                    {
                        ValueMap &map = *CUFF_RA.v.asMap();
                        if (i->x & bc::kMoveSource)
                            map.set(CUFF_RB.v.asStr(), std::move(CUFF_RC.v));
                        else
                            map.set(CUFF_RB.v.asStr(), CUFF_RC.v);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ChkKey)
                    {
                        if (CUFF_UNLIKELY(!CUFF_RA.v.isStr()))
                            throw TypeError("map keys must be strings", CUFF_NODE(MapLiteral)->loc);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Index)
                    {
                        const Value &target = CUFF_RB.v;
                        const Value &index = CUFF_RC.v;
                        if (CUFF_LIKELY(target.isList() && index.isNumber()))
                        {
                            const auto &items = target.asList()->items;
                            const double d = index.asNumber();
                            if (CUFF_LIKELY(d >= 1.0 && d <= static_cast<double>(items.size())))
                            {
                                const size_t at = static_cast<size_t>(d);
                                if (CUFF_LIKELY(static_cast<double>(at) == d))
                                {
                                    CUFF_RA.v.putOwned(items[at - 1]);
                                    CUFF_VM_NEXT();
                                }
                            }
                        }
                        {
                            Value v = indexInto(target, index, CUFF_NODE(IndexAccess)->loc);
                            CUFF_RA.v = std::move(v);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Slice)
                    {
                        {
                            const Reg *bounds = &CUFF_RC;
                            Value v = sliceInto(CUFF_RB.v, bounds[0].v, bounds[1].v, CUFF_NODE(SliceAccess)->loc);
                            CUFF_RA.v = std::move(v);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ToStr)
                    {
                        Value &v = CUFF_RA.v;
                        if (!v.isStr())
                        {
                            toStr(v, *CUFF_NODE(FStringExpr));
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Concat)
                    {
                        concat(CUFF_RA.v, &CUFF_RB, *CUFF_NODE(FStringExpr));
                        CUFF_VM_NEXT();
                    }

                    // ---- objects ----
                    CUFF_VM_CASE(GetField)
                    {
                        const Value &target = CUFF_RB.v;
                        bc::FieldCache &cache = P->fieldCaches[static_cast<size_t>(i->c)];
                        if (CUFF_LIKELY(target.isObject()))
                        {
                            const ObjectData &o = *target.asObject();
                            if (CUFF_LIKELY(o.cls->serial == cache.serial))
                            {
                                const ClassInfo::Field &f = o.cls->fields[cache.index];
                                if (CUFF_LIKELY(!f.isPrivate || fr->owner == f.declaredIn))
                                {
                                    CUFF_RA.v.putOwned(o.fields[cache.index]);
                                    CUFF_VM_NEXT();
                                }
                            }
                        }
                        getFieldSlow(CUFF_RA.v, target, *CUFF_NODE(MemberAccess), fr->owner, cache);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(SetField)
                    {
                        const Value &holder = CUFF_RA.v;
                        Value &src = CUFF_RB.v;
                        bc::FieldCache &cache = P->fieldCaches[static_cast<size_t>(i->c)];
                        if (CUFF_LIKELY(holder.isObject()))
                        {
                            ObjectData &o = *holder.asObject();
                            if (CUFF_LIKELY(o.cls->serial == cache.serial))
                            {
                                const ClassInfo::Field &f = o.cls->fields[cache.index];
                                if (CUFF_LIKELY(!f.isConstant && (!f.isPrivate || fr->owner == f.declaredIn) &&
                                                (f.typeKind == 0 || isKind(src, f.typeKind))))
                                {
                                    CUFF_TAKE(o.fields[cache.index], src, i->x & bc::kMoveSource);
                                    CUFF_VM_NEXT();
                                }
                            }
                        }
                        {
                            Value v;
                            CUFF_TAKE(v, src, i->x & bc::kMoveSource);
                            setFieldSlow(holder, std::move(v), *CUFF_NODE(PlaceStep), fr->owner, cache);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(With)
                    {
                        const Value &object = CUFF_RB.v;
                        if (CUFF_UNLIKELY(!object.isObject()))
                            throwWithNeedsObject(object, *CUFF_NODE(WithStmt));
                        CUFF_RA.v = object;
                        CUFF_VM_NEXT();
                    }

                    // ---- calls ----
                    CUFF_VM_CASE(Call)
                    {
                        const uint32_t id = static_cast<uint32_t>(i->c);
                        Reg *base = &CUFF_RA;
                        if (CUFF_LIKELY(id < userById_.size() && userById_[id].decl != nullptr))
                        {
                            UserFn &fn = userById_[id];
                            Proto *p = fn.proto;
                            if (CUFF_UNLIKELY(!p || !p->ready))
                                p = fn.proto = protoOf(*fn.decl);
                            if (invoke(*fr, p, base, static_cast<uint32_t>(i->b), &base->v, nullptr, fn.decl->name,
                                       (i->x & bc::kCallAwaited) != 0, CUFF_SITE_LOC))
                                CUFF_LOAD_FRAME();
                            CUFF_VM_NEXT();
                        }
                        if (callOther(*fr, *i, id, base, &base->v))
                            CUFF_LOAD_FRAME();
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(CallW)
                    {
                        if (callWith(*fr, *i, &CUFF_RA, static_cast<uint32_t>(i->b)))
                            CUFF_LOAD_FRAME();
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(CallM)
                    {
                        Reg *base = &CUFF_RA;
                        const uint32_t argc = static_cast<uint32_t>(i->b);
                        bc::MethodCache &cache = P->methodCaches[static_cast<size_t>(i->c)];
                        const ClassInfo::Method *m;
                        if (CUFF_LIKELY(base->v.isObject() && base->v.asObject()->cls->serial == cache.serial))
                        {
                            m = static_cast<const ClassInfo::Method *>(cache.method);
                        }
                        else
                        {
                            m = findMethodFor(*fr, *i, base, argc, cache);
                            if (!m)
                                CUFF_VM_NEXT();
                        }
                        if (CUFF_UNLIKELY(m->decl->isPrivate && m->owner != fr->owner))
                            throwPrivateMethod(*fr, *i, *m);
                        Proto *p = cache.proto;
                        if (CUFF_UNLIKELY(!p->ready))
                            p = cache.proto = protoOf(*m->decl);
                        if (invoke(*fr, p, base, argc, &base->v, m->owner, m->decl->name, (i->x & bc::kCallAwaited) != 0, CUFF_SITE_LOC))
                            CUFF_LOAD_FRAME();
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(CallS)
                    {
                        if (callSuper(*fr, *i, &CUFF_RA, static_cast<uint32_t>(i->b)))
                            CUFF_LOAD_FRAME();
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(AwaitF)
                    {
                        awaitCheck(*fr, *i, *CUFF_NODE(AwaitExpr));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(AwaitM)
                    {
                        const MethodCall &mc = *std::get_if<MethodCall>(&CUFF_NODE(AwaitExpr)->call->data);
                        const Value &receiver = CUFF_RA.v;
                        const ClassInfo::Method *m = receiver.isObject() ? receiver.asObject()->cls->findMethod(mc.nameId) : nullptr;
                        if (!m || !m->decl->isAsync)
                            throwAwaitOnNonAsync(mc.name, mc.loc);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(AwaitS)
                    {
                        const SuperCall &sc = *std::get_if<SuperCall>(&CUFF_NODE(AwaitExpr)->call->data);
                        const ClassInfo::Method *m = parentOf(*fr, sc.loc).findMethod(sc.nameId);
                        if (m && !m->decl->isAsync)
                            throwAwaitOnNonAsync(sc.name, sc.loc);
                        CUFF_VM_NEXT();
                    }
                    // A frame's result is left in its first register, which is released last. What an `init` returns
                    // is not used: its call gives the object, and that is in the first register already.
                    CUFF_VM_CASE(Ret)
                    {
                        if (CUFF_LIKELY(fr->constructing == nullptr))
                        {
                            Value &result = CUFF_RA.v;
                            if (&result != &regs[0].v)
                                regs[0].v.take(result);
                        }
                        goto do_return;
                    }
                    CUFF_VM_CASE(RetNil)
                    {
                        if (CUFF_LIKELY(fr->constructing == nullptr))
                            regs[0].v.clear();
                        goto do_return;
                    }
                    CUFF_VM_CASE(ArgJmp)
                    {
                        if (fr->argc > static_cast<uint32_t>(i->a))
                            ip += i->c;
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(DefChk)
                    {
                        Reg &cell = CUFF_RA;
                        if (P->typedParams && i->x != 0 && CUFF_UNLIKELY(!isKind(cell.v, i->x)))
                            checkDefault(*fr, static_cast<uint32_t>(i->b), cell.v);
                        if (i->c)
                            cell.meta = static_cast<uint32_t>(i->c);
                        CUFF_VM_NEXT();
                    }

                    // ---- loops ----
                    CUFF_VM_CASE(ForPrep)
                    {
                        Reg *base = &CUFF_RA;
                        const LoopStmt &loop = *CUFF_NODE(LoopStmt);
                        forPrepare(base, loop);
                        CUFF_TICK(loop.loc);
                        if (i->b != bc::kNoCell)
                            CUFF_RB.v.setNumber(base[0].v.asNumber());
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ForNext)
                    {
                        Reg *base = &CUFF_RA;
                        const double at = base[0].v.asNumber();
                        if (at != base[1].v.asNumber())
                        {
                            const double next = at + base[2].v.asNumber();
                            base[0].v.setNumber(next);
                            CUFF_TICK(CUFF_NODE(LoopStmt)->loc);
                            if (i->b != bc::kNoCell)
                                CUFF_RB.v.setNumber(next);
                            ip += i->c;
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(EachPrep)
                    {
                        eachPrepare(&CUFF_RA, CUFF_RB.v, *CUFF_NODE(LoopStmt));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(EachNext)
                    {
                        Reg *base = &CUFF_RA;
                        bool more;
                        {
                            Value item;
                            more = eachNext(base, item);
                            if (more)
                            {
                                CUFF_TICK(CUFF_NODE(LoopStmt)->loc);
                                if (i->x != 0 && CUFF_UNLIKELY(!isKind(item, i->x)))
                                    checkLoopValue(*CUFF_NODE(LoopStmt), item);
                                CUFF_RB.v = std::move(item);
                            }
                        }
                        if (!more)
                        {
                            base[0].v.clear();
                            ip += i->c;
                        }
                        CUFF_VM_NEXT();
                    }

                    // ---- statements ----
                    CUFF_VM_CASE(GlobalChk)
                    {
                        globalCheck(*fr, *CUFF_NODE(GlobalStmt));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Bridge)
                    {
                        CUFF_RA.meta = bc::kMetaDeclared;
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(DefFunc)
                    {
                        const FunctionDecl &decl = *CUFF_NODE(FunctionDecl);
                        if (fr->inFunction)
                            throwNestedFunction(decl);
                        registerFunction(decl);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Use)
                    {
                        useStatement(*fr, *CUFF_NODE(UseStmt), i->a);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Fail)
                    {
                        failWith(CUFF_RA.v, *CUFF_NODE(FailStmt));
                    }
                    CUFF_VM_CASE(PlaceChk)
                    {
                        placeCheck(*fr, *i, P->nodes[static_cast<size_t>(i - P->code.data())]);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(PlaceSet)
                    {
                        placeSet(*fr, *i, *CUFF_NODE(AssignStmt));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(CollOp)
                    {
                        collectionOp(*fr, *i, *CUFF_NODE(CollectionOpStmt));
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ListPush)
                    {
                        {
                            Value v;
                            CUFF_TAKE(v, CUFF_RB.v, i->x & bc::kMoveSource);
                            applyCollectionOp(*CUFF_NODE(CollectionOpStmt), CUFF_RA.v, std::move(v));
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(SetIndex)
                    {
                        {
                            Value v;
                            CUFF_TAKE(v, CUFF_RC.v, i->x & bc::kMoveSource);
                            writeIndex(CUFF_RA.v, CUFF_RB.v, std::move(v), CUFF_NODE(PlaceStep)->loc);
                        }
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(ErrChk)
                    {
                        const OrElseStmt &oe = *CUFF_NODE(OrElseStmt);
                        checkDeclaredType(oe.errorType, CUFF_RA.v, oe.errorName, oe.typeLoc);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(RetChk)
                    {
                        if (!fr->inFunction)
                            throwReturnValueOutsideFunction(CUFF_NODE(ReturnStmt)->loc);
                        CUFF_VM_NEXT();
                    }
                    CUFF_VM_CASE(Raise)
                    {
                        raise(*fr, static_cast<bc::RaiseKind>(i->x), P->nodes[static_cast<size_t>(i - P->code.data())]);
                    }
                    CUFF_VM_CASE(DynReset)
                    {
                        for (size_t d = dynVars_.size(); d > fr->dynBase; --d)
                            if (dynVars_[d - 1].scope >= i->a && dynVars_[d - 1].scope <= i->b)
                                dynVars_.erase(dynVars_.begin() + static_cast<std::ptrdiff_t>(d - 1));
                        CUFF_VM_NEXT();
                    }
#if !CUFF_VM_LABELS
                    case Op::Count:
                        break;
                    }
                    throw InternalEngineError("unknown instruction");
#endif

                do_return:
                {
                    if (CUFF_UNLIKELY(fr->constructing != nullptr))
                        fr->constructing->constructing = false;
                    for (uint32_t k = P->numCells - 1; k != 0; --k)
                        regs[k].v.clear();
                    if (fr->counted)
                        --callDepth_;
                    if (CUFF_UNLIKELY(P->hasModuleUse) && dynVars_.size() > fr->dynBase)
                        dynVars_.resize(fr->dynBase);
                    Value *ret = fr->ret;
                    const uint32_t segment = fr->segment;
                    popFrame();
                    const uint32_t below = frameCount_ ? top_->segment : 0;
                    if (CUFF_UNLIKELY(below != segment))
                    {
                        curSeg_ = below;
                        segEnd_ = segments_[below].cells.get() + segments_[below].size;
                    }
                    // Most calls ask for the result in the very register it is in.
                    if (ret != &regs[0].v)
                        ret->take(regs[0].v);
                    if (frameCount_ == stopCount)
                        return;
                    CUFF_LOAD_FRAME();
                    CUFF_VM_NEXT();
                }
                }
                catch (CuffError &e)
                {
                    if (!handleError(e, stopCount))
                        throw;
                    CUFF_LOAD_FRAME();
                }
                catch (...)
                {
                    unwindTo(stopCount);
                    throw;
                }
            }

#undef CUFF_LOAD_FRAME
#undef CUFF_REG
#undef CUFF_RA
#undef CUFF_RB
#undef CUFF_RC
#undef CUFF_KB
#undef CUFF_KC
#undef CUFF_NODE
#undef CUFF_SITE_LOC
#undef CUFF_TICK
#undef CUFF_TAKE
#undef CUFF_VM_CASE
#undef CUFF_VM_NEXT
#undef CUFF_ARITH
#undef CUFF_DIVIDE
#undef CUFF_MODULO
#undef CUFF_COMPARE
#undef CUFF_JUMP_IF
#undef CUFF_JUMP_ORDERED
#undef CUFF_JUMP_EQUAL
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

        // ---- programs and modules ----------------------------------------------------------------------------------

        // Runs the top level of a program against `vars`: the main program against the interpreter's own variables, a
        // module against a table of its own. Each statement is compiled just before it runs, so the compiler knows
        // which variables the statements before it have declared, and a statement that is never reached costs
        // nothing. `inFunction`, `pure` and `owner` describe the code that asked for the program: nothing for the main
        // program, the function that wrote `use` for a module.
        void execProgram(const Program &program, bc::VarTable &vars, bool inFunction, bool pure, const ClassInfo *owner)
        {
            for (const auto &s : program.statements)
                if (s->kind == StmtKind::FunctionDecl)
                    registerFunction(*std::get_if<FunctionDecl>(&s->data));
            registerClasses(program);

            Proto proto;
            for (const auto &s : program.statements)
            {
                if (s->kind == StmtKind::FunctionDecl || s->kind == StmtKind::ClassDecl)
                    continue;
                compiler_.compileStatement(*s, vars, program.source, proto);
                if (CUFF_UNLIKELY(config_.onCompiled != nullptr))
                    config_.onCompiled(proto);
                Value ignored;
                Frame *f = enterPlain(&proto, &ignored);
                f->vars = &vars;
                f->inFunction = inFunction;
                f->isPure = pure;
                f->owner = owner;
                runLoop(frameCount_ - 1);
            }

            drainTaskQueue();
        }

        // `use DLC:name` adds a library's functions; `use name from "path"` runs a module once and brings what its top
        // level declared into the scope that wrote the statement.
        CUFF_NOINLINE void useStatement(Frame &fr, const UseStmt &use, int32_t scope)
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
            loadCustomModule(fr, use.name, use.path, use.loc, scope);
        }

        // One variable of a module's top level, declared in scope `scope` of the code that `fr` is running. The
        // compiler gave every variable that code declares a register; a name it never declares is kept beside the frame.
        void importVariable(Frame &fr, int32_t scope, uint32_t nameId, const Value &v, uint32_t meta)
        {
            meta &= ~bc::kMetaWatched;
            Proto &p = *fr.proto;
            if (scope == 0 && p.kind == Proto::Kind::TopLevel)
            {
                Reg *slot = fr.vars->slotFor(nameId);
                slot->v = v;
                setSlotMeta(*slot, meta);
                return;
            }
            auto it = p.cellByScopeName.find(Proto::scopeNameKey(static_cast<uint32_t>(scope), nameId));
            if (it != p.cellByScopeName.end())
            {
                Reg &cell = cellAt(fr, it->second);
                cell.v = v;
                cell.meta = meta;
                return;
            }
            for (size_t d = fr.dynBase; d < dynVars_.size(); ++d)
                if (dynVars_[d].nameId == nameId && dynVars_[d].scope == scope)
                {
                    dynVars_[d].cell.v = v;
                    dynVars_[d].cell.meta = meta;
                    return;
                }
            dynVars_.push_back(DynVar{nameId, scope, Reg{v, meta}});
        }

        void loadCustomModule(Frame &fr, const std::string &moduleName, const std::string &relPath, const SourceLocation &loc,
                              int32_t scope)
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

            const std::string key = canon.u8string();  // string() throws on Windows for a name outside the ANSI code page

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
                err.related = "  in '" + shown + "' [" + errorCodeTag(e.code) + "] at " + e.location.toString() + ":";
                Snippet snippet;
                if (buildSnippet(source->text, e.location, snippet))
                    err.related += "\n" + formatSnippet(snippet, e.message, "  ");
                else
                    err.related += " " + e.message;
                throw err;
            }

            // The module's AST must outlive its functions, which stay registered even if the module body fails.
            loadedModules_.push_back(std::move(modProgram));
            const Program &program = *loadedModules_.back();

            ImportDepthGuard depthGuard(moduleDepth_);
            guardNative(loc);
            // Only the module's top level runs against these variables. Its functions are ordinary functions of the
            // interpreter, so they reach these names through `global x` once the copy below has put them into the
            // main program.
            bc::VarTable moduleVars;
            try
            {
                execProgram(program, moduleVars, fr.inFunction, fr.isPure, fr.owner);
            }
            catch (CuffError &e)
            {
                e.attachSource(program.source);
                throw;
            }
            for (size_t k = 0; k < moduleVars.size(); ++k)
            {
                const Reg &slot = *moduleVars.at(k);
                if (slot.meta & bc::kMetaDeclared)
                    importVariable(fr, scope, moduleVars.nameAt(k), slot.v, slot.meta);
            }
            mark.committed = true;
        }

    };

}
