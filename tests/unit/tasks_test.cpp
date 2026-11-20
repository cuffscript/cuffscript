// Tasks from the host's side: the budgets while tasks wait, an engine that is used again after tasks failed,
// calls from the host that start tasks, and network requests that overlap.
#include "engine/CuffEngine.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
    using cuff::Engine;
    using cuff::Value;

    int pass = 0, fail = 0;

    void check(bool cond, const std::string &msg)
    {
        if (cond)
            ++pass;
        else
        {
            ++fail;
            std::cout << "FAIL: " << msg << "\n";
        }
    }

    bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

    template <typename Body>
    std::string printed(Body body)
    {
        std::ostringstream out;
        std::streambuf *old = std::cout.rdbuf(out.rdbuf());
        body();
        std::cout.rdbuf(old);
        return out.str();
    }

    double secondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    const char *kSleepers = "use DLC:time\n"
                            "async func sleeper(number seconds, str name) do:\n"
                            "    time_sleep(seconds)\n"
                            "    print(name)\n"
                            "    return name\n"
                            "end\n";

    void budgetTests()
    {
        // The time limit ends a program that is only waiting, and soon after the limit, not after the sleep.
        Engine::Options options;
        options.timeoutMs = 150;
        auto start = std::chrono::steady_clock::now();
        auto r = Engine::execute("use DLC:time\ntime_sleep(30)\n", ".", options);
        check(!r.success && has(r.error, "[E6-002]") && has(r.error, "time_sleep(30)"), "the time limit ends a sleep: " + r.error);
        check(secondsSince(start) < 5, "and it does so when the limit is reached");

        start = std::chrono::steady_clock::now();
        printed([&] { r = Engine::execute(std::string(kSleepers) + "sleeper(30, \"never\")\nprint(\"top level done\")\n", ".", options); });
        check(!r.success && has(r.error, "[E6-002]") && has(r.error, "time_sleep(seconds)"),
              "the time limit ends a program whose tasks are all asleep, where one of them waits: " + r.error);
        check(secondsSince(start) < 5, "without waiting for the task");

        // The step budget is one budget for all tasks.
        Engine::Options few;
        few.maxSteps = 2000;
        r = Engine::execute("async func spin() do:\n    number i = 0\n    loop while true do:\n        i = i + 1\n    end\nend\n"
                            "Task t = spin()\nawait t\n",
                            ".", few);
        check(!r.success && has(r.error, "[E6-001]"), "a task that never ends runs out of steps");
        r = Engine::execute("async func spin() do:\n    loop while true do:\n    end\nend\nTask t = spin()\n"
                            "await t or_else do:\n    print(\"caught\")\nend\n",
                            ".", few);
        check(!r.success && has(r.error, "[E6-001]"), "and or_else around the await does not catch that");
    }

    void engineAfterFailureTests()
    {
        Engine engine;
        engine.run(kSleepers);

        // An error while tasks are waiting ends the run, and nothing of those tasks runs later.
        std::string out = printed([&]
                                  {
                                      auto r = engine.run("sleeper(0.05, \"late\")\nTask t = sleeper(0.02, \"mid\")\nprint(1 / 0)\n");
                                      check(!r.success && has(r.error, "[E4-006]"), "the run fails with the error of the top level");
                                  });
        check(out.empty(), "no task ran before the error: " + out);
        out = printed([&]
                      {
                          std::this_thread::sleep_for(std::chrono::milliseconds(80));
                          auto r = engine.run("print(\"next run\")\n");
                          check(r.success, "the engine runs again: " + r.error);
                      });
        check(out == "next run\n", "and the tasks of the failed run are gone: " + out);

        // A task's error that ends the program leaves the engine usable, too.
        auto r = engine.run("async func broken() do:\n    time_sleep(0.001)\n    fail \"task failed\"\nend\n"
                            "broken()\nsleeper(0.05, \"other\")\ntime_sleep(1)\n");
        check(!r.success && has(r.error, "task failed"), "an error in a task without a Task value ends the run");
        out = printed([&] { engine.run("Task again = sleeper(0.001, \"again\")\nprint(await again)\n"); });
        check(out == "again\nagain\n", "tasks work in the next run: " + out);

        // A run does not return while tasks are alive, so a Task from an earlier run has always finished.
        out = printed([&]
                      {
                          engine.run("Task kept = sleeper(0.01, \"kept\")\n");
                          engine.run("print(kept.done(), await kept)\n");
                      });
        check(out == "kept\ntrue kept\n", "a Task kept from an earlier run is done and still gives its result: " + out);

        // Many runs, each with tasks that fail in every way, leave nothing behind that a later run trips over.
        bool allAsExpected = true;
        printed([&]
                {
                    for (int i = 0; i < 200; ++i)
                    {
                        // The runs are one program, so each declares names of its own.
                        const std::string n = std::to_string(i);
                        auto a = engine.run("Task t" + n + " = sleeper(0, \"x\")\nawait t" + n + "\nfail \"stop\"\n");
                        auto b = engine.run("list both" + n + " = []\nasync func w" + n + "(number other) do:\n    global both" + n +
                                            "\n    return await both" + n + "[other]\nend\n"
                                            "add w" + n + "(2) to both" + n + "\nadd w" + n + "(1) to both" + n + "\n");
                        allAsExpected = allAsExpected && !a.success && has(a.error, "stop") && !b.success && has(b.error, "[E4-043]");
                    }
                    check(engine.run("Task last = sleeper(0, \"y\")\nprint(await last)\n").success, "the engine still works");
                });
        check(allAsExpected, "200 runs that end in an error or a deadlock each report it");
    }

    void hostCallTests()
    {
        Engine engine;
        engine.run(std::string(kSleepers) +
                   "list log = []\n"
                   "async func record(str what) do:\n    global log\n    time_sleep(0.002)\n    add what to log\n    return what\nend\n"
                   "func start_two() do:\n    record(\"a\")\n    record(\"b\")\n    return \"started\"\nend\n"
                   "func wait_for_two() do:\n    Task a = record(\"c\")\n    Task b = record(\"d\")\n    return await a + await b\nend\n");

        auto r = engine.call("start_two");
        check(r.success && r.value.asStr() == "started", "call() gives the function's value");
        check(engine.get("log")->asList()->items.size() == 2, "and returns when the tasks the function started have finished");

        r = engine.call("wait_for_two");
        check(r.success && r.value.asStr() == "cd", "a function called by the host can await tasks: " + r.error);

        // An async function called by the host runs to its end, as `await f()` does.
        r = engine.call("record", {Value::makeStr("e")});
        check(r.success && r.value.isStr() && r.value.asStr() == "e", "call() of an async function gives its result, not a Task");

        // A host function may not wait, but a task may call it.
        int calls = 0;
        engine.addFunction("host_count", [&](cuff::Args &) -> Value { return Value::makeNumber(++calls); });
        const std::string out = printed([&]
                                        { engine.run("async func use_host() do:\n    time_sleep(0.001)\n    return host_count()\nend\n"
                                                     "Task a = use_host()\nTask b = use_host()\nprint(await a + await b)\n"); });
        check(out == "3\n" && calls == 2, "tasks call a function of the host: " + out);
    }

#if !defined(_WIN32)
    // A server that answers each connection after `delayMs`, on a thread of its own per connection.
    struct SlowServer
    {
        int listener = -1;
        int port = 0;
        std::thread acceptor;
        std::vector<std::thread> workers;

        bool start(int delayMs, int connections)
        {
            listener = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t len = sizeof addr;
            if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || listen(listener, 128) != 0 ||
                getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
                return false;
            port = ntohs(addr.sin_port);
            acceptor = std::thread([this, delayMs, connections]()
                                   {
                                       for (int k = 0; k < connections; ++k)
                                       {
                                           const int c = accept(listener, nullptr, nullptr);
                                           if (c < 0)
                                               return;
                                           workers.emplace_back([c, delayMs, k]()
                                                                {
                                                                    std::string request;
                                                                    char buf[1024];
                                                                    while (request.find("\r\n\r\n") == std::string::npos)
                                                                    {
                                                                        const ssize_t n = recv(c, buf, sizeof buf, 0);
                                                                        if (n <= 0)
                                                                            break;
                                                                        request.append(buf, static_cast<size_t>(n));
                                                                    }
                                                                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                                                                    const std::string body = "answer " + std::to_string(k);
                                                                    const std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
                                                                    send(c, reply.data(), reply.size(), 0);
                                                                    close(c);
                                                                });
                                       }
                                   });
            return true;
        }

        void stop()
        {
            if (acceptor.joinable())
                acceptor.join();
            for (std::thread &w : workers)
                w.join();
            if (listener >= 0)
                close(listener);
        }
    };

    void networkOverlapTests()
    {
        // 40 requests, each answered after 150 ms. One after another they would take 6 s; at most 32 are under way
        // at once, so they take two rounds.
        const int count = 40;
        SlowServer server;
        if (!server.start(150, count))
        {
            std::cout << "SKIP: could not listen on the loopback address\n";
            return;
        }
        Engine::Options options;
        options.allowPrivateNetworkTargets = true;
        const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/";
        const auto start = std::chrono::steady_clock::now();
        std::string out;
        Engine::Outcome r;
        out = printed([&]
                      {
                          r = Engine::execute("use DLC:network\n"
                                              "async func fetch() do:\n    return network_get(\"" + url + "\")[\"status\"]\nend\n"
                                              "list tasks = []\n"
                                              "loop repeat number i = 1 ~ " + std::to_string(count) + " do:\n    add fetch() to tasks\nend\n"
                                              "number ok = 0\n"
                                              "loop each Task t in tasks do:\n    if await t is 200 do: ok = ok + 1 end\nend\n"
                                              "print(ok)\n",
                                              ".", options);
                      });
        const double took = secondsSince(start);
        server.stop();
        check(r.success && out == std::to_string(count) + "\n", "every request is answered: " + out + r.error);
        check(took < 3.0, "requests made by tasks overlap: " + std::to_string(took) + " s for " + std::to_string(count) + " requests of 0.15 s");
        check(took >= 0.29, "and no more than 32 are under way at once: " + std::to_string(took) + " s");
    }
#endif
}

int main()
{
    budgetTests();
    engineAfterFailureTests();
    hostCallTests();
#if !defined(_WIN32)
    networkOverlapTests();
#endif
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
