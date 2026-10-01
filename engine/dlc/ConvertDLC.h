#pragma once

#include "DLCCommon.h"

#include <cerrno>
#include <cstdlib>

namespace cuff
{

    // ---- DLC:convert ----
    inline void registerConvertDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["to_number"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_number", args, 1, loc);
            const Value &v = args[0];
            if (v.isNumber())
                return v;
            if (v.isBool())
                return Value::makeNumber(v.asBool() ? 1.0 : 0.0);
            if (v.isStr())
            {
                // Only plain decimal text is accepted: partial parses like
                // "12abc" and forms like "nan", "inf" or hex floats would hide
                // bugs, so the whole string (minus surrounding whitespace)
                // must be an ordinary finite number.
                double d;
                if (!textutil::parseDecimal(v.asStr(), d))
                    throw ValueError("to_number() could not parse \"" + v.asStr() + "\" as a number", loc);
                return Value::makeNumber(d);
            }
            throw TypeError("to_number() cannot convert a " + valueTypeName(v.type()) + " to a number", loc);
        };

        reg["to_str"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_str", args, 1, loc);
            if (args[0].isStr())
                return args[0];
            return Value::makeStr(args[0].toDisplayString());
        };

        reg["to_boolean"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("to_boolean", args, 1, loc);
            return Value::makeBool(args[0].truthy());
        };
    }

} // namespace cuff
