#pragma once

#include "Value.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

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
        void declare(const std::string &name, Value value, bool isConstant)
        {
            vars_[name] = std::move(value);
            if (isConstant)
                constants_.insert(name);
            else
                constants_.erase(name);
        }

        // Pre-sizes the local variable table — called once per function call
        // with the parameter count, to avoid rehashing as parameters are
        // declared one at a time.
        void reserve(size_t n) { vars_.reserve(n); }

        bool isDeclaredHere(const std::string &name) const { return vars_.count(name) > 0; }

        // Mark `name` as referring to the global scope for the rest of this
        // function call (`change name to global`). Applied to the nearest
        // enclosing function-scope environment so it survives through any
        // block scopes (or_else) nested inside the same call.
        void declareGlobal(const std::string &name)
        {
            Environment *e = this;
            while (!e->isFunctionScope_ && e->parent_)
                e = e->parent_;
            e->globalDeclared_.insert(name);
        }

        struct Lookup
        {
            Value *value = nullptr;
            Environment *owner = nullptr; // environment that actually stores it
        };

        // Used for both reads and change-writes: walks block scopes up to
        // the nearest function-scope environment, then (if not found there
        // and not explicitly global-declared) falls back to true global.
        Lookup resolve(const std::string &name)
        {
            Environment *e = this;
            while (true)
            {
                if (e->isFunctionScope_ && e->globalDeclared_.count(name))
                {
                    auto it = e->global_->vars_.find(name);
                    if (it != e->global_->vars_.end())
                        return {&it->second, e->global_};
                    return {nullptr, nullptr};
                }
                auto it = e->vars_.find(name);
                if (it != e->vars_.end())
                    return {&it->second, e};
                if (e->isFunctionScope_)
                    break;
                e = e->parent_;
            }
            // Implicit read fallback to true global (Python-like).
            if (e != e->global_)
            {
                auto it = e->global_->vars_.find(name);
                if (it != e->global_->vars_.end())
                    return {&it->second, e->global_};
            }
            return {nullptr, nullptr};
        }

        bool isConstantIn(Environment *owner, const std::string &name) const
        {
            return owner && owner->constants_.count(name) > 0;
        }

        Environment *globalEnv() { return global_; }

        // Introspection used only for merging a freshly-executed module's
        // top-level bindings into the importing script's scope (see
        // Interpreter::loadCustomModule).
        const std::unordered_map<std::string, Value> &localVars() const { return vars_; }
        bool isConstantHere(const std::string &name) const { return constants_.count(name) > 0; }

    private:
        Environment *parent_;
        Environment *global_;
        bool isFunctionScope_;
        std::unordered_map<std::string, Value> vars_;
        std::unordered_set<std::string> constants_;
        std::unordered_set<std::string> globalDeclared_;
    };

} // namespace cuff
