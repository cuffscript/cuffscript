// Browser/WASM entry point for the CuffScript engine. A program run here has no arguments: args() is an empty list.
// Compiled by `make wasm` (see Makefile). Not part of the native `cuffc` build.
//
// print()/input() inside the interpreter talk to std::cout/std::cin as usual
// (see engine/interpreter/NativeFunctions.h); the Emscripten runtime forwards
// those through the `print` / `stdin` callbacks the host page passes in when
// instantiating the module, so no engine code changes were needed here.
#include <emscripten/bind.h>
#include "../engine/CuffEngine.h"
#include "../engine/debug/BytecodePrinter.h"
#include <iostream>

using namespace emscripten;

struct RunOutcome
{
    bool success;
    std::string error;
    bool exited;    // the program called exit()
    int exitStatus; // with this status (0 when it did not)
};

static RunOutcome toOutcome(const cuff::Engine::Outcome &result)
{
    return RunOutcome{result.success, result.error, result.exited, result.exitStatus};
}

// Parses and executes `source`. `scriptDir` resolves relative
// `use <name> from <path>` imports against the virtual filesystem
// (see FS.mkdirTree/writeFile on the JS side).
RunOutcome cuffRun(const std::string &source, const std::string &scriptDir)
{
    return toOutcome(cuff::Engine::execute(source, scriptDir));
}

// Parses `source` only and writes the token/AST dump to stdout, mirroring
// `cuffc --ast`. Used by the IDE's AST view panel.
RunOutcome cuffDump(const std::string &source)
{
    const cuff::Engine::Parsed parsed = cuff::Engine::parse(source);
    cuff::Engine::dump(parsed);
    return RunOutcome{parsed.success, parsed.error, false, 0};
}

// Runs `source` and writes each piece of bytecode to stdout as the engine compiles it, in order with what the
// program prints, mirroring `cuffc --bytecode`.
RunOutcome cuffBytecode(const std::string &source, const std::string &scriptDir)
{
    cuff::Engine::Options options;
    options.onCompiled = [](const cuff::bc::Proto &proto) { std::cout << cuff::BytecodePrinter::print(proto); };
    return toOutcome(cuff::Engine::execute(source, scriptDir, options));
}

EMSCRIPTEN_BINDINGS(cuffscript_wasm)
{
    value_object<RunOutcome>("RunOutcome")
        .field("success", &RunOutcome::success)
        .field("error", &RunOutcome::error)
        .field("exited", &RunOutcome::exited)
        .field("exitStatus", &RunOutcome::exitStatus);

    function("cuffRun", &cuffRun);
    function("cuffDump", &cuffDump);
    function("cuffBytecode", &cuffBytecode);
}
