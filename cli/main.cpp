// cuffsh — interactive shell for CuffScript, built directly on the same
// engine main.cpp/cuffc uses for one-shot script execution. Nothing here
// re-implements language semantics: parsing/execution always go through
// cuff::Session (see engine/Session.h). This file is
// the front end: prompt/keys live in LineEditor.h, terminal setup in
// Terminal.h, platform differences in Platform.h, the session in Repl.h.

#include "../engine/CuffEngine.h"
#include "../engine/EngineOptionsCli.h"
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
                  << cuff::engineOptionsHelp()
                  << "  --no-color             disable colored output\n"
                  << "  --no-banner            skip the startup banner\n"
                  << "  -h, --help             show this help and exit\n"
                  << "  --version              show version information and exit\n\n"
                  << "Defaults: modules may be loaded from the current directory, and each entry stops\n"
                  << "after " << kDefaultTimeoutMs << " ms. The engine cannot cancel a running entry, so --timeout is the\n"
                  << "main safety net for a runaway loop; with --timeout 0, Ctrl+C during execution\n"
                  << "closes the whole shell instead (see :help).\n";
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
            continue;
        }
        if (arg == "--no-banner")
        {
            options.showBanner = false;
            continue;
        }

        // The flags that configure the engine are shared with cuffc (engine/EngineOptionsCli.h).
        std::string optionError;
        const cuff::OptionStatus status = cuff::parseEngineOption(args, i, options.engineConfig, optionError);
        if (status == cuff::OptionStatus::Invalid)
        {
            std::cerr << "Error: " << optionError << "\n";
            return 2;
        }
        if (status == cuff::OptionStatus::Consumed)
            continue;

        if (arg.size() > 1 && arg[0] == '-')
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

    // The shared parser stores --root as typed; the engine wants the platform's own path encoding.
    if (!options.engineConfig.rootDir.empty())
        options.engineConfig.rootDir = cuff::cli::platform::enginePath(options.engineConfig.rootDir);

    if (cuff::cli::platform::isMsysPty(0))
    {
        std::cerr << "Error: this looks like a Git Bash / MSYS terminal, which gives Windows programs no console to read\n"
                  << "keys from. Run cuffsh from cmd, PowerShell or Windows Terminal, or start it as: winpty cuffsh.exe\n"
                  << "(Piping a script in, e.g. `echo ... | cuffsh.exe`, works everywhere.)\n";
        return 2;
    }

    cuff::cli::Terminal term;
    cuff::cli::Repl repl(term, options);
    return repl.run();
}
