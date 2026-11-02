// Several threads, each with its own Session, parsing and running different programs at the same time.
//
// The engine's contract for an embedder is "one Session per thread, no sharing". Two things are shared by every
// thread and have to be synchronised: the table that turns identifier names into numbers (common/NameInterner.h) and
// the generator behind DLC:random (dlc/RandomDLC.h). Every thread here introduces names of its own, declares classes
// and makes objects (every session has its own class table, including the built-in Error), uses patterns and draws
// random numbers, so an unsynchronised table or generator corrupts, hangs or crashes; build this test with
// -fsanitize=thread to see a data race itself.
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
                const std::string base = "T" + std::to_string(t) + "Base" + std::to_string(round);
                const std::string child = "T" + std::to_string(t) + "Child" + std::to_string(round);
                const std::string classes =
                    "class " + base + " do:\n    set number v to " + std::to_string(round) + "\n    set list log to []\n"
                    "    func init(number extra to 0) do:\n        change self.v to self.v + extra\n    end\n"
                    "    func get() do:\n        return self.v\n    end\nend\n"
                    "class " + child + " extends " + base + " do:\n    func get() do:\n        return super.get() * 2\n    end\nend\n"
                    "set " + child + " obj to " + child + "(" + std::to_string(t) + ")\n"
                    "set number got to 0\n"
                    "with obj do:\n    change got to get()\nend\n"
                    "if got is not " + std::to_string((round + t) * 2) + " or !(\"x" + std::to_string(round) + "\" matches p\"x[num]+\") do:\n    set number z to 1 / 0\nend\n"
                    "fail \"t" + std::to_string(t) + "\" or_else as err do:\n    if err.message is not \"t" + std::to_string(t) + "\" do:\n        set number z to 1 / 0\n    end\nend\n";
                const std::string source = classes + "set number " + a + " to " + std::to_string(round) + "\n"
                                           "func " + f + "(n) do:\n    return n + " + std::to_string(t) + "\nend\n"
                                           "if " + f + "(" + a + ") is not " + std::to_string(round + t) + " do:\n    set number z to 1 / 0\nend\n"
                                           "use DLC:random\n"
                                           "set number d to random_int(1, 6)\n"
                                           "set number u to random_float()\n"
                                           "set list deck to random_shuffle([1, 2, 3, 4])\n"
                                           "set number picked to random_choice([1, 2, 3])\n"
                                           "if d < 1 or d > 6 or u < 0 or u >= 1 or picked < 1 or picked > 3 do:\n    set number z to 1 / 0\nend\n";
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
