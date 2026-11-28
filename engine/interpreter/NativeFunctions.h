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
#include "../dlc/CsvDLC.h"
#include "../dlc/HashDLC.h"
#include "../dlc/PathDLC.h"
#include "../dlc/HardwareDLC.h"
#include "../dlc/MethodTable.h"
#include <cstring>
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

    // What print writes: the values with a space between them, then a line feed. The line is put together and written
    // at once: each write to the stream costs more than the text it writes. A long str is written as it is, not copied.
    // Showing an object can run its to_str and fail; what came before it is written first, as it would have been.
    CUFF_NOINLINE inline void writeLine(std::ostream &out, std::vector<Value> &args)
    {
        std::string line;
        try
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i)
                    line += ' ';
                if (args[i].isStr() && args[i].asStr().size() > 4096)
                {
                    out.write(line.data(), static_cast<std::streamsize>(line.size()));
                    line.clear();
                    out << args[i].asStr();
                }
                else
                    args[i].appendDisplay(line);
            }
        }
        catch (...)
        {
            out.write(line.data(), static_cast<std::streamsize>(line.size()));
            throw;
        }
        line += '\n';
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
    }

    inline void registerBuiltins(std::unordered_map<std::string, NativeFn> &reg)
    {
        // What every program asks of a str, a list or a map, whichever libraries it loads.
        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;
        reg["index_of"] = nativeIndexOf;
        // Only the interpreter can see a task, and it puts the working task_done in place of this one, which keeps
        // the name among the built-ins for whatever lists them.
        reg["task_done"] = [](std::vector<Value> &, const SourceLocation &loc) -> Value
        {
            throw CuffRuntimeError(ErrorCode::UnsupportedOperation, "task_done() needs the interpreter", loc);
        };
        // The same for args, which gives what the host put in the interpreter's Config.
        reg["args"] = [](std::vector<Value> &, const SourceLocation &loc) -> Value
        {
            throw CuffRuntimeError(ErrorCode::UnsupportedOperation, "args() needs the interpreter", loc);
        };

        reg["print"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            writeLine(std::cout, args);
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

        // print, to standard error: for what is not the program's output (progress, warnings, a usage line). What
        // the program printed before comes first when both go to one place.
        reg["print_error"] = [](std::vector<Value> &args, const SourceLocation &) -> Value
        {
            std::cout.flush();
            writeLine(std::cerr, args);
            std::cerr.flush();
            return Value::makeEmpty();
        };

        reg["assert"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("assert", args, 1, 2, loc);
            const std::string *message = args.size() == 2 ? &expectStr("assert", args, 1, loc) : nullptr;
            if (args[0].truthy())
                return Value::makeEmpty();
            throw CuffRuntimeError(ErrorCode::AssertionFailed, message ? *message : std::string("assertion failed"), loc);
        };

        // Ends the program at once with this status (0 when none is given): no `or_else` stops it, and tasks that are
        // still running do not finish. The host decides what the status means; cuffc ends with it.
        reg["exit"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("exit", args, 0, 1, loc);
            int status = 0;
            if (!args.empty())
            {
                const double d = expectNumber("exit", args, 0, loc);
                if (!(d >= 0 && d <= 255) || d != std::floor(d))
                    throw ValueError("exit() expects a whole number from 0 to 255, got " + formatCuffNumber(d), loc);
                status = static_cast<int>(d);
            }
            std::cout.flush();
            throw ProgramExit{status};
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
        HardwareDLCOptions hardware;
    };

    struct DLCEntry
    {
        const char *name;
        void (*registerFn)(NativeTable &, const DLCOptions &);
    };

    // The one list of `use DLC:<name>` libraries: to add one, include its header above, add a row here, and add its name
    // to the DLC_NAMES line of engine/dlc/libraries.txt, which every build file reads (`tools/docs.py check` fails until
    // the two agree). The
    // "unknown library" hint below is built from this table, in this order.
    //
    // A build can leave libraries out with -DCUFF_EXCLUDE_DLC_<NAME> (`make EXCLUDE_DLC="network hardware"`). A library
    // left out has no row, so it is unknown as a name that never existed is: `use DLC:<name>` is E5-004 with the
    // libraries that are there in its hint, its methods are not methods, and the command-line flags that only it
    // reads are not flags. Its code is not referred to, so it is not in the binary. What a host can set for it in
    // Engine::Options stays, and is not read.
#if defined(CUFF_EXCLUDE_DLC_CONVERT)
#error "DLC:convert cannot be left out: its functions (to_str, to_number, ...) are also built-ins of every program"
#endif
    inline const std::vector<DLCEntry> &dlcTable()
    {
        static const std::vector<DLCEntry> table = {
#ifndef CUFF_EXCLUDE_DLC_MATH
            {"math", [](NativeTable &reg, const DLCOptions &) { registerMathDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_STRING
            {"string", [](NativeTable &reg, const DLCOptions &) { registerStringDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_TIME
            {"time", [](NativeTable &reg, const DLCOptions &) { registerTimeDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_DATETIME
            {"datetime", [](NativeTable &reg, const DLCOptions &) { registerDatetimeDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_RANDOM
            {"random", [](NativeTable &reg, const DLCOptions &) { registerRandomDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_LIST
            {"list", [](NativeTable &reg, const DLCOptions &) { registerListDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_MAP
            {"map", [](NativeTable &reg, const DLCOptions &) { registerMapDLC(reg); }},
#endif
            {"convert", [](NativeTable &reg, const DLCOptions &) { registerConvertDLC(reg); }},
#ifndef CUFF_EXCLUDE_DLC_JSON
            {"json", [](NativeTable &reg, const DLCOptions &) { registerJsonDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_NETWORK
            {"network", [](NativeTable &reg, const DLCOptions &opts) { registerNetworkDLC(reg, opts.network); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_FILESYSTEM
            {"filesystem", [](NativeTable &reg, const DLCOptions &opts) { registerFilesystemDLC(reg, opts.filesystem); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_CSV
            {"csv", [](NativeTable &reg, const DLCOptions &) { registerCsvDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_HASH
            {"hash", [](NativeTable &reg, const DLCOptions &) { registerHashDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_PATH
            {"path", [](NativeTable &reg, const DLCOptions &) { registerPathDLC(reg); }},
#endif
#ifndef CUFF_EXCLUDE_DLC_HARDWARE
            {"hardware", [](NativeTable &reg, const DLCOptions &opts) { registerHardwareDLC(reg, opts.hardware); }},
#endif
        };
        return table;
    }

    // Whether `use DLC:<name>` can load `name` in this build.
    inline bool dlcInBuild(const char *name)
    {
        for (const DLCEntry &entry : dlcTable())
            if (std::strcmp(entry.name, name) == 0)
                return true;
        return false;
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
