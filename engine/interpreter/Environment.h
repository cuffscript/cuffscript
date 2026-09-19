#pragma once

#include "Value.h"
#include "../common/NameInterner.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>

namespace cuff
{

    // =========================================================================
    // Scoping model (see docs/SPEC.md "함수 레벨 스코프"):
    //
    //  - CuffScript has no nested functions/closures, so a function call only
    //    ever sees the global scope plus its own locals -- never a caller's
    //    locals. Each function call gets a fresh, function-scope Environment
    //    whose `parent_` is null; `global_` points at the one true global
    //    Environment for fallback reads and explicit `global` writes.
    //
    //  - `if`/`loop` bodies do NOT introduce a new scope (matching Python:
    //    a variable set inside an `if` stays visible for the rest of the
    //    function). The interpreter simply reuses the current Environment
    //    for those bodies -- see Interpreter::execBlock.
    //
    //  - `or_else` fallback bodies DO get their own block-local Environment
    //    (spec: "새로운 변수를 선언할 수도 있으며, 이 경우 블록 내 로컬
    //    스코프를 갖습니다"). These chain to their enclosing Environment via
    //    `parent_` for reads/writes of pre-existing names, while `set`
    //    always declares into the innermost (block) scope.
    //
    //  - Reading an undeclared local implicitly falls back to the global
    //    scope (Python-like). *Writing* via `change` to a name that isn't
    //    already local requires either that the name already exists locally,
    //    or that it was explicitly bridged with `change name to global`
    //    first -- `change` never silently creates a new global.
    // =========================================================================
    class Environment
    {
    public:
        // Root/global scope.
        Environment() : parent_(nullptr), global_(this), isFunctionScope_(true) {}

        enum class Kind
        {
            FunctionScope, // isolated from the caller; `ref` is the true global env
            BlockScope     // chains to `ref` for reads/writes; `set` still local
        };

        // Function-call or or_else-block scope. See Kind above.
        Environment(Kind kind, Environment &ref)
            : parent_(kind == Kind::BlockScope ? &ref : nullptr),
              global_(kind == Kind::FunctionScope ? &ref : ref.global_),
              isFunctionScope_(kind == Kind::FunctionScope)
        {
        }

        // Environments are referenced by raw pointer from other Environments
        // (parent_/global_) and are always created as stack-local variables
        // whose lifetime nests correctly with their parent's — see
        // Interpreter::callUserFunction / execOrElse. Copying or moving one
        // after the fact would silently invalidate those back-pointers, so
        // both are disabled outright rather than risking a dangling pointer.
        Environment(const Environment &) = delete;
        Environment &operator=(const Environment &) = delete;
        Environment(Environment &&) = delete;
        Environment &operator=(Environment &&) = delete;

        // `set` always declares into *this* (innermost) scope.
        void declare(uint32_t nameId, Value value, bool isConstant)
        {
            if (Value *existing = findLocal(nameId))
            {
                *existing = std::move(value);
                setConstantFlag(nameId, isConstant);
                return;
            }
            vars_.emplace_back(nameId, std::move(value));
            if (isConstant)
                constants_.push_back(nameId);
        }

        bool declareLoopVar(uint32_t nameId, Value value)
        {
            if (Value *existing = findLocal(nameId))
            {
                if (!constants_.empty() &&
                    std::find(constants_.begin(), constants_.end(), nameId) != constants_.end())
                    return false;
                *existing = std::move(value);
                return true;
            }
            vars_.emplace_back(nameId, std::move(value));
            return true;
        }

        // Convenience overload for the few paths that only have a name string
        // (module merging). Interning is a hash lookup, so it stays off the
        // hot path where the parser already supplied an id.
        void declare(const std::string &name, Value value, bool isConstant)
        {
            declare(internName(name), std::move(value), isConstant);
        }

        // Pre-sizes the local variable table — called once per function call
        // with the parameter count, so binding N parameters is one allocation.
        void reserve(size_t n) { vars_.reserve(n); }

        bool isDeclaredHere(uint32_t nameId) const
        {
            return const_cast<Environment *>(this)->findLocal(nameId) != nullptr;
        }

        // Mark `name` as referring to the global scope for the rest of this
        // function call (`change name to global`). Applied to the nearest
        // enclosing function-scope environment so it survives through any
        // block scopes (or_else) nested inside the same call.
        void declareGlobal(uint32_t nameId)
        {
            Environment *e = this;
            while (!e->isFunctionScope_ && e->parent_)
                e = e->parent_;
            if (std::find(e->globalDeclared_.begin(), e->globalDeclared_.end(), nameId) == e->globalDeclared_.end())
                e->globalDeclared_.push_back(nameId);
        }

        struct Lookup
        {
            Value *value = nullptr;
            Environment *owner = nullptr; // environment that actually stores it
        };

        // Used for both reads and change-writes: walks block scopes up to
        // the nearest function-scope environment, then (if not found there
        // and not explicitly global-declared) falls back to true global.
        Lookup resolve(uint32_t nameId)
        {
            Environment *e = this;
            while (true)
            {
                if (e->isFunctionScope_ && !e->globalDeclared_.empty() &&
                    std::find(e->globalDeclared_.begin(), e->globalDeclared_.end(), nameId) != e->globalDeclared_.end())
                {
                    if (Value *v = e->global_->findLocal(nameId))
                        return {v, e->global_};
                    return {nullptr, nullptr};
                }
                if (Value *v = e->findLocal(nameId))
                    return {v, e};
                if (e->isFunctionScope_)
                    break;
                e = e->parent_;
            }
            // Implicit read fallback to true global (Python-like).
            if (e != e->global_)
            {
                if (Value *v = e->global_->findLocal(nameId))
                    return {v, e->global_};
            }
            return {nullptr, nullptr};
        }

        bool isConstantIn(Environment *owner, uint32_t nameId) const
        {
            if (!owner)
                return false;
            return std::find(owner->constants_.begin(), owner->constants_.end(), nameId) != owner->constants_.end();
        }

        Environment *globalEnv() { return global_; }

        // Introspection used only for merging a freshly-executed module's
        // top-level bindings into the importing script's scope (see
        // Interpreter::loadCustomModule).
        const std::vector<std::pair<uint32_t, Value>> &localVars() const { return vars_; }
        bool isConstantHere(uint32_t nameId) const
        {
            return std::find(constants_.begin(), constants_.end(), nameId) != constants_.end();
        }

    private:
        Environment *parent_;
        Environment *global_;
        bool isFunctionScope_;
        // Scopes are small in practice (a function's parameters plus a handful
        // of locals), and a linear scan over a contiguous vector beats hashing
        // there: it needs one allocation for the whole scope instead of one
        // node per variable, and most name comparisons fail on length or the
        // first character. Measured: binding 3 parameters went from ~181ns to
        // ~57ns. The global scope can grow larger, so it gets a lazily-built
        // index once it passes kIndexThreshold entries (see findLocal).
        std::vector<std::pair<uint32_t, Value>> vars_;
        std::vector<uint32_t> constants_;
        std::vector<uint32_t> globalDeclared_;
        mutable std::unordered_map<uint32_t, size_t> index_;
        mutable bool indexValid_ = false;
        static constexpr size_t kIndexThreshold = 16;

        Value *findLocal(uint32_t nameId)
        {
            if (vars_.size() >= kIndexThreshold)
            {
                if (!indexValid_ || index_.size() != vars_.size())
                {
                    index_.clear();
                    index_.reserve(vars_.size());
                    for (size_t i = 0; i < vars_.size(); ++i)
                        index_[vars_[i].first] = i;
                    indexValid_ = true;
                }
                auto it = index_.find(nameId);
                return it == index_.end() ? nullptr : &vars_[it->second].second;
            }
            for (auto &kv : vars_)
                if (kv.first == nameId)
                    return &kv.second;
            return nullptr;
        }

        void setConstantFlag(uint32_t nameId, bool isConstant)
        {
            auto it = std::find(constants_.begin(), constants_.end(), nameId);
            if (isConstant && it == constants_.end())
                constants_.push_back(nameId);
            else if (!isConstant && it != constants_.end())
                constants_.erase(it);
        }
    };

} // namespace cuff
