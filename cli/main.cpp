// cuffsh — interactive shell for CuffScript, built directly on the same
// engine main.cpp/cuffc uses for one-shot script execution. Nothing here
// re-implements language semantics: parsing/execution always go through
// cuff::CuffEngine / cuff::Interpreter (see engine/CuffEngine.h). This file is
// the front end: prompt/keys live in LineEditor.h, terminal setup in
// Terminal.h, platform differences in Platform.h, the session in Repl.h.

#include "../engine/CuffEngine.h"
#include "Platform.h"
#include "Repl.h"
#include "Terminal.h"
#include "Version.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    // The unmodified engine's run() can't be cancelled from outside once it
    // has started, so an interactive shell needs its own bound on how long
    // one entered chunk may run, or a single infinite loop would hang the
    // session with no way out short of closing the window. This is that
    // bound. The engine only checks it every ~1000 loop iterations/calls, and
    // time spent waiting inside input() counts, so a program that waits on the
    // keyboard for longer than this and then does a burst of work can hit it:
    // use --timeout 0 for such programs.
    constexpr uint64_t kDefaultTimeoutMs = 15000;

    void printUsage(const char *prog)
    {
        std::cout << "CuffScript Interactive Shell " << cuff::cli::kVersion << "\n"
                  << "Enter runs what you've typed; Shift+Enter (or Alt+Enter) inserts a newline\n"
                  << "(for if/loop/func and other multi-line code) instead of running it yet.\n\n"
                  << "Usage: " << prog << " [options] [file.cuff]\n"
                  << "  file.cuff              run this script in the session before the prompt\n"
                  << "                         appears\n"
                  << "  --root <dir>           allow 'use ... from' to load modules anywhere\n"
                  << "                         under <dir> (default: current directory)\n"
                  << "  --max-steps <n>        stop each entry after <n> loop iterations +\n"
                  << "                         function calls (0 = unlimited)\n"
                  << "  --timeout <ms>         stop each entry after <ms> milliseconds\n"
                  << "                         (default " << kDefaultTimeoutMs << "; 0 = unlimited). The engine cannot cancel a\n"
                  << "                         running entry, so this is the main safety net for a\n"
                  << "                         runaway loop; with 0, Ctrl+C during execution closes\n"
                  << "                         the whole shell instead (see :help)\n"
                  << "  --no-network           disable 'use DLC:network' entirely\n"
                  << "  --allow-private-network\n"
                  << "                         let DLC:network reach loopback/private addresses\n"
                  << "  --no-filesystem        disable 'use DLC:filesystem' entirely\n"
                  << "  --no-color             disable colored output\n"
                  << "  --no-banner            skip the startup banner\n"
                  << "  -h, --help             show this help and exit\n"
                  << "  --version              show version information and exit\n";
    }

    bool parseCount(const std::string &text, uint64_t &out)
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

} // namespace

int main(int argc, char **argv)
{
    const std::vector<std::string> args = cuff::cli::platform::commandLineArgs(argc, argv);
    const char *prog = argc > 0 ? argv[0] : "cuffsh";

    cuff::cli::Repl::Options options;
    options.engineConfig.timeoutMs = static_cast<uint32_t>(kDefaultTimeoutMs);

    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string &arg = args[i];

        // Options that take a value: consume it, or complain once.
        auto takeValue = [&](std::string &value) -> bool
        {
            if (i + 1 >= args.size())
            {
                std::cerr << "Error: " << arg << " requires a value\n";
                return false;
            }
            value = args[++i];
            return true;
        };

        if (arg == "-h" || arg == "--help")
        {
            printUsage(prog);
            return 0;
        }
        if (arg == "--version")
        {
            std::cout << "cuffsh " << cuff::cli::kVersion << "\n";
            return 0;
        }
        if (arg == "--no-color")
        {
            options.useColor = false;
        }
        else if (arg == "--no-banner")
        {
            options.showBanner = false;
        }
        else if (arg == "--no-network")
        {
            options.engineConfig.networkEnabled = false;
        }
        else if (arg == "--allow-private-network")
        {
            options.engineConfig.allowPrivateNetworkTargets = true;
        }
        else if (arg == "--no-filesystem")
        {
            options.engineConfig.filesystemEnabled = false;
        }
        else if (arg == "--root")
        {
            std::string v;
            if (!takeValue(v))
                return 2;
            options.engineConfig.rootDir = cuff::cli::platform::enginePath(v);
        }
        else if (arg == "--max-steps" || arg == "--timeout")
        {
            std::string v;
            if (!takeValue(v))
                return 2;
            uint64_t n = 0;
            if (!parseCount(v, n))
            {
                std::cerr << "Error: " << arg << " expects a non-negative integer\n";
                return 2;
            }
            if (arg == "--max-steps")
                options.engineConfig.maxSteps = n;
            else
                options.engineConfig.timeoutMs = static_cast<uint32_t>(std::min<uint64_t>(n, UINT32_MAX));
        }
        else if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << "Error: unknown option '" << arg << "' (see --help)\n";
            return 2;
        }
        else
        {
            if (!options.startupFile.empty())
            {
                std::cerr << "Error: only one script file may be given\n";
                return 2;
            }
            options.startupFile = arg;
        }
    }

    if (cuff::cli::platform::isMsysPty(0))
    {
        std::cerr << "Error: this looks like a Git Bash / MSYS terminal, which gives Windows programs no console to read\n"
                  << "keys from. Run cuffsh from cmd, PowerShell or Windows Terminal, or start it as: winpty cuffsh.exe\n"
                  << "(Piping a script in, e.g. `echo ... | cuffsh.exe`, works everywhere.)\n";
        return 2;
    }

    cuff::cli::Terminal term;
    cuff::cli::Repl repl(term, options);
    repl.run();
    return 0;
}
