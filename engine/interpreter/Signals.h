#pragma once

#include "Value.h"
#include "../common/SourceLocation.h"

namespace cuff
{

    enum class ExecResult
    {
        Normal,
        Return,
        Stop
    };

    struct ExecOutcome
    {
        ExecResult result = ExecResult::Normal;
        Value returnValue;  // meaningful only when result == Return
        SourceLocation loc;

        static ExecOutcome normal() { return ExecOutcome{}; }

        static ExecOutcome makeReturn(Value v, SourceLocation l)
        {
            ExecOutcome o;
            o.result = ExecResult::Return;
            o.returnValue = std::move(v);
            o.loc = l;
            return o;
        }

        static ExecOutcome makeStop(SourceLocation l)
        {
            ExecOutcome o;
            o.result = ExecResult::Stop;
            o.loc = l;
            return o;
        }
    };

}
