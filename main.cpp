#include "engine/CuffEngine.h"
#include "engine/EngineOptionsCli.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
    void printUsage(std::ostream &out, const char *prog)
    {
        out << "Usage: " << prog << " [options] [script.cuff]\n"
            << "  (no file, or -)        read the script from stdin\n"
            << "  --ast                  print tokens + AST instead of running the script\n"
            << "  -h, --help             show this help and exit\n"
            << cuff::engineOptionsHelp()
            << "By default modules may be loaded from the script's own directory only.\n";
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
}

int main(int argc, char *argv[])
{
    std::ios::sync_with_stdio(false);

    bool debugMode = false;
    bool onlyPaths = false; // after "--" every argument is a file name, even one that starts with '-'
    std::string path;
    bool havePath = false;
    cuff::CuffEngine::Options options;

    const std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &arg = args[i];

        if (!onlyPaths && arg.size() > 1 && arg[0] == '-')
        {
            if (arg == "--")
            {
                onlyPaths = true;
                continue;
            }
            if (arg == "--ast" || arg == "--tokens" || arg == "--debug")
            {
                debugMode = true;
                continue;
            }
            if (arg == "-h" || arg == "--help")
            {
                printUsage(std::cout, argv[0]);
                return 0;
            }
            std::string error;
            switch (cuff::parseEngineOption(args, i, options, error))
            {
            case cuff::OptionStatus::Consumed:
                continue;
            case cuff::OptionStatus::Invalid:
                std::cerr << "Error: " << error << "\n";
                return 2;
            case cuff::OptionStatus::NotEngineOption:
                std::cerr << "Error: unknown option '" << arg << "' (see --help)\n";
                return 2;
            }
        }

        if (havePath)
        {
            std::cerr << "Error: only one script file may be given\n";
            return 2;
        }
        havePath = true;
        if (arg != "-")
            path = arg;
    }

    std::string source;
    std::string scriptDir = ".";

    if (!path.empty())
    {
        // Binary: the engine understands CRLF itself, and a text-mode read on Windows would also stop at a
        // stray Ctrl+Z byte.
        std::ifstream file(path, std::ios::binary);
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
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        source = readLimited(std::cin);
    }

    if (debugMode)
    {
        cuff::CuffEngine::Result result = cuff::CuffEngine::run(source);
        cuff::CuffEngine::debugDump(result);
        return result.success ? 0 : 1;
    }

    cuff::CuffEngine::Result result = cuff::CuffEngine::execute(std::move(source), scriptDir, options);
    if (!result.success)
    {
        std::cerr << "ERROR: " << result.error << "\n";
        return 1;
    }
    return 0;
}
