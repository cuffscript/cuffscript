#pragma once

#include "DLCCommon.h"

#include <chrono>

namespace cuff
{

    inline void registerTimeDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        auto nowFn = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("time_now", args, 0, loc);
            auto now = std::chrono::system_clock::now().time_since_epoch();
            double secs = std::chrono::duration<double>(now).count();
            return Value::makeNumber(secs);
        };
        reg["time_now"] = nowFn;
        reg["time_timestamp"] = nowFn;
    }

}
