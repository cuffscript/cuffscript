#include "engine/CuffEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char *argv[])
{
    std::string source;

    if (argc > 1)
    {
        // Read from file
        std::ifstream file(argv[1]);
        if (!file)
        {
            std::cerr << "Error: Cannot open file '" << argv[1] << "'\n";
            return 1;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        source = ss.str();
    }
    else
    {
        // Read from stdin
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        source = ss.str();
    }

    cuff::CuffEngine::Result result = cuff::CuffEngine::run(source);
    cuff::CuffEngine::debugDump(result);

    return result.success ? 0 : 1;
}
