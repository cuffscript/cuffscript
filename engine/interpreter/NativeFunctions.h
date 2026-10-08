#pragma once

#include "Value.h"
#include "../dlc/DLCCommon.h"
#include "../dlc/MathDLC.h"
#include "../dlc/StringDLC.h"
#include "../dlc/TimeDLC.h"
#include "../dlc/RandomDLC.h"
#include "../dlc/ListDLC.h"
#include "../dlc/MapDLC.h"
#include "../dlc/ConvertDLC.h"
#include "../dlc/NetworkDLC.h"
#include "../dlc/FilesystemDLC.h"
#include "../dlc/JsonDLC.h"
#include <iostream>
#include <string>

namespace cuff
{

    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg);

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
            ensureStringSize(line.size(), loc);
            return Value::makeStr(std::move(line));
        };

        reg["type_of"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("type_of", args, 1, loc);
            return Value::makeStr(valueTypeName(args[0].type()));
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
