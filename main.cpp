#include "engine/CuffEngine.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cerrno>

namespace
{
    void printUsage(const char *prog)
    {
        std::cerr << "Usage: " << prog << " [options] [script.cuff]\n"
                  << "  (no file)          read the script from stdin\n"
                  << "  --ast              print tokens + AST instead of running the script\n"
                  << "  --root <dir>       allow 'use ... from' to load modules anywhere under <dir>\n"
                  << "                     (default: the script's own directory)\n"
                  << "  --max-steps <n>    stop after <n> loop iterations + function calls\n"
                  << "  --timeout <ms>     stop after <ms> milliseconds of run time\n"
                  << "  --no-network        disable 'use DLC:network' entirely\n"
                  << "  --allow-private-network\n"
                  << "                     let DLC:network reach loopback/private/link-local addresses\n";
    }

    // Reads at most limit+1 bytes so an oversized input is detected (and
    // rejected by the engine) without ever being held in memory in full.
    std::string readLimited(std::istream &in)
    {
        const size_t cap = cuff::limits::kMaxSourceBytes + 1;
        std::string data;
        char buf[1 << 16];
        while (data.size() < cap && in.good())
        {
            in.read(buf, static_cast<std::streamsize>(std::min(sizeof buf, cap - data.size())));
            data.append(buf, static_cast<size_t>(in.gcount()));
        }
        return data;
    }

    bool parseCount(const char *text, uint64_t &out)
    {
        if (!text || !*text)
            return false;
        char *end = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(text, &end, 10);
        if (errno != 0 || *end != '\0' || text[0] == '-')
            return false;
        out = v;
        return true;
    }
}

int main(int argc, char *argv[])
{
    std::ios::sync_with_stdio(false);

    bool debugMode = false;
    std::string path;
    cuff::CuffEngine::Options options;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto needValue = [&](const char *flag) -> const char *
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: " << flag << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--ast" || arg == "--tokens" || arg == "--debug")
        {
            debugMode = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--root")
        {
            const char *v = needValue("--root");
            if (!v)
                return 2;
            options.rootDir = v;
        }
        else if (arg == "--no-network")
        {
            options.networkEnabled = false;
        }
        else if (arg == "--allow-private-network")
        {
            options.allowPrivateNetworkTargets = true;
        }
        else if (arg == "--max-steps" || arg == "--timeout")
        {
            const char *v = needValue(arg.c_str());
            uint64_t n = 0;
            if (!v || !parseCount(v, n))
            {
                std::cerr << "Error: " << arg << " expects a non-negative integer\n";
                return 2;
            }
            if (arg == "--max-steps")
                options.maxSteps = n;
            else
                options.timeoutMs = static_cast<uint32_t>(std::min<uint64_t>(n, UINT32_MAX));
        }
        else
        {
            path = arg;
        }
    }

    std::string source;
    std::string scriptDir = ".";

    if (!path.empty())
    {
        std::ifstream file(path);
        if (!file)
        {
            std::cerr << "Error: Cannot open file '" << path << "'\n";
            return 1;
        }
        source = readLimited(file);

        std::filesystem::path p(path);
        scriptDir = p.has_parent_path() ? p.parent_path().string() : ".";
    }
    else
    {
        source = readLimited(std::cin);
    }

    if (debugMode)
    {
        cuff::CuffEngine::Result result = cuff::CuffEngine::run(source);
        cuff::CuffEngine::debugDump(result);
        return result.success ? 0 : 1;
    }

    cuff::CuffEngine::Result result = cuff::CuffEngine::execute(source, scriptDir, options);
    if (!result.success)
    {
        std::cerr << "ERROR: " << result.error << "\n";
        return 1;
    }
    return 0;
}
