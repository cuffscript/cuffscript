#pragma once

#include <atomic>

namespace cuff::cli
{

    // Set for the duration of interp_->run() (see Repl.h's RunningGuard) so
    // the Ctrl+C handler in Terminal.h knows whether a computation is
    // actually in progress. Idle Ctrl+C is handled directly inside
    // LineEditor's own raw-mode key reading (it sees the raw 0x03 byte /
    // console key event itself), so no separate "interrupted" flag is
    // needed for that case.
    inline std::atomic<bool> g_programRunning{false};

} // namespace cuff::cli
