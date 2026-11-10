#pragma once

#include "Value.h"
#include "../dlc/DLCCommon.h"
#include "../dlc/MathDLC.h"
#include "../dlc/StringDLC.h"
#include "../dlc/TimeDLC.h"
#include "../dlc/DatetimeDLC.h"
#include "../dlc/RandomDLC.h"
#include "../dlc/ListDLC.h"
#include "../dlc/MapDLC.h"
#include "../dlc/ConvertDLC.h"
#include "../dlc/NetworkDLC.h"
#include "../dlc/FilesystemDLC.h"
#include "../dlc/JsonDLC.h"
#include "../dlc/MethodTable.h"
#include <iostream>
#include <string>

namespace cuff
{

    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg);

    namespace textbuiltins
    {

        inline const PatternData &expectPattern(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
        {
            if (!args[i].isPattern())
                throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a pattern, got " +
                                    valueTypeName(args[i].type()),
                                loc, "write the pattern as p\"...\", or build one from a str with pattern(text)");
            return *args[i].asPattern();
        }

        inline Value makeStrList(const std::vector<std::string> &parts)
        {
            auto list = std::make_shared<ValueList>();
            list->items.reserve(parts.size());
            for (const auto &p : parts)
                list->items.push_back(Value::makeStr(p));
            return Value::makeList(std::move(list));
        }

    }

    // The text functions: the text is always the first argument, the pattern the second. They need no `use`.
    inline void registerTextBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        using regex::RegexEngine;
        using namespace textbuiltins;

        reg["find"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("find", args, 2, loc);
            const std::string &text = expectStr("find", args, 0, loc);
            const PatternData &pat = expectPattern("find", args, 1, loc);
            regex::MatchOutcome out;
            if (!RegexEngine::search(pat.compiled, text, 0, pat.flags(), loc, out))
                return Value::makeEmpty();
            return Value::makeStr(text.substr(out.start, out.end - out.start));
        };

        reg["find_all"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("find_all", args, 2, loc);
            const std::string &text = expectStr("find_all", args, 0, loc);
            const PatternData &pat = expectPattern("find_all", args, 1, loc);
            auto spans = RegexEngine::searchAllSpans(pat.compiled, text, pat.flags(), loc);
            auto list = std::make_shared<ValueList>();
            list->items.reserve(spans.size());
            for (const auto &m : spans)
                list->items.push_back(Value::makeStr(text.substr(m.first, m.second - m.first)));
            return Value::makeList(std::move(list));
        };

        reg["match"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("match", args, 2, loc);
            const std::string &text = expectStr("match", args, 0, loc);
            const PatternData &pat = expectPattern("match", args, 1, loc);
            regex::MatchOutcome out;
            if (!RegexEngine::search(pat.compiled, text, 0, pat.flags(), loc, out))
                return Value::makeEmpty();
            auto mr = std::make_shared<MatchResult>();
            mr->matched = true;
            mr->wholeMatch = text.substr(out.start, out.end - out.start);
            mr->positional = out.positional;
            mr->named = out.named;
            return Value::makeMatch(std::move(mr));
        };

        reg["replace"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("replace", args, 3, loc);
            const std::string &text = expectStr("replace", args, 0, loc);
            const PatternData &pat = expectPattern("replace", args, 1, loc);
            const std::string &with = expectStr("replace", args, 2, loc);
            regex::Flags flags = pat.flags();
            flags.global = false;
            return Value::makeStr(RegexEngine::replace(pat.compiled, text, with, flags, loc));
        };

        reg["replace_all"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("replace_all", args, 3, loc);
            const std::string &text = expectStr("replace_all", args, 0, loc);
            const PatternData &pat = expectPattern("replace_all", args, 1, loc);
            const std::string &with = expectStr("replace_all", args, 2, loc);
            regex::Flags flags = pat.flags();
            flags.global = true;
            return Value::makeStr(RegexEngine::replace(pat.compiled, text, with, flags, loc));
        };

        reg["split"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("split", args, 2, loc);
            const std::string &text = expectStr("split", args, 0, loc);
            const PatternData &pat = expectPattern("split", args, 1, loc);
            return makeStrList(RegexEngine::split(pat.compiled, text, loc));
        };

        reg["count"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("count", args, 2, loc);
            const std::string &text = expectStr("count", args, 0, loc);
            const PatternData &pat = expectPattern("count", args, 1, loc);
            return Value::makeNumber(static_cast<double>(RegexEngine::count(pat.compiled, text, pat.flags(), loc)));
        };

        // pattern(text) builds a pattern while the program runs; `pattern(text, "i")` adds flags (i, m). Patterns that
        // are built over and over from the same text are compiled once per interpreter.
        auto cache = std::make_shared<RegexEngine>();
        reg["pattern"] = [cache](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("pattern", args, 1, 2, loc);
            const std::string &text = expectStr("pattern", args, 0, loc);
            bool ignoreCase = false, multiline = false;
            if (args.size() == 2)
            {
                for (char c : expectStr("pattern", args, 1, loc))
                {
                    if (c == 'i')
                        ignoreCase = true;
                    else if (c == 'm')
                        multiline = true;
                    else
                        throw ValueError("pattern() flags may only contain 'i' and 'm', got '" + std::string(1, c) + "'", loc,
                                         c == 'g' ? "'g' does not exist: find_all, replace_all and count work on every match" : "");
                }
            }
            auto data = std::make_shared<PatternData>();
            data->compiled = cache->compile(text, loc);
            data->source = text;
            data->caseInsensitive = ignoreCase;
            data->multiline = multiline;
            return Value::makePattern(std::move(data));
        };
    }

    inline void registerBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["print"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i)
                    std::cout << ' ';
                if (args[i].isStr())
                    std::cout << args[i].asStr();
                else
                    std::cout << args[i].toDisplayString();
            }
            std::cout << '\n';
            return Value::makeEmpty();
        };

        reg["input"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("input", args, 0, 1, loc);
            if (!args.empty())
                std::cout << expectStr("input", args, 0, loc);
            std::string line;
            if (!std::getline(std::cin, line))
                return Value::makeStr(std::string());
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            ensureStringSize(line.size(), loc);
            return Value::makeStr(std::move(line));
        };

        reg["type_of"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("type_of", args, 1, loc);
            if (args[0].isObject())
                return Value::makeStr(args[0].asObject()->cls->name);
            return Value::makeStr(valueTypeName(args[0].type()));
        };

        registerTextBuiltins(reg);

        // A shallow copy: the new list, map or object holds the same elements, fields or values as the old one.
        reg["copy"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("copy", args, 1, loc);
            const Value &v = args[0];
            if (v.isList())
            {
                auto out = std::make_shared<ValueList>();
                out->items = v.asList()->items;
                return Value::makeList(std::move(out));
            }
            if (v.isMap())
            {
                const ValueMap &src = *v.asMap();
                auto out = std::make_shared<ValueMap>();
                out->reserve(src.size());
                for (size_t i = 0; i < src.size(); ++i)
                    out->set(src.keys()[i], src.values()[i]);
                return Value::makeMap(std::move(out));
            }
            if (v.isObject())
            {
                const ObjectData &src = *v.asObject();
                auto out = std::make_shared<ObjectData>();
                out->cls = src.cls;
                out->fields = src.fields;
                out->origin = src.origin;
                return Value::makeObject(std::move(out));
            }
            throw TypeError("copy() expects a list, map or object, got " + valueTypeName(v.type()), loc);
        };

        // Conversion functions are available as core builtins.
        registerConvertDLC(reg);
    }

    using NativeTable = std::unordered_map<std::string, NativeFn>;

    // Host settings that individual libraries read when `use DLC:<name>` loads them.
    struct DLCOptions
    {
        NetworkDLCOptions network;
        FilesystemDLCOptions filesystem;
    };

    struct DLCEntry
    {
        const char *name;
        void (*registerFn)(NativeTable &, const DLCOptions &);
    };

    // The one list of `use DLC:<name>` libraries: to add one, include its header above and add a row here.
    // The "unknown library" hint below is built from this table, in this order.
    inline const std::vector<DLCEntry> &dlcTable()
    {
        static const std::vector<DLCEntry> table = {
            {"math", [](NativeTable &reg, const DLCOptions &) { registerMathDLC(reg); }},
            {"string", [](NativeTable &reg, const DLCOptions &) { registerStringDLC(reg); }},
            {"time", [](NativeTable &reg, const DLCOptions &) { registerTimeDLC(reg); }},
            {"datetime", [](NativeTable &reg, const DLCOptions &) { registerDatetimeDLC(reg); }},
            {"random", [](NativeTable &reg, const DLCOptions &) { registerRandomDLC(reg); }},
            {"list", [](NativeTable &reg, const DLCOptions &) { registerListDLC(reg); }},
            {"map", [](NativeTable &reg, const DLCOptions &) { registerMapDLC(reg); }},
            {"convert", [](NativeTable &reg, const DLCOptions &) { registerConvertDLC(reg); }},
            {"json", [](NativeTable &reg, const DLCOptions &) { registerJsonDLC(reg); }},
            {"network", [](NativeTable &reg, const DLCOptions &opts) { registerNetworkDLC(reg, opts.network); }},
            {"filesystem", [](NativeTable &reg, const DLCOptions &opts) { registerFilesystemDLC(reg, opts.filesystem); }},
        };
        return table;
    }

    inline void registerDLC(const std::string &libName, NativeTable &reg, const SourceLocation &loc,
                            const DLCOptions &opts = DLCOptions())
    {
        std::string available;
        for (const DLCEntry &entry : dlcTable())
        {
            if (libName == entry.name)
            {
                entry.registerFn(reg, opts);
                return;
            }
            available += (available.empty() ? "DLC:" : ", DLC:") + std::string(entry.name);
        }
        throw ModuleError(ErrorCode::UnknownDLC, "unknown DLC library 'DLC:" + libName + "'", loc,
                           "available libraries: " + available);
    }

}
