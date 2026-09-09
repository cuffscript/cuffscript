#pragma once

#include "Value.h"
#include "../common/SourceLocation.h"

namespace cuff
{

    // =========================================================================
    // Non-exception control-flow signaling.
    //
    // `return` and `stop` used to be implemented by throwing a small struct
    // (ReturnSignal/StopSignal) and catching it at the function/loop
    // boundary. That was correct but slow: a thrown-and-caught C++ exception
    // costs on the order of microseconds, and every single `return`
    // statement threw one — for a recursion-heavy program that dominates
    // runtime completely (measured: removing this cut a fib(27) benchmark
    // from ~2.75s to a small fraction of that).
    //
    // Instead, execStatement/execBlock/execIf/execLoop all return an
    // ExecOutcome value. Normal execution costs one extra enum compare per
    // statement; `return`/`stop` cost nothing more than constructing and
    // returning a small struct, exactly like any other function return.
    //
    // This also fixes a real correctness bug the exception-based version
    // had: `stop` used to only be caught by the *nearest* try/catch for
    // StopSignal, which was installed only around loop bodies — so `stop`
    // used inside a function with no loop of its own would silently escape
    // through the function-call boundary and terminate whatever loop
    // happened to be active in the *caller*. callUserFunction now explicitly
    // treats a Stop outcome reaching it as an error (`stop` outside any
    // loop), and execProgram does the same for a Return/Stop outcome
    // reaching the top level, instead of crashing the whole process with an
    // uncaught exception.
    // =========================================================================
    enum class ExecResult
    {
        Normal, // fell through — no early exit
        Return, // `return` — propagates up to the nearest function-call boundary
        Stop    // `stop` — propagates up to the nearest enclosing loop
    };

    struct ExecOutcome
    {
        ExecResult result = ExecResult::Normal;
        Value returnValue;  // meaningful only when result == Return
        SourceLocation loc; // where the return/stop occurred (for stray-signal errors)

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

} // namespace cuff
