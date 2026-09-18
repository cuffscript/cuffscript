#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace cuff
{

    // Variable names are interned to dense integer IDs at parse time, so the
    // interpreter's scope lookups compare ints instead of strings. Profiling a
    // recursion-heavy benchmark showed name comparison dominating variable
    // resolution; an int compare is a single instruction and lets the linear
    // scan over a small scope stay in cache.
    //
    // IDs are process-global and never recycled. The table is small (one entry
    // per distinct identifier in the program) and lives for the whole run, so
    // there is nothing to invalidate.
    class NameInterner
    {
    public:
        static NameInterner &instance()
        {
            static NameInterner inst;
            return inst;
        }

        uint32_t intern(const std::string &name)
        {
            auto it = ids_.find(name);
            if (it != ids_.end())
                return it->second;
            uint32_t id = static_cast<uint32_t>(names_.size());
            names_.push_back(name);
            ids_.emplace(name, id);
            return id;
        }

        const std::string &nameOf(uint32_t id) const { return names_[id]; }

    private:
        std::unordered_map<std::string, uint32_t> ids_;
        std::vector<std::string> names_;
    };

    inline uint32_t internName(const std::string &name)
    {
        return NameInterner::instance().intern(name);
    }

} // namespace cuff
