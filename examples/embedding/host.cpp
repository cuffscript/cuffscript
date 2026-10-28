// A minimal C++ program that hosts the CuffScript engine.
// Build from this folder:  g++ -std=c++17 -I../.. -o host host.cpp
#include "engine/CuffEngine.h"
#include <iostream>
#include <sstream>

namespace
{
    // print() writes to std::cout, so a host captures a script's output by swapping the stream buffer.
    struct CaptureCout
    {
        std::ostringstream buffer;
        std::streambuf *previous = std::cout.rdbuf(buffer.rdbuf());
        ~CaptureCout() { std::cout.rdbuf(previous); }
    };
}

int main()
{
    // 1. A whole program, run once, with a budget and the outside world switched off.
    cuff::CuffEngine::Options options;
    options.timeoutMs = 2000;
    options.maxSteps = 1000000;
    options.networkEnabled = false;
    options.filesystemEnabled = false;

    std::string captured;
    {
        CaptureCout capture;
        auto result = cuff::CuffEngine::execute("print(6 * 7)\n", ".", options);
        captured = capture.buffer.str();
        if (!result.success)
            std::cerr << result.error << "\n";
    }
    std::cout << "captured: " << captured;

    // 2. A failing program: the report is ready to print and nothing is thrown.
    auto failed = cuff::CuffEngine::execute("print(1 / 0)\n", ".", options);
    std::cout << "success: " << std::boolalpha << failed.success << "\n" << failed.error << "\n";

    // 3. A program that arrives in pieces: later pieces see what earlier ones declared.
    cuff::Session session(options);
    session.run("set number n to 5\n");
    auto piece = session.run("print(n * 2)\n");
    std::cout << "piece ok: " << piece.success << "\n";
    session.reset();
    auto after = session.run("print(n)\n");
    std::cout << "after reset ok: " << after.success << "\n";

    // 4. Parse only, for tools that want the syntax tree.
    auto parsed = cuff::CuffEngine::run("set number x to 1\n", false);
    std::cout << "parsed ok: " << parsed.success << ", has tree: " << (parsed.ast != nullptr) << "\n";
    return 0;
}
