#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace cuff
{

    // Interns variable names into stable integer IDs.
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

}
