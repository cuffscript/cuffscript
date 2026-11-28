// What a host program does with an Engine besides handing it text: calling a function of the script, reading and
// setting its variables, and giving it functions of its own.
#include "engine/CuffEngine.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    using cuff::Args;
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

    // What `body` printed.
    template <typename Body>
    std::string printed(Body body)
    {
        std::ostringstream out;
        std::streambuf *old = std::cout.rdbuf(out.rdbuf());
        body();
        std::cout.rdbuf(old);
        return out.str();
    }

    Value list(std::initializer_list<double> numbers)
    {
        auto items = std::make_shared<cuff::ValueList>();
        for (double n : numbers)
            items->items.push_back(Value::makeNumber(n));
        return Value::makeList(std::move(items));
    }

    void callTests()
    {
        Engine engine;
        check(engine.run("func plus(number a, number b) do:\n    return a + b\nend\n"
                         "func greet(str name, str greeting = \"Hello\") do:\n    return f\"{greeting}, {name}!\"\nend\n"
                         "func total(list xs) do:\n    number sum = 0\n    loop each number x in xs do:\n        sum = sum + x\n    end\n    return sum\nend\n"
                         "func nothing() do:\nend\n"
                         "func boom() do:\n    return 1 / 0\nend\n"
                         "number calls = 0\n"
                         "func counted() do:\n    global calls\n    calls = calls + 1\n    return calls\nend\n")
                  .success,
              "the program that declares the functions runs");

        auto r = engine.call("plus", {Value::makeNumber(2), Value::makeNumber(3)});
        check(r.success && r.value.isNumber() && r.value.asNumber() == 5, "call() gives the function's value");
        r = engine.call("greet", {Value::makeStr("Ann")});
        check(r.success && r.value.isStr() && r.value.asStr() == "Hello, Ann!", "call() uses a default for an argument that is not given");
        r = engine.call("total", {list({1, 2, 3.5})});
        check(r.success && r.value.asNumber() == 6.5, "call() passes a list");
        r = engine.call("nothing");
        check(r.success && r.value.isEmpty(), "a function that returns nothing gives empty");

        r = engine.call("plus", {Value::makeNumber(2)});
        check(!r.success && has(r.error, "[E4-010]") && has(r.error, "plus() expects 2 argument(s), got 1"), "call() checks the number of arguments");
        check(!has(r.error, "line"), "an error of the call itself names no line: " + r.error);
        r = engine.call("plus", {Value::makeNumber(2), Value::makeStr("x")});
        check(!r.success && has(r.error, "[E4-030]"), "call() checks the type of an argument");
        r = engine.call("missing");
        check(!r.success && has(r.error, "[E4-002]") && has(r.error, "undefined function 'missing'"), "call() of a function that does not exist is E4-002");
        r = engine.call("Error");
        check(!r.success && has(r.error, "is a class, not a function"), "call() of a class says so");
        r = engine.call("print", {Value::makeStr("x")});
        check(!r.success && has(r.error, "[E4-002]"), "call() reaches the program's functions, not the built-ins");

        r = engine.call("boom");
        check(!r.success && has(r.error, "[E4-006]") && has(r.error, "return 1 / 0"), "an error inside the function is drawn from the program's text: " + r.error);
        r = engine.call("plus", {Value::makeNumber(1), Value::makeNumber(1)});
        check(r.success && r.value.asNumber() == 2, "the engine works after a call that failed");

        check(engine.call("counted").value.asNumber() == 1 && engine.call("counted").value.asNumber() == 2, "calls share the program's variables");
        check(engine.get("calls")->asNumber() == 2, "get() sees what the calls changed");
    }

    void variableTests()
    {
        Engine engine;
        engine.run("number n = 5\nstr name = \"Ann\"\nconstant number LIMIT = 10\nlist items = [1, 2]\nany loose = empty\n"
                   "func show() do:\n    global shared\n    return shared\nend\n");

        check(engine.get("n") && engine.get("n")->asNumber() == 5, "get() reads a number");
        check(engine.get("name")->asStr() == "Ann", "get() reads a str");
        check(!engine.get("missing"), "get() of a name that is not declared gives nothing");
        check(engine.get("loose") && engine.get("loose")->isEmpty(), "get() of a variable that holds empty gives empty, not nothing");
        check(!engine.get("show"), "a function is not a variable");

        check(engine.set("n", Value::makeNumber(7)).success && engine.get("n")->asNumber() == 7, "set() assigns");
        auto r = engine.set("n", Value::makeStr("seven"));
        check(!r.success && has(r.error, "[E4-020]") && engine.get("n")->asNumber() == 7, "set() keeps a variable to its declared type");
        r = engine.set("LIMIT", Value::makeNumber(11));
        check(!r.success && has(r.error, "[E4-003]") && engine.get("LIMIT")->asNumber() == 10, "set() does not change a constant");
        check(engine.set("loose", Value::makeStr("anything")).success, "set() stores any value in an `any` variable");

        check(engine.set("shared", Value::makeNumber(42)).success, "set() declares a variable that the program has not");
        check(engine.call("show").value.asNumber() == 42, "a function reaches a variable the host declared with `global`");
        check(printed([&] { engine.run("print(shared + 1)\nshared = \"text\"\nprint(shared)\n"); }) == "43\ntext\n",
              "a variable the host declared is `any` for the program");

        for (const char *bad : {"", "2x", "a b", "if", "x-y", "caf\xC3\xA9"})
        {
            r = engine.set(bad, Value::makeNumber(1));
            check(!r.success && has(r.error, "[E4-025]"), std::string("set() refuses the name '") + bad + "'");
        }

        // A list is shared between the host and the program, as it is between two variables.
        Value items = *engine.get("items");
        items.asList()->items.push_back(Value::makeNumber(3));
        check(printed([&] { engine.run("print(items)\n"); }) == "[1, 2, 3]\n", "a list taken with get() is the program's own list");
    }

    void hostFunctionTests()
    {
        Engine engine;
        int calls = 0;
        engine.addFunction("host_add", [&](Args &args) -> Value
                           {
                               args.expectCount(2);
                               ++calls;
                               return Value::makeNumber(args.number(0) + args.number(1));
                           });
        engine.addFunction("host_join", [](Args &args) -> Value
                           {
                               args.expectCount(1, 2);
                               std::string out;
                               for (const Value &item : args.list(0).items)
                                   out += (out.empty() ? "" : (args.size() == 2 ? args.str(1) : std::string(","))) + item.toDisplayString();
                               return Value::makeStr(out);
                           });
        engine.addFunction("host_flag", [](Args &args) -> Value { return Value::makeBool(!args.boolean(0)); });
        engine.addFunction("host_keys", [](Args &args) -> Value { return Value::makeNumber(static_cast<double>(args.map(0).size())); });
        engine.addFunction("host_nothing", [](Args &) -> Value { return Value(); });
        engine.addFunction("host_fail", [](Args &args) -> Value { args.fail("the device is not connected"); });
        engine.addFunction("host_throw", [](Args &) -> Value { throw std::runtime_error("out of paper"); });

        check(printed([&] { engine.run("print(host_add(2, 3), host_join([1, 2, 3]), host_join([1, 2], \"-\"), host_flag(false), host_keys({\"a\": 1}))\n"
                                       "any nothing = host_nothing()\nprint(nothing is empty)\n"); }) == "5 1,2,3 1-2 true 1\ntrue\n",
              "a script calls the host's functions");
        check(calls == 1, "the host function ran once");

        auto r = engine.run("print(host_add(1))\n");
        check(!r.success && has(r.error, "[E4-010]") && has(r.error, "host_add() expects 2 argument(s), got 1") && has(r.error, "print(host_add(1))"),
              "a wrong number of arguments is reported at the call: " + r.error);
        r = engine.run("print(host_add(1, \"2\"))\n");
        check(!r.success && has(r.error, "[E4-005]") && has(r.error, "host_add() expects argument 2 to be a number, got str"), "a wrong type is E4-005");
        r = engine.run("print(host_flag())\n");
        check(!r.success && has(r.error, "[E4-010]") && has(r.error, "host_flag() expects at least 1 argument(s), got 0"), "reading an argument that was not given is E4-010");
        r = engine.run("print(host_flag(1))\n");
        check(!r.success && has(r.error, "[E4-005]") && has(r.error, "to be a boolean, got number"), "boolean() checks its argument");

        r = engine.run("host_fail()\n");
        check(!r.success && has(r.error, "[E4-042]") && has(r.error, "host_fail() failed: the device is not connected"), "fail() is E4-042 at the call: " + r.error);
        r = engine.run("host_throw()\n");
        check(!r.success && has(r.error, "[E4-042]") && has(r.error, "host_throw() failed: out of paper"), "an exception of the host is E4-042");
        check(printed([&] { engine.run("host_fail() or_else as Error e do:\n    print(e.code, e.message)\nend\nprint(\"after\")\n"); }) ==
                  "E4-042 host_fail() failed: the device is not connected\nafter\n",
              "a script catches a host function's failure with or_else");

        // The rules for names are those of the built-in functions.
        check(printed([&] { engine.run("func host_add(number a, number b) do:\n    return \"mine\"\nend\nprint(host_add(1, 2))\n"); }) == "mine\n",
              "a function of the program with the same name is the one a call finds");
        Engine other;
        other.addFunction("length", [](Args &) -> Value { return Value::makeStr("the host's"); });
        check(printed([&] { other.run("use DLC:string\nuse DLC:list\nprint(length(\"abc\"))\n"); }) == "the host's\n",
              "a library loaded later does not replace a host function");
        r = other.run("class length do:\nend\n");
        check(!r.success, "a class cannot take the name of a host function");

        // reset() forgets the program and keeps the functions.
        engine.reset();
        check(!engine.get("nothing"), "reset() forgets the program's variables");
        check(printed([&] { engine.run("print(host_add(20, 22))\n"); }) == "42\n", "reset() keeps the host's functions");

        // A host function that is called while the engine runs cannot use the engine.
        Engine nested;
        Engine::Outcome inner;
        nested.addFunction("reenter", [&](Args &) -> Value
                           {
                               inner = nested.run("print(1)\n");
                               return Value::makeBool(nested.set("x", Value::makeNumber(1)).success);
                           });
        const std::string out = printed([&] { nested.run("print(reenter())\n"); });
        check(!inner.success && has(inner.error, "already running") && out == "false\n", "a host function cannot run or change the engine that called it: " + out + inner.error);
        check(printed([&] { nested.run("print(2)\n"); }) == "2\n", "the engine works after the refused use");
    }

    void cycleTests()
    {
        // A list that contains itself, or objects that hold each other, are freed when nothing else refers to them.
        // (The engines that count what collectCycles() frees do not look for cycles on their own, so that the count
        // is the same in a build that looks at every step.)
        Engine::Options off;
        off.collectCycles = false;
        Engine engine(off);
        engine.run("list loop_list = []\nadd loop_list to loop_list\n"
                   "class Pair do:\n    any other = empty\nend\nPair a = Pair()\nPair b = Pair()\na.other = b\nb.other = a\n");
        check(engine.collectCycles() == 0, "a cycle that a variable holds is in use");
        check(printed([&] { engine.run("print(length(loop_list), a.other.other is a)\n"); }) == "1 true\n", "and is unchanged");

        engine.run("loop_list = []\na = Pair()\nb = Pair()\n");
        check(engine.collectCycles() == 3, "the cycles nothing refers to any more are freed");
        check(engine.collectCycles() == 0, "and only once");

        // What the host holds is in use, however the program let go of it.
        engine.run("list held = [1]\nadd held to held\n");
        Value kept = *engine.get("held");
        engine.run("held = []\n");
        check(engine.collectCycles() == 0 && kept.asList()->items.size() == 2, "a cycle the host holds is kept");

        // A value can outlive the engine that made it.
        {
            Engine shortLived;
            shortLived.run("list self_held = [\"x\"]\nadd self_held to self_held\n");
            kept = *shortLived.get("self_held");
        }
        check(kept.asList()->items.size() == 2 && kept.asList()->items[0].asStr() == "x", "a value the host holds outlives its engine");
        // With its engine gone nothing will free that cycle, so the host takes it apart before letting go.
        kept.asList()->items.clear();
        kept = Value();

        // While a program runs, cycles are freed as it goes, and what it uses stays as it was.
        Engine busy;
        const std::string out = printed([&]
                                        {
                                            busy.run("class Node do:\n    any next = empty\n    list data = []\nend\n"
                                                     "Node keep = Node()\nkeep.next = keep\nadd \"kept\" to keep.data\n"
                                                     "loop repeat number i = 1 ~ 100000 do:\n    Node x = Node()\n    Node y = Node()\n"
                                                     "    x.next = y\n    y.next = x\n    add i to x.data\nend\n"
                                                     "print(keep.next.next.data)\n");
                                        });
        check(out == "[\"kept\"]\n", "a program that makes many cycles keeps what it uses: " + out);
        check(busy.collectCycles() < 20000, "and most of the cycles it dropped were freed while it ran");

        // A cycle can pass through a task: an object that keeps the Task whose result is that object.
        Engine tasks(off);
        tasks.run("class Holder do:\n    any job = empty\nend\nasync func give(Holder h) do:\n    return h\nend\n"
                  "Holder kept = Holder()\nkept.job = give(kept)\nany same = await kept.job\n"
                  "loop repeat number i = 1 ~ 3000 do:\n    Holder h = Holder()\n    h.job = give(h)\n    any back = await h.job\nend\n");
        check(tasks.collectCycles() >= 2999 * 2, "a cycle through a task's result is freed");
        check(printed([&] { tasks.run("print(await kept.job is kept, copy(kept.job) is kept.job)\n"); }) == "true false\n",
              "and one that is in use keeps its result");
        check(printed([&] { tasks.run("Task t = give(kept)\nTask other = copy(t)\nt = other\nprint(await t is kept)\n"); }) == "true\n",
              "a copy of a Task gives the same result");

        Engine manual(off);
        manual.run("loop repeat number i = 1 ~ 5000 do:\n    list l = []\n    add l to l\nend\n");
        // (The list made last is still where the loop's variable was, so one of them is in use.)
        check(manual.collectCycles() >= 4999, "with collectCycles off, the cycles wait for collectCycles() or the engine's end");
    }

    void oneShotAndNamesTests()
    {
        check(printed([] { Engine::execute("print(6 * 7)\n"); }) == "42\n", "execute() runs a program once");
        const auto failed = Engine::execute("print(1 / 0)\n", ".");
        check(!failed.success && has(failed.error, "[E4-006] Runtime Error at line 1, column 9"), "execute() gives the report");

        // The names of 5.x still compile.
        cuff::Session session;
        cuff::Session::Outcome a = session.run("number n = 2\n");
        cuff::CuffEngine::Result b = cuff::CuffEngine::execute("number m = 3\n", ".", cuff::CuffEngine::Options());
        check(a.success && b.success, "Session and CuffEngine are names for Engine");

        const auto parsed = Engine::parse("number x = 1\n", false);
        check(parsed.success && parsed.ast && parsed.rawTokens.empty(), "parse() gives the tree and, when not asked, no tokens");
        const auto broken = Engine::parse("number x = \n");
        check(!broken.success && has(broken.error, "[E2-001]") && !broken.ast, "parse() reports a syntax error");
    }

    // exit() ends a run with a status and no report, args() gives Options::arguments, and the engine goes on.
    void exitAndArgumentsTests()
    {
        Engine::Options options;
        options.arguments = {"one", "two words", "--flag"};
        Engine engine(options);
        std::string out = printed([&] { check(engine.run("print(args())\n").success, "args() runs"); });
        check(out == "[\"one\", \"two words\", \"--flag\"]\n", "args() gives Options::arguments in order: " + out);

        Engine::Outcome r;
        out = printed([&] { r = engine.run("print(\"a\")\nexit(3)\nprint(\"b\")\n"); });
        check(r.success && r.error.empty() && r.exited && r.exitStatus == 3 && out == "a\n", "exit(3) ends the run with status 3 and no report");
        r = engine.run("number left = 1\n");
        check(r.success && !r.exited && r.exitStatus == 0, "the engine runs again after exit(), and a run that ends by itself has not exited");

        check(engine.run("func leave(number code) do:\n    exit(code)\nend\n").success, "a function that exits is declared");
        r = engine.call("leave", {Value::makeNumber(0)});
        check(r.success && r.exited && r.exitStatus == 0, "exit() in a call() ends the call the same way");

        r = engine.run("func f() do:\n    exit()\nend\nf() or_else as Error e do:\n    print(\"caught\")\nend\n");
        check(r.exited && r.exitStatus == 0, "or_else does not catch exit()");

        r = engine.run("exit(-1)\n");
        check(!r.success && !r.exited && has(r.error, "[E4-025]"), "a status outside 0 to 255 is E4-025");

        r = Engine::execute("exit(9)\n");
        check(r.success && r.exited && r.exitStatus == 9, "execute() gives the status");
        out = printed([] { Engine::execute("print(args())\n"); });
        check(out == "[]\n", "with no arguments args() is an empty list");
    }
}

int main()
{
    callTests();
    variableTests();
    hostFunctionTests();
    cycleTests();
    oneShotAndNamesTests();
    exitAndArgumentsTests();
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
