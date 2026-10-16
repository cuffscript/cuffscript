#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cuff
{

    // Turns an identifier into a small number, so scopes can be looked up by number instead of by string.
    //
    // The table is shared by the whole process: a name gets the same number in every program, module and
    // thread, which is what lets a function from one module find the variables of another by number. Because
    // threads share it, it is locked. An embedder may therefore run one Session per thread with no further
    // care (see tests/unit/threads_test.cpp); the lock is held only for the lookup.
    //
    // Names are never removed, so the table grows by the number of *distinct* identifiers the process has ever
    // seen. One program is bounded by the source size limit; a host that runs unbounded numbers of unrelated
    // untrusted programs in one long-lived process should count that among the things it limits.
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
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = ids_.find(name);
            if (it != ids_.end())
                return it->second;
            const uint32_t id = static_cast<uint32_t>(ids_.size());
            ids_.emplace(name, id);
            return id;
        }

    private:
        std::mutex mutex_;
        std::unordered_map<std::string, uint32_t> ids_;
    };

    inline uint32_t internName(const std::string &name)
    {
        return NameInterner::instance().intern(name);
    }

}
