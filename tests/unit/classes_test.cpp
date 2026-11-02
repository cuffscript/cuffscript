// The object model: who owns the class table, what survives between the runs of a session, the order things
// happen in when an object is made, how `with` looks a name up, and how errors become objects.
#include "engine/CuffEngine.h"
#include "engine/Session.h"
#include <iostream>
#include <sstream>
#include <string>

using cuff::CuffEngine;
using cuff::Session;

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

    struct Captured
    {
        bool ok = false;
        std::string out;
        std::string error;
    };

    // Runs `src` and collects what it prints.
    template <typename Run>
    Captured capture(Run run)
    {
        std::ostringstream buffer;
        std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());
        Captured c;
        try
        {
            auto r = run();
            c.ok = r.success;
            c.error = r.error;
        }
        catch (...)
        {
            std::cout.rdbuf(old);
            throw;
        }
        std::cout.rdbuf(old);
        c.out = buffer.str();
        return c;
    }

    Captured runOnce(const std::string &src)
    {
        return capture([&] { return CuffEngine::execute(src, "."); });
    }

    Captured runIn(Session &session, const std::string &src)
    {
        return capture([&] { return session.run(src); });
    }

    void expectOutput(const std::string &name, const std::string &src, const std::string &expected)
    {
        Captured c = runOnce(src);
        check(c.ok && c.out == expected,
              name + ": expected '" + expected + "', got '" + c.out + "'" + (c.ok ? "" : " / " + c.error.substr(0, 200)));
    }

    void expectError(const std::string &name, const std::string &src, const std::string &code)
    {
        Captured c = runOnce(src);
        check(!c.ok && c.error.find("[" + code + "]") != std::string::npos,
              name + ": expected " + code + ", got: " + (c.ok ? std::string("success") : c.error.substr(0, 200)));
    }
}

int main()
{
    // ---- the class table belongs to the interpreter -----------------------------------------------------------
    {
        Session a, b;
        runIn(a, "class Shared do:\n    func who() do:\n        return \"a\"\n    end\nend\n");
        runIn(b, "class Shared do:\n    func who() do:\n        return \"b\"\n    end\nend\n");
        Captured ra = runIn(a, "print(Shared().who())\n");
        Captured rb = runIn(b, "print(Shared().who())\n");
        check(ra.out == "a\n" && rb.out == "b\n", "two sessions keep their own classes: " + ra.out + rb.out);
        Session c;
        Captured rc = runIn(c, "print(Shared().who())\n");
        check(!rc.ok && rc.error.find("[E4-002]") != std::string::npos, "a new session does not see another's classes");
    }

    // ---- a session: classes and objects across entries ----------------------------------------------------------
    {
        Session s;
        runIn(s, "class Counter do:\n    set number n to 0\n    func bump() do:\n        change self.n to self.n + 1\n        return self.n\n    end\nend\n"
                 "set Counter c to Counter()\n");
        Captured r = runIn(s, "print(c.bump(), c.bump())\n");
        check(r.ok && r.out == "1 2\n", "an object made in one entry works in the next: " + r.out + r.error);

        runIn(s, "class Counter do:\n    set number n to 100\n    func bump() do:\n        change self.n to self.n + 10\n        return self.n\n    end\nend\n");
        r = runIn(s, "print(c.bump(), Counter().bump())\n");
        check(r.ok && r.out == "3 110\n", "a class declared again applies to new objects only: " + r.out + r.error);

        r = runIn(s, "class Counter do:\n    set number n to 0\nend\nclass Counter do:\nend\n");
        check(!r.ok && r.error.find("[E4-038]") != std::string::npos, "the same class twice in one entry is an error");

        Session t;
        runIn(t, "class A do:\n    func f() do:\n        return 1\n    end\nend\n");
        runIn(t, "func A() do:\n    return 2\nend\n");
        r = runIn(t, "print(1)\n");
        check(!r.ok || r.out == "1\n", "a function named like an earlier class is handled without a crash");
        t.reset();
        r = runIn(t, "print(A())\n");
        check(!r.ok && r.error.find("[E4-002]") != std::string::npos, "reset forgets every class");
    }

    // ---- making an object: defaults from the oldest class down, then init ------------------------------------------
    expectOutput("construction order",
                 "set list log to []\n"
                 "func mark(text) do:\n    global log\n    add text to log\n    return text\nend\n"
                 "class A do:\n    set str a to mark(\"A.default\")\n    func init() do:\n        global log\n        add \"A.init\" to log\n    end\nend\n"
                 "class B extends A do:\n    set str b to mark(\"B.default\")\n    func init() do:\n        super.init()\n        global log\n        add \"B.init\" to log\n    end\nend\n"
                 "set B b to B()\nprint(log)\n",
                 "[\"A.default\", \"B.default\", \"A.init\", \"B.init\"]\n");
    expectOutput("each object gets fresh defaults",
                 "class Bag do:\n    set list items to []\nend\nset Bag a to Bag()\nset Bag b to Bag()\nadd 1 to a.items\nprint(a.items, b.items)\n",
                 "[1] []\n");
    expectOutput("a failing init makes no object",
                 "class Box do:\n    func init() do:\n        fail \"no box\"\n    end\nend\nset Box made to empty\n"
                 "change made to Box() or_else as e do:\n    print(e.message)\nend\nprint(made is empty)\n",
                 "no box\ntrue\n");
    expectOutput("a child without init uses the parent's",
                 "class A do:\n    set number n to 0\n    func init(number n) do:\n        change self.n to n\n    end\nend\nclass B extends A do:\nend\nprint(B(7).n)\n",
                 "7\n");
    expectError("init is not inherited when the child defines its own", "class A do:\n    func init(number n) do:\n        print(n)\n    end\nend\nclass B extends A do:\n    func init() do:\n        print(0)\n    end\nend\nB(1)\n", "E4-010");
    expectOutput("a constant field is set by init and then fixed",
                 "class K do:\n    set constant number ID to 0\n    func init(number id) do:\n        change self.ID to id\n    end\nend\nset K k to K(5)\nprint(k.ID)\n"
                 "change k.ID to 6 or_else as e do:\n    print(e.code)\nend\n",
                 "5\nE4-003\n");

    expectError("a constant field's name must be in capitals", "class K do:\n    set constant number id to 0\nend\nprint(1)\n", "E4-004");

    // ---- private is checked against the class that wrote the method ----------------------------------------------
    expectOutput("an inherited method reads its own class's private field",
                 "class A do:\n    private set number secret to 42\n    func reveal() do:\n        return self.secret\n    end\nend\nclass B extends A do:\nend\nprint(B().reveal())\n",
                 "42\n");
    expectError("a child method cannot read the parent's private field",
                "class A do:\n    private set number secret to 42\nend\nclass B extends A do:\n    func peek() do:\n        return self.secret\n    end\nend\nprint(B().peek())\n", "E4-034");
    expectOutput("two objects of one class see each other's private fields",
                 "class A do:\n    private set number v to 1\n    func init(number v) do:\n        change self.v to v\n    end\n    func sum(A other) do:\n        return self.v + other.v\n    end\nend\nprint(A(3).sum(A(4)))\n",
                 "7\n");

    // ---- with: the order a name is looked up in -----------------------------------------------------------------
    expectOutput("with: local, then object, then outer",
                 "class P do:\n    set number x to 1\n    set number y to 2\nend\nset number x to 10\nset number z to 30\nset P p to P()\n"
                 "with p do:\n    print(x, z)\n    set number y to 99\n    print(y, .y)\nend\n",
                 "1 30\n99 2\n");
    expectOutput("with: the innermost object first",
                 "class P do:\n    set number v to 0\n    func init(number v) do:\n        change self.v to v\n    end\nend\n"
                 "with P(1) do:\n    with P(2) do:\n        print(v, .v)\n    end\n    print(v)\nend\n",
                 "2 2\n1\n");
    expectOutput("with: the object is worked out once",
                 "set number made to 0\nclass P do:\n    set number v to 0\n    func hit() do:\n        change self.v to self.v + 1\n    end\nend\n"
                 "func build() do:\n    global made\n    change made to made + 1\n    return P()\nend\n"
                 "with build() do:\n    hit()\n    hit()\n    print(v)\nend\nprint(made)\n",
                 "2\n1\n");
    expectOutput("with inside a method keeps self",
                 "class P do:\n    set number v to 5\n    func f(P other) do:\n        with other do:\n            return self.v\n        end\n    end\nend\nclass Q do:\n    set number v to 8\nend\n"
                 "print(P().f(P()))\n",
                 "5\n");
    expectError("with needs an object", "with [1] do:\n    print(1)\nend\n", "E4-035");
    expectError("with: a private member stays private", "class P do:\n    private set number s to 1\nend\nwith P() do:\n    print(s)\nend\n", "E4-034");

    // ---- errors are objects ---------------------------------------------------------------------------------------
    expectOutput("an engine error arrives as an Error",
                 "print(1 / 0) or_else as e do:\n    print(e.code, e.line, e.column, e isa Error)\nend\n",
                 "E4-006 1 9 true\n");
    expectOutput("fail with text wraps it in an Error",
                 "fail \"boom\" or_else as e do:\n    print(e.code, e.message, type_of(e))\nend\n",
                 "E7-001 boom Error\n");
    expectOutput("a subclass of Error keeps its class",
                 "class MyError extends Error do:\n    set number level to 3\nend\nfail MyError(\"deep\") or_else as e do:\n    print(type_of(e), e.level, e isa MyError, e isa Error)\nend\n",
                 "MyError 3 true true\n");
    expectOutput("rethrown error keeps its class",
                 "class MyError extends Error do:\nend\nfunc f() do:\n    fail MyError(\"x\")\nend\n"
                 "func g() do:\n    f() or_else as a do:\n        fail a\n    end\nend\ng() or_else as b do:\n    print(type_of(b))\nend\n",
                 "MyError\n");
    {
        Captured c = runOnce("class Bad extends Error do:\nend\nfail Bad(\"visible\")\n");
        check(!c.ok && c.error.find("[E7-001] Program Error") != std::string::npos && c.error.find("Bad") != std::string::npos &&
                  c.error.find("visible") != std::string::npos,
              "an uncaught user error names its class: " + c.error.substr(0, 200));
    }
    expectError("an error that is not caught ends the program", "fail \"x\"\nprint(1)\n", "E7-001");

    // ---- objects and values ------------------------------------------------------------------------------------------
    expectOutput("type_of, isa and identity",
                 "class A do:\nend\nclass B extends A do:\nend\nset B b to B()\nset A same to b\nprint(type_of(b), b isa A, same is b, A() is A(), b isa B)\n",
                 "B true true false true\n");
    expectOutput("copy is shallow and independent",
                 "class P do:\n    set list l to []\n    set number n to 1\nend\nset P a to P()\nset P b to a.copy()\nchange b.n to 2\nadd 1 to b.l\nprint(a.n, b.n, a.l)\n",
                 "1 2 [1]\n");
    expectOutput("display", "class Plain do:\nend\nclass Named do:\n    func to_str() do:\n        return \"named!\"\n    end\nend\nprint(Plain(), Named(), f\"{Named()}\")\n",
                 "<Plain> named! named!\n");
    expectOutput("pure methods can read fields and call pure methods",
                 "class P do:\n    set number n to 2\n    pure func double() do:\n        return self.n * 2\n    end\n    pure func quad() do:\n        return self.double() * 2\n    end\nend\nprint(P().quad())\n",
                 "8\n");
    expectError("a pure method cannot call an impure one",
                "class P do:\n    func loud() do:\n        return 1\n    end\n    pure func calm() do:\n        return self.loud()\n    end\nend\nP().calm()\n", "E4-029");

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
