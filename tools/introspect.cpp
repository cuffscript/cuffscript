// Prints what the engine knows about itself, one tab-separated record per line, for tools/docs.py:
//
//     fn       <library>  <name>       a function a `use DLC:<library>` makes available
//     builtin  <name>                  a function that is always available
//     method   <name> <function> <receivers> <library>
//                                      a built-in method, the function it runs, the kinds of value it can be called
//                                      on (joined with '|') and the library the function comes from (empty: always)
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
#include <set>
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

    std::string receiverNames(unsigned mask)
    {
        static const struct
        {
            unsigned bit;
            const char *name;
        } kinds[] = {{cuff::RecvStr, "str"},     {cuff::RecvList, "list"},   {cuff::RecvMap, "map"},         {cuff::RecvNumber, "number"},
                     {cuff::RecvBoolean, "boolean"}, {cuff::RecvEmpty, "empty"}, {cuff::RecvMatch, "match"}, {cuff::RecvPattern, "pattern"},
                     {cuff::RecvObject, "object"}};
        if (mask == cuff::RecvAny)
            return "any";
        std::string out;
        for (const auto &k : kinds)
            if (mask & k.bit)
                out += (out.empty() ? "" : "|") + std::string(k.name);
        return out;
    }

    void emitLimit(const char *name, unsigned long long value)
    {
        std::cout << "limit\t" << name << '\t' << value << '\n';
    }
}

int main()
{
    std::set<std::pair<std::string, std::string>> known; // (library, function)
    {
        cuff::NativeTable builtins;
        cuff::registerBuiltins(builtins);
        for (const std::string &name : namesOf(builtins))
        {
            std::cout << "builtin\t" << name << '\n';
            known.insert({"", name});
        }
    }
    for (const cuff::DLCEntry &entry : cuff::dlcTable())
    {
        cuff::NativeTable table;
        entry.registerFn(table, cuff::DLCOptions());
        for (const std::string &name : namesOf(table))
        {
            std::cout << "fn\t" << entry.name << '\t' << name << '\n';
            known.insert({entry.name, name});
        }
    }
    // Every method must run a function that exists in the library it names.
    for (const cuff::MethodEntry &m : cuff::methodTable())
    {
        if (!known.count({m.library, m.function}))
        {
            std::cerr << "method '" << m.method << "' runs " << m.function << ", which is not in "
                      << (*m.library ? std::string("DLC:") + m.library : std::string("the always-available functions")) << "\n";
            return 1;
        }
        std::cout << "method\t" << m.method << '\t' << m.function << '\t' << receiverNames(m.receivers) << '\t' << m.library << '\n';
    }
#include "limits_dump.inc"
    return 0;
}
