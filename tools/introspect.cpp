// Prints what the engine knows about itself, one tab-separated record per line, for tools/docs.py:
//
//     fn       <library>  <name>       a function a `use DLC:<library>` makes available
//     builtin  <name>                  a function that is always available
//     error    <tag> <Name> <category> <recoverable 0/1>
//     limit    <name> <value>          a constant from engine/common/Limits.h
//
// The list of limits and error codes is not written here: docs.py reads the names out of the engine headers and
// generates tools/limits_dump.inc (included below) into a scratch directory, so there is nothing to keep in sync.
// Build it through `python3 tools/docs.py`, not by hand.
#include "../engine/common/ErrorCodes.h"
#include "../engine/common/Limits.h"
#include "../engine/interpreter/NativeFunctions.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> namesOf(const cuff::NativeTable &table)
    {
        std::vector<std::string> names;
        for (const auto &entry : table)
            names.push_back(entry.first);
        std::sort(names.begin(), names.end());
        return names;
    }

    void emitError(cuff::ErrorCode code, const char *name)
    {
        std::cout << "error\t" << cuff::errorCodeTag(code) << '\t' << name << '\t' << cuff::errorCategoryName(code) << '\t'
                  << (cuff::errorCodeRecoverable(code) ? 1 : 0) << '\n';
    }

    void emitLimit(const char *name, unsigned long long value)
    {
        std::cout << "limit\t" << name << '\t' << value << '\n';
    }
}

int main()
{
    {
        cuff::NativeTable builtins;
        cuff::registerBuiltins(builtins);
        for (const std::string &name : namesOf(builtins))
            std::cout << "builtin\t" << name << '\n';
    }
    for (const cuff::DLCEntry &entry : cuff::dlcTable())
    {
        cuff::NativeTable table;
        entry.registerFn(table, cuff::DLCOptions());
        for (const std::string &name : namesOf(table))
            std::cout << "fn\t" << entry.name << '\t' << name << '\n';
    }
#include "limits_dump.inc"
    return 0;
}
