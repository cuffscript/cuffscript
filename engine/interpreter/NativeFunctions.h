#pragma once

// The DLC (library) implementations themselves live under engine/dlc/ —
// one file per library, plus DLCCommon.h for the argument-checking helpers
// and UTF-8 text utilities several of them share. This file is now just:
// the always-available core builtins (print/input/type_of, no 'use' needed),
// and registerDLC(), the single dispatcher `use DLC:name` calls into.
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

    // ---- Always-available builtins ----

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

        // to_number/to_str/to_boolean are common enough to be core builtins
        // rather than requiring `use DLC:convert` first. `use DLC:convert`
        // still works — it just re-registers the same functions.
        registerConvertDLC(reg);
    }

    inline void registerDLC(const std::string &libName, std::unordered_map<std::string, NativeFn> &reg, const SourceLocation &loc,
                            NetworkDLCOptions networkOpts = NetworkDLCOptions(),
                            FilesystemDLCOptions filesystemOpts = FilesystemDLCOptions())
    {
        if (libName == "math")
            registerMathDLC(reg);
        else if (libName == "string")
            registerStringDLC(reg);
        else if (libName == "time")
            registerTimeDLC(reg);
        else if (libName == "random")
            registerRandomDLC(reg);
        else if (libName == "list")
            registerListDLC(reg);
        else if (libName == "map")
            registerMapDLC(reg);
        else if (libName == "convert")
            registerConvertDLC(reg);
        else if (libName == "json")
            registerJsonDLC(reg);
        else if (libName == "network")
            registerNetworkDLC(reg, networkOpts);
        else if (libName == "filesystem")
            registerFilesystemDLC(reg, filesystemOpts);
        else
            throw ModuleError(ErrorCode::UnknownDLC, "unknown DLC library 'DLC:" + libName + "'", loc,
                               "available libraries: DLC:math, DLC:string, DLC:time, DLC:random, DLC:list, DLC:map, DLC:convert, DLC:json, DLC:network, DLC:filesystem");
    }

} // namespace cuff
