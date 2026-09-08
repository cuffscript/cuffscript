#pragma once

#include "Value.h"
#include "../common/CuffError.h"
#include "../common/SourceLocation.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>

namespace cuff
{

    using NativeFn = std::function<Value(std::vector<Value> &, const SourceLocation &)>;

    inline void expectArgCount(const char *fn, std::vector<Value> &args, size_t n, const SourceLocation &loc)
    {
        if (args.size() != n)
            throw ArgumentError(std::string(fn) + "() expects " + std::to_string(n) + " argument(s), got " + std::to_string(args.size()), loc);
    }

    inline void expectArgRange(const char *fn, std::vector<Value> &args, size_t lo, size_t hi, const SourceLocation &loc)
    {
        if (args.size() < lo || args.size() > hi)
            throw ArgumentError(std::string(fn) + "() expects " + std::to_string(lo) + "-" + std::to_string(hi) + " argument(s), got " + std::to_string(args.size()), loc);
    }

    inline double expectNumber(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isNumber())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a number, got " + valueTypeName(args[i].type()), loc);
        return args[i].asNumber();
    }

    inline const std::string &expectStr(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
    {
        if (!args[i].isStr())
            throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a str, got " + valueTypeName(args[i].type()), loc);
        return args[i].asStr();
    }

    // ---- Always-available builtins ----

    inline void registerBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["print"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i)
                    std::cout << " ";
                std::cout << args[i].toDisplayString();
            }
            std::cout << "\n";
            return Value::makeEmpty();
        };

        reg["input"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("input", args, 0, 1, loc);
            if (!args.empty())
                std::cout << expectStr("input", args, 0, loc);
            std::string line;
            if (!std::getline(std::cin, line))
                return Value::makeStr("");
            return Value::makeStr(line);
        };
    }

    // ---- DLC:math ----
    inline void registerMathDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["sqrt"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("sqrt", args, 1, loc);
            double v = expectNumber("sqrt", args, 0, loc);
            if (v < 0)
                throw ArgumentError("sqrt() cannot take the square root of a negative number", loc);
            return Value::makeNumber(std::sqrt(v));
        };
        reg["abs"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("abs", args, 1, loc);
            return Value::makeNumber(std::fabs(expectNumber("abs", args, 0, loc)));
        };
        reg["pow"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("pow", args, 2, loc);
            return Value::makeNumber(std::pow(expectNumber("pow", args, 0, loc), expectNumber("pow", args, 1, loc)));
        };
        reg["round"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("round", args, 1, loc);
            return Value::makeNumber(std::round(expectNumber("round", args, 0, loc)));
        };
        reg["floor"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("floor", args, 1, loc);
            return Value::makeNumber(std::floor(expectNumber("floor", args, 0, loc)));
        };
        reg["ceil"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("ceil", args, 1, loc);
            return Value::makeNumber(std::ceil(expectNumber("ceil", args, 0, loc)));
        };
        reg["min"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            if (args.empty())
                throw ArgumentError("min() expects at least 1 argument", loc);
            double best = expectNumber("min", args, 0, loc);
            for (size_t i = 1; i < args.size(); ++i)
                best = std::min(best, expectNumber("min", args, i, loc));
            return Value::makeNumber(best);
        };
        reg["max"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            if (args.empty())
                throw ArgumentError("max() expects at least 1 argument", loc);
            double best = expectNumber("max", args, 0, loc);
            for (size_t i = 1; i < args.size(); ++i)
                best = std::max(best, expectNumber("max", args, i, loc));
            return Value::makeNumber(best);
        };
    }

    // ---- DLC:string ----
    inline void registerStringDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["upper"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("upper", args, 1, loc);
            std::string s = expectStr("upper", args, 0, loc);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                            { return std::toupper(c); });
            return Value::makeStr(s);
        };
        reg["lower"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("lower", args, 1, loc);
            std::string s = expectStr("lower", args, 0, loc);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                            { return std::tolower(c); });
            return Value::makeStr(s);
        };
        reg["trim"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("trim", args, 1, loc);
            std::string s = expectStr("trim", args, 0, loc);
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return Value::makeStr("");
            size_t b = s.find_last_not_of(" \t\r\n");
            return Value::makeStr(s.substr(a, b - a + 1));
        };
        reg["length"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("length", args, 1, loc);
            if (args[0].isStr())
                return Value::makeNumber(static_cast<double>(args[0].asStr().size()));
            if (args[0].isList())
                return Value::makeNumber(static_cast<double>(args[0].asList()->items.size()));
            if (args[0].isMap())
                return Value::makeNumber(static_cast<double>(args[0].asMap()->size()));
            throw TypeError("length() expects a str, list, or map, got " + valueTypeName(args[0].type()), loc);
        };
        reg["contains"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("contains", args, 2, loc);
            const std::string &hay = expectStr("contains", args, 0, loc);
            const std::string &needle = expectStr("contains", args, 1, loc);
            return Value::makeBool(hay.find(needle) != std::string::npos);
        };
        reg["starts_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("starts_with", args, 2, loc);
            const std::string &s = expectStr("starts_with", args, 0, loc);
            const std::string &pre = expectStr("starts_with", args, 1, loc);
            return Value::makeBool(s.compare(0, pre.size(), pre) == 0);
        };
        reg["ends_with"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("ends_with", args, 2, loc);
            const std::string &s = expectStr("ends_with", args, 0, loc);
            const std::string &suf = expectStr("ends_with", args, 1, loc);
            if (suf.size() > s.size())
                return Value::makeBool(false);
            return Value::makeBool(s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
        };
    }

    // ---- DLC:time ----
    inline void registerTimeDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto nowFn = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("now", args, 0, loc);
            auto now = std::chrono::system_clock::now().time_since_epoch();
            double secs = std::chrono::duration<double>(now).count();
            return Value::makeNumber(secs);
        };
        reg["now"] = nowFn;
        reg["timestamp"] = nowFn;
    }

    // ---- DLC:random ----
    inline void registerRandomDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto rng = std::make_shared<std::mt19937>(std::random_device{}());
        reg["random"] = [rng](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random", args, 0, loc);
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::makeNumber(dist(*rng));
        };
        reg["random_int"] = [rng](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_int", args, 2, loc);
            long lo = static_cast<long>(expectNumber("random_int", args, 0, loc));
            long hi = static_cast<long>(expectNumber("random_int", args, 1, loc));
            if (lo > hi)
                throw ArgumentError("random_int() expects the first argument to be <= the second", loc);
            std::uniform_int_distribution<long> dist(lo, hi);
            return Value::makeNumber(static_cast<double>(dist(*rng)));
        };
    }

    // ---- DLC:network ----
    // Real network access is out of scope for this interpreter (no sandboxing
    // story for it yet). The module still loads successfully — `use
    // DLC:network` never fails by itself — but calling any of its functions
    // fails clearly, rather than pretending to succeed.
    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto stub = [](const std::string &name)
        {
            return [name](std::vector<Value> &, const SourceLocation &loc) -> Value
            {
                throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                                   "DLC:network's " + name + "() is not available in this interpreter (no network sandboxing implemented)",
                                   loc, "network access must be provided by the host embedding this engine");
            };
        };
        reg["fetch"] = stub("fetch");
        reg["get"] = stub("get");
        reg["post"] = stub("post");
    }

    // Dispatches `use DLC:<name>` to the right registration function.
    inline void registerDLC(const std::string &libName, std::unordered_map<std::string, NativeFn> &reg, const SourceLocation &loc)
    {
        if (libName == "math")
            registerMathDLC(reg);
        else if (libName == "string")
            registerStringDLC(reg);
        else if (libName == "time")
            registerTimeDLC(reg);
        else if (libName == "random")
            registerRandomDLC(reg);
        else if (libName == "network")
            registerNetworkDLC(reg);
        else
            throw ModuleError(ErrorCode::UnknownDLC, "unknown DLC library 'DLC:" + libName + "'", loc,
                               "available libraries: DLC:math, DLC:string, DLC:time, DLC:random, DLC:network");
    }

} // namespace cuff
