#include "engine/CuffEngine.h"
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

    // The engine must fail with `code` (e.g. "E2008") instead of crashing.
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
    expectError("deep parens", "print(" + rep("(", N) + "1" + rep(")", N) + ")\n", "E2008");
    expectError("unary minus chain", "print(" + rep("-", N) + "1)\n", "E2008");
    expectError("unary not chain", "print(" + rep("!", N) + "true)\n", "E2008");
    expectError("nested lists", "set list x to " + rep("[", N) + "1" + rep("]", N) + "\n", "E2008");
    expectError("nested maps", "set map x to " + rep("{\"a\": ", N) + "1" + rep("}", N) + "\n", "E2008");
    expectError("nested ifs", rep("if true do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2008");
    expectError("nested loops", rep("loop while false do: ", N) + "print(1) " + rep("end ", N) + "\n", "E2008");
    expectError("nested match calls", "print(" + rep("await ", N) + "f())\n", "E2008");
    expectError("index chain", "set list a to [1]\nprint(a" + rep("[1]", N) + ")\n", "E2008");
    expectError("long binary chain", "print(" + rep("1+", 20000) + "1)\n", "E2008");

    expectOk("100 nested parens", "set number x to " + rep("(", 100) + "1" + rep(")", 100) + "\n");
    expectOk("100 nested lists", "set list x to " + rep("[", 100) + rep("]", 100) + "\n");
    expectOk("200 nested ifs", rep("if true do: ", 200) + "print(1) " + rep("end ", 200) + "\n");
    expectOk("500-term chain", "set number x to " + rep("1+", 500) + "1\n");
    expectOk("1500-term chain", "set number x to " + rep("1+", 1500) + "1\n");

    expectError("huge number literal", "print(1" + rep("0", 400) + ")\n", "E1003");
    expectError("source too large", std::string(cuff::limits::kMaxSourceBytes + 1, ' '), "E2009");

    // ---- runtime stack safety ----
    {
        std::string body = "set returnable function down(n) do:\n    if n is 0 do:\n        return 0\n    end\n";
        int depth = 60;
        for (int i = 0; i < depth; ++i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "if true do:\n";
        body += rep("    ", 1 + static_cast<size_t>(depth)) + "return 1 + down(n - 1)\n";
        for (int i = depth - 1; i >= 0; --i)
            body += rep("    ", 1 + static_cast<size_t>(i)) + "end\n";
        body += "    return 0\nend\n";
        expectError("nested blocks x deep recursion", body + "print(down(990))\n", "E4017");
        expectOk("nested blocks x shallow recursion", body + "print(down(5))\n");
    }
    expectOk("plain recursion at depth 900",
             "set returnable function down(n) do:\n    if n is 0 do:\n        return 0\n    end\n    return 1 + down(n - 1)\nend\nprint(down(900))\n");
    expectError("infinite recursion",
                "set returnable function boom(n) do:\n    return boom(n + 1)\nend\nprint(boom(1))\n", "E4017");

    // ---- value structure safety ----
    expectOk("print a cyclic list", "set list a to []\nadd a to a\nprint(a)\n");
    // Circular structures of the same shape compare equal instead of recursing forever.
    expectOk("compare cyclic lists",
             "set list a to []\nadd a to a\nset list b to []\nadd b to b\nif a is b do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectOk("compare 5000-deep nested lists",
             "set list x to []\nset list z to []\nloop repeat i to 1 ~ 5000 do:\n    set list y to [x]\n    change x to y\n    set list w to [z]\n    change z to w\nend\nif x is z do:\n    print(1)\nelse do:\n    print(1 / 0)\nend\n");
    expectError("to_json cyclic list", "use DLC:json\nset list a to []\nadd a to a\nprint(to_json(a))\n", "E4025");
    expectOk("destroy 300000-deep list",
             "set list x to []\nloop repeat i to 1 ~ 300000 do:\n    set list y to [x]\n    change x to y\nend\nprint(1)\n");
    expectOk("print 100000-deep list",
             "set list x to []\nloop repeat i to 1 ~ 100000 do:\n    set list y to [x]\n    change x to y\nend\nprint(x)\n");
    expectError("from_json too deep", "use DLC:json\nprint(from_json(\"" + rep("[", 5000) + rep("]", 5000) + "\"))\n", "E4025");

    // ---- size limits ----
    expectError("string doubling", "set str s to \"aaaaaaaa\"\nloop repeat i to 1 ~ 40 do:\n    change s to s + s\nend\n", "E4026");
    expectError("oversized range", "use DLC:list\nprint(range(1, 1000000000))\n", "E4026");
    expectError("repeat_str too large", "use DLC:string\nprint(repeat_str(\"abcdefgh\", 1000000000))\n", "E4026");
    expectError("index beyond exact range", "set list a to [1]\nprint(a[1000000000000000000000000000000])\n", "E4025");

    // ---- legitimate large inputs must keep working ----
    expectOk("1.3M regex matches",
             "set str s to \"ab \"\nloop repeat i to 1 ~ 19 do:\n    change s to s + s\nend\nset list w to find \"[let]+\" from s g\nprint(w[1])\n");
    expectOk("linked list of 2000 maps",
             "set map node to {\"next\": empty}\nloop repeat i to 1 ~ 2000 do:\n    set map n2 to {\"next\": node}\n    change node to n2\nend\nprint(1)\n");

    // ---- regex limits ----
    expectError("regex group nesting", "print(\"a\" is \"" + rep("(", N) + "a" + rep(")", N) + "\")\n", "E3011");
    expectError("regex quantifier overflow", "print(\"a\" is \"[num]99999999999\")\n", "E3004");

    // ---- execution budget ----
    {
        CuffEngine::Options o;
        o.maxSteps = 1000;
        expectError("step limit", "loop while true do:\n    set number x to 1\nend\n", "E6001", o);
        expectOk("step limit not hit", "set number t to 0\nloop repeat i to 1 ~ 100 do:\n    change t to t + i\nend\n");
    }
    {
        CuffEngine::Options o;
        o.timeoutMs = 150;
        auto start = std::chrono::steady_clock::now();
        expectError("timeout", "loop while true do:\n    set number x to 1\nend\n", "E6002", o);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(ms < 5000, "timeout stops the run promptly (took " + std::to_string(ms) + " ms)");
    }
    {
        // A budget error is terminal: or_else must not be able to swallow it.
        CuffEngine::Options o;
        o.maxSteps = 500;
        expectError("or_else cannot catch step limit",
                    "loop while true do:\n    set number x to 1\nend or_else do:\n    print(\"caught\")\nend\n", "E6001", o);
    }

    // ---- module sandbox ----
    {
        namespace fs = std::filesystem;
        fs::path base = fs::temp_directory_path() / "cuff_limits_test";
        fs::remove_all(base);
        fs::create_directories(base / "proj" / "lib");
        fs::create_directories(base / "shared");
        {
            std::ofstream(base / "proj" / "lib" / "helper.cuff") << "set returnable function seven() do:\n    return 7\nend\n";
            std::ofstream(base / "shared" / "outside.cuff") << "set returnable function eight() do:\n    return 8\nend\n";
        }
        const std::string dir = (base / "proj").string();

        auto ok = CuffEngine::execute("use helper from ./lib\nset number x to seven()\n", dir);
        check(ok.success, "module inside the script directory loads: " + ok.error.substr(0, 120));

        auto up = CuffEngine::execute("use outside from ../shared\n", dir);
        check(!up.success && up.error.find("[E5006]") != std::string::npos, "module outside the root is rejected");

        CuffEngine::Options wide;
        wide.rootDir = base.string();
        auto widened = CuffEngine::execute("use outside from ../shared\nset number y to eight()\n", dir, wide);
        check(widened.success, "rootDir widens the sandbox: " + widened.error.substr(0, 120));

        auto abs = CuffEngine::execute("use outside from " + (base / "shared").string() + "\n", dir);
        check(!abs.success && abs.error.find("[E5006]") != std::string::npos, "absolute module path is rejected");

        fs::remove_all(base);
    }

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
