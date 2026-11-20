#pragma once

#include "DLCCommon.h"

#include <chrono>

namespace cuff
{

    // time_sleep(): over when its moment has come.
    struct SleepWait final : Wait
    {
        Clock::time_point until;
        explicit SleepWait(Clock::time_point t) : until(t) {}
        bool advance() override { return Clock::now() >= until; }
        Value finish(const SourceLocation &) override { return Value(); }
        Clock::time_point wakeAt() const override { return until; }
    };

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

        // Waits without holding the other tasks up: the machine runs them until the moment has come.
        reg["time_sleep"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("time_sleep", args, 1, loc);
            const double seconds = expectNumber("time_sleep", args, 0, loc);
            if (!(seconds >= 0) || seconds > limits::kMaxSleepSeconds)
                throw ValueError("time_sleep() expects a number of seconds from 0 to " + std::to_string(limits::kMaxSleepSeconds) + ", got " +
                                     args[0].toDisplayString(),
                                 loc);
            const auto until = Wait::Clock::now() + std::chrono::duration_cast<Wait::Clock::duration>(std::chrono::duration<double>(seconds));
            if (WaitHook *machine = activeWaitHook())
            {
                machine->waitFor(std::make_unique<SleepWait>(until));
                return Value();
            }
            for (auto now = Wait::Clock::now(); now < until; now = Wait::Clock::now())
                sleepMilliseconds(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(until - now).count()) + 1);
            return Value();
        };
    }

}
