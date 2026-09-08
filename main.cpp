#include "engine/CuffEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

namespace
{
    void printUsage(const char *prog)
    {
        std::cerr << "Usage: " << prog << " [--ast|--tokens] [script.cuff]\n"
                  << "  (no file)   read the script from stdin\n"
                  << "  --ast       print tokens + AST instead of running the script\n";
    }
}

int main(int argc, char *argv[])
{
    bool debugMode = false;
    std::string path;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--ast" || arg == "--tokens" || arg == "--debug")
        {
            debugMode = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
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
        std::ostringstream ss;
        ss << file.rdbuf();
        source = ss.str();

        std::filesystem::path p(path);
        scriptDir = p.has_parent_path() ? p.parent_path().string() : ".";
    }
    else
    {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        source = ss.str();
    }

    if (debugMode)
    {
        cuff::CuffEngine::Result result = cuff::CuffEngine::run(source);
        cuff::CuffEngine::debugDump(result);
        return result.success ? 0 : 1;
    }

    cuff::CuffEngine::Result result = cuff::CuffEngine::execute(source, scriptDir);
    if (!result.success)
    {
        std::cerr << "ERROR: " << result.error << "\n";
        return 1;
    }
    return 0;
}
