#include "engine/CuffEngine.h"
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#define CUFF_TEST_HAS_PTHREAD 1
#endif
#if defined(__linux__)
#include <sys/resource.h>
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
    expectError("nested lists", "list x = " + rep("[", N) + "1" + rep("]", N) + "\n", "E2-008");
    expectError("nested maps", "map x = " + rep("{\"a\": ", N) + "1" + rep("}", N) + "\n", "E2-008");
    expectError("nested ifs", rep("if true do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2-008");
    expectError("nested loops", rep("loop while false do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2-008");
    expectError("nested match calls", "print(" + rep("await ", N) + "f())\n", "E2-008");
    expectError("index chain", "list a = [1]\nprint(a" + rep("[1]", N) + ")\n", "E2-008");
    expectError("long binary chain", "print(" + rep("1+", 20000) + "1)\n", "E2-008");
    expectError("power chain", "print(" + rep("2**", N) + "2)\n", "E2-008");
    expectError("long logical chain", "print(" + rep("true and ", 20000) + "true)\n", "E2-008");

    expectOk("100 nested parens", "number x = " + rep("(", 100) + "1" + rep(")", 100) + "\n");
    expectOk("100 nested lists", "list x = " + rep("[", 100) + rep("]", 100) + "\n");
    expectOk("200 nested ifs", rep("if true do: ", 200) + "print(1) " + rep("end ", 200) + "\n");
    expectOk("480 nested parens (near the raised 512 ceiling)", "number x = " + rep("(", 480) + "1" + rep(")", 480) + "\n");
    expectOk("480 nested ifs (near the raised 512 ceiling)", rep("if true do: ", 480) + "print(1) " + rep("end ", 480) + "\n");
    expectError("700 nested parens exceeds the raised 512 ceiling", "print(" + rep("(", 700) + "1" + rep(")", 700) + ")\n", "E2-008");
    expectOk("500-term chain", "number x = " + rep("1+", 500) + "1\n");
    expectOk("1500-term chain", "number x = " + rep("1+", 1500) + "1\n");
    expectOk("500-term logical chain", "boolean x = " + rep("true and ", 500) + "true\n");

    expectError("huge number literal", "print(1" + rep("0", 400) + ")\n", "E1-003");
    expectError("source too large", std::string(cuff::limits::kMaxSourceBytes + 1, ' '), "E2-009");

    // ---- runtime stack safety ----
    {
        std::string body = "func down(any n) do:\n    if n is 0 do:\n        return 0\n    end\n";
        int depth = 60;
        for (int i = 0; i < depth; ++i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "if true do:\n";
        body += rep("    ", 1 + static_cast<size_t>(depth)) + "return 1 + down(n - 1)\n";
        for (int i = depth - 1; i >= 0; --i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "end\n";
        body += "    return 0\nend\n";
        // A call takes a frame of the bytecode machine and no native stack, so the blocks a call is written in add
        // nothing to what a deep recursion costs: the call depth limit is the only one it can meet.
        expectOk("nested blocks x deep recursion", body + "print(down(990))\n");
        expectOk("nested blocks x shallow recursion", body + "print(down(5))\n");
        expectError("nested blocks x recursion past the call depth limit", body + "print(down(1001))\n", "E4-017");
    }
    expectOk("plain recursion at depth 900",
             "func down(any n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + down(n - 1)\nend\nprint(down(900))\n");
    expectError("infinite recursion",
                "func boom(any n) do:\n    return boom(n + 1)\nend\nprint(boom(1))\n", "E4-017");
    // What does use native stack is work that nests inside the engine, and the stack budget ends it. A default value
    // that makes another object of its own class never finishes making the first one.
    expectError("a default value that makes its own class", "class Node do:\n    any next = Node()\nend\nNode first = Node()\n", "E4-017");
    expectError("default values that make each other",
                "class A do:\n    any b = B()\nend\nclass B do:\n    any a = A()\nend\nA first = A()\n", "E4-017");
    {
        // An object shown by print runs its to_str, which shows the next object: each level is a call the engine
        // makes from inside the machine. The chain is as deep as calls may go, and the call depth limit ends it.
        const std::string chain = "class Link do:\n    any next = empty\n    func to_str() do:\n        return f\"<{self.next}>\"\n    end\nend\n"
                                  "func chain(number n) do:\n    Link head = Link()\n    loop repeat number i = 2 ~ n do:\n"
                                  "        Link link = Link()\n        link.next = head\n        head = link\n    end\n    return head\nend\n";
        expectOk("to_str through 200 objects", chain + "str text = to_str(chain(200))\n");
        expectError("to_str through more objects than calls may nest", chain + "str text = to_str(chain(1200))\n", "E4-017");
    }

    // ---- value structure safety ----
    expectOk("print a cyclic list", "list a = []\nadd a to a\nprint(a)\n");
    // Circular structures of the same shape compare equal instead of recursing forever.
    expectOk("compare cyclic lists",
             "list a = []\nadd a to a\nlist b = []\nadd b to b\nif a is b do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectOk("compare 5000-deep nested lists",
             "list x = []\nlist z = []\nloop repeat number i = 1 ~ 5000 do:\n    list y = [x]\n    x = y\n    list w = [z]\n    z = w\nend\nif x is z do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectError("to_json cyclic list", "use DLC:json\nlist a = []\nadd a to a\nprint(to_json(a))\n", "E4-025");
    expectOk("destroy 300000-deep list",
             "list x = []\nloop repeat number i = 1 ~ 300000 do:\n    list y = [x]\n    x = y\nend\nprint(1)\n");
    expectOk("print 100000-deep list",
             "list x = []\nloop repeat number i = 1 ~ 100000 do:\n    list y = [x]\n    x = y\nend\nprint(x)\n");
    expectError("from_json too deep", "use DLC:json\nprint(from_json(\"" + rep("[", 5000) + rep("]", 5000) + "\"))\n", "E4-025");

    // ---- size limits ----
    expectError("string doubling", "str s = \"aaaaaaaa\"\nloop repeat number i = 1 ~ 40 do:\n    s = s + s\nend\n", "E4-026");
    expectError("oversized range", "use DLC:list\nprint(list_range(1, 1000000000))\n", "E4-026");
    expectError("repeat_str too large", "use DLC:string\nprint(str_repeat(\"abcdefgh\", 1000000000))\n", "E4-026");
    expectError("index beyond exact range", "list a = [1]\nprint(a[1000000000000000000000000000000])\n", "E4-025");

    // ---- legitimate large inputs must keep working ----
    expectOk("1.3M regex matches",
             "str s = \"ab \"\nloop repeat number i = 1 ~ 19 do:\n    s = s + s\nend\nlist w = find_all(s, p\"[let]+\")\nprint(w[1])\n");
    expectOk("linked list of 2000 maps",
             "map node = {\"next\": empty}\nloop repeat number i = 1 ~ 2000 do:\n    map n2 = {\"next\": node}\n    node = n2\nend\nprint(1)\n");

    // ---- classes ----
    {
        auto chain = [](int n)
        {
            std::string src = "class C0 do:\nend\n";
            for (int i = 1; i < n; ++i)
                src += "class C" + std::to_string(i) + " extends C" + std::to_string(i - 1) + " do:\nend\n";
            return src + "print(1)\n";
        };
        expectOk("a chain of 64 ancestors", chain(65));
        expectError("a chain of 65 ancestors", chain(66), "E4-026");

        auto members = [](int n)
        {
            std::string src = "class M do:\n";
            for (int i = 0; i < n; ++i)
                src += "    number f" + std::to_string(i) + " = " + std::to_string(i) + "\n";
            return src + "end\nprint(M().f0)\n";
        };
        expectOk("1024 members", members(1024));
        expectError("1025 members", members(1025), "E4-026");
        expectError("members counted with the inherited ones",
                    members(1000) + "class N extends M do:\n" + "    number a0 = 0\n    number a1 = 0\n    number a2 = 0\n    number a3 = 0\n"
                                    "    number a4 = 0\n    number a5 = 0\n    number a6 = 0\n    number a7 = 0\n"
                                    "    number a8 = 0\n    number a9 = 0\n    number b0 = 0\n    number b1 = 0\n"
                                    "    number b2 = 0\n    number b3 = 0\n    number b4 = 0\n    number b5 = 0\n"
                                    "    number b6 = 0\n    number b7 = 0\n    number b8 = 0\n    number b9 = 0\n"
                                    "    number c0 = 0\n    number c1 = 0\n    number c2 = 0\n    number c3 = 0\n"
                                    "    number c4 = 0\nend\n",
                    "E4-026");

        auto classes = [](size_t n)
        {
            std::string src;
            for (size_t i = 0; i < n; ++i)
                src += "class K" + std::to_string(i) + " do:\nend\n";
            return src + "print(1)\n";
        };
        expectOk("4096 classes", classes(4096));
        expectError("4097 classes", classes(4097), "E4-026");

        // objects that point at each other are fine to build and to throw away
        expectOk("a long chain of objects",
                 "class Node do:\n    any next = empty\nend\nNode head = Node()\n"
                 "loop repeat number i = 1 ~ 20000 do:\n    Node n = Node()\n    n.next = head\n    head = n\nend\nprint(1)\n");
        expectError("a method that never returns", "class R do:\n    func go(number n) do:\n        return self.go(n + 1)\n    end\nend\nR().go(1)\n", "E4-017");
    }

    // ---- regex limits ----
    expectError("regex group nesting", "print(\"a\" matches p\"" + rep("(", N) + "a" + rep(")", N) + "\")\n", "E3-011");
    expectError("regex quantifier overflow", "print(\"a\" matches p\"[num]99999999999\")\n", "E3-004");

    // ---- execution budget ----
    {
        CuffEngine::Options o;
        o.maxSteps = 1000;
        expectError("step limit", "loop while true do:\n    number x = 1\nend\n", "E6-001", o);
        expectOk("step limit not hit", "number t = 0\nloop repeat number i = 1 ~ 100 do:\n    t = t + i\nend\n");
    }
    {
        CuffEngine::Options o;
        o.timeoutMs = 150;
        auto start = std::chrono::steady_clock::now();
        expectError("timeout", "loop while true do:\n    number x = 1\nend\n", "E6-002", o);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(ms < 5000, "timeout stops the run promptly (took " + std::to_string(ms) + " ms)");
    }
    {
        // A budget error is terminal: or_else must not be able to swallow it.
        CuffEngine::Options o;
        o.maxSteps = 500;
        expectError("or_else cannot catch step limit",
                    "loop while true do:\n    number x = 1\nend or_else do:\n    print(\"caught\")\nend\n", "E6-001", o);
    }

    // ---- module sandbox ----
    {
        namespace fs = std::filesystem;
        fs::path base = fs::temp_directory_path() / "cuff_limits_test";
        fs::remove_all(base);
        fs::create_directories(base / "proj" / "lib");
        fs::create_directories(base / "shared");
        {
            std::ofstream(base / "proj" / "lib" / "helper.cuff") << "func seven() do:\n    return 7\nend\n";
            std::ofstream(base / "shared" / "outside.cuff") << "func eight() do:\n    return 8\nend\n";
        }
        const std::string dir = (base / "proj").string();

        auto ok = CuffEngine::execute("use helper from ./lib\nnumber x = seven()\n", dir);
        check(ok.success, "module inside the script directory loads: " + ok.error.substr(0, 120));

        auto up = CuffEngine::execute("use outside from ../shared\n", dir);
        check(!up.success && up.error.find("[E5-006]") != std::string::npos, "module outside the root is rejected");

        CuffEngine::Options wide;
        wide.rootDir = base.string();
        auto widened = CuffEngine::execute("use outside from ../shared\nnumber y = eight()\n", dir, wide);
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
        std::string chain = "number r = 1";
        for (int i = 1; i < terms; ++i)
            chain += " + 1";
        chain += "\nif r is not " + std::to_string(terms) + " do:\n    number z = 1 / 0\nend\n";

        CuffEngine::Options small;
        small.stackBudgetBytes = 512 * 1024;
        auto r = CuffEngine::execute(chain, ".", small);
        check(r.success, "a chain of " + std::to_string(terms) + " terms evaluates under a 512 KiB stack budget: " + r.error.substr(0, 200));

        std::string idx = "str t = \"a\"";
        for (int i = 2; i < terms; ++i)
            idx += "[1]";
        idx += "\nif t is not \"a\" do:\n    number z = 1 / 0\nend\n";
        r = CuffEngine::execute(idx, ".", small);
        check(r.success, "an index chain of " + std::to_string(terms) + " steps evaluates under a 512 KiB budget: " + r.error.substr(0, 200));

        std::string sl = "str u = \"abcdef\"";
        for (int i = 0; i < 3000; ++i)
            sl += "[1~6]";
        sl += "\nif u is not \"abcdef\" do:\n    number z = 1 / 0\nend\n";
        r = CuffEngine::execute(sl, ".", small);
        check(r.success, "a slice chain evaluates under a 512 KiB budget: " + r.error.substr(0, 200));

        // One element past the limit is refused by the parser, with the documented code.
        std::string tooLong = "number r = 1";
        for (int i = 1; i <= static_cast<int>(cuff::limits::kMaxExprHeight) + 5; ++i)
            tooLong += " + 1";
        tooLong += "\n";
        r = CuffEngine::execute(tooLong, ".", small);
        check(!r.success && r.error.find("[E2-008]") != std::string::npos, "a chain past kMaxExprHeight is E2-008: " + r.error.substr(0, 120));
    }

    // `and` / `or` chains take the same iterative path, and stop evaluating once the result is decided: the
    // operands that would fail are never reached.
    {
        const int terms = static_cast<int>(cuff::limits::kMaxExprHeight) - 10;
        CuffEngine::Options small;
        small.stackBudgetBytes = 512 * 1024;

        std::string andChain = "boolean r = true";
        for (int i = 1; i < terms; ++i)
            andChain += " and true";
        andChain += "\nif !r do:\n    number z = 1 / 0\nend\n";
        auto r = CuffEngine::execute(andChain, ".", small);
        check(r.success, "an 'and' chain of " + std::to_string(terms) + " terms evaluates under a 512 KiB budget: " + r.error.substr(0, 200));

        std::string orChain = "boolean r = true";
        for (int i = 1; i < terms; ++i)
            orChain += " or 1 / 0 is 1";
        orChain += "\nif !r do:\n    number z = 1 / 0\nend\n";
        r = CuffEngine::execute(orChain, ".", small);
        check(r.success, "an 'or' chain stops at the first true operand: " + r.error.substr(0, 200));

        std::string andFalse = "boolean r = false";
        for (int i = 1; i < terms; ++i)
            andFalse += " and 1 / 0 is 1";
        andFalse += "\nif r do:\n    number z = 1 / 0\nend\n";
        r = CuffEngine::execute(andFalse, ".", small);
        check(r.success, "an 'and' chain stops at the first false operand: " + r.error.substr(0, 200));
    }

    // The evaluation order of a long chain is the same as that of the recursive form: left to right, and a
    // failure is reported at the operator that failed.
    {
        std::string chain = "number r = 10";
        for (int i = 0; i < 200; ++i)
            chain += " - 1";
        chain += "\n";
        auto r = CuffEngine::execute(chain + "number q = 1 / (r + 190)\n", ".", CuffEngine::Options());
        check(!r.success && r.error.find("[E4-006]") != std::string::npos, "a long chain computes the same value as a short one: " + r.error.substr(0, 120));
        std::string mixed = "list v = [1, 2, 3]\nnumber r = 0";
        for (int i = 0; i < 100; ++i)
            mixed += " + v[2]";
        mixed += "\nnumber q = 1 / (r - 200)\n";
        r = CuffEngine::execute(mixed, ".", CuffEngine::Options());
        check(!r.success && r.error.find("[E4-006]") != std::string::npos, "operators and indexing mixed in one chain: " + r.error.substr(0, 120));
    }

    // Every supported platform can say how much stack is left, which is what lets the budget follow the real stack.
    // (A platform that cannot makes the engine use its default budget, which is safe, but this is the place to
    // notice that a platform lost the ability: on Windows it is looked up at run time, see Attributes.h.)
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    check(cuff::availableStackBytes() > 64 * 1024, "the platform reports how much stack is left: " + std::to_string(cuff::availableStackBytes()));
    {
        // The engine asks before every parse and every run, and on Linux the answer is kept between questions
        // (Attributes.h): asked twice from one place, it is the same.
        const size_t first = cuff::availableStackBytes();
        const size_t second = cuff::availableStackBytes();
        const size_t apart = first > second ? first - second : second - first;
        check(apart < 4096, "the stack that is left is the same when asked twice: " + std::to_string(first) + ", " + std::to_string(second));
    }
#endif

#if defined(__linux__) && defined(__GLIBC__)
    // What is kept follows the stack limit of the process: with half the limit, the first thread has less left, and
    // with the limit back it has what it had.
    {
        struct rlimit before;
        if (getrlimit(RLIMIT_STACK, &before) == 0 && before.rlim_cur != RLIM_INFINITY && before.rlim_cur >= 2 * 1024 * 1024)
        {
            const size_t whole = cuff::availableStackBytes();
            struct rlimit half = before;
            half.rlim_cur = before.rlim_cur / 2;
            if (setrlimit(RLIMIT_STACK, &half) == 0)
            {
                const size_t halved = cuff::availableStackBytes();
                setrlimit(RLIMIT_STACK, &before);
                const size_t restored = cuff::availableStackBytes();
                check(halved + before.rlim_cur / 4 < whole,
                      "a lower stack limit leaves less stack: " + std::to_string(whole) + " became " + std::to_string(halved));
                const size_t apart = whole > restored ? whole - restored : restored - whole;
                check(apart < 4096, "the limit put back leaves what there was: " + std::to_string(whole) + ", " + std::to_string(restored));
            }
        }
    }
#endif

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

        // Each thread is told about its own stack, whatever another thread was told before it.
        {
            struct Asked
            {
                size_t left = 0;
            } asked;
            const size_t mine = cuff::availableStackBytes();
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 256 * 1024);
            pthread_t t;
            auto ask = [](void *p) -> void *
            {
                static_cast<Asked *>(p)->left = cuff::availableStackBytes();
                return nullptr;
            };
            if (pthread_create(&t, &attr, ask, &asked) == 0)
            {
                pthread_join(t, nullptr);
                // A thread may get more stack than it asked for, and is told about all of it: on arm64 macOS a stack
                // is 12 KiB larger than requested, because the thread's own record shares its top page.
                check(asked.left > 128 * 1024 && asked.left < 512 * 1024,
                      "a thread that asked for a 256 KiB stack is told about that stack: " + std::to_string(asked.left));
                const size_t after = cuff::availableStackBytes();
                const size_t apart = mine > after ? mine - after : after - mine;
                check(apart < 4096, "and the first thread is still told about its own: " + std::to_string(mine) + ", " + std::to_string(after));
            }
            pthread_attr_destroy(&attr);
        }

        // A default value that makes another object of its class nests inside the engine without end. The thread
        // has 1 MiB, and 64 MiB was asked for: the budget is cut to what the thread has, and the error is a clean one.
        Job deep;
        deep.source = "class Node do:\n    any next = Node()\nend\nNode first = Node()\n";
        deep.options.stackBudgetBytes = 64 * 1024 * 1024;
        runOnSmallStack(deep, 1024 * 1024);
        check(!deep.success && deep.error.find("[E4-017]") != std::string::npos,
              "a 64 MiB budget on a 1 MiB stack ends in E4-017, not a crash: " + deep.error.substr(0, 160));

        // A program's own calls are frames of the machine: recursion to the call depth limit needs no more native
        // stack than a program without a single call, so it runs on a thread far too small to recurse on natively.
        Job recursion;
        recursion.source = "func f(any n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + f(n - 1)\nend\n"
                           "number d = f(" + std::to_string(cuff::limits::kMaxCallDepth - 1) + ")\n";
        runOnSmallStack(recursion, 256 * 1024);
        check(recursion.success, "recursion to the call depth limit runs on a 256 KiB stack: " + recursion.error.substr(0, 160));

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

    // The call depth limit is what ends recursion, whatever the stack budget: here the smallest budget the engine can
    // end up with.
    {
        CuffEngine::Options minimal;
        minimal.stackBudgetBytes = cuff::limits::kDefaultStackBudget;
        const int depth = cuff::limits::kMaxCallDepth - 1;
        auto r = CuffEngine::execute("func f(any n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + f(n - 1)\nend\n"
                                     "number d = f(" + std::to_string(depth) + ")\n",
                                     ".", minimal);
        check(r.success, "plain recursion reaches the call depth limit under the default stack budget: " + r.error.substr(0, 160));
        r = CuffEngine::execute("func f(any n) do:\n    return f(n + 1)\nend\nnumber d = f(1)\n", ".", minimal);
        check(!r.success && r.error.find("[E4-017]") != std::string::npos, "unbounded recursion ends in E4-017: " + r.error.substr(0, 120));
        r = CuffEngine::execute("func f(any n) do:\n    return f(n + 1)\nend\nnumber d = f(1)\n", ".", minimal);
        check(r.error.find("maximum call depth") != std::string::npos, "and names the call depth limit: " + r.error.substr(0, 120));
    }

    // Modules: how deep they may import each other, and how large one file may be (both are E5-007).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cuff_limits_test_modules";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        const int depth = cuff::limits::kMaxImportDepth;
        for (int i = 0; i < depth + 4; ++i)
        {
            std::ofstream f(dir / ("m" + std::to_string(i) + ".cuff"));
            f << (i + 1 < depth + 4 ? "use m" + std::to_string(i + 1) + " from ./\n" : std::string("number last = 1\n"));
        }
        // m0 is imported at depth 0, m1 at depth 1, and so on: the module imported at depth `depth` is refused.
        auto r = CuffEngine::execute("use m0 from ./\n", dir.string(), CuffEngine::Options());
        check(!r.success && r.error.find("[E5-007]") != std::string::npos, "a chain of modules deeper than the limit is E5-007: " + r.error.substr(0, 120));
        r = CuffEngine::execute("use m" + std::to_string(depth + 4 - 1 - (depth - 1)) + " from ./\n", dir.string(), CuffEngine::Options());
        check(r.success, "a chain exactly at the limit is accepted: " + r.error.substr(0, 120));

        {
            std::ofstream big(dir / "big.cuff", std::ios::binary);
            big << "note: ";
        }
        fs::resize_file(dir / "big.cuff", cuff::limits::kMaxSourceBytes + 1);
        r = CuffEngine::execute("use big from ./\n", dir.string(), CuffEngine::Options());
        check(!r.success && r.error.find("[E5-007]") != std::string::npos, "a module larger than the source limit is E5-007: " + r.error.substr(0, 120));
        fs::remove_all(dir, ec);
    }

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
