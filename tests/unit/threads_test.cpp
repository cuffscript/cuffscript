// Several threads, each with its own Session, parsing and running different programs at the same time.
//
// The engine's contract for an embedder is "one Session per thread, no sharing". That holds only if nothing the
// threads share is unsynchronised, and one thing is shared: the table that turns identifier names into numbers
// (common/NameInterner.h). Every thread here introduces names of its own, so an unsynchronised table corrupts, hangs
// or crashes; build this test with -fsanitize=thread to see the data race itself.
#include "engine/CuffEngine.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// A MinGW built with Win32 threads declares <thread> but not std::thread; there is nothing to test there.
#if defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)
int main()
{
    std::cout << "skipped: this toolchain has no std::thread\n0 passed, 0 failed\n";
    return 0;
}
#else
int main()
{
    constexpr int kThreads = 6;
    constexpr int kRounds = 150;
    std::atomic<int> failures{0};
    std::vector<std::string> firstError(kThreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t, &failures, &firstError]()
                             {
            cuff::EngineOptions options;
            options.networkEnabled = false;
            options.filesystemEnabled = false;
            cuff::Session session(options);
            for (int round = 0; round < kRounds; ++round)
            {
                // Fresh names every round, so every round adds to the shared table.
                const std::string a = "t" + std::to_string(t) + "_a" + std::to_string(round);
                const std::string f = "t" + std::to_string(t) + "_f" + std::to_string(round);
                const std::string source = "set number " + a + " to " + std::to_string(round) + "\n"
                                           "set returnable func " + f + "(n) do:\n    return n + " + std::to_string(t) + "\nend\n"
                                           "if " + f + "(" + a + ") is not " + std::to_string(round + t) + " do:\n    set number z to 1 / 0\nend\n";
                auto outcome = session.run(source);
                if (!outcome.success)
                {
                    if (failures.fetch_add(1) == 0)
                        firstError[t] = outcome.error;
                }
            } });
    }
    for (auto &th : threads)
        th.join();

    for (const auto &e : firstError)
        if (!e.empty())
            std::cout << "FAIL: " << e << "\n";
    std::cout << (failures == 0 ? "all threads ran correctly\n" : "some runs failed\n");
    std::cout << (kThreads * kRounds - failures.load()) << " passed, " << failures.load() << " failed\n";
    return failures == 0 ? 0 : 1;
}
#endif
