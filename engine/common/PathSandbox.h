#pragma once

#include <filesystem>

namespace cuff
{
    // True if `path` (expected to already be canonicalized/normalized, same
    // as `root`) lies inside `root` or equals it. Component-wise comparison
    // rather than a string prefix check, so "/root-extra" is correctly
    // rejected as outside "/root". Shared by module loading (`use ... from
    // ...`, see Interpreter::loadCustomModule) and DLC:filesystem, which are
    // the engine's only two features that ever touch the real filesystem —
    // both confine themselves to one sandbox root (Interpreter::moduleRoot_,
    // derived from CuffEngine::Options::rootDir) so a script can never read
    // or write outside the directory the host chose to expose.
    inline bool isInsideRoot(const std::filesystem::path &path, const std::filesystem::path &root)
    {
        auto r = root.begin();
        auto p = path.begin();
        for (; r != root.end(); ++r, ++p)
        {
            if (r->empty())
                break;
            if (p == path.end() || *r != *p)
                return false;
        }
        return true;
    }
}
