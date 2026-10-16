#pragma once

// The Unicode width tables live in the engine (engine/common/Width.h) so the line editor and the engine's
// error messages agree on how wide a character is. This header only keeps the names the editor uses.

#include "../engine/common/Width.h"

namespace cuff::cli
{
    using cuff::width::CpRange;
    using cuff::width::cpWidth;
}
