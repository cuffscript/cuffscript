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
                    "class " + base + " do:\n    number v = " + std::to_string(round) + "\n    list log = []\n"
                    "    func init(number extra = 0) do:\n        self.v = self.v + extra\n    end\n"
                    "    func get() do:\n        return self.v\n    end\nend\n"
                    "class " + child + " extends " + base + " do:\n    func get() do:\n        return super.get() * 2\n    end\nend\n"
                    + child + " obj = " + child + "(" + std::to_string(t) + ")\n"
                    "number got = 0\n"
                    "with obj do:\n    got = get()\nend\n"
                    "if got is not " + std::to_string((round + t) * 2) + " or !(\"x" + std::to_string(round) + "\" matches p\"x[num]+\") do:\n    number z = 1 / 0\nend\n"
                    "fail \"t" + std::to_string(t) + "\" or_else as Error err do:\n    if err.message is not \"t" + std::to_string(t) + "\" do:\n        number z = 1 / 0\n    end\nend\n";
                const std::string source = classes + "number " + a + " = " + std::to_string(round) + "\n"
                                           "func " + f + "(any n) do:\n    return n + " + std::to_string(t) + "\nend\n"
                                           "if " + f + "(" + a + ") is not " + std::to_string(round + t) + " do:\n    number z = 1 / 0\nend\n"
                                           "use DLC:random\n"
                                           "number d = random_int(1, 6)\n"
                                           "number u = random_float()\n"
                                           "list deck = random_shuffle([1, 2, 3, 4])\n"
                                           "number picked = random_choice([1, 2, 3])\n"
                                           "if d < 1 or d > 6 or u < 0 or u >= 1 or picked < 1 or picked > 3 do:\n    number z = 1 / 0\nend\n";
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
