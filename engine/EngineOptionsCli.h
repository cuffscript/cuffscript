#pragma once

#include "Session.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace cuff
{

    enum class OptionStatus
    {
        NotEngineOption, // not one of ours: the front end handles it (or rejects it)
        Consumed,        // recognised and applied; `i` now points at its last argument
        Invalid          // recognised but malformed; `error` says why
    };

    // Reads a non-negative decimal integer, rejecting signs, spaces, trailing text and overflow.
    inline bool parseCount(const std::string &text, uint64_t &out)
    {
        if (text.empty() || text[0] < '0' || text[0] > '9')
            return false;
        char *end = nullptr;
        errno = 0;
        const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
        if (errno != 0 || *end != '\0')
            return false;
        out = v;
        return true;
    }

    // The command-line flags that configure the engine (EngineOptions). `cuffc` and `cuffsh` both call this
    // first, so the two accept the same flags with the same meaning and a new engine option is wired up once.
    // `--root` is stored as given; a front end that needs a different path encoding converts it afterwards.
    inline OptionStatus parseEngineOption(const std::vector<std::string> &args, size_t &i, EngineOptions &options,
                                          std::string &error)
    {
        const std::string &arg = args[i];
        auto needValue = [&](std::string &value) -> bool
        {
            if (i + 1 >= args.size())
            {
                error = arg + " requires a value";
                return false;
            }
            value = args[++i];
            return true;
        };

        if (arg == "--no-network")
            options.networkEnabled = false;
        else if (arg == "--allow-private-network")
            options.allowPrivateNetworkTargets = true;
        else if (arg == "--no-filesystem")
            options.filesystemEnabled = false;
        else if (arg == "--root")
        {
            std::string v;
            if (!needValue(v))
                return OptionStatus::Invalid;
            options.rootDir = v;
        }
        else if (arg == "--max-steps" || arg == "--timeout")
        {
            std::string v;
            if (!needValue(v))
                return OptionStatus::Invalid;
            uint64_t n = 0;
            if (!parseCount(v, n))
            {
                error = arg + " expects a non-negative integer";
                return OptionStatus::Invalid;
            }
            if (arg == "--max-steps")
                options.maxSteps = n;
            else
                options.timeoutMs = static_cast<uint32_t>(std::min<uint64_t>(n, UINT32_MAX));
        }
        else
            return OptionStatus::NotEngineOption;
        return OptionStatus::Consumed;
    }

    // Usage lines for the flags parseEngineOption() accepts, so every front end documents them identically.
    inline const char *engineOptionsHelp()
    {
        return "  --root <dir>           allow 'use ... from' to load modules anywhere under <dir>\n"
               "  --max-steps <n>        stop after <n> loop iterations + function calls (0 = unlimited)\n"
               "  --timeout <ms>         stop after <ms> milliseconds of run time (0 = unlimited)\n"
               "  --no-network           switch DLC:network off (calls into it raise E5-005)\n"
               "  --allow-private-network\n"
               "                         let DLC:network reach loopback/private/link-local addresses\n"
               "  --no-filesystem        switch DLC:filesystem off (calls into it raise E5-005)\n";
    }

}
