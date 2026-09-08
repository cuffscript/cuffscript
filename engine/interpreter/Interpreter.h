#pragma once

#include "Value.h"
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
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cctype>

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
    //    exist yet. This engine's model, chosen for correctness and safety
    //    over concurrency: `await` (and calling an async function without
    //    `await`) both execute the function synchronously, to completion,
    //    immediately. `await` additionally requires its target to actually
    //    be declared `async`, so it still functions as useful documentation.
    //  - Adding a new expression/statement kind: add the case to evalExpr /
    //    execStatement below (the switch is exhaustive and -Werror=switch
    //    will fail the build if a case is missed, which is intentional).
    // =========================================================================
    class Interpreter
    {
    public:
        Interpreter() { registerBuiltins(natives_); }

        // Entry point for the top-level script. `scriptDir` is used to
        // resolve relative `use ... from ...` paths; the caller must keep
        // `program` alive for the interpreter's whole lifetime (userFunctions_
        // stores raw pointers into it).
        void run(const Program &program, const std::string &scriptDir)
        {
            scriptDir_ = scriptDir.empty() ? std::string(".") : scriptDir;
            execProgram(program, globalEnv_);
        }

    private:
        Environment globalEnv_;
        std::unordered_map<std::string, NativeFn> natives_;
        std::unordered_map<std::string, const FunctionDecl *> userFunctions_;
        std::vector<std::unique_ptr<Program>> loadedModules_; // keeps imported-module ASTs alive
        std::unordered_set<std::string> importedPaths_;
        std::string scriptDir_ = ".";
        regex::RegexEngine regexEngine_;

        int callDepth_ = 0;
        bool inFunctionBody_ = false;
        bool currentFunctionReturnable_ = false;
        static constexpr int kMaxCallDepth = 1000;

        // RAII guard for entering/leaving a user function call — keeps the
        // call-depth counter and the two "current frame" flags correct even
        // when a ReturnSignal or an error unwinds through callUserFunction.
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
                execStatement(*s, env);
            }
        }

        void execBlock(const std::vector<std::unique_ptr<Stmt>> &body, Environment &env)
        {
            for (auto &s : body)
                execStatement(*s, env);
        }

        void registerFunction(const FunctionDecl &decl)
        {
            userFunctions_[decl.name] = &decl;
        }

        void execStatement(const Stmt &stmt, Environment &env)
        {
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
                execDeclaration(std::get<DeclarationStmt>(stmt.data), env);
                break;
            case StmtKind::Change:
                execChange(std::get<ChangeStmt>(stmt.data), env);
                break;
            case StmtKind::FunctionDecl:
            {
                const auto &decl = std::get<FunctionDecl>(stmt.data);
                if (inFunctionBody_)
                {
                    throw CuffRuntimeError(ErrorCode::NestedFunctionNotSupported,
                                           "nested function definitions are not supported ('" + decl.name + "' is defined inside another function)",
                                           decl.loc, "move '" + decl.name + "' to the top level");
                }
                registerFunction(decl); // reached for functions nested in top-level if/loop bodies
                break;
            }
            case StmtKind::IfStmt:
                execIf(std::get<IfStmt>(stmt.data), env);
                break;
            case StmtKind::LoopStmt:
                execLoop(std::get<LoopStmt>(stmt.data), env);
                break;
            case StmtKind::StopStmt:
                throw StopSignal{};
            case StmtKind::ReturnStmt:
            {
                const auto &r = std::get<ReturnStmt>(stmt.data);
                Value v = r.value ? evalExpr(*r.value, env) : Value::makeEmpty();
                if (r.value && !currentFunctionReturnable_)
                {
                    throw CuffRuntimeError(ErrorCode::UnsupportedOperation,
                                           "cannot return a value from a non-returnable function",
                                           r.loc, "declare it with 'set returnable function' to allow returning a value");
                }
                throw ReturnSignal{std::move(v)};
            }
            case StmtKind::AwaitStmt:
                execAwaitStmt(std::get<AwaitStmt>(stmt.data), env);
                break;
            case StmtKind::UseStmt:
                execUse(std::get<UseStmt>(stmt.data), env);
                break;
            case StmtKind::ExprStmt:
                evalExpr(*std::get<ExprStmt>(stmt.data).expr, env);
                break;
            case StmtKind::CollectionOp:
                execCollectionOp(std::get<CollectionOpStmt>(stmt.data), env);
                break;
            case StmtKind::OrElse:
                execOrElse(std::get<OrElseStmt>(stmt.data), env);
                break;
            }
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

        static void checkDeclaredType(const std::string &varType, const Value &v, const std::string &name, const SourceLocation &loc)
        {
            // `empty` is a universal "no value" sentinel — any declared type
            // may hold it (this is what lets `find`/`match`/map lookups that
            // come up empty be stored directly in a typed variable, to then
            // be handled with `or_else` or an `is empty` check).
            if (v.isEmpty())
                return;

            bool ok = true;
            if (varType == "number")
                ok = v.isNumber();
            else if (varType == "str")
                ok = v.isStr();
            else if (varType == "list")
                ok = v.isList();
            else if (varType == "map")
                ok = v.isMap();
            else if (varType == "boolean")
                ok = v.isBool();
            else if (varType == "empty")
                ok = false; // already handled above; a non-empty value can never satisfy `empty`
            else if (varType == "match")
                ok = v.isMatch();
            if (!ok)
            {
                throw CuffRuntimeError(ErrorCode::DeclarationTypeMismatch,
                                       "cannot assign a " + valueTypeName(v.type()) + " value to " + varType + " variable '" + name + "'",
                                       loc, "the declared type (" + varType + ") and the assigned value's type must match at 'set'");
            }
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
            env.declare(decl.name, std::move(v), decl.isConstant);
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
                env.declareGlobal(c.name);
                return;
            }

            auto look = env.resolve(c.name);
            if (!look.value)
            {
                throw UndefinedVariableError("cannot change undefined variable '" + c.name + "'", c.loc,
                                             "declare it first with 'set', or bridge a global with 'change " + c.name + " to global'");
            }
            if (env.isConstantIn(look.owner, c.name))
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

        void assignLoopVar(const std::string &name, Value v, Environment &env, const SourceLocation &loc)
        {
            if (env.isDeclaredHere(name) && env.isConstantIn(&env, name))
            {
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot use constant '" + name + "' as a loop variable", loc);
            }
            env.declare(name, std::move(v), false);
        }

        // ---- Control flow ----

        void execIf(const IfStmt &ifs, Environment &env)
        {
            for (auto &branch : ifs.branches)
            {
                if (!branch.condition || evalExpr(*branch.condition, env).truthy())
                {
                    execBlock(branch.body, env);
                    return;
                }
            }
        }

        void execLoop(const LoopStmt &loop, Environment &env)
        {
            try
            {
                if (loop.kind == LoopStmt::LoopKind::Repeat)
                {
                    Value startV = evalExpr(*loop.repeatStart, env);
                    Value endV = evalExpr(*loop.repeatEnd, env);
                    if (!startV.isNumber() || !endV.isNumber())
                        throw TypeError("'loop repeat' bounds must be numbers", loop.loc);
                    long long start = static_cast<long long>(std::llround(startV.asNumber()));
                    long long end = static_cast<long long>(std::llround(endV.asNumber()));
                    if (start <= end)
                    {
                        for (long long i = start; i <= end; ++i)
                        {
                            assignLoopVar(loop.repeatVar, Value::makeNumber(static_cast<double>(i)), env, loop.loc);
                            execBlock(loop.body, env);
                        }
                    }
                    else
                    {
                        for (long long i = start; i >= end; --i)
                        {
                            assignLoopVar(loop.repeatVar, Value::makeNumber(static_cast<double>(i)), env, loop.loc);
                            execBlock(loop.body, env);
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
                        execBlock(loop.body, env);
                    }
                }
            }
            catch (StopSignal &)
            {
                // `stop` just exits the loop.
            }
        }

        void execOrElse(const OrElseStmt &oe, Environment &env)
        {
            try
            {
                execStatement(*oe.primaryStmt, env);
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
                    if (!env.isDeclaredHere(decl.name))
                        env.declare(decl.name, Value::makeEmpty(), false);
                }

                Environment blockEnv(Environment::Kind::BlockScope, env);
                execBlock(oe.fallbackBody, blockEnv);
            }
        }

        // ---- Collections ----

        void execCollectionOp(const CollectionOpStmt &co, Environment &env)
        {
            auto look = env.resolve(co.collectionName);
            if (!look.value)
                throw UndefinedVariableError("undefined collection '" + co.collectionName + "'", co.loc);
            if (env.isConstantIn(look.owner, co.collectionName))
                throw ConstantError(ErrorCode::ConstantReassignment, "cannot modify constant collection '" + co.collectionName + "'", co.loc);

            Value &target = *look.value;

            switch (co.opKind)
            {
            case CollectionOpStmt::OpKind::Add:
            {
                if (!target.isList())
                    throw TypeError("'add ... to' requires a list, got " + valueTypeName(target.type()), co.loc);
                Value v = evalExpr(*co.addValue, env);
                target.asList()->items.push_back(std::move(v));
                break;
            }
            case CollectionOpStmt::OpKind::Replace:
            {
                Value idxKey = evalExpr(*co.indexOrKey, env);
                Value newVal = evalExpr(*co.newValue, env);
                std::vector<Value> idxVals{idxKey};
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
                        size_t real = resolveIndex1Based(static_cast<long long>(std::llround(rv.asNumber())), items.size(), co.loc);
                        items.erase(items.begin() + static_cast<long>(real));
                    }
                    else
                    {
                        auto it = std::find_if(items.begin(), items.end(), [&](const Value &v)
                                                { return v.strictEquals(rv); });
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

            void write(Value v)
            {
                if (kind == Kind::ListIndex)
                    list->items[index] = std::move(v);
                else
                    map->set(key, std::move(v));
            }

            static ContainerSlot forList(ValueList *l, size_t i)
            {
                ContainerSlot s;
                s.kind = Kind::ListIndex;
                s.list = l;
                s.index = i;
                return s;
            }
            static ContainerSlot forMap(ValueMap *m, std::string k)
            {
                ContainerSlot s;
                s.kind = Kind::MapKey;
                s.map = m;
                s.key = std::move(k);
                return s;
            }
        };

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
                size_t real = resolveIndex1Based(static_cast<long long>(std::llround(indexVal.asNumber())), target.asList()->items.size(), loc);
                return target.asList()->items[real];
            }
            if (target.isStr())
            {
                if (!indexVal.isNumber())
                    throw TypeError("str index must be a number (1-based)", loc);
                const std::string &s = target.asStr();
                size_t real = resolveIndex1Based(static_cast<long long>(std::llround(indexVal.asNumber())), s.size(), loc);
                return Value::makeStr(std::string(1, s[real]));
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
                    size_t real = resolveIndex1Based(static_cast<long long>(std::llround(indexVal.asNumber())), m->positional.size(), loc);
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
            long long s = static_cast<long long>(std::llround(startVal.asNumber()));
            long long e = static_cast<long long>(std::llround(endVal.asNumber()));

            if (target.isList())
            {
                size_t n = target.asList()->items.size();
                size_t rs = resolveIndex1Based(s, n, loc);
                size_t re = resolveIndex1Based(e, n, loc);
                auto result = std::make_shared<ValueList>();
                if (rs <= re)
                    for (size_t i = rs; i <= re; ++i)
                        result->items.push_back(target.asList()->items[i]);
                return Value::makeList(result);
            }
            if (target.isStr())
            {
                const std::string &str = target.asStr();
                size_t n = str.size();
                size_t rs = resolveIndex1Based(s, n, loc);
                size_t re = resolveIndex1Based(e, n, loc);
                if (rs > re)
                    return Value::makeStr("");
                return Value::makeStr(str.substr(rs, re - rs + 1));
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
                size_t real = resolveIndex1Based(static_cast<long long>(std::llround(finalIdx.asNumber())), current.asList()->items.size(), loc);
                return ContainerSlot::forList(current.asList().get(), real);
            }
            if (current.isMap())
            {
                if (!finalIdx.isStr())
                    throw TypeError("map keys are strings — cannot assign with a " + valueTypeName(finalIdx.type()), loc);
                return ContainerSlot::forMap(current.asMap().get(), finalIdx.asStr());
            }
            throw TypeError("cannot index-assign into a " + valueTypeName(current.type()) + " value", loc);
        }

        // ==== Expression evaluation =============================================

        Value evalExpr(const Expr &expr, Environment &env)
        {
            switch (expr.kind)
            {
            case ExprKind::Number:
                return Value::makeNumber(std::get<NumberLiteral>(expr.data).value);
            case ExprKind::String:
                return Value::makeStr(std::get<StringLiteral>(expr.data).value);
            case ExprKind::Bool:
                return Value::makeBool(std::get<BoolLiteral>(expr.data).value);
            case ExprKind::Empty:
                return Value::makeEmpty();
            case ExprKind::Identifier:
            {
                const auto &id = std::get<IdentifierExpr>(expr.data);
                auto look = env.resolve(id.name);
                if (!look.value)
                    throw UndefinedVariableError("undefined variable '" + id.name + "'", id.loc);
                return *look.value;
            }
            case ExprKind::List:
            {
                const auto &list = std::get<ListLiteral>(expr.data);
                auto out = std::make_shared<ValueList>();
                out->items.reserve(list.elements.size());
                for (auto &e : list.elements)
                    out->items.push_back(evalExpr(*e, env));
                return Value::makeList(out);
            }
            case ExprKind::Map:
            {
                const auto &map = std::get<MapLiteral>(expr.data);
                auto out = std::make_shared<ValueMap>();
                for (auto &pair : map.pairs)
                {
                    Value k = evalExpr(*pair.key, env);
                    if (!k.isStr())
                        throw TypeError("map keys must be strings", map.loc);
                    Value v = evalExpr(*pair.value, env);
                    out->set(k.asStr(), std::move(v));
                }
                return Value::makeMap(out);
            }
            case ExprKind::FString:
            {
                const auto &fs = std::get<FStringExpr>(expr.data);
                std::string out;
                for (auto &seg : fs.segments)
                {
                    if (seg.isExpression)
                        out += evalExpr(*seg.expr, env).toDisplayString();
                    else
                        out += seg.text;
                }
                return Value::makeStr(out);
            }
            case ExprKind::BinaryOp:
                return evalBinaryOp(std::get<BinaryOp>(expr.data), env);
            case ExprKind::UnaryOp:
                return evalUnaryOp(std::get<UnaryOp>(expr.data), env);
            case ExprKind::IndexAccess:
            {
                const auto &idx = std::get<IndexAccess>(expr.data);
                Value t = evalExpr(*idx.target, env);
                Value i = evalExpr(*idx.index, env);
                return indexInto(t, i, idx.loc);
            }
            case ExprKind::SliceAccess:
            {
                const auto &sl = std::get<SliceAccess>(expr.data);
                Value t = evalExpr(*sl.target, env);
                Value s = evalExpr(*sl.start, env);
                Value e = evalExpr(*sl.end, env);
                return sliceInto(t, s, e, sl.loc);
            }
            case ExprKind::FunctionCall:
            {
                const auto &fc = std::get<FunctionCall>(expr.data);
                std::vector<Value> args;
                args.reserve(fc.args.size());
                for (auto &a : fc.args)
                    args.push_back(evalExpr(*a, env));
                return callFunction(fc.functionName, args, fc.loc);
            }
            case ExprKind::Await:
            {
                const auto &aw = std::get<AwaitExpr>(expr.data);
                checkAsyncTarget(aw.call->functionName, aw.loc);
                std::vector<Value> args;
                args.reserve(aw.call->args.size());
                for (auto &a : aw.call->args)
                    args.push_back(evalExpr(*a, env));
                return callFunction(aw.call->functionName, args, aw.loc);
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

        Value evalUnaryOp(const UnaryOp &u, Environment &env)
        {
            Value operand = evalExpr(*u.operand, env);
            if (u.op == "!")
                return Value::makeBool(!operand.truthy());
            if (u.op == "-")
            {
                if (!operand.isNumber())
                    throw TypeError("unary '-' requires a number, got " + valueTypeName(operand.type()), u.loc);
                return Value::makeNumber(-operand.asNumber());
            }
            throw InternalEngineError("unknown unary operator '" + u.op + "'");
        }

        static std::string lowerAscii(const std::string &s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                            { return std::tolower(c); });
            return out;
        }

        Value evalBinaryOp(const BinaryOp &b, Environment &env)
        {
            if (b.op == "is" || b.op == "IS" || b.op == "is not" || b.op == "IS not")
            {
                Value l = evalExpr(*b.left, env);
                Value r = evalExpr(*b.right, env);
                bool ci = !b.op.empty() && b.op[0] == 'I';
                bool eq;
                if (ci && l.isStr() && r.isStr())
                    eq = lowerAscii(l.asStr()) == lowerAscii(r.asStr());
                else
                    eq = l.strictEquals(r);
                bool negated = (b.op == "is not" || b.op == "IS not");
                return Value::makeBool(negated ? !eq : eq);
            }

            if (b.op == ">" || b.op == "<" || b.op == ">=" || b.op == "<=")
            {
                Value l = evalExpr(*b.left, env);
                Value r = evalExpr(*b.right, env);
                if (l.isNumber() && r.isNumber())
                {
                    double a = l.asNumber(), c = r.asNumber();
                    if (b.op == ">")
                        return Value::makeBool(a > c);
                    if (b.op == "<")
                        return Value::makeBool(a < c);
                    if (b.op == ">=")
                        return Value::makeBool(a >= c);
                    return Value::makeBool(a <= c);
                }
                if (l.isStr() && r.isStr())
                {
                    const std::string &a = l.asStr();
                    const std::string &c = r.asStr();
                    if (b.op == ">")
                        return Value::makeBool(a > c);
                    if (b.op == "<")
                        return Value::makeBool(a < c);
                    if (b.op == ">=")
                        return Value::makeBool(a >= c);
                    return Value::makeBool(a <= c);
                }
                throw TypeError("cannot compare " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()) + " with '" + b.op + "'", b.loc,
                                "comparisons work on two numbers or two strings");
            }

            Value l = evalExpr(*b.left, env);
            Value r = evalExpr(*b.right, env);

            if (b.op == "+")
            {
                if (l.isNumber() && r.isNumber())
                    return Value::makeNumber(l.asNumber() + r.asNumber());
                if (l.isStr() && r.isStr())
                    return Value::makeStr(l.asStr() + r.asStr());
                if (l.isList() && r.isList())
                {
                    auto out = std::make_shared<ValueList>();
                    out->items = l.asList()->items;
                    out->items.insert(out->items.end(), r.asList()->items.begin(), r.asList()->items.end());
                    return Value::makeList(out);
                }
                throw TypeError("cannot add " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc,
                                "'+' works on number+number, str+str, or list+list");
            }
            if (b.op == "-")
            {
                if (l.isNumber() && r.isNumber())
                    return Value::makeNumber(l.asNumber() - r.asNumber());
                throw TypeError("'-' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);
            }
            if (b.op == "*")
            {
                if (l.isNumber() && r.isNumber())
                    return Value::makeNumber(l.asNumber() * r.asNumber());
                throw TypeError("'*' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);
            }
            if (b.op == "/")
            {
                if (l.isNumber() && r.isNumber())
                {
                    if (r.asNumber() == 0.0)
                        throw DivisionByZeroError("division by zero", b.loc);
                    return Value::makeNumber(l.asNumber() / r.asNumber());
                }
                throw TypeError("'/' requires two numbers, got " + valueTypeName(l.type()) + " and " + valueTypeName(r.type()), b.loc);
            }
            throw InternalEngineError("unknown binary operator '" + b.op + "'");
        }

        // ---- Function calls ----

        void checkAsyncTarget(const std::string &name, const SourceLocation &loc)
        {
            auto it = userFunctions_.find(name);
            if (it != userFunctions_.end() && !it->second->isAsync)
            {
                throw CuffRuntimeError(ErrorCode::AwaitOnNonAsync,
                                       "'await' can only be used with an async function; '" + name + "' is not declared async", loc,
                                       "declare it with 'set async function " + name + "(...) do:', or call it without 'await'");
            }
            // Native/DLC functions aren't classified async/sync — awaiting one
            // is allowed and just calls it normally.
        }

        Value callFunction(const std::string &name, std::vector<Value> &args, const SourceLocation &loc)
        {
            auto nativeIt = natives_.find(name);
            if (nativeIt != natives_.end())
                return nativeIt->second(args, loc);

            auto userIt = userFunctions_.find(name);
            if (userIt == userFunctions_.end())
                throw UndefinedFunctionError("undefined function '" + name + "'", loc,
                                             "check the spelling, or make sure it's declared before this point");

            return callUserFunction(*userIt->second, args, loc);
        }

        Value callUserFunction(const FunctionDecl &decl, std::vector<Value> &args, const SourceLocation &loc)
        {
            if (args.size() != decl.params.size())
                throw ArgumentError(decl.name + "() expects " + std::to_string(decl.params.size()) +
                                        " argument(s), got " + std::to_string(args.size()),
                                    loc);

            if (callDepth_ + 1 > kMaxCallDepth)
                throw StackOverflowError("maximum call depth (" + std::to_string(kMaxCallDepth) +
                                             ") exceeded while calling '" + decl.name + "' — check for infinite recursion",
                                         loc);

            FrameGuard guard(this, decl.isReturnable);

            Environment funcEnv(Environment::Kind::FunctionScope, globalEnv_);
            for (size_t i = 0; i < decl.params.size(); ++i)
                funcEnv.declare(decl.params[i], args[i], false);

            try
            {
                execBlock(decl.body, funcEnv);
            }
            catch (ReturnSignal &r)
            {
                return std::move(r.value);
            }
            return Value::makeEmpty();
        }

        void execAwaitStmt(const AwaitStmt &aw, Environment &env)
        {
            checkAsyncTarget(aw.expr->call->functionName, aw.loc);
            std::vector<Value> args;
            args.reserve(aw.expr->call->args.size());
            for (auto &a : aw.expr->call->args)
                args.push_back(evalExpr(*a, env));
            callFunction(aw.expr->call->functionName, args, aw.loc); // result intentionally discarded
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
                auto all = regexEngine_.searchAll(compiled, targetVal.asStr(), flags, f.loc);
                auto list = std::make_shared<ValueList>();
                for (auto &m : all)
                    list->items.push_back(Value::makeStr(targetVal.asStr().substr(m.start, m.end - m.start)));
                return Value::makeList(list);
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

        void loadCustomModule(const std::string &moduleName, const std::string &relPath, const SourceLocation &loc, Environment &env)
        {
            namespace fs = std::filesystem;
            fs::path base = fs::path(scriptDir_);
            fs::path full = base / relPath / (moduleName + ".cuff");

            std::error_code ec;
            fs::path canon = fs::weakly_canonical(full, ec);
            std::string key = ec ? full.string() : canon.string();

            // Idempotent: a module already loaded (including the diamond- or
            // circular-import case) is treated as a no-op rather than
            // re-parsed/re-executed or flagged as an error.
            if (importedPaths_.count(key))
                return;
            importedPaths_.insert(key); // mark before parsing to make self-cycles a safe no-op too

            std::ifstream file(full);
            if (!file)
                throw ModuleError(ErrorCode::ModuleNotFound,
                                  "could not find module file '" + full.string() + "' for 'use " + moduleName + " from " + relPath + "'",
                                  loc, "paths are resolved relative to the running script's directory");

            std::ostringstream ss;
            ss << file.rdbuf();
            std::string source = ss.str();

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
                                  "failed to parse module '" + full.string() + "': " + e.message, loc);
            }

            // Execute the module's top level into its own environment, then
            // merge its top-level variables into the importer's current
            // scope. Function declarations are automatically visible to the
            // importer too, since userFunctions_ is a single registry shared
            // by the whole interpreter (execProgram registers them there).
            Environment moduleEnv;
            execProgram(*modProgram, moduleEnv);
            for (const auto &kv : moduleEnv.localVars())
                env.declare(kv.first, kv.second, moduleEnv.isConstantHere(kv.first));

            loadedModules_.push_back(std::move(modProgram));
        }
    };

} // namespace cuff
