// Browser/WASM entry point for the CuffScript engine.
// Compiled by `make wasm` (see Makefile). Not part of the native `cuffc` build.
//
// print()/input() inside the interpreter talk to std::cout/std::cin as usual
// (see engine/interpreter/NativeFunctions.h); the Emscripten runtime forwards
// those through the `print` / `stdin` callbacks the host page passes in when
// instantiating the module, so no engine code changes were needed here.
#include <emscripten/bind.h>
#include "../engine/CuffEngine.h"

using namespace emscripten;

struct RunOutcome
{
    bool success;
    std::string error;
};

static RunOutcome toOutcome(const cuff::CuffEngine::Result &result)
{
    return RunOutcome{result.success, result.error};
}

// Parses and executes `source`. `scriptDir` resolves relative
// `use <name> from <path>` imports against the virtual filesystem
// (see FS.mkdirTree/writeFile on the JS side).
RunOutcome cuffRun(const std::string &source, const std::string &scriptDir)
{
    return toOutcome(cuff::CuffEngine::execute(source, scriptDir));
}

// Parses `source` only and writes the token/AST dump to stdout, mirroring
// `cuffc --ast`. Used by the IDE's "AST 보기" panel.
RunOutcome cuffDump(const std::string &source)
{
    cuff::CuffEngine::Result result = cuff::CuffEngine::run(source);
    cuff::CuffEngine::debugDump(result);
    return toOutcome(result);
}

EMSCRIPTEN_BINDINGS(cuffscript_wasm)
{
    value_object<RunOutcome>("RunOutcome")
        .field("success", &RunOutcome::success)
        .field("error", &RunOutcome::error);

    function("cuffRun", &cuffRun);
    function("cuffDump", &cuffDump);
}
