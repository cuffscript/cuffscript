#include "engine/CuffEngine.h"
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#define CUFF_TEST_HAS_PTHREAD 1
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using cuff::CuffEngine;

namespace
{
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

    std::string rep(const std::string &s, size_t n)
    {
        std::string out;
        out.reserve(s.size() * n);
        for (size_t i = 0; i < n; ++i)
            out += s;
        return out;
    }

    CuffEngine::Result runSource(const std::string &src, const CuffEngine::Options &opts = CuffEngine::Options())
    {
        return CuffEngine::execute(src, ".", opts);
    }

    // The engine must fail with `code` (e.g. "E2-008") instead of crashing.
    void expectError(const std::string &name, const std::string &src, const std::string &code,
                     const CuffEngine::Options &opts = CuffEngine::Options())
    {
        auto r = runSource(src, opts);
        check(!r.success && r.error.find("[" + code + "]") != std::string::npos,
              name + ": expected " + code + ", got: " + (r.success ? std::string("success") : r.error.substr(0, 160)));
    }

    void expectOk(const std::string &name, const std::string &src)
    {
        auto r = runSource(src);
        check(r.success, name + ": expected success, got: " + r.error.substr(0, 160));
    }
}

int main()
{
    const size_t N = 100000;

    // ---- parser nesting ----
    expectError("deep parens", "print(" + rep("(", N) + "1" + rep(")", N) + ")\n", "E2-008");
    expectError("unary minus chain", "print(" + rep("-", N) + "1)\n", "E2-008");
    expectError("unary not chain", "print(" + rep("!", N) + "true)\n", "E2-008");
    expectError("nested lists", "set list x to " + rep("[", N) + "1" + rep("]", N) + "\n", "E2-008");
    expectError("nested maps", "set map x to " + rep("{\"a\": ", N) + "1" + rep("}", N) + "\n", "E2-008");
    expectError("nested ifs", rep("if true do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2-008");
    expectError("nested loops", rep("loop while false do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2-008");
    expectError("nested match calls", "print(" + rep("await ", N) + "f())\n", "E2-008");
    expectError("index chain", "set list a to [1]\nprint(a" + rep("[1]", N) + ")\n", "E2-008");
    expectError("long binary chain", "print(" + rep("1+", 20000) + "1)\n", "E2-008");

    expectOk("100 nested parens", "set number x to " + rep("(", 100) + "1" + rep(")", 100) + "\n");
    expectOk("100 nested lists", "set list x to " + rep("[", 100) + rep("]", 100) + "\n");
    expectOk("200 nested ifs", rep("if true do: ", 200) + "print(1) " + rep("end ", 200) + "\n");
    expectOk("480 nested parens (near the raised 512 ceiling)", "set number x to " + rep("(", 480) + "1" + rep(")", 480) + "\n");
    expectOk("480 nested ifs (near the raised 512 ceiling)", rep("if true do: ", 480) + "print(1) " + rep("end ", 480) + "\n");
    expectError("700 nested parens exceeds the raised 512 ceiling", "print(" + rep("(", 700) + "1" + rep(")", 700) + ")\n", "E2-008");
    expectOk("500-term chain", "set number x to " + rep("1+", 500) + "1\n");
    expectOk("1500-term chain", "set number x to " + rep("1+", 1500) + "1\n");

    expectError("huge number literal", "print(1" + rep("0", 400) + ")\n", "E1-003");
    expectError("source too large", std::string(cuff::limits::kMaxSourceBytes + 1, ' '), "E2-009");

    // ---- runtime stack safety ----
    {
        std::string body = "set returnable func down(n) do:\n    if n is 0 do:\n        return 0\n    end\n";
        int depth = 60;
        for (int i = 0; i < depth; ++i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "if true do:\n";
        body += rep("    ", 1 + static_cast<size_t>(depth)) + "return 1 + down(n - 1)\n";
        for (int i = depth - 1; i >= 0; --i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "end\n";
        body += "    return 0\nend\n";
        expectError("nested blocks x deep recursion", body + "print(down(990))\n", "E4-017");
        expectOk("nested blocks x shallow recursion", body + "print(down(5))\n");
    }
    expectOk("plain recursion at depth 900",
             "set returnable func down(n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + down(n - 1)\nend\nprint(down(900))\n");
    expectError("infinite recursion",
                "set returnable func boom(n) do:\n    return boom(n + 1)\nend\nprint(boom(1))\n", "E4-017");

    // ---- value structure safety ----
    expectOk("print a cyclic list", "set list a to []\nadd a to a\nprint(a)\n");
    // Circular structures of the same shape compare equal instead of recursing forever.
    expectOk("compare cyclic lists",
             "set list a to []\nadd a to a\nset list b to []\nadd b to b\nif a is b do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectOk("compare 5000-deep nested lists",
             "set list x to []\nset list z to []\nloop repeat i to 1 ~ 5000 do:\n    set list y to [x]\n    change x to y\n    set list w to [z]\n    change z to w\nend\nif x is z do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectError("to_json cyclic list", "use DLC:json\nset list a to []\nadd a to a\nprint(to_json(a))\n", "E4-025");
    expectOk("destroy 300000-deep list",
             "set list x to []\nloop repeat i to 1 ~ 300000 do:\n    set list y to [x]\n    change x to y\nend\nprint(1)\n");
    expectOk("print 100000-deep list",
             "set list x to []\nloop repeat i to 1 ~ 100000 do:\n    set list y to [x]\n    change x to y\nend\nprint(x)\n");
    expectError("from_json too deep", "use DLC:json\nprint(from_json(\"" + rep("[", 5000) + rep("]", 5000) + "\"))\n", "E4-025");

    // ---- size limits ----
    expectError("string doubling", "set str s to \"aaaaaaaa\"\nloop repeat i to 1 ~ 40 do:\n    change s to s + s\nend\n", "E4-026");
    expectError("oversized range", "use DLC:list\nprint(list_range(1, 1000000000))\n", "E4-026");
    expectError("repeat_str too large", "use DLC:string\nprint(str_repeat(\"abcdefgh\", 1000000000))\n", "E4-026");
    expectError("index beyond exact range", "set list a to [1]\nprint(a[1000000000000000000000000000000])\n", "E4-025");

    // ---- legitimate large inputs must keep working ----
    expectOk("1.3M regex matches",
             "set str s to \"ab \"\nloop repeat i to 1 ~ 19 do:\n    change s to s + s\nend\nset list w to find \"[let]+\" from s g\nprint(w[1])\n");
    expectOk("linked list of 2000 maps",
             "set map node to {\"next\": empty}\nloop repeat i to 1 ~ 2000 do:\n    set map n2 to {\"next\": node}\n    change node to n2\nend\nprint(1)\n");

    // ---- regex limits ----
    expectError("regex group nesting", "print(\"a\" is \"" + rep("(", N) + "a" + rep(")", N) + "\")\n", "E3-011");
    expectError("regex quantifier overflow", "print(\"a\" is \"[num]99999999999\")\n", "E3-004");

    // ---- execution budget ----
    {
        CuffEngine::Options o;
        o.maxSteps = 1000;
        expectError("step limit", "loop while true do:\n    set number x to 1\nend\n", "E6-001", o);
        expectOk("step limit not hit", "set number t to 0\nloop repeat i to 1 ~ 100 do:\n    change t to t + i\nend\n");
    }
    {
        CuffEngine::Options o;
        o.timeoutMs = 150;
        auto start = std::chrono::steady_clock::now();
        expectError("timeout", "loop while true do:\n    set number x to 1\nend\n", "E6-002", o);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(ms < 5000, "timeout stops the run promptly (took " + std::to_string(ms) + " ms)");
    }
    {
        // A budget error is terminal: or_else must not be able to swallow it.
        CuffEngine::Options o;
        o.maxSteps = 500;
        expectError("or_else cannot catch step limit",
                    "loop while true do:\n    set number x to 1\nend or_else do:\n    print(\"caught\")\nend\n", "E6-001", o);
    }

    // ---- module sandbox ----
    {
        namespace fs = std::filesystem;
        fs::path base = fs::temp_directory_path() / "cuff_limits_test";
        fs::remove_all(base);
        fs::create_directories(base / "proj" / "lib");
        fs::create_directories(base / "shared");
        {
            std::ofstream(base / "proj" / "lib" / "helper.cuff") << "set returnable func seven() do:\n    return 7\nend\n";
            std::ofstream(base / "shared" / "outside.cuff") << "set returnable func eight() do:\n    return 8\nend\n";
        }
        const std::string dir = (base / "proj").string();

        auto ok = CuffEngine::execute("use helper from ./lib\nset number x to seven()\n", dir);
        check(ok.success, "module inside the script directory loads: " + ok.error.substr(0, 120));

        auto up = CuffEngine::execute("use outside from ../shared\n", dir);
        check(!up.success && up.error.find("[E5-006]") != std::string::npos, "module outside the root is rejected");

        CuffEngine::Options wide;
        wide.rootDir = base.string();
        auto widened = CuffEngine::execute("use outside from ../shared\nset number y to eight()\n", dir, wide);
        check(widened.success, "rootDir widens the sandbox: " + widened.error.substr(0, 120));

        // Spelled as a plain root-relative path: on Windows (base / "shared").string() is "C:\...", which is not
        // valid CuffScript source and would stop in the lexer (E2-001) before the sandbox rule is reached. The rule
        // runs before the file system is touched, so the directory does not have to exist.
        auto abs = CuffEngine::execute("use outside from /cuff_limits_test/shared\n", dir);
        check(!abs.success && abs.error.find("[E5-006]") != std::string::npos &&
                  abs.error.find("absolute module paths are not allowed") != std::string::npos,
              "absolute module path is rejected: " + abs.error.substr(0, 120));

        fs::remove_all(base);
    }


    // ---- limits that depend on one another -------------------------------------------------------------------

    // A chain as long as the parser admits is evaluated without recursion, so it runs even when the stack budget
    // is far too small for one native frame per term.
    {
        const int terms = static_cast<int>(cuff::limits::kMaxExprHeight) - 10;
        std::string chain = "set number r to 1";
        for (int i = 1; i < terms; ++i)
            chain += " + 1";
        chain += "\nif r is not " + std::to_string(terms) + " do:\n    set number z to 1 / 0\nend\n";

        CuffEngine::Options small;
        small.stackBudgetBytes = 512 * 1024;
        auto r = CuffEngine::execute(chain, ".", small);
        check(r.success, "a chain of " + std::to_string(terms) + " terms evaluates under a 512 KiB stack budget: " + r.error.substr(0, 200));

        std::string idx = "set str t to \"a\"";
        for (int i = 2; i < terms; ++i)
            idx += "[1]";
        idx += "\nif t is not \"a\" do:\n    set number z to 1 / 0\nend\n";
        r = CuffEngine::execute(idx, ".", small);
        check(r.success, "an index chain of " + std::to_string(terms) + " steps evaluates under a 512 KiB budget: " + r.error.substr(0, 200));

        std::string sl = "set str u to \"abcdef\"";
        for (int i = 0; i < 3000; ++i)
            sl += "[1~6]";
        sl += "\nif u is not \"abcdef\" do:\n    set number z to 1 / 0\nend\n";
        r = CuffEngine::execute(sl, ".", small);
        check(r.success, "a slice chain evaluates under a 512 KiB budget: " + r.error.substr(0, 200));

        // One element past the limit is refused by the parser, with the documented code.
        std::string tooLong = "set number r to 1";
        for (int i = 1; i <= static_cast<int>(cuff::limits::kMaxExprHeight) + 5; ++i)
            tooLong += " + 1";
        tooLong += "\n";
        r = CuffEngine::execute(tooLong, ".", small);
        check(!r.success && r.error.find("[E2-008]") != std::string::npos, "a chain past kMaxExprHeight is E2-008: " + r.error.substr(0, 120));
    }

    // The evaluation order of a long chain is the same as that of the recursive form: left to right, and a
    // failure is reported at the operator that failed.
    {
        std::string chain = "set number r to 10";
        for (int i = 0; i < 200; ++i)
            chain += " - 1";
        chain += "\n";
        auto r = CuffEngine::execute(chain + "set number q to 1 / (r + 190)\n", ".", CuffEngine::Options());
        check(!r.success && r.error.find("[E4-006]") != std::string::npos, "a long chain computes the same value as a short one: " + r.error.substr(0, 120));
        std::string mixed = "set list v to [1, 2, 3]\nset number r to 0";
        for (int i = 0; i < 100; ++i)
            mixed += " + v[2]";
        mixed += "\nset number q to 1 / (r - 200)\n";
        r = CuffEngine::execute(mixed, ".", CuffEngine::Options());
        check(!r.success && r.error.find("[E4-006]") != std::string::npos, "operators and indexing mixed in one chain: " + r.error.substr(0, 120));
    }

    // The stack plan: a configured budget is honoured only as far as the thread can back it.
    {
        using cuff::Interpreter;
        constexpr size_t MiB = 1024 * 1024;
        auto p = Interpreter::planStack(64 * MiB, 1 * MiB);
        check(p.hard <= 1 * MiB && p.soft < p.hard, "a configured 64 MiB on a 1 MiB stack is clamped to what exists");
        p = Interpreter::planStack(0, 8 * MiB);
        check(p.hard == 8 * MiB - 256 * 1024 && p.soft < p.hard, "an unconfigured budget is derived from the real stack");
        p = Interpreter::planStack(1 * MiB, 8 * MiB);
        check(p.soft == 1 * MiB && p.hard == 4 * MiB, "a configured budget that fits is kept as given");
        p = Interpreter::planStack(1000 * MiB, 0);
        check(p.soft == cuff::limits::kMaxStackBudget, "a configured budget above the ceiling is capped");
        p = Interpreter::planStack(0, 0);
        check(p.soft == cuff::limits::kDefaultStackBudget, "without any information the default budget is used");
        p = Interpreter::planStack(0, 100 * MiB);
        check(p.soft == cuff::limits::kMaxStackBudget, "a huge real stack is still capped at the ceiling");
    }

#ifdef CUFF_TEST_HAS_PTHREAD
    // Asking for more stack than the thread has must end in a clean E4-017, and a program the parser accepted must
    // not crash a small thread merely by being parsed, run and freed.
    {
        struct Job
        {
            std::string source;
            cuff::EngineOptions options;
            bool success = false;
            std::string error;
        };
        auto runOnSmallStack = [](Job &job, size_t stackBytes)
        {
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, stackBytes);
            pthread_t t;
            auto body = [](void *p) -> void *
            {
                Job *j = static_cast<Job *>(p);
                cuff::Session session(j->options);
                auto out = session.run(j->source);
                j->success = out.success;
                j->error = out.error;
                return nullptr;
            };
            if (pthread_create(&t, &attr, body, &job) == 0)
                pthread_join(t, nullptr);
            else
                job.error = "could not create a thread with a " + std::to_string(stackBytes) + " byte stack";
            pthread_attr_destroy(&attr);
        };

        // Recursion with a heavy body would need megabytes; the thread has 1 MiB, and 64 MiB was asked for.
        std::string body;
        for (int i = 0; i < 20; ++i)
            body += std::string(4 + 4 * i, ' ') + "if n > -1 do:\n";
        body += std::string(4 + 4 * 20, ' ') + "set number r to 1 + f(n - 1)\n";
        for (int i = 19; i >= 0; --i)
            body += std::string(4 + 4 * i, ' ') + "end\n";
        Job deep;
        deep.source = "set returnable func f(n) do:\n    set number r to 0\n    if n is 0 do:\n        return 0\n    end\n" + body +
                      "    return r\nend\nprint(f(900))\n";
        deep.options.stackBudgetBytes = 64 * 1024 * 1024;
        runOnSmallStack(deep, 1024 * 1024);
        check(!deep.success && deep.error.find("[E4-017]") != std::string::npos,
              "a 64 MiB budget on a 1 MiB stack ends in E4-017, not a crash: " + deep.error.substr(0, 160));

        // The longest chain the parser admits, parsed, run and freed on a 512 KiB stack.
        Job chain;
        chain.source = "print(1";
        for (int i = 1; i < static_cast<int>(cuff::limits::kMaxExprHeight) - 10; ++i)
            chain.source += "+1";
        chain.source += ")\n";
        runOnSmallStack(chain, 512 * 1024);
        check(chain.success || chain.error.find("[E4-017]") != std::string::npos || chain.error.find("[E2-008]") != std::string::npos,
              "the longest chain on a 512 KiB stack runs or fails cleanly: " + chain.error.substr(0, 160));
    }
#endif

#if defined(__OPTIMIZE__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    // The call depth limit is reachable by plain recursion with the smallest stack budget the engine can end up
    // with. (Unoptimised and sanitizer builds use several times the stack per call, so they are left out.)
    {
        CuffEngine::Options minimal;
        minimal.stackBudgetBytes = cuff::limits::kDefaultStackBudget;
        const int depth = cuff::limits::kMaxCallDepth - 1;
        auto r = CuffEngine::execute("set returnable func f(n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + f(n - 1)\nend\n"
                                     "set number d to f(" + std::to_string(depth) + ")\n",
                                     ".", minimal);
        check(r.success, "plain recursion reaches the call depth limit under the default stack budget: " + r.error.substr(0, 160));
        r = CuffEngine::execute("set returnable func f(n) do:\n    return f(n + 1)\nend\nset number d to f(1)\n", ".", minimal);
        check(!r.success && r.error.find("[E4-017]") != std::string::npos, "unbounded recursion ends in E4-017: " + r.error.substr(0, 120));
    }
#endif

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
