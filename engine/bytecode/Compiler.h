#pragma once

#include "Bytecode.h"
#include "../common/Attributes.h"
#include "../common/CuffError.h"
#include "../parser/ASTNodes.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cuff::bc
{

    inline uint32_t typeCode(const TypeSpec &type)
    {
        return type.kind == TypeKind::Class ? kClassTypeBase + type.classNameId : static_cast<uint32_t>(type.kind);
    }

    // Turns one function, one statement of a program's top level, or the default value of a field into a Proto.
    //
    // A variable of a function lives in a register, and the compiler decides for every use of a name what it stands
    // for. Where the text settles it (the declaration is an earlier statement of the same block or of a block around
    // it, so it has run whenever the use runs), the use is the register itself. Where it does not (a name declared in
    // an `if` and used after it, a name inside a `with` block, a declaration recovered by `or_else`), the use becomes a
    // lookup the machine performs with the rules of the language, and the variable's meta records what the compiler
    // could not know. The second form is always right; the first is used only where it gives the same answer.
    class Compiler
    {
    public:
        explicit Compiler(VarTable &mainTable) : main_(mainTable) {}

        // Below this address the compiler stops following nested syntax and raises E4-017 (Interpreter::planStack).
        void setStackLimit(uintptr_t limit) { stackLimit_ = limit; }

        void compileFunction(const FunctionDecl &decl, Proto &p)
        {
            begin(Proto::Kind::Function, nullptr, decl.source);
            decl_ = &decl;
            scanFunction(decl);
            run(p, [&] { genFunction(decl); });
            p.decl = &decl;
            p.line = static_cast<uint32_t>(decl.loc.line);
            p.isPure = decl.isPure;
            p.isAsync = decl.isAsync;
            p.isMethod = decl.isMethod;
            p.typedParams = decl.hasTypedParams;
            p.nparams = static_cast<uint32_t>(decl.params.size());
            p.required = static_cast<uint32_t>(decl.requiredParams);
            p.paramBase = decl.isMethod ? 1 : 0;
            p.paramMeta.clear();
            for (const TypeSpec &t : decl.paramTypes)
                p.paramMeta.push_back(makeMeta(typeCode(t), false));
        }

        void compileStatement(const Stmt &stmt, VarTable &table, const SourceRef &source, Proto &p)
        {
            begin(Proto::Kind::TopLevel, &table, source);
            scanStmt(stmt, 0, false);
            finishScan();
            run(p, [&]
                {
                    compileStmt(stmt);
                    emit(Op::RetNil);
                });
            p.line = static_cast<uint32_t>(std::visit([](const auto &n) { return n.loc.line; }, stmt.data));
        }

        void compileThunk(const Expr &expr, const SourceRef &source, Proto &p)
        {
            begin(Proto::Kind::Thunk, nullptr, source);
            finishScan();
            run(p, [&]
                {
                    const Operand v = gen(expr, kNoCell);
                    emit(Op::Ret, 0, off(v.cell));
                });
            p.line = static_cast<uint32_t>(locOf(expr).line);
        }

    private:
        // ---- what the text declares ------------------------------------------------------------------------------

        struct Var
        {
            uint32_t nameId = 0;
            uint32_t scope = 0;
            uint32_t cell = 0;
            uint32_t firstMeta = 0;
            bool sameAttrs = true;    // every statement that declares it gives it the same type, and none is constant
            bool anyConst = false;
            bool placeholder = false; // a declaration of it is guarded by or_else, which declares it `empty` on failure
            bool loopVar = false;
            bool needsMeta = false;   // some use has to ask the machine whether, and as what, it is declared
            bool hidden = false;      // a parameter whose default has not been reached yet
            bool definite = false;    // declared for certain at the statement being compiled
            bool defStatic = false;   // ... and as what is known too
            uint32_t defMeta = 0;
        };

        struct Scope
        {
            int parent = -1;
            bool isWith = false;
            int32_t withCell = kNoCell;
            uint32_t last = 0; // the scopes inside this one are numbered up to here
            std::vector<uint32_t> vars;
        };

        struct Bridge
        {
            uint32_t nameId = 0;
            uint32_t cell = 0;
            bool definite = false;
        };

        struct Operand
        {
            uint32_t cell;
            bool temp; // a register this expression owns: it may be taken from
        };

        struct Undo
        {
            enum Kind : uint8_t
            {
                OfVar,
                OfBridge,
                OfGlobal
            } kind;
            uint32_t index;
            bool definite;
            bool defStatic;
            uint32_t defMeta;
        };

        struct LoopCtx
        {
            std::vector<uint32_t> stops;
            std::vector<uint32_t> skips;
        };

        VarTable &main_;
        uintptr_t stackLimit_ = 0;

        Proto::Kind kind_ = Proto::Kind::Function;
        VarTable *table_ = nullptr; // a top-level statement: the variables of its program
        const FunctionDecl *decl_ = nullptr;
        SourceRef source_;
        bool hasModuleUse_ = false;

        std::vector<Var> vars_;
        std::vector<Scope> scopes_;
        std::vector<Bridge> bridges_;
        std::unordered_map<uint64_t, uint32_t> varIndex_;
        std::unordered_map<const void *, uint32_t> scopeOf_;
        std::unordered_set<uint32_t> topSites_; // the names this top-level statement declares at the top level
        uint32_t namedEnd_ = 0;

        // Reset for each pass.
        Proto *p_ = nullptr;
        uint32_t top_ = 0;
        uint32_t maxCells_ = 0;
        uint32_t curScope_ = 0;
        bool restart_ = false;
        bool usesMeta_ = false;
        bool usesSnapshot_ = false;
        std::vector<Undo> undo_;
        std::vector<LoopCtx> loops_;
        std::unordered_map<uint32_t, bool> topDefinite_;
        std::unordered_map<uint64_t, int32_t> numberConsts_;
        std::unordered_map<const void *, int32_t> pointerConsts_;
        std::unordered_map<Reg *, int32_t> gIndex_;

        static int32_t off(uint32_t cell) { return static_cast<int32_t>(cell) * kCellBytes; }

        void begin(Proto::Kind kind, VarTable *table, const SourceRef &source)
        {
            kind_ = kind;
            table_ = table;
            decl_ = nullptr;
            source_ = source;
            hasModuleUse_ = false;
            vars_.clear();
            scopes_.clear();
            bridges_.clear();
            varIndex_.clear();
            scopeOf_.clear();
            topSites_.clear();
            scopes_.emplace_back();
        }

        template <typename Body>
        void run(Proto &p, Body body)
        {
            p_ = &p;
#ifdef CUFF_DYNAMIC_NAMES
            // A build for the tests: every variable is asked for at run time, so the paths that ordinary code only
            // takes for its unusual names run the whole suite.
            for (Var &v : vars_)
                v.needsMeta = true;
#endif
            // The first pass assumes that no variable needs its meta kept. A use that does (needMeta) asks for another
            // pass, and that pass can find more of them. They are few, so this settles at once in practice; after three
            // passes every variable keeps its meta, which leaves nothing to ask for.
            for (int pass = 0;; ++pass)
            {
                resetPass(p);
                body();
                if (!restart_)
                    break;
                if (pass == 2)
                    for (Var &v : vars_)
                        v.needsMeta = true;
            }
            p.kind = kind_;
            p.source = source_;
            p.numCells = maxCells_ + 1; // one spare register, so a call with no arguments has somewhere to return to
            p.namedBegin = kind_ == Proto::Kind::Function && decl_ ? (decl_->isMethod ? 1u : 0u) + static_cast<uint32_t>(decl_->params.size()) : 0;
            p.namedEnd = namedEnd_;
            p.usesMeta = usesMeta_;
            p.usesSnapshot = usesSnapshot_;
            p.hasModuleUse = hasModuleUse_;
            if (hasModuleUse_)
                for (const Var &v : vars_)
                    p.cellByScopeName[Proto::scopeNameKey(v.scope, v.nameId)] = off(v.cell);
            p.ready = true;
            p_ = nullptr;
        }

        void resetPass(Proto &p)
        {
            p.code.clear();
            p.nodes.clear();
            p.consts.clear();
            p.gcells.clear();
            p.handlers.clear();
            p.names.clear();
            p.steps.clear();
            p.withLists.clear();
            p.fieldCaches.clear();
            p.methodCaches.clear();
            p.withCaches.clear();
            p.cellByScopeName.clear();
            p.tickLocs.clear();
            p.callRecs.clear();
            top_ = namedEnd_;
            maxCells_ = namedEnd_;
            curScope_ = 0;
            restart_ = false;
            usesMeta_ = hasModuleUse_;
            usesSnapshot_ = false;
            undo_.clear();
            loops_.clear();
            topDefinite_.clear();
            numberConsts_.clear();
            pointerConsts_.clear();
            gIndex_.clear();
            for (Var &v : vars_)
            {
                v.definite = false;
                v.defStatic = false;
                v.hidden = false;
                if (v.needsMeta)
                    usesMeta_ = true;
            }
            for (Bridge &b : bridges_)
                b.definite = false;
            for (Scope &s : scopes_)
                s.withCell = kNoCell;
        }

        CUFF_ALWAYS_INLINE void guard(const SourceLocation &loc) const
        {
            if (stackPointer() < stackLimit_)
                stackExhausted(loc);
        }

        [[noreturn]] CUFF_COLD static void stackExhausted(const SourceLocation &loc)
        {
            throw StackOverflowError("native stack budget exhausted — calls and blocks are nested too deeply", loc);
        }

        // ---- the scan: which names each scope declares -------------------------------------------------------------

        Var *findVar(uint32_t scope, uint32_t nameId)
        {
            auto it = varIndex_.find(Proto::scopeNameKey(scope, nameId));
            return it == varIndex_.end() ? nullptr : &vars_[it->second];
        }

        Bridge *findBridge(uint32_t nameId)
        {
            for (Bridge &b : bridges_)
                if (b.nameId == nameId)
                    return &b;
            return nullptr;
        }

        void addSite(uint32_t scope, uint32_t nameId, uint32_t meta, bool placeholder, bool loopVar)
        {
            if (scope == 0 && kind_ == Proto::Kind::TopLevel)
            {
                topSites_.insert(nameId);
                return;
            }
            Var *v = findVar(scope, nameId);
            if (!v)
            {
                varIndex_[Proto::scopeNameKey(scope, nameId)] = static_cast<uint32_t>(vars_.size());
                scopes_[scope].vars.push_back(static_cast<uint32_t>(vars_.size()));
                vars_.emplace_back();
                v = &vars_.back();
                v->nameId = nameId;
                v->scope = scope;
                v->firstMeta = meta;
            }
            else if (v->firstMeta != meta)
            {
                v->sameAttrs = false;
            }
            if (meta & kMetaConst)
            {
                v->anyConst = true;
                v->sameAttrs = false;
            }
            v->placeholder = v->placeholder || placeholder;
            v->loopVar = v->loopVar || loopVar;
        }

        uint32_t newScope(uint32_t parent, bool isWith, const void *node)
        {
            const uint32_t id = static_cast<uint32_t>(scopes_.size());
            scopes_.emplace_back();
            scopes_[id].parent = static_cast<int>(parent);
            scopes_[id].isWith = isWith;
            scopeOf_[node] = id;
            return id;
        }

        void scanBody(const std::vector<std::unique_ptr<Stmt>> &body, uint32_t scope)
        {
            for (const auto &s : body)
                scanStmt(*s, scope, false);
        }

        void scanStmt(const Stmt &stmt, uint32_t scope, bool guarded)
        {
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
            {
                const auto &d = *std::get_if<DeclarationStmt>(&stmt.data);
                guard(d.loc);
                addSite(scope, d.nameId, makeMeta(typeCode(d.type), d.isConstant), guarded, false);
                break;
            }
            case StmtKind::LoopStmt:
            {
                const auto &l = *std::get_if<LoopStmt>(&stmt.data);
                guard(l.loc);
                if (l.kind != LoopStmt::LoopKind::While)
                    addSite(scope, l.repeatVarId, makeMeta(typeCode(l.varType), false), false, true);
                scanBody(l.body, scope);
                break;
            }
            case StmtKind::IfStmt:
            {
                const auto &i = *std::get_if<IfStmt>(&stmt.data);
                guard(i.loc);
                for (const auto &branch : i.branches)
                    scanBody(branch.body, scope);
                break;
            }
            case StmtKind::WithStmt:
            {
                const auto &w = *std::get_if<WithStmt>(&stmt.data);
                guard(w.loc);
                const uint32_t inner = newScope(scope, true, &w);
                scanBody(w.body, inner);
                scopes_[inner].last = static_cast<uint32_t>(scopes_.size()) - 1;
                break;
            }
            case StmtKind::OrElse:
            {
                const auto &oe = *std::get_if<OrElseStmt>(&stmt.data);
                guard(oe.loc);
                scanStmt(*oe.primaryStmt, scope, oe.primaryStmt->kind == StmtKind::Declaration);
                const uint32_t inner = newScope(scope, false, &oe);
                if (!oe.errorName.empty())
                    addSite(inner, oe.errorNameId, makeMeta(typeCode(oe.errorType), false), false, false);
                scanBody(oe.fallbackBody, inner);
                scopes_[inner].last = static_cast<uint32_t>(scopes_.size()) - 1;
                break;
            }
            case StmtKind::Global:
                if (kind_ == Proto::Kind::Function)
                    for (const auto &n : std::get_if<GlobalStmt>(&stmt.data)->names)
                        if (!findBridge(n.nameId))
                        {
                            bridges_.emplace_back();
                            bridges_.back().nameId = n.nameId;
                        }
                break;
            case StmtKind::UseStmt:
                if (!std::get_if<UseStmt>(&stmt.data)->isDLC)
                    hasModuleUse_ = true;
                break;
            default:
                break;
            }
        }

        void scanFunction(const FunctionDecl &decl)
        {
            for (size_t i = 0; i < decl.params.size(); ++i)
                addSite(0, decl.paramIds[i], makeMeta(typeCode(decl.paramTypes[i]), false), false, false);
            scanBody(decl.body, 0);
            finishScan();
        }

        // Gives every named variable its register: `self`, the parameters in order, the other variables of the function,
        // then those of the blocks inside it, then one per `global` name (only its meta is used).
        void finishScan()
        {
            scopes_[0].last = static_cast<uint32_t>(scopes_.size()) - 1;
            uint32_t next = decl_ && decl_->isMethod ? 1 : 0;
            for (uint32_t s = 0; s < scopes_.size(); ++s)
                for (uint32_t vi : scopes_[s].vars)
                    vars_[vi].cell = next++;
            for (Bridge &b : bridges_)
                b.cell = next++;
            namedEnd_ = next;
            for (Var &v : vars_)
                v.needsMeta = v.placeholder || hasModuleUse_ || (v.loopVar && v.anyConst);
        }

        // ---- emitting ----------------------------------------------------------------------------------------------

        uint32_t pc() const { return static_cast<uint32_t>(p_->code.size()); }

        uint32_t emit(Op op, uint16_t x = 0, int32_t a = 0, int32_t b = 0, int32_t c = 0, const void *node = nullptr)
        {
            p_->code.push_back(Instr{static_cast<uint16_t>(op), x, a, b, c});
            p_->nodes.push_back(node);
            return pc() - 1;
        }

        uint32_t emitJump(Op op, uint16_t x = 0, int32_t a = 0, int32_t b = 0, const void *node = nullptr)
        {
            return emit(op, x, a, b, 0, node);
        }

        void patchTo(uint32_t at, uint32_t target)
        {
            p_->code[at].c = static_cast<int32_t>(target) - static_cast<int32_t>(at + 1);
        }

        void patchHere(const std::vector<uint32_t> &jumps)
        {
            for (uint32_t j : jumps)
                patchTo(j, pc());
        }

        uint32_t allocTemp()
        {
            const uint32_t cell = top_++;
            if (top_ > maxCells_)
                maxCells_ = top_;
            return cell;
        }

        void reserveTo(uint32_t top)
        {
            top_ = top;
            if (top_ > maxCells_)
                maxCells_ = top_;
        }

        int32_t numberConst(double d)
        {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            auto it = numberConsts_.find(bits);
            if (it != numberConsts_.end())
                return it->second;
            const int32_t k = static_cast<int32_t>(p_->consts.size()) * kConstBytes;
            p_->consts.push_back(Value::makeNumber(d));
            numberConsts_.emplace(bits, k);
            return k;
        }

        int32_t valueConst(const void *key, Value v)
        {
            auto it = pointerConsts_.find(key);
            if (it != pointerConsts_.end())
                return it->second;
            const int32_t k = static_cast<int32_t>(p_->consts.size()) * kConstBytes;
            p_->consts.push_back(std::move(v));
            pointerConsts_.emplace(key, k);
            return k;
        }

        int32_t gcell(Reg *slot)
        {
            auto it = gIndex_.find(slot);
            if (it != gIndex_.end())
                return it->second;
            const int32_t g = static_cast<int32_t>(p_->gcells.size());
            p_->gcells.push_back(slot);
            gIndex_.emplace(slot, g);
            return g;
        }

        // ---- what is declared for certain ----------------------------------------------------------------------------

        void needMeta(Var &v)
        {
            usesMeta_ = true;
            if (!v.needsMeta)
            {
                v.needsMeta = true;
                restart_ = true; // its declarations were compiled without the meta this use reads
            }
        }

        void markDefinite(Var &v, uint32_t meta, bool attrsStatic)
        {
            undo_.push_back(Undo{Undo::OfVar, static_cast<uint32_t>(&v - vars_.data()), v.definite, v.defStatic, v.defMeta});
            v.definite = true;
            v.defStatic = attrsStatic && !hasModuleUse_;
            v.defMeta = meta;
        }

        void markBridgeDefinite(Bridge &b)
        {
            undo_.push_back(Undo{Undo::OfBridge, static_cast<uint32_t>(&b - bridges_.data()), b.definite, false, 0});
            b.definite = true;
        }

        void markGlobalDefinite(uint32_t nameId)
        {
            auto it = topDefinite_.find(nameId);
            undo_.push_back(Undo{Undo::OfGlobal, nameId, it != topDefinite_.end() && it->second, false, 0});
            topDefinite_[nameId] = true;
        }

        void restoreDefinite(size_t mark)
        {
            while (undo_.size() > mark)
            {
                const Undo &u = undo_.back();
                switch (u.kind)
                {
                case Undo::OfVar:
                    vars_[u.index].definite = u.definite;
                    vars_[u.index].defStatic = u.defStatic;
                    vars_[u.index].defMeta = u.defMeta;
                    break;
                case Undo::OfBridge:
                    bridges_[u.index].definite = u.definite;
                    break;
                case Undo::OfGlobal:
                    topDefinite_[u.index] = u.definite;
                    break;
                }
                undo_.pop_back();
            }
        }

        // ---- what a name stands for --------------------------------------------------------------------------------

        struct Resolved
        {
            enum Kind : uint8_t
            {
                Local,  // a register
                Global, // a variable of the top level (g)
                Lookup  // a NameRec the machine walks
            } kind = Lookup;
            Var *var = nullptr;
            int32_t g = 0;
            bool checked = false;    // Global: it may not be declared yet
            bool attrsKnown = false; // what it was declared as is known here: `meta`
            uint32_t meta = 0;
            uint32_t rec = 0;
        };

        uint32_t makeRec(uint32_t nameId, const std::string &name, const std::vector<NameStep> &steps)
        {
            NameRec rec;
            rec.nameId = nameId;
            rec.name = &name;
            rec.firstStep = static_cast<uint32_t>(p_->steps.size());
            rec.stepCount = static_cast<uint32_t>(steps.size());
            p_->steps.insert(p_->steps.end(), steps.begin(), steps.end());
            p_->names.push_back(rec);
            return static_cast<uint32_t>(p_->names.size()) - 1;
        }

        int32_t newWithCache()
        {
            p_->withCaches.emplace_back();
            return static_cast<int32_t>(p_->withCaches.size()) - 1;
        }

        // A variable of the top level as this unit may treat it: declared for certain if it is declared right now
        // (a variable is never undeclared), and with known attributes if nothing this unit runs can declare it again.
        Resolved globalHere(Reg *slot, uint32_t nameId, bool bridged)
        {
            Resolved r;
            r.kind = Resolved::Global;
            r.g = gcell(slot);
            const bool declared = (slot->meta & kMetaDeclared) != 0;
            auto it = topDefinite_.find(nameId);
            const bool definiteHere = !bridged && it != topDefinite_.end() && it->second;
            r.checked = !declared && !definiteHere;
            r.attrsKnown = declared && !hasModuleUse_ && (bridged || !topSites_.count(nameId));
            r.meta = slot->meta;
            if (bridged)
            {
                // A function outlives the statement that first called it, so it is compiled again if this changes.
                slot->meta |= kMetaWatched;
                usesSnapshot_ = true;
            }
            return r;
        }

        // `variablesOnly` leaves out what is not a variable of the running code: the members of `with` objects, and
        // (for a top-level statement) the program's variables, which the machine can find by name.
        Resolved resolve(uint32_t nameId, const std::string &name, bool forceLookup = false, bool variablesOnly = false)
        {
#ifdef CUFF_DYNAMIC_NAMES
            forceLookup = true;
#endif
            std::vector<NameStep> steps;
            auto finish = [&]() -> Resolved
            {
                Resolved r;
                r.kind = Resolved::Lookup;
                r.rec = makeRec(nameId, name, steps);
                return r;
            };

            for (uint32_t s = curScope_;; s = static_cast<uint32_t>(scopes_[s].parent))
            {
                Var *v = findVar(s, nameId);
                if (v && !v->hidden)
                {
                    if (v->definite)
                    {
                        if (steps.empty() && !forceLookup && v->defStatic)
                        {
                            Resolved r;
                            r.kind = Resolved::Local;
                            r.var = v;
                            r.attrsKnown = true;
                            r.meta = v->defMeta;
                            return r;
                        }
                        if (steps.empty() && !forceLookup)
                        {
                            Resolved r;
                            r.kind = Resolved::Local;
                            r.var = v;
                            return r;
                        }
                        needMeta(*v);
                        steps.push_back(NameStep{NameStep::LocalSure, off(v->cell), 0});
                        return finish();
                    }
                    needMeta(*v);
                    steps.push_back(NameStep{NameStep::Local, off(v->cell), 0});
                }
                if (hasModuleUse_ && !(s == 0 && kind_ == Proto::Kind::TopLevel))
                    steps.push_back(NameStep{NameStep::Dynamic, static_cast<int32_t>(s), 0});
                if (s != 0)
                {
                    if (scopes_[s].isWith && !variablesOnly)
                        steps.push_back(NameStep{NameStep::With, off(static_cast<uint32_t>(scopes_[s].withCell)), newWithCache()});
                    continue;
                }

                if (kind_ == Proto::Kind::TopLevel)
                {
                    if (variablesOnly)
                        return finish();
                    Reg *slot = table_->slotFor(nameId);
                    Resolved g = globalHere(slot, nameId, false);
                    if (steps.empty() && !forceLookup)
                        return g;
                    steps.push_back(NameStep{NameStep::Global, 0, g.g});
                    return finish();
                }
                if (Bridge *b = findBridge(nameId))
                {
                    Reg *slot = main_.slotFor(nameId);
                    if (b->definite && !hasModuleUse_)
                    {
                        if (steps.empty() && !forceLookup)
                            return globalHere(slot, nameId, true);
                        steps.push_back(NameStep{NameStep::BridgeSure, 0, gcell(slot)});
                    }
                    else
                    {
                        usesMeta_ = true;
                        steps.push_back(NameStep{NameStep::Bridge, off(b->cell), gcell(slot)});
                    }
                }
                return finish();
            }
        }

        // The `with` objects around the code being compiled, innermost first: where a call by a plain name looks for
        // a method before it looks for a function.
        bool withObjects(std::vector<int32_t> &cells) const
        {
            for (uint32_t s = curScope_; s != 0; s = static_cast<uint32_t>(scopes_[s].parent))
                if (scopes_[s].isWith)
                    cells.push_back(off(static_cast<uint32_t>(scopes_[s].withCell)));
            return !cells.empty();
        }

        int32_t innermostWith() const
        {
            for (uint32_t s = curScope_; s != 0; s = static_cast<uint32_t>(scopes_[s].parent))
                if (scopes_[s].isWith)
                    return off(static_cast<uint32_t>(scopes_[s].withCell));
            return off(0);
        }

        // ---- what type an expression gives -------------------------------------------------------------------------

        static TypeKind kindOfMeta(uint32_t meta)
        {
            const uint32_t type = meta >> kMetaTypeShift;
            return type >= kClassTypeBase ? TypeKind::Class : static_cast<TypeKind>(type);
        }

        // The type of a variable's value, where the text guarantees it: the variable is declared for certain, as one
        // type, and never by a declaration that `or_else` may have recovered (which leaves `empty` in it).
        TypeKind kindOfName(uint32_t nameId) const
        {
            Compiler *self = const_cast<Compiler *>(this);
            for (uint32_t s = curScope_;; s = static_cast<uint32_t>(scopes_[s].parent))
            {
                const Var *v = self->findVar(s, nameId);
                if (v && !v->hidden)
                    return v->definite && v->defStatic && !v->placeholder ? kindOfMeta(v->defMeta) : TypeKind::Any;
                if (hasModuleUse_)
                    return TypeKind::Any;
                if (s != 0)
                {
                    if (scopes_[s].isWith)
                        return TypeKind::Any;
                    continue;
                }
                if (kind_ != Proto::Kind::TopLevel || topSites_.count(nameId))
                    return TypeKind::Any;
                const Reg *slot = table_->find(nameId);
                if (!slot || !(slot->meta & kMetaDeclared) || (slot->meta & kMetaLoose))
                    return TypeKind::Any;
                return kindOfMeta(slot->meta);
            }
        }

        // Any: not known. Otherwise the expression gives a value of that type or raises an error.
        TypeKind staticKind(const Expr &e, int depth = 0) const
        {
            switch (e.kind)
            {
            case ExprKind::Number:
                return TypeKind::Number;
            case ExprKind::String:
            case ExprKind::FString:
                return TypeKind::Str;
            case ExprKind::Bool:
            case ExprKind::Isa:
                return TypeKind::Boolean;
            case ExprKind::Pattern:
                return TypeKind::Pattern;
            case ExprKind::List:
                return TypeKind::List;
            case ExprKind::Map:
                return TypeKind::Map;
            case ExprKind::Identifier:
                return kindOfName(std::get_if<IdentifierExpr>(&e.data)->nameId);
            case ExprKind::UnaryOp:
                return std::get_if<UnaryOp>(&e.data)->op == UnOp::Not ? TypeKind::Boolean : TypeKind::Number;
            case ExprKind::BinaryOp:
            {
                const auto &b = *std::get_if<BinaryOp>(&e.data);
                switch (b.op)
                {
                case BinOp::Sub:
                case BinOp::Mul:
                case BinOp::Div:
                case BinOp::Mod:
                case BinOp::Pow:
                    return TypeKind::Number;
                case BinOp::Add:
                {
                    // `+` takes two numbers, two strings or two lists, so one operand of a known type settles it.
                    const TypeKind r = staticKind(*b.right, depth + 1);
                    if (r == TypeKind::Number || r == TypeKind::Str || r == TypeKind::List)
                        return r;
                    if (depth >= 8)
                        return TypeKind::Any;
                    const TypeKind l = staticKind(*b.left, depth + 1);
                    return l == TypeKind::Number || l == TypeKind::Str || l == TypeKind::List ? l : TypeKind::Any;
                }
                default:
                    return TypeKind::Boolean;
                }
            }
            default:
                return TypeKind::Any;
            }
        }

        bool valueFits(const Expr &value, uint32_t meta) const
        {
            const TypeKind declared = kindOfMeta(meta);
            if (declared == TypeKind::Any)
                return true;
            if (declared == TypeKind::Class || declared == TypeKind::Object || declared == TypeKind::Empty ||
                declared == TypeKind::Match)
                return false;
            return staticKind(value) == declared;
        }

        // ---- expressions -------------------------------------------------------------------------------------------

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

        static SourceLocation locOf(const Expr &e)
        {
            return std::visit([](const auto &n) { return n.loc; }, e.data);
        }

        // The register a result goes to: the one asked for, the operand's own if it is a temporary, or a new one.
        uint32_t resultCell(int32_t dst, const Operand *reuse = nullptr)
        {
            if (dst != kNoCell)
                return static_cast<uint32_t>(dst);
            if (reuse && reuse->temp)
                return reuse->cell;
            return allocTemp();
        }

        Operand done(uint32_t cell, int32_t dst, uint32_t mark)
        {
            if (dst != kNoCell)
            {
                top_ = mark;
                return Operand{cell, false};
            }
            top_ = cell + 1;
            return Operand{cell, true};
        }

        // Compiles `e`. With a destination, the value is written there by the last instruction and by no other, so a
        // variable that is the destination still has its old value while the expression is worked out and if it fails.
        // Without one, the result is a variable's own register or a temporary at the top of the frame.
        //
        // A chain that leans to the left (a + b + c, x[1][2], a.b.c) is as tall as it is long, so it is compiled
        // from its first operand outwards in a loop, not by recursion.
        Operand gen(const Expr &e, int32_t dst)
        {
            const Expr *first = spineOperand(e);
            if (!first)
                return genLeaf(e, dst);
            guard(locOf(e));
            const uint32_t mark = top_;
            if (!spineOperand(*first))
            {
                const Operand left = gen(*first, kNoCell);
                return genStep(e, left, dst, mark);
            }
            std::vector<const Expr *> chain;
            const Expr *node = &e;
            while (const Expr *next = spineOperand(*node))
            {
                chain.push_back(node);
                node = next;
            }
            Operand acc = gen(*node, kNoCell);
            for (size_t k = chain.size(); k-- > 0;)
                acc = genStep(*chain[k], acc, k == 0 ? dst : kNoCell, mark);
            return acc;
        }

        static bool numberLiteral(const Expr &e, double &out)
        {
            if (e.kind == ExprKind::Number)
            {
                out = std::get_if<NumberLiteral>(&e.data)->value;
                return true;
            }
            if (e.kind == ExprKind::UnaryOp)
            {
                const auto &u = *std::get_if<UnaryOp>(&e.data);
                if (u.op == UnOp::Negate && u.operand->kind == ExprKind::Number)
                {
                    out = -std::get_if<NumberLiteral>(&u.operand->data)->value;
                    return true;
                }
            }
            return false;
        }

        // One node of a chain, with its first operand already in `left`.
        Operand genStep(const Expr &e, Operand left, int32_t dst, uint32_t mark)
        {
            switch (e.kind)
            {
            case ExprKind::BinaryOp:
            {
                const auto &b = *std::get_if<BinaryOp>(&e.data);
                if (b.op == BinOp::And || b.op == BinOp::Or)
                {
                    const uint32_t r = resultCell(dst, &left);
                    const uint32_t jump = emitJump(b.op == BinOp::And ? Op::JAnd : Op::JOr, 0, off(r), off(left.cell));
                    const uint32_t inner = top_;
                    const Operand right = gen(*b.right, kNoCell);
                    emit(Op::ToBool, 0, off(r), off(right.cell));
                    top_ = inner;
                    patchTo(jump, pc());
                    return done(r, dst, mark);
                }
                double k = 0;
                if (arithmeticOp(b.op) && numberLiteral(*b.right, k))
                {
                    const uint32_t r = resultCell(dst, &left);
                    emit(static_cast<Op>(static_cast<int>(Op::AddK) + (static_cast<int>(b.op) - static_cast<int>(BinOp::Add))), 0, off(r),
                         off(left.cell), numberConst(k), &b);
                    return done(r, dst, mark);
                }
                const Operand right = gen(*b.right, kNoCell);
                const uint32_t r = resultCell(dst, left.temp ? &left : &right);
                emit(binaryOpcode(b.op), 0, off(r), off(left.cell), off(right.cell), &b);
                return done(r, dst, mark);
            }
            case ExprKind::IndexAccess:
            {
                const auto &idx = *std::get_if<IndexAccess>(&e.data);
                const Operand index = gen(*idx.index, kNoCell);
                const uint32_t r = resultCell(dst, left.temp ? &left : &index);
                emit(Op::Index, 0, off(r), off(left.cell), off(index.cell), &idx);
                return done(r, dst, mark);
            }
            case ExprKind::SliceAccess:
            {
                const auto &sl = *std::get_if<SliceAccess>(&e.data);
                const uint32_t base = top_;
                reserveTo(base + 2);
                top_ = base + 1;
                gen(*sl.start, static_cast<int32_t>(base));
                top_ = base + 2;
                gen(*sl.end, static_cast<int32_t>(base + 1));
                const uint32_t r = dst != kNoCell ? static_cast<uint32_t>(dst) : (left.temp ? left.cell : base);
                emit(Op::Slice, 0, off(r), off(left.cell), off(base), &sl);
                return done(r, dst, mark);
            }
            case ExprKind::Member:
            {
                const auto &ma = *std::get_if<MemberAccess>(&e.data);
                const uint32_t r = resultCell(dst, &left);
                emit(Op::GetField, 0, off(r), off(left.cell), newFieldCache(), &ma);
                return done(r, dst, mark);
            }
            case ExprKind::MethodCall:
            {
                const auto &mc = *std::get_if<MethodCall>(&e.data);
                uint32_t base;
                if (left.temp)
                    base = left.cell;
                else
                {
                    base = allocTemp();
                    emit(Op::Move, 0, off(base), off(left.cell));
                }
                return genMethodCall(mc, base, dst, mark, nullptr);
            }
            default:
            {
                const auto &isa = *std::get_if<IsaExpr>(&e.data);
                const uint32_t r = resultCell(dst, &left);
                emit(Op::Isa, 0, off(r), off(left.cell), 0, &isa);
                return done(r, dst, mark);
            }
            }
        }

        static bool arithmeticOp(BinOp op) { return op <= BinOp::Pow; }

        static Op binaryOpcode(BinOp op)
        {
            switch (op)
            {
            case BinOp::Add: return Op::Add;
            case BinOp::Sub: return Op::Sub;
            case BinOp::Mul: return Op::Mul;
            case BinOp::Div: return Op::Div;
            case BinOp::Mod: return Op::Mod;
            case BinOp::Pow: return Op::Pow;
            case BinOp::Is: return Op::Eq;
            case BinOp::IsNot: return Op::Ne;
            case BinOp::Matches: return Op::Matches;
            case BinOp::Greater: return Op::Gt;
            case BinOp::Less: return Op::Lt;
            case BinOp::GreaterEq: return Op::Ge;
            case BinOp::LessEq: return Op::Le;
            case BinOp::And:
            case BinOp::Or:
                break;
            }
            return Op::Add;
        }

        int32_t newFieldCache()
        {
            p_->fieldCaches.emplace_back();
            return static_cast<int32_t>(p_->fieldCaches.size()) - 1;
        }

        int32_t newMethodCache()
        {
            p_->methodCaches.emplace_back();
            return static_cast<int32_t>(p_->methodCaches.size()) - 1;
        }

        // Arguments go into consecutive registers from `first`; each is written by its own last instruction.
        void genArgs(const std::vector<std::unique_ptr<Expr>> &args, uint32_t first)
        {
            reserveTo(first + static_cast<uint32_t>(args.size()));
            for (size_t i = 0; i < args.size(); ++i)
            {
                top_ = first + static_cast<uint32_t>(i) + 1;
                gen(*args[i], static_cast<int32_t>(first + i));
            }
            top_ = first + static_cast<uint32_t>(args.size());
        }

        Operand callResult(uint32_t base, int32_t dst, uint32_t mark)
        {
            if (dst != kNoCell)
            {
                emit(Op::Take, 0, dst * kCellBytes, off(base));
                top_ = mark;
                return Operand{static_cast<uint32_t>(dst), false};
            }
            top_ = base + 1;
            return Operand{base, true};
        }

        // receiver.name(args), with the receiver already in `base`. `awaited` is the `await` the call stands under.
        Operand genMethodCall(const MethodCall &mc, uint32_t base, int32_t dst, uint32_t mark, const AwaitExpr *awaited)
        {
            if (awaited)
                emit(Op::AwaitM, 0, off(base), 0, 0, awaited);
            genArgs(mc.args, base + 1);
            emit(Op::CallM, awaited ? kCallAwaited : 0, off(base), static_cast<int32_t>(mc.args.size()), newMethodCache(),
                 awaited ? static_cast<const void *>(awaited) : static_cast<const void *>(&mc));
            return callResult(base, dst, mark);
        }

        Operand genCall(const FunctionCall &fc, int32_t dst, const AwaitExpr *awaited)
        {
            const uint32_t mark = top_;
            const void *node = awaited ? static_cast<const void *>(awaited) : static_cast<const void *>(&fc);
            const uint16_t x = awaited ? kCallAwaited : 0;
            std::vector<int32_t> holders;
            if (withObjects(holders))
            {
                p_->withLists.push_back(std::move(holders));
                const int32_t list = static_cast<int32_t>(p_->withLists.size()) - 1;
                if (awaited)
                    emit(Op::AwaitF, 1, 0, 0, list, awaited);
                const uint32_t base = allocTemp(); // the receiver, if the name turns out to be a method of a `with` object
                genArgs(fc.args, base + 1);
                noteCallName(emit(Op::CallW, x, off(base), static_cast<int32_t>(fc.args.size()), list, node), fc);
                return callResult(base, dst, mark);
            }
            if (awaited)
                emit(Op::AwaitF, 0, 0, 0, 0, awaited);
            const uint32_t base = top_;
            reserveTo(base + 1);
            genArgs(fc.args, base);
            if (fc.args.empty())
                top_ = base + 1;
            noteCallName(emit(Op::Call, x, off(base), static_cast<int32_t>(fc.args.size()), static_cast<int32_t>(fc.functionNameId), node), fc);
            return callResult(base, dst, mark);
        }

        // A call to a name that is no function says "x is a number, not a function" when a variable has the name.
        // Only a call whose name some scope around it declares needs a way to look that variable up.
        void noteCallName(uint32_t at, const FunctionCall &fc)
        {
            bool possible = hasModuleUse_ || (kind_ == Proto::Kind::Function && findBridge(fc.functionNameId));
            for (uint32_t s = curScope_; !possible; s = static_cast<uint32_t>(scopes_[s].parent))
            {
                possible = findVar(s, fc.functionNameId) != nullptr;
                if (s == 0)
                    break;
            }
            if (possible)
                p_->callRecs[at] = resolve(fc.functionNameId, fc.functionName, true, true).rec;
        }

        Operand genSuperCall(const SuperCall &sc, int32_t dst, const AwaitExpr *awaited)
        {
            const uint32_t mark = top_;
            if (awaited)
                emit(Op::AwaitS, 0, 0, 0, 0, awaited);
            const uint32_t base = allocTemp();
            emit(Op::Move, 0, off(base), off(0));
            genArgs(sc.args, base + 1);
            emit(Op::CallS, awaited ? kCallAwaited : 0, off(base), static_cast<int32_t>(sc.args.size()), 0,
                 awaited ? static_cast<const void *>(awaited) : static_cast<const void *>(&sc));
            return callResult(base, dst, mark);
        }

        Operand genAwait(const AwaitExpr &aw, int32_t dst)
        {
            const Expr &call = *aw.call;
            switch (call.kind)
            {
            case ExprKind::MethodCall:
            {
                const auto &mc = *std::get_if<MethodCall>(&call.data);
                const uint32_t mark = top_;
                const uint32_t base = allocTemp();
                if (!mc.target)
                    emit(Op::Move, 0, off(base), innermostWith());
                else
                    gen(*mc.target, static_cast<int32_t>(base));
                top_ = base + 1;
                return genMethodCall(mc, base, dst, mark, &aw);
            }
            case ExprKind::SuperCall:
                return genSuperCall(*std::get_if<SuperCall>(&call.data), dst, &aw);
            default:
                return genCall(*std::get_if<FunctionCall>(&call.data), dst, &aw);
            }
        }

        Operand genLeaf(const Expr &e, int32_t dst)
        {
            guard(locOf(e));
            const uint32_t mark = top_;
            switch (e.kind)
            {
            case ExprKind::Number:
            {
                const uint32_t r = resultCell(dst);
                emit(Op::LoadK, 0, off(r), numberConst(std::get_if<NumberLiteral>(&e.data)->value));
                return done(r, dst, mark);
            }
            case ExprKind::String:
            {
                const auto &s = *std::get_if<StringLiteral>(&e.data);
                const uint32_t r = resultCell(dst);
                emit(Op::LoadK, 0, off(r), valueConst(s.shared.get(), Value::makeStr(s.shared)));
                return done(r, dst, mark);
            }
            case ExprKind::Bool:
            {
                const uint32_t r = resultCell(dst);
                emit(Op::LoadBool, std::get_if<BoolLiteral>(&e.data)->value ? 1 : 0, off(r));
                return done(r, dst, mark);
            }
            case ExprKind::Empty:
            {
                const uint32_t r = resultCell(dst);
                emit(Op::LoadNil, 0, off(r));
                return done(r, dst, mark);
            }
            case ExprKind::Pattern:
            {
                const auto &pat = *std::get_if<PatternLiteral>(&e.data);
                const uint32_t r = resultCell(dst);
                emit(Op::LoadK, 0, off(r), valueConst(pat.value.get(), Value::makePattern(pat.value)));
                return done(r, dst, mark);
            }
            case ExprKind::Identifier:
            {
                const auto &id = *std::get_if<IdentifierExpr>(&e.data);
                const Resolved r = resolve(id.nameId, id.name);
                if (r.kind == Resolved::Local)
                {
                    if (dst == kNoCell)
                        return Operand{r.var->cell, false};
                    if (static_cast<uint32_t>(dst) != r.var->cell)
                        emit(Op::Move, 0, dst * kCellBytes, off(r.var->cell));
                    return Operand{static_cast<uint32_t>(dst), false};
                }
                const uint32_t out = resultCell(dst);
                if (r.kind == Resolved::Global)
                    emit(r.checked ? Op::GetGC : Op::GetG, 0, off(out), r.g, 0, &id);
                else
                    emit(Op::GetName, 0, off(out), static_cast<int32_t>(r.rec), 0, &id);
                return done(out, dst, mark);
            }
            case ExprKind::Self:
                if (dst == kNoCell)
                    return Operand{0, false};
                emit(Op::Move, 0, dst * kCellBytes, off(0));
                return Operand{static_cast<uint32_t>(dst), false};
            case ExprKind::List:
                return genList(*std::get_if<ListLiteral>(&e.data), dst, mark);
            case ExprKind::Map:
                return genMap(*std::get_if<MapLiteral>(&e.data), dst, mark);
            case ExprKind::FString:
                return genFString(*std::get_if<FStringExpr>(&e.data), dst, mark);
            case ExprKind::UnaryOp:
            {
                const auto &u = *std::get_if<UnaryOp>(&e.data);
                double k = 0;
                if (numberLiteral(e, k))
                {
                    const uint32_t r = resultCell(dst);
                    emit(Op::LoadK, 0, off(r), numberConst(k));
                    return done(r, dst, mark);
                }
                const Operand operand = gen(*u.operand, kNoCell);
                const uint32_t r = resultCell(dst, &operand);
                emit(u.op == UnOp::Not ? Op::Not : Op::Neg, 0, off(r), off(operand.cell), 0, &u);
                return done(r, dst, mark);
            }
            case ExprKind::FunctionCall:
                return genCall(*std::get_if<FunctionCall>(&e.data), dst, nullptr);
            case ExprKind::SuperCall:
                return genSuperCall(*std::get_if<SuperCall>(&e.data), dst, nullptr);
            case ExprKind::Await:
                return genAwait(*std::get_if<AwaitExpr>(&e.data), dst);
            case ExprKind::Member:
            {
                // `.name`: a member of the innermost `with` object.
                const auto &ma = *std::get_if<MemberAccess>(&e.data);
                const uint32_t r = resultCell(dst);
                emit(Op::GetField, 0, off(r), innermostWith(), newFieldCache(), &ma);
                return done(r, dst, mark);
            }
            case ExprKind::MethodCall:
            {
                const auto &mc = *std::get_if<MethodCall>(&e.data);
                const uint32_t base = allocTemp();
                emit(Op::Move, 0, off(base), innermostWith());
                return genMethodCall(mc, base, dst, mark, nullptr);
            }
            default:
                break;
            }
            throw InternalEngineError("unhandled expression kind");
        }

        static constexpr size_t kInlineListItems = 32;

        Operand genList(const ListLiteral &list, int32_t dst, uint32_t mark)
        {
            const size_t n = list.elements.size();
            if (n <= kInlineListItems)
            {
                const uint32_t base = top_;
                reserveTo(base + 1);
                genArgs(list.elements, base);
                const uint32_t r = dst != kNoCell ? static_cast<uint32_t>(dst) : base;
                emit(Op::NewList, 0, off(r), off(base), static_cast<int32_t>(n));
                return done(r, dst, mark);
            }
            const uint32_t r = allocTemp();
            emit(Op::NewList, 0, off(r), off(r), 0);
            for (const auto &el : list.elements)
            {
                const Operand v = gen(*el, kNoCell);
                emit(Op::ListAdd, v.temp ? kMoveSource : 0, off(r), off(v.cell));
                top_ = r + 1;
            }
            return finishBuilt(r, dst, mark);
        }

        // A value that was built in a temporary over several instructions goes to its destination in one.
        Operand finishBuilt(uint32_t r, int32_t dst, uint32_t mark)
        {
            if (dst == kNoCell)
            {
                top_ = r + 1;
                return Operand{r, true};
            }
            emit(Op::Take, 0, dst * kCellBytes, off(r));
            top_ = mark;
            return Operand{static_cast<uint32_t>(dst), false};
        }

        Operand genMap(const MapLiteral &map, int32_t dst, uint32_t mark)
        {
            const uint32_t r = allocTemp();
            emit(Op::NewMap, 0, off(r), 0, static_cast<int32_t>(map.pairs.size()));
            for (const auto &pair : map.pairs)
            {
                const Operand key = gen(*pair.key, kNoCell);
                if (pair.key->kind != ExprKind::String)
                    emit(Op::ChkKey, 0, off(key.cell), 0, 0, &map);
                const Operand value = gen(*pair.value, kNoCell);
                emit(Op::MapSet, value.temp ? kMoveSource : 0, off(r), off(key.cell), off(value.cell));
                top_ = r + 1;
            }
            return finishBuilt(r, dst, mark);
        }

        Operand genFString(const FStringExpr &fs, int32_t dst, uint32_t mark)
        {
            size_t exprs = 0;
            for (const auto &seg : fs.segments)
                if (seg.isExpression)
                    ++exprs;
            if (exprs == 0)
            {
                std::string text;
                for (const auto &seg : fs.segments)
                    text += seg.text;
                const uint32_t r = resultCell(dst);
                emit(Op::LoadK, 0, off(r), valueConst(&fs, Value::makeStr(std::move(text))));
                return done(r, dst, mark);
            }
            const uint32_t base = top_;
            reserveTo(base + static_cast<uint32_t>(exprs));
            uint32_t i = 0;
            for (const auto &seg : fs.segments)
            {
                if (!seg.isExpression)
                    continue;
                top_ = base + i + 1;
                gen(*seg.expr, static_cast<int32_t>(base + i));
                emit(Op::ToStr, 0, off(base + i), 0, 0, &fs);
                ++i;
            }
            top_ = base + static_cast<uint32_t>(exprs);
            const uint32_t r = dst != kNoCell ? static_cast<uint32_t>(dst) : base;
            emit(Op::Concat, 0, off(r), off(base), 0, &fs);
            return done(r, dst, mark);
        }

        // ---- conditions --------------------------------------------------------------------------------------------

        static constexpr uint32_t kMaxConditionHeight = 24;

        // Emits jumps that are taken when the truth of `e` is `when`; control falls through otherwise. `extra` is put
        // on the jumps that leave through `jumps` (a loop's backward jump counts a pass).
        void genCond(const Expr &e, bool when, std::vector<uint32_t> &jumps, uint16_t extra = 0)
        {
            guard(locOf(e));
            const uint32_t mark = top_;
            const uint16_t sense = static_cast<uint16_t>((when ? kJumpIfTrue : 0) | extra);
            if (e.kind == ExprKind::UnaryOp)
            {
                const auto &u = *std::get_if<UnaryOp>(&e.data);
                if (u.op == UnOp::Not)
                {
                    genCond(*u.operand, !when, jumps, extra);
                    return;
                }
            }
            else if (e.kind == ExprKind::BinaryOp && e.height <= kMaxConditionHeight)
            {
                const auto &b = *std::get_if<BinaryOp>(&e.data);
                if (b.op == BinOp::And || b.op == BinOp::Or)
                {
                    // `a and b` is false as soon as a is, `a or b` true as soon as a is.
                    const bool decides = b.op == BinOp::Or;
                    if (when == decides)
                    {
                        genCond(*b.left, when, jumps, extra);
                        genCond(*b.right, when, jumps, extra);
                    }
                    else
                    {
                        std::vector<uint32_t> skip;
                        genCond(*b.left, decides, skip);
                        genCond(*b.right, when, jumps, extra);
                        patchHere(skip);
                    }
                    return;
                }
                Op op = Op::Count;
                bool flip = false;
                switch (b.op)
                {
                case BinOp::Less: op = Op::JLt; break;
                case BinOp::LessEq: op = Op::JLe; break;
                case BinOp::Greater: op = Op::JGt; break;
                case BinOp::GreaterEq: op = Op::JGe; break;
                case BinOp::Is: op = Op::JEq; break;
                case BinOp::IsNot: op = Op::JEq; flip = true; break;
                default: break;
                }
                if (op != Op::Count)
                {
                    const uint16_t x = flip ? static_cast<uint16_t>(sense ^ kJumpIfTrue) : sense;
                    const Operand left = gen(*b.left, kNoCell);
                    double k = 0;
                    if (numberLiteral(*b.right, k))
                    {
                        const Op opK = static_cast<Op>(static_cast<int>(Op::JLtK) + (static_cast<int>(op) - static_cast<int>(Op::JLt)));
                        jumps.push_back(emitJump(opK, x, off(left.cell), numberConst(k), &b));
                    }
                    else
                    {
                        const Operand right = gen(*b.right, kNoCell);
                        jumps.push_back(emitJump(op, x, off(left.cell), off(right.cell), &b));
                    }
                    top_ = mark;
                    return;
                }
            }
            const Operand v = gen(e, kNoCell);
            jumps.push_back(emitJump(Op::JmpIf, sense, off(v.cell)));
            top_ = mark;
        }

        // ---- statements --------------------------------------------------------------------------------------------

        void compileBlock(const std::vector<std::unique_ptr<Stmt>> &body)
        {
            const size_t mark = undo_.size();
            for (const auto &s : body)
                compileStmt(*s);
            restoreDefinite(mark);
        }

        bool pureFunction() const { return kind_ == Proto::Kind::Function && decl_->isPure; }

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

        void compileStmt(const Stmt &stmt)
        {
            const uint32_t mark = top_;
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
                compileDeclaration(*std::get_if<DeclarationStmt>(&stmt.data), false);
                break;
            case StmtKind::Assign:
                compileAssign(*std::get_if<AssignStmt>(&stmt.data));
                break;
            case StmtKind::Global:
            {
                const auto &g = *std::get_if<GlobalStmt>(&stmt.data);
                emit(Op::GlobalChk, 0, 0, 0, 0, &g);
                if (kind_ == Proto::Kind::Function)
                    for (const auto &n : g.names)
                    {
                        Bridge &b = *findBridge(n.nameId);
                        emit(Op::Bridge, 0, off(b.cell));
                        markBridgeDefinite(b);
                    }
                break;
            }
            case StmtKind::FunctionDecl:
                emit(Op::DefFunc, 0, 0, 0, 0, std::get_if<FunctionDecl>(&stmt.data));
                break;
            case StmtKind::ClassDecl:
                break;
            case StmtKind::IfStmt:
                compileIf(*std::get_if<IfStmt>(&stmt.data));
                break;
            case StmtKind::LoopStmt:
                compileLoop(*std::get_if<LoopStmt>(&stmt.data));
                break;
            case StmtKind::WithStmt:
                compileWith(*std::get_if<WithStmt>(&stmt.data));
                break;
            case StmtKind::StopStmt:
            {
                const auto &s = *std::get_if<StopStmt>(&stmt.data);
                if (loops_.empty())
                    emit(Op::Raise, static_cast<uint16_t>(RaiseKind::StopOutsideLoop), 0, 0, 0, &s);
                else
                    loops_.back().stops.push_back(emitJump(Op::Jmp));
                break;
            }
            case StmtKind::SkipStmt:
            {
                const auto &s = *std::get_if<SkipStmt>(&stmt.data);
                if (loops_.empty())
                    emit(Op::Raise, static_cast<uint16_t>(RaiseKind::SkipOutsideLoop), 0, 0, 0, &s);
                else
                    loops_.back().skips.push_back(emitJump(Op::Jmp));
                break;
            }
            case StmtKind::ReturnStmt:
            {
                const auto &r = *std::get_if<ReturnStmt>(&stmt.data);
                guard(r.loc);
                if (kind_ == Proto::Kind::Function)
                {
                    if (!r.value)
                    {
                        emit(Op::RetNil);
                        break;
                    }
                    const Operand v = gen(*r.value, kNoCell);
                    emit(Op::Ret, 0, off(v.cell));
                    break;
                }
                if (r.value)
                {
                    emit(Op::RetChk, 0, 0, 0, 0, &r);
                    gen(*r.value, kNoCell);
                }
                emit(Op::Raise, static_cast<uint16_t>(RaiseKind::ReturnOutsideFunction), 0, 0, 0, &r);
                break;
            }
            case StmtKind::FailStmt:
            {
                const auto &f = *std::get_if<FailStmt>(&stmt.data);
                guard(f.loc);
                const Operand v = gen(*f.value, kNoCell);
                emit(Op::Fail, 0, off(v.cell), 0, 0, &f);
                break;
            }
            case StmtKind::AwaitStmt:
                genAwait(*std::get_if<AwaitStmt>(&stmt.data)->expr, kNoCell);
                break;
            case StmtKind::UseStmt:
                emit(Op::Use, 0, static_cast<int32_t>(curScope_), 0, 0, std::get_if<UseStmt>(&stmt.data));
                break;
            case StmtKind::ExprStmt:
                gen(*std::get_if<ExprStmt>(&stmt.data)->expr, kNoCell);
                break;
            case StmtKind::CollectionOp:
                compileCollectionOp(*std::get_if<CollectionOpStmt>(&stmt.data));
                break;
            case StmtKind::OrElse:
                compileOrElse(*std::get_if<OrElseStmt>(&stmt.data));
                break;
            }
            top_ = mark;
        }

        // Stores a value that is already known to fit into a register variable, with its meta if some use reads it.
        void storeLocal(Var &v, Operand value, uint32_t meta, bool declares)
        {
            if (declares && v.needsMeta)
                emit(Op::Decl, value.temp ? kMoveSource : 0, off(v.cell), off(value.cell), static_cast<int32_t>(meta));
            else if (value.cell != v.cell)
                emit(value.temp ? Op::Take : Op::Move, 0, off(v.cell), off(value.cell));
        }

        void compileDeclaration(const DeclarationStmt &d, bool guarded)
        {
            guard(d.loc);
            const uint32_t meta = makeMeta(typeCode(d.type), d.isConstant);
            const bool top = curScope_ == 0 && kind_ == Proto::Kind::TopLevel;
            Var *v = top ? nullptr : findVar(curScope_, d.nameId);
            const bool badName = d.isConstant && !isValidConstantName(d.name);
            const bool fits = valueFits(*d.value, meta);
            const bool mayFreeze = d.isConstant && (d.type.kind == TypeKind::List || d.type.kind == TypeKind::Any);

            if (v && fits && !badName && !mayFreeze)
            {
                gen(*d.value, static_cast<int32_t>(v->cell));
                if (v->needsMeta)
                    emit(Op::SetMeta, 0, off(v->cell), 0, static_cast<int32_t>(meta));
            }
            else
            {
                Operand value = gen(*d.value, kNoCell);
                if (badName)
                {
                    emit(Op::Raise, static_cast<uint16_t>(RaiseKind::ConstantName), 0, 0, 0, &d);
                    return;
                }
                if (!fits)
                    emit(Op::ChkDecl, static_cast<uint16_t>(d.type.kind), off(value.cell), 0, 0, &d);
                if (mayFreeze)
                {
                    if (!value.temp)
                    {
                        const uint32_t t = allocTemp();
                        emit(Op::Move, 0, off(t), off(value.cell));
                        value = Operand{t, true};
                    }
                    emit(Op::Freeze, 0, off(value.cell));
                }
                if (v)
                    storeLocal(*v, value, meta, true);
                else
                    emit(Op::DeclG, value.temp ? kMoveSource : 0, gcell(table_->slotFor(d.nameId)), off(value.cell),
                         static_cast<int32_t>(meta));
            }
            if (v)
                markDefinite(*v, meta, !guarded || (v->sameAttrs && !v->anyConst));
            else
                markGlobalDefinite(d.nameId);
        }

        void compileIf(const IfStmt &ifs)
        {
            guard(ifs.loc);
            std::vector<uint32_t> ends;
            for (size_t i = 0; i < ifs.branches.size(); ++i)
            {
                const auto &branch = ifs.branches[i];
                std::vector<uint32_t> next;
                if (branch.condition)
                    genCond(*branch.condition, false, next);
                compileBlock(branch.body);
                if (i + 1 < ifs.branches.size())
                    ends.push_back(emitJump(Op::Jmp));
                patchHere(next);
            }
            patchHere(ends);
        }

        // Binds the loop variable to the value in `from` at the start of a pass, where the loop instruction itself
        // could not write it: a variable of the top level, or one whose meta is read.
        void bindLoopVar(const LoopStmt &loop, Var *v, uint32_t from)
        {
            const int32_t meta = static_cast<int32_t>(makeMeta(typeCode(loop.varType), false));
            if (v)
                emit(Op::BindL, 0, off(v->cell), off(from), meta, &loop);
            else
                emit(Op::BindG, 0, gcell(table_->slotFor(loop.repeatVarId)), off(from), meta, &loop);
        }

        void compileLoop(const LoopStmt &loop)
        {
            guard(loop.loc);
            const size_t defMark = undo_.size();
            const bool top = curScope_ == 0 && kind_ == Proto::Kind::TopLevel;
            Var *v = loop.kind == LoopStmt::LoopKind::While || top ? nullptr : findVar(curScope_, loop.repeatVarId);
            const uint32_t meta = makeMeta(typeCode(loop.varType), false);
            const bool direct = v && !v->needsMeta;
            loops_.emplace_back();

            switch (loop.kind)
            {
            case LoopStmt::LoopKind::While:
            {
                const uint32_t enter = emitJump(Op::Jmp);
                const uint32_t body = pc();
                compileBlock(loop.body);
                patchHere(loops_.back().skips);
                patchTo(enter, pc());
                std::vector<uint32_t> back;
                genCond(*loop.condition, true, back, kJumpTicks);
                for (uint32_t j : back)
                {
                    patchTo(j, body);
                    p_->tickLocs.emplace(j, &loop.loc);
                }
                break;
            }
            case LoopStmt::LoopKind::Repeat:
            {
                const uint32_t base = top_;
                reserveTo(base + 3);
                top_ = base + 1;
                gen(*loop.repeatStart, static_cast<int32_t>(base));
                top_ = base + 2;
                gen(*loop.repeatEnd, static_cast<int32_t>(base + 1));
                top_ = base + 3;
                const bool inPlace = direct && (loop.varType.kind == TypeKind::Number || loop.varType.kind == TypeKind::Any);
                emit(Op::ForPrep, 0, off(base), inPlace ? off(v->cell) : kNoCell, 0, &loop);
                const uint32_t body = pc();
                // A plain register of another declared type needs no binding: ForPrep refuses the type first.
                if (!inPlace && !direct)
                    bindLoopVar(loop, v, base);
                markLoopVar(loop, v, meta);
                compileBlock(loop.body);
                patchHere(loops_.back().skips);
                const uint32_t next = emit(Op::ForNext, 0, off(base), inPlace ? off(v->cell) : kNoCell, 0, &loop);
                patchTo(next, body);
                break;
            }
            case LoopStmt::LoopKind::Each:
            {
                const uint32_t base = top_;
                reserveTo(base + 3);
                top_ = base + 3;
                const Operand coll = gen(*loop.collection, kNoCell);
                emit(Op::EachPrep, 0, off(base), off(coll.cell), 0, &loop);
                top_ = base + 3;
                const uint32_t head = pc();
                const TypeKind kind = loop.varType.kind;
                const bool simpleKind = kind != TypeKind::Class;
                const bool inPlace = direct && simpleKind;
                const uint32_t next = emit(Op::EachNext, inPlace ? static_cast<uint16_t>(kind) : 0, off(base),
                                           inPlace ? off(v->cell) : off(base + 2), 0, &loop);
                if (!inPlace)
                {
                    if (kind != TypeKind::Any)
                        emit(Op::ChkLoop, static_cast<uint16_t>(kind), off(base + 2), 0, 0, &loop);
                    if (direct)
                        emit(Op::Take, 0, off(v->cell), off(base + 2));
                    else
                        bindLoopVar(loop, v, base + 2);
                }
                markLoopVar(loop, v, meta);
                compileBlock(loop.body);
                const uint32_t back = emitJump(Op::Jmp);
                patchTo(back, head);
                for (uint32_t j : loops_.back().skips)
                    patchTo(j, head);
                patchTo(next, pc());
                break;
            }
            }
            patchHere(loops_.back().stops);
            loops_.pop_back();
            restoreDefinite(defMark);
        }

        void markLoopVar(const LoopStmt &loop, Var *v, uint32_t meta)
        {
            if (v)
                markDefinite(*v, meta, v->sameAttrs && !v->anyConst);
            else if (loop.kind != LoopStmt::LoopKind::While)
                markGlobalDefinite(loop.repeatVarId);
        }

        // Entering a block that has variables of its own: none of them is declared yet, whatever an earlier pass left.
        void enterScope(uint32_t scope)
        {
            const Scope &s = scopes_[scope];
            if (!s.vars.empty())
            {
                bool any = false;
                for (uint32_t vi : s.vars)
                    any = any || vars_[vi].needsMeta;
                if (any)
                    emit(Op::Undecl, 0, off(vars_[s.vars.front()].cell), static_cast<int32_t>(s.vars.size()));
            }
            if (hasModuleUse_)
                emit(Op::DynReset, 0, static_cast<int32_t>(scope), static_cast<int32_t>(s.last));
        }

        void compileWith(const WithStmt &w)
        {
            guard(w.loc);
            const uint32_t scope = scopeOf_.at(&w);
            const uint32_t holder = allocTemp();
            const Operand object = gen(*w.object, kNoCell);
            emit(Op::With, 0, off(holder), off(object.cell), 0, &w);
            top_ = holder + 1;
            scopes_[scope].withCell = static_cast<int32_t>(holder);
            enterScope(scope);
            const uint32_t outer = curScope_;
            curScope_ = scope;
            compileBlock(w.body);
            curScope_ = outer;
        }

        void compileOrElse(const OrElseStmt &oe)
        {
            guard(oe.loc);
            const uint32_t scope = scopeOf_.at(&oe);
            const bool declares = oe.primaryStmt->kind == StmtKind::Declaration;
            const DeclarationStmt *decl = declares ? std::get_if<DeclarationStmt>(&oe.primaryStmt->data) : nullptr;
            const bool top = curScope_ == 0 && kind_ == Proto::Kind::TopLevel;
            Var *declared = declares && !top ? findVar(curScope_, decl->nameId) : nullptr;
            const size_t defMark = undo_.size();

            const uint32_t start = pc();
            const uint32_t mark = top_;
            if (declares)
                compileDeclaration(*decl, true);
            else
                compileStmt(*oe.primaryStmt);
            top_ = mark;
            const uint32_t end = pc();
            const uint32_t skip = emitJump(Op::Jmp);

            // The handler: what the primary statement declared for certain is not certain here.
            restoreDefinite(defMark);
            const uint32_t target = pc();
            const uint32_t placeholderMeta = declares ? (makeMeta(typeCode(decl->type), false) | kMetaLoose) : 0;
            if (declared)
            {
                emit(Op::PlaceholdL, 0, off(declared->cell), 0, static_cast<int32_t>(placeholderMeta));
                markDefinite(*declared, placeholderMeta, declared->sameAttrs && !declared->anyConst);
            }
            else if (declares)
            {
                emit(Op::PlaceholdG, 0, gcell(table_->slotFor(decl->nameId)), 0, static_cast<int32_t>(placeholderMeta));
                markGlobalDefinite(decl->nameId);
            }
            enterScope(scope);
            Var *error = oe.errorName.empty() ? nullptr : findVar(scope, oe.errorNameId);
            if (error)
            {
                const uint32_t meta = makeMeta(typeCode(oe.errorType), false);
                if (oe.errorType.kind != TypeKind::Any)
                    emit(Op::ErrChk, 0, off(error->cell), 0, 0, &oe);
                if (error->needsMeta)
                    emit(Op::SetMeta, 0, off(error->cell), 0, static_cast<int32_t>(meta));
            }
            p_->handlers.push_back(Handler{start, end, target, error ? off(error->cell) : kNoCell});

            const uint32_t outer = curScope_;
            curScope_ = scope;
            if (error)
                markDefinite(*error, makeMeta(typeCode(oe.errorType), false), true);
            compileBlock(oe.fallbackBody);
            curScope_ = outer;
            patchTo(skip, pc());

            restoreDefinite(defMark);
            if (declared)
                markDefinite(*declared, makeMeta(typeCode(decl->type), decl->isConstant), declared->sameAttrs && !declared->anyConst);
            else if (declares)
                markGlobalDefinite(decl->nameId);
        }

        // ---- places ------------------------------------------------------------------------------------------------

        struct Root
        {
            RootKind kind = RootKind::Name;
            int32_t operand = 0;     // a register, a g or a NameRec
            bool variable = false;   // a variable for certain (not an object, and not a name that may be a member)
            bool attrsKnown = false;
            bool constant = false;
            bool checked = false;    // a top-level variable that may not be declared yet
            uint32_t meta = 0;
            Var *var = nullptr;
        };

        Root classifyRoot(const Place &pl)
        {
            Root root;
            if (pl.root == Place::Root::Self)
            {
                root.kind = RootKind::Self;
                return root;
            }
            if (pl.root == Place::Root::With)
            {
                root.kind = RootKind::WithObj;
                root.operand = innermostWith();
                return root;
            }
            Resolved r = resolve(pl.nameId, pl.name);
            if (r.kind == Resolved::Local && !r.attrsKnown)
                r = resolve(pl.nameId, pl.name, true);
            switch (r.kind)
            {
            case Resolved::Local:
                root.kind = RootKind::Local;
                root.operand = off(r.var->cell);
                root.var = r.var;
                root.variable = true;
                root.attrsKnown = true;
                root.meta = r.meta;
                root.constant = (r.meta & kMetaConst) != 0;
                break;
            case Resolved::Global:
                root.kind = RootKind::Global;
                root.operand = r.g;
                root.variable = true;
                root.attrsKnown = r.attrsKnown;
                root.checked = r.checked;
                root.meta = r.meta;
                root.constant = r.attrsKnown && (r.meta & kMetaConst) != 0;
                break;
            case Resolved::Lookup:
                root.kind = RootKind::Name;
                root.operand = static_cast<int32_t>(r.rec);
                break;
            }
            return root;
        }

        static bool hasFieldStep(const Place &pl)
        {
            for (const auto &s : pl.steps)
                if (!s.isIndex)
                    return true;
            return false;
        }

        // The index expressions of a place, in order, into consecutive registers.
        uint32_t genPlaceIndexes(const Place &pl)
        {
            const uint32_t base = top_;
            uint32_t n = 0;
            for (const auto &s : pl.steps)
                if (s.isIndex)
                    ++n;
            reserveTo(base + n);
            uint32_t i = 0;
            for (const auto &s : pl.steps)
            {
                if (!s.isIndex)
                    continue;
                top_ = base + i + 1;
                gen(*s.index, static_cast<int32_t>(base + i));
                ++i;
            }
            top_ = base + n;
            return base;
        }

        // The register that holds a variable root's value now.
        uint32_t rootValue(const Root &root)
        {
            if (root.kind == RootKind::Local)
                return static_cast<uint32_t>(root.operand / kCellBytes);
            const uint32_t t = allocTemp();
            emit(Op::GetG, 0, off(t), root.operand);
            return t;
        }

        // What has to be settled before the indexes and the value of a place statement are worked out: that its
        // first name exists, that a constant is not written, that a pure function writes no field. Returns false
        // when the statement cannot run at all.
        bool genPlaceGuards(const Place &pl, const Root &root, bool collection, const void *node)
        {
            const bool objectRoot = root.kind == RootKind::Self || root.kind == RootKind::WithObj;
            const bool touches = objectRoot || hasFieldStep(pl);
            const bool staticRoot = objectRoot || (root.variable && root.attrsKnown);
            const uint16_t x = static_cast<uint16_t>(static_cast<uint16_t>(root.kind) | (collection ? kRootCollection : 0) |
                                                     (root.kind == RootKind::Local && root.constant ? kRootConstant : 0));

            if (staticRoot && root.constant && pl.steps.empty())
            {
                emit(Op::Raise, static_cast<uint16_t>(collection ? RaiseKind::ConstantCollection : RaiseKind::ConstantAssign), 0, 0, 0, node);
                return false;
            }
            if (!staticRoot || (root.constant && !collection) || (kind_ != Proto::Kind::Function && touches))
            {
                emit(Op::PlaceChk, x, 0, 0, root.operand, node);
                return true;
            }
            if (pureFunction() && touches)
            {
                emit(Op::Raise, static_cast<uint16_t>(collection ? RaiseKind::PureFieldWriteCollection : RaiseKind::PureFieldWrite), 0, 0, 0,
                     node);
                return false;
            }
            return true;
        }

        void compileAssign(const AssignStmt &c)
        {
            guard(c.loc);
            const Place &pl = c.target;
            const Root root = classifyRoot(pl);
            const bool plainVar = root.variable && root.attrsKnown && !root.constant;

            if (pl.steps.empty() && plainVar)
            {
                if (root.kind == RootKind::Local)
                {
                    if (valueFits(*c.value, root.meta))
                    {
                        gen(*c.value, root.operand / kCellBytes);
                        return;
                    }
                    const Operand v = gen(*c.value, kNoCell);
                    const TypeKind kind = kindOfMeta(root.meta);
                    emit(Op::Store, static_cast<uint16_t>(static_cast<uint16_t>(kind) | (v.temp ? kMoveSource : 0)), root.operand, off(v.cell),
                         static_cast<int32_t>(root.meta >> kMetaTypeShift), &c);
                    return;
                }
                const bool fits = valueFits(*c.value, root.meta);
                const Operand v = gen(*c.value, kNoCell);
                emit(fits ? Op::SetGU : Op::SetG, v.temp ? kMoveSource : 0, root.operand, off(v.cell), 0, &c);
                return;
            }
            if (pl.steps.empty() && root.kind == RootKind::Global && !root.attrsKnown)
            {
                emit(Op::CheckG, 0, root.operand, 0, 0, &c);
                const Operand v = gen(*c.value, kNoCell);
                emit(Op::SetG, v.temp ? kMoveSource : 0, root.operand, off(v.cell), 0, &c);
                return;
            }

            if (!genPlaceGuards(pl, root, false, &c))
                return;

            // One field of an object: object.name = value.
            if ((root.kind == RootKind::Self || plainVar) && pl.steps.size() == 1 && !pl.steps[0].isIndex)
            {
                const Operand v = gen(*c.value, kNoCell);
                const uint32_t holder = root.kind == RootKind::Self ? 0 : rootValue(root);
                emit(Op::SetField, v.temp ? kMoveSource : 0, off(holder), off(v.cell), newFieldCache(), &pl.steps[0]);
                return;
            }
            // One item of a list or a map held by a variable: items[i] = value.
            if (plainVar && pl.steps.size() == 1 && pl.steps[0].isIndex)
            {
                const uint32_t base = genPlaceIndexes(pl);
                const Operand v = gen(*c.value, kNoCell);
                const uint32_t container = rootValue(root);
                emit(Op::SetIndex, v.temp ? kMoveSource : 0, off(container), off(base), off(v.cell), &pl.steps[0]);
                return;
            }

            const uint32_t base = genPlaceIndexes(pl);
            const Operand v = gen(*c.value, kNoCell);
            emit(Op::PlaceSet, static_cast<uint16_t>(static_cast<uint16_t>(root.kind) | (v.temp ? kMoveSource : 0)), off(base), off(v.cell),
                 root.operand, &c);
        }

        void compileCollectionOp(const CollectionOpStmt &co)
        {
            guard(co.loc);
            const Place &pl = co.target;
            const Root root = classifyRoot(pl);
            const bool plainVar = root.variable && root.attrsKnown && !root.constant;

            if (!genPlaceGuards(pl, root, true, &co))
                return;

            if (plainVar && pl.steps.empty() && co.opKind == CollectionOpStmt::OpKind::Add)
            {
                const Operand v = gen(*co.value, kNoCell);
                const uint32_t list = rootValue(root);
                emit(Op::ListPush, v.temp ? kMoveSource : 0, off(list), off(v.cell), 0, &co);
                return;
            }

            const uint32_t base = genPlaceIndexes(pl);
            const Operand v = gen(*co.value, kNoCell);
            emit(Op::CollOp, static_cast<uint16_t>(static_cast<uint16_t>(root.kind) | kRootCollection | (v.temp ? kMoveSource : 0)), off(base),
                 off(v.cell), root.operand, &co);
        }

        // ---- a function ----------------------------------------------------------------------------------------------

        void genFunction(const FunctionDecl &decl)
        {
            const size_t n = decl.params.size();
            for (size_t i = decl.requiredParams; i < n; ++i)
                findVar(0, decl.paramIds[i])->hidden = true;
            for (size_t i = 0; i < decl.requiredParams; ++i)
                markDefinite(*findVar(0, decl.paramIds[i]), makeMeta(typeCode(decl.paramTypes[i]), false), true);

            // A parameter that was given no argument gets its default, worked out here, where the parameters before
            // it are already bound and the ones after it are not.
            for (size_t i = decl.requiredParams; i < n; ++i)
            {
                Var &v = *findVar(0, decl.paramIds[i]);
                const uint32_t meta = makeMeta(typeCode(decl.paramTypes[i]), false);
                const uint32_t given = emitJump(Op::ArgJmp, 0, static_cast<int32_t>(i));
                const uint32_t mark = top_;
                gen(*decl.paramDefaults[i], static_cast<int32_t>(v.cell));
                top_ = mark;
                emit(Op::DefChk, static_cast<uint16_t>(decl.paramTypes[i].kind), off(v.cell), static_cast<int32_t>(i),
                     v.needsMeta ? static_cast<int32_t>(meta) : 0, &decl);
                patchTo(given, pc());
                v.hidden = false;
                markDefinite(v, meta, true);
            }
            for (const auto &s : decl.body)
                compileStmt(*s);
            emit(Op::RetNil);
        }
    };

}
