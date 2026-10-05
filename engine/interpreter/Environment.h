#pragma once

#include "Value.h"
#include "../common/NameInterner.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>

namespace cuff
{

    class Environment
    {
    public:
        Environment() : parent_(nullptr), global_(this), isFunctionScope_(true) {}

        // Function scopes are isolated from callers; block scopes chain to their enclosing scope.
        enum class Kind
        {
            FunctionScope,
            BlockScope
        };

        Environment(Kind kind, Environment &ref)
            : parent_(kind == Kind::BlockScope ? &ref : nullptr),
              global_(kind == Kind::FunctionScope ? &ref : ref.global_),
              isFunctionScope_(kind == Kind::FunctionScope)
        {
        }

        // Environments are stack-local and referenced by raw pointer, so lifetimes must nest.
        Environment(const Environment &) = delete;
        Environment &operator=(const Environment &) = delete;
        Environment(Environment &&) = delete;
        Environment &operator=(Environment &&) = delete;

        void declare(uint32_t nameId, Value value, bool isConstant)
        {
            if (Value *existing = findLocal(nameId))
            {
                *existing = std::move(value);
                setConstantFlag(nameId, isConstant);
                return;
            }
            append(nameId, std::move(value));
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
            append(nameId, std::move(value));
            return true;
        }

        void declare(const std::string &name, Value value, bool isConstant)
        {
            declare(internName(name), std::move(value), isConstant);
        }

        void reserve(size_t n) { vars_.reserve(n); }

        void adoptStorage(std::vector<std::pair<uint32_t, Value>> &&storage)
        {
            storage.clear();
            vars_ = std::move(storage);
        }

        std::vector<std::pair<uint32_t, Value>> releaseStorage() { return std::move(vars_); }

        bool isDeclaredHere(uint32_t nameId) const
        {
            return const_cast<Environment *>(this)->findLocal(nameId) != nullptr;
        }

        // Marks the name as a global bridge for this function call.
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
            Environment *owner = nullptr;
        };

        // Local bindings win over `change x to global`; otherwise lookup falls back to the true global scope.
        Lookup resolve(uint32_t nameId)
        {
            Environment *e = this;
            while (true)
            {
                if (Value *v = e->findLocal(nameId))
                    return {v, e};
                if (e->isFunctionScope_)
                {
                    if (!e->globalDeclared_.empty() &&
                        std::find(e->globalDeclared_.begin(), e->globalDeclared_.end(), nameId) != e->globalDeclared_.end())
                    {
                        if (Value *v = e->global_->findLocal(nameId))
                            return {v, e->global_};
                        return {nullptr, nullptr};
                    }
                    break;
                }
                e = e->parent_;
            }
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

        const std::vector<std::pair<uint32_t, Value>> &localVars() const { return vars_; }
        bool isConstantHere(uint32_t nameId) const
        {
            return std::find(constants_.begin(), constants_.end(), nameId) != constants_.end();
        }

    private:
        Environment *parent_;
        Environment *global_;
        bool isFunctionScope_;
        std::vector<std::pair<uint32_t, Value>> vars_;
        std::vector<uint32_t> constants_;
        std::vector<uint32_t> globalDeclared_;
        std::unique_ptr<std::unordered_map<uint32_t, size_t>> index_;
        static constexpr size_t kIndexThreshold = 16;

        // vars_ is append-only, so the name index never goes stale.
        void append(uint32_t nameId, Value value)
        {
            vars_.emplace_back(nameId, std::move(value));
            if (index_)
            {
                index_->emplace(nameId, vars_.size() - 1);
            }
            else if (vars_.size() >= kIndexThreshold)
            {
                index_ = std::make_unique<std::unordered_map<uint32_t, size_t>>();
                index_->reserve(vars_.size() * 2);
                for (size_t i = 0; i < vars_.size(); ++i)
                    (*index_)[vars_[i].first] = i;
            }
        }

        Value *findLocal(uint32_t nameId)
        {
            if (index_)
            {
                auto it = index_->find(nameId);
                return it == index_->end() ? nullptr : &vars_[it->second].second;
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

}
