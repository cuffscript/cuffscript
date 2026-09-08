#pragma once

#include "Value.h"

namespace cuff
{

    // Thrown by `return` and caught at the function-call boundary.
    struct ReturnSignal
    {
        Value value;
    };

    // Thrown by `stop` and caught at the nearest enclosing loop.
    struct StopSignal
    {
    };

} // namespace cuff
