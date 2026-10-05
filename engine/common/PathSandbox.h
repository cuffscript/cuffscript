#pragma once

#include <filesystem>

namespace cuff
{
    // Component-wise containment check, not a string prefix test.
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
