#pragma once

#include "../common/SourceText.h"
#include "../interpreter/Value.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cuff
{
    struct FunctionDecl;
}

namespace cuff::bc
{

    // One place a value can be: a register of a running function, or a variable of a program's top level. `meta` says
    // what a variable was declared as; the machine reads it only where the compiler could not work the answer out.
    struct Reg
    {
        Value v;
        uint32_t meta = 0;
    };

    // meta: bit 0 declared, bit 1 constant, bit 2 the value may not be of the declared type (a declaration that failed
    // and was recovered by `or_else` holds `empty`), bit 3 compiled code has assumed something about this variable,
    // and the declared type above them (0 accepts every value; see Compiler::typeCode).
    constexpr uint32_t kMetaDeclared = 1u << 0;
    constexpr uint32_t kMetaConst = 1u << 1;
    constexpr uint32_t kMetaLoose = 1u << 2;
    constexpr uint32_t kMetaWatched = 1u << 3;
    constexpr uint32_t kMetaTypeShift = 4;
    constexpr uint32_t kMetaAttrs = ~(kMetaLoose | kMetaWatched);

    constexpr uint32_t makeMeta(uint32_t typeCode, bool isConstant)
    {
        return (typeCode << kMetaTypeShift) | (isConstant ? kMetaConst : 0u) | kMetaDeclared;
    }

    // A class as a declared type is its name's number above this; below it is a TypeKind.
    constexpr uint32_t kClassTypeBase = 16;

    constexpr int32_t kCellBytes = static_cast<int32_t>(sizeof(Reg));
    constexpr int32_t kConstBytes = static_cast<int32_t>(sizeof(Value));
    constexpr int32_t kNoCell = -1;

    // The instruction set, with what each instruction's operands `a`, `b` and `c` are: `r` a register, `k` a constant,
    // `g` a variable of the top level, `j` a jump, `n` a name to look up, `m` a variable's meta, `t` a declared type,
    // `o` the root of a place (what it is depends on the root's kind in `x`), `c` an inline cache, `f` the name of a
    // function or class, `i` a plain number, `-` not used.
    // docs/internals/bytecode.md describes each instruction, and debug/BytecodePrinter.h prints them.
#define CUFF_OPCODES(X)                                                                                                \
    X(LoadK, "rk-") X(LoadNil, "r--") X(LoadBool, "r--") X(Move, "rr-") X(Take, "rr-") X(GetG, "rg-")                  \
    X(GetGC, "rg-") X(SetG, "gr-") X(SetGU, "gr-") X(CheckG, "g--") X(DeclG, "grm") X(PlaceholdG, "g-m")               \
    X(GetName, "rn-") X(Store, "rrt") X(Decl, "rrm") X(SetMeta, "r-m") X(Undecl, "ri-") X(ChkDecl, "r--")              \
    X(Freeze, "r--") X(PlaceholdL, "r-m") X(BindL, "rrm") X(BindG, "grm") X(ChkLoop, "r--") X(Add, "rrr")              \
    X(Sub, "rrr") X(Mul, "rrr") X(Div, "rrr") X(Mod, "rrr") X(Pow, "rrr") X(AddK, "rrk") X(SubK, "rrk")                \
    X(MulK, "rrk") X(DivK, "rrk") X(ModK, "rrk") X(PowK, "rrk") X(Neg, "rr-") X(Not, "rr-") X(ToBool, "rr-")           \
    X(Lt, "rrr") X(Le, "rrr") X(Gt, "rrr") X(Ge, "rrr") X(Eq, "rrr") X(Ne, "rrr") X(Matches, "rrr") X(Isa, "rr-")      \
    X(Jmp, "--j") X(JmpIf, "r-j") X(JLt, "rrj") X(JLe, "rrj") X(JGt, "rrj") X(JGe, "rrj") X(JEq, "rrj")                \
    X(JLtK, "rkj") X(JLeK, "rkj") X(JGtK, "rkj") X(JGeK, "rkj") X(JEqK, "rkj") X(JAnd, "rrj") X(JOr, "rrj")            \
    X(NewList, "rri") X(ListAdd, "rr-") X(NewMap, "r-i") X(MapSet, "rrr") X(ChkKey, "r--") X(Index, "rrr")             \
    X(Slice, "rrr") X(ToStr, "r--") X(Concat, "rr-") X(GetField, "rrc") X(SetField, "rrc") X(With, "rr-")              \
    X(Call, "rif") X(CallW, "rii") X(CallM, "ric") X(CallS, "ri-") X(AwaitF, "--i") X(AwaitM, "r--")                   \
    X(AwaitS, "---") X(Ret, "r--") X(RetNil, "---") X(ArgJmp, "i-j") X(DefChk, "rim") X(ForPrep, "rr-")                \
    X(ForNext, "rrj") X(EachPrep, "rr-") X(EachNext, "rrj") X(GlobalChk, "---") X(Bridge, "r--") X(DefFunc, "---")     \
    X(Use, "i--") X(Fail, "r--") X(PlaceChk, "--o") X(PlaceSet, "rro") X(CollOp, "rro") X(ListPush, "rr-")             \
    X(SetIndex, "rrr") X(ErrChk, "r--") X(RetChk, "---") X(Raise, "---") X(DynReset, "ii-")

    enum class Op : uint16_t
    {
#define CUFF_OP_ENUM(name, operands) name,
        CUFF_OPCODES(CUFF_OP_ENUM)
#undef CUFF_OP_ENUM
        Count
    };

    // Registers and constants are byte offsets (from the frame's first register, from the first constant), so that
    // reaching one is an addition; a jump is counted in instructions from the instruction after it.
    struct Instr
    {
        uint16_t op;
        uint16_t x;
        int32_t a;
        int32_t b;
        int32_t c;
    };

    // `x` of a conditional jump.
    constexpr uint16_t kJumpIfTrue = 1;  // jump when the test holds (otherwise when it does not)
    constexpr uint16_t kJumpTicks = 2;   // taking the jump is one pass of a loop: count it against the step budget

    // `x` of Store and of the instructions that take a value out of a register.
    constexpr uint16_t kKindMask = 0xFF;
    constexpr uint16_t kMoveSource = 0x100; // the source is a temporary: take it instead of copying it

    // `x` of the call instructions.
    constexpr uint16_t kCallAwaited = 1;

    // What a statically known error is (Raise).
    enum class RaiseKind : uint16_t
    {
        ConstantName,     // a constant whose name is not in capitals
        ConstantAssign,   // an assignment to a constant
        ConstantCollection,
        StopOutsideLoop,
        SkipOutsideLoop,
        ReturnOutsideFunction,
        PureFieldWrite,
        PureFieldWriteCollection,
        LoopConstant, // a constant used as a loop variable
    };

    // Where a place (the target of an assignment, `add` or `remove`) starts.
    enum class RootKind : uint16_t
    {
        Self,    // c: unused
        WithObj, // c: the register of the `with` object
        Local,   // c: the variable's register
        Global,  // c: the variable (g)
        Name,    // c: a NameRec, looked up when the statement runs
    };
    constexpr uint16_t kRootMask = 0x0F;
    constexpr uint16_t kRootCollection = 0x10; // the place is the target of `add` or `remove`, not of an assignment
    constexpr uint16_t kRootConstant = 0x20;   // a Local root is a constant (the register's own meta may not be kept)

    // An `or_else`: an error raised by instructions [start, end) of the function continues at `target`. `errCell`
    // is the register that receives the Error object, or kNoCell.
    struct Handler
    {
        uint32_t start;
        uint32_t end;
        uint32_t target;
        int32_t errCell;
    };

    // One step of looking a name up when the program runs, from the innermost scope outwards.
    struct NameStep
    {
        enum Kind : uint8_t
        {
            Local,      // a: register. The variable if its declaration has run
            LocalSure,  // a: register. Its declaration has run
            With,       // a: register of a `with` object, b: cache. A field of that object
            Dynamic,    // a: scope. A variable a module brought into that scope
            Bridge,     // a: register whose meta says `global name` has run, b: g
            BridgeSure, // b: g. `global name` has run
            Global,     // b: g. A variable of the top level this code belongs to
        };
        Kind kind;
        int32_t a;
        int32_t b;
    };

    struct NameRec
    {
        uint32_t nameId;
        const std::string *name;
        uint32_t firstStep;
        uint32_t stepCount;
    };

    struct FieldCache
    {
        uint32_t serial = 0;
        uint32_t index = 0;
    };

    struct Proto;

    struct MethodCache
    {
        uint32_t serial = 0;
        const void *method = nullptr; // ClassInfo::Method
        Proto *proto = nullptr;
    };

    // Whether a class has a field of a given name, for a name written inside a `with` block.
    struct WithCache
    {
        uint32_t serial = 0;
        int32_t index = -1;
    };

    // The compiled form of a function, of one statement of a program's top level, or of the default value of a field.
    struct Proto
    {
        enum class Kind : uint8_t
        {
            Function,
            TopLevel,
            Thunk
        };

        std::vector<Instr> code;
        std::vector<const void *> nodes; // the syntax tree node each instruction came from: names and locations for errors
        std::vector<Value> consts;
        std::vector<Reg *> gcells;
        std::vector<Handler> handlers;
        std::vector<NameRec> names;
        std::vector<NameStep> steps;
        std::vector<std::vector<int32_t>> withLists; // the `with` objects around a call, innermost first
        std::vector<FieldCache> fieldCaches;
        std::vector<MethodCache> methodCaches;
        std::vector<WithCache> withCaches;
        std::vector<uint32_t> paramMeta;
        std::unordered_map<uint64_t, int32_t> cellByScopeName; // only where a module can add variables to a scope
        std::unordered_map<uint32_t, const SourceLocation *> tickLocs; // the loop a pass-counting jump belongs to
        std::unordered_map<uint32_t, uint32_t> callRecs; // a call by a name that a variable in scope also has: its NameRec

        Kind kind = Kind::Function;
        bool ready = false;
        bool isPure = false;
        bool isAsync = false;
        bool isMethod = false;
        bool typedParams = false;
        bool usesMeta = false;       // some variable's meta is read: clear the named registers' meta on entry
        bool usesSnapshot = false;   // compiled against what the program's variables were declared as at the time
        bool hasModuleUse = false;
        uint32_t numCells = 0;
        uint32_t namedBegin = 0; // the registers of named variables other than parameters
        uint32_t namedEnd = 0;
        uint32_t nparams = 0;
        uint32_t required = 0;
        uint32_t paramBase = 0; // 1 for a method: register 0 is `self`
        const FunctionDecl *decl = nullptr;
        SourceRef source;
        uint32_t line = 0; // where the code was written, for a listing

        static uint64_t scopeNameKey(uint32_t scope, uint32_t nameId)
        {
            return (static_cast<uint64_t>(scope) << 32) | nameId;
        }
    };

    // The variables of a program's top level (or of a module's). The compiler gives each name a slot when it first
    // meets it, declared or not, and compiled code holds the slot's address, which never changes.
    class VarTable
    {
    public:
        VarTable() = default;
        VarTable(const VarTable &) = delete;
        VarTable &operator=(const VarTable &) = delete;

        Reg *slotFor(uint32_t nameId)
        {
            auto it = index_.find(nameId);
            if (it != index_.end())
                return at(it->second);
            const uint32_t idx = static_cast<uint32_t>(names_.size());
            if ((idx & (kChunk - 1)) == 0)
                chunks_.push_back(std::make_unique<Reg[]>(kChunk));
            names_.push_back(nameId);
            index_.emplace(nameId, idx);
            return at(idx);
        }

        Reg *find(uint32_t nameId)
        {
            auto it = index_.find(nameId);
            return it == index_.end() ? nullptr : at(it->second);
        }

        bool isDeclared(uint32_t nameId)
        {
            const Reg *r = find(nameId);
            return r && (r->meta & kMetaDeclared);
        }

        size_t size() const { return names_.size(); }
        uint32_t nameAt(size_t i) const { return names_[i]; }
        Reg *at(size_t i) { return &chunks_[i / kChunk][i % kChunk]; }

    private:
        static constexpr size_t kChunk = 64;
        std::vector<std::unique_ptr<Reg[]>> chunks_;
        std::vector<uint32_t> names_;
        std::unordered_map<uint32_t, uint32_t> index_;
    };

}
