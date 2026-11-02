// The grammar, production by production: what the parser accepts, what it rejects, and the hint it gives for
// syntax that no longer exists. The programs are only parsed, never run.
#include "engine/CuffEngine.h"
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

    bool contains(const std::string &hay, const std::string &needle)
    {
        return hay.find(needle) != std::string::npos;
    }

    void accepts(const std::string &name, const std::string &src)
    {
        CuffEngine::Result r = CuffEngine::run(src, false);
        check(r.success, name + ": should parse, got: " + r.error.substr(0, 200));
    }

    void rejects(const std::string &name, const std::string &src, const std::string &code = "E2-001",
                 const std::string &hint = "")
    {
        CuffEngine::Result r = CuffEngine::run(src, false);
        const bool codeOk = !r.success && contains(r.error, "[" + code + "]");
        check(codeOk, name + ": should fail with " + code + ", got: " + (r.success ? std::string("success") : r.error.substr(0, 200)));
        if (codeOk && !hint.empty())
            check(contains(r.error, "hint: " + hint), name + ": hint should be '" + hint + "', got: " + r.error.substr(0, 300));
    }

    const std::string kClass = "class Box do:\n    set number size to 1\n    func grow() do:\n        change self.size to self.size + 1\n    end\nend\n";
}

int main()
{
    // ---- declaration --------------------------------------------------------------------------------------------
    accepts("number", "set number n to 1\n");
    accepts("constant", "set constant number LIMIT to 3\n");
    accepts("every builtin type", "set str a to \"x\"\nset boolean b to true\nset list c to []\nset map d to {}\n"
                                  "set pattern e to p\"a\"\nset match f to empty\nset object g to empty\nset empty h to empty\n");
    accepts("class type", kClass + "set Box b to Box()\n");
    rejects("no type", "set n to 1\n", "E2-001", "write the type first, for example: set number n to ...");
    rejects("no initial value", "set number n\n");
    accepts("type names as variables", "set number list to 1\nset number map to 2\nset number str to 3\nset number number to 4\n"
                                       "set number pattern to 5\nset number count to 6\nset number find to 7\nprint(list + map + str + number + pattern + count + find)\n");

    // ---- place: change, add, remove -----------------------------------------------------------------------------
    accepts("change name", "set number n to 1\nchange n to 2\n");
    accepts("change element", "set list g to [[1]]\nchange g[1][1] to 2\n");
    accepts("change field", kClass + "set Box b to Box()\nchange b.size to 2\nchange b.size to b.size + 1\n");
    accepts("change deep", "set map m to {}\nchange m[\"a\"][\"b\"] to 1\n");
    accepts("add to element", "set list g to [[1]]\nadd 5 to g[1]\n");
    accepts("add to field", kClass + "set Box b to Box()\nadd 5 to b.size\n");
    accepts("remove from element", "set list g to [[1, 2]]\nremove 1 from g[1]\n");
    accepts("remove from field", kClass + "set Box b to Box()\nremove 1 from b.size\n");
    rejects("change a call", "change f() to 1\n", "E2-007");
    rejects("change a slice", "set list g to [1]\nchange g[1~2] to 1\n", "E2-007");
    rejects("change a literal", "change 5 to 1\n");
    rejects("add to a call", "add 1 to f()\n", "E2-007");
    accepts("change self field in a method", "class A do:\n    set number n to 0\n    func f() do:\n        change self.n to 1\n        add 1 to self.n\n    end\nend\n");
    rejects("change self itself", "class A do:\n    func f() do:\n        change self to 1\n    end\nend\n", "E2-007");

    // ---- global ---------------------------------------------------------------------------------------------------
    accepts("global one", "func f() do:\n    global total\nend\n");
    accepts("global several", "func f() do:\n    global a, b, c\nend\n");
    rejects("global with to", "func f() do:\n    global x to 1\nend\n");

    // ---- return, fail, stop, skip -----------------------------------------------------------------------------
    accepts("return", "func f() do:\n    return\nend\nfunc g() do:\n    return 1\nend\n");
    accepts("fail", "fail \"x\"\nfail Error(\"x\")\n");
    accepts("stop and skip", "loop while true do:\n    skip\n    stop\nend\n");
    rejects("fail without a value", "fail\n");

    // ---- blocks ---------------------------------------------------------------------------------------------------
    accepts("if else", "if true do:\n    print(1)\nelse if false do:\n    print(2)\nelse do:\n    print(3)\nend\n");
    accepts("one-line if", "if true do: print(1) end\n");
    accepts("loop repeat", "loop repeat i to 1 ~ 3 do:\n    print(i)\nend\n");
    accepts("loop while", "loop while false do:\n    print(1)\nend\n");
    accepts("loop each", "loop each x in [1] do:\n    print(x)\nend\n");
    accepts("loop each one line", "loop each x in [1] do: print(x) end\n");
    rejects("loop each without in", "loop each x [1] do:\n    print(x)\nend\n");
    rejects("loop each without a name", "loop each in [1] do:\n    print(1)\nend\n");
    rejects("loop without a kind", "loop do:\n    print(1)\nend\n");
    accepts("with", kClass + "set Box b to Box()\nwith b do:\n    grow()\n    print(.size)\n    change size to 3\nend\n");
    accepts("nested with", kClass + "set Box b to Box()\nwith b do:\n    with b do:\n        print(.size)\n    end\nend\n");
    rejects("with without do", "with 1\nend\n");
    rejects("dot outside with", "print(.size)\n");
    rejects("dot call outside with", ".grow()\n");
    rejects("dot place outside with", "change .size to 1\n");
    accepts("or_else", "print(1) or_else do:\n    print(2)\nend\n");
    accepts("or_else as", "print(1) or_else as err do:\n    print(err.message)\nend\n");
    rejects("or_else as without a name", "print(1) or_else as do:\n    print(2)\nend\n");
    accepts("as is an ordinary name", "set number as to 1\nprint(as)\n");

    // ---- functions ------------------------------------------------------------------------------------------------
    accepts("func", "func f(a, b) do:\n    return a + b\nend\n");
    accepts("func modifiers", "async func a() do:\n    print(1)\nend\npure func b() do:\n    return 1\nend\nasync pure func c() do:\n    return 1\nend\npure async func d() do:\n    return 1\nend\n");
    accepts("one-line func", "func f() do: return 1 end\n");
    accepts("typed and default parameters", "func f(number a, str b to \"x\", c to 1, list d to []) do:\n    return a\nend\n");
    accepts("parameter named like a type", "func f(list) do:\n    return list\nend\nfunc g(list list) do:\n    return list\nend\n");
    accepts("class typed parameter", kClass + "func f(Box b, object o) do:\n    return b\nend\n");
    rejects("set func", "set func f() do:\nend\n", "E2-001", "write `func name(...) do:` without 'set'");
    rejects("set returnable func", "set returnable func f() do:\n    return 1\nend\n", "E2-001", "write `func`; return works in every function");
    rejects("set async func", "set async func f() do:\nend\n", "E2-001", "write `func name(...) do:` without 'set'");
    rejects("returnable after the name", "func f() returnable do:\n    return 1\nend\n");
    rejects("repeated modifier", "async async func f() do:\nend\n");
    rejects("func without a body", "func f()\n");
    rejects("required parameter after a default", "func f(a to 1, b) do:\n    return a\nend\n");
    rejects("private outside a class", "private func f() do:\nend\n");

    // ---- class ----------------------------------------------------------------------------------------------------
    accepts("class", kClass);
    accepts("class extends", kClass + "class Crate extends Box do:\nend\n");
    accepts("class members", "class A do:\n    set str s to \"\"\n    set constant number N to 1\n    private set list l to []\n"
                             "    func init(x) do:\n        change self.s to x\n    end\n    private func hidden() do:\n        return 1\n    end\n"
                             "    async func later() do:\n        print(1)\n    end\n    pure func calm() do:\n        return self.N\n    end\nend\n");
    accepts("empty class", "class Empty do:\nend\n");
    rejects("class with a statement", "class A do:\n    print(1)\nend\n");
    rejects("class with an assignment", "class A do:\n    change x to 1\nend\n");
    rejects("class without end", "class A do:\n    set number n to 1\n");
    rejects("class extends nothing", "class A extends do:\nend\n");
    rejects("class in a block", "if true do:\n    class A do:\n    end\nend\n", "E4-036");
    rejects("class in a function", "func f() do:\n    class A do:\n    end\nend\n", "E4-036");
    rejects("self in a field default", "class A do:\n    set number a to 1\n    set number b to self.a\nend\n");

    // ---- self and super -------------------------------------------------------------------------------------------
    rejects("self outside a method", "print(self)\n", "E2-001", "a method is a func declared in a class body");
    rejects("self in a function", "func f() do:\n    return self\nend\n");
    rejects("super outside a method", "super.go()\n");
    rejects("super without a parent", "class A do:\n    func f() do:\n        super.f()\n    end\nend\n");
    accepts("super with a parent", kClass + "class C extends Box do:\n    func grow() do:\n        super.grow()\n    end\nend\n");
    rejects("super without a call", kClass + "class C extends Box do:\n    func grow() do:\n        return super.grow\n    end\nend\n");

    // ---- expressions ----------------------------------------------------------------------------------------------
    accepts("logic", "print(true and false or !true)\n");
    accepts("comparisons", "print(1 < 2, 1 <= 2, 1 > 2, 1 >= 2, 1 is 1, 1 is not 2)\n");
    accepts("matches", "print(\"a\" matches p\"a\", !(\"a\" matches p\"b\"))\n");
    accepts("not matches without parentheses", "print(!\"a\" matches p\"b\")\n");
    accepts("isa", kClass + "print(Box() isa Box, 1 isa Box)\n");
    rejects("isa needs a class name", "print(1 isa 2)\n");
    accepts("arithmetic", "print(1 + 2 * 3 - 4 / 5 % 6 ** 2, -1, - -1)\n");
    accepts("indexing and slices", "set list a to [1, 2, 3]\nprint(a[1], a[-1], a[1 ~ 2], a[2 ~ 3][1])\n");
    accepts("literals", "print([1, 2,], {\"a\": 1,}, [], {}, \"s\", 'single', f\"x{1 + 1}\", empty, true, false)\n");
    accepts("calls", "func f(a) do:\n    return a\nend\nprint(f(1), f(f(2)))\n");
    accepts("method call on literals", "use DLC:string\nprint(\"a\".upper(), [1].length(), 5.sqrt())\n");
    accepts("keyword as method name", "use DLC:string\nprint(\"a\".repeat(2))\n");
    accepts("await", "async func f() do:\n    return 1\nend\nset number r to await f()\nawait f()\n");
    rejects("await a non-call", "set number r to await 5\n");
    accepts("grouping", "print((1 + 2) * 3)\n");

    // ---- pattern literals -----------------------------------------------------------------------------------------
    accepts("pattern literal", "set pattern p to p\"[num]+\"\n");
    accepts("pattern flags", "set pattern a to p\"a\"i\nset pattern b to p\"a\"m\nset pattern c to p\"a\"im\n");
    accepts("pattern with an escaped quote", "set pattern p to p\"a\\\"b\"\n");
    accepts("pattern with a backslash at the end", "set pattern p to p\"a\\\\\"\n");
    rejects("pattern with the g flag", "set pattern p to p\"a\"g\n");
    rejects("pattern syntax error", "set pattern p to p\"[num\"\n", "E3-002");
    rejects("empty token in a pattern", "set pattern p to p\"[]\"\n", "E3-006");
    rejects("unterminated pattern", "set pattern p to p\"abc\n");
    accepts("p as a name", "set number p to 1\nprint(p + 1)\n");

    // ---- removed syntax, with the hint that says what to write ----------------------------------------------------
    rejects("find from", "print(find \"[num]\" from t)\n", "E2-001", "use find(text, p\"[num]\")");
    rejects("find from global", "print(find \"[num]\" from t g)\n", "E2-001", "use find_all(text, p\"[num]\")");
    rejects("match from", "print(match t from \"(a)\")\n", "E2-001", "use match(text, p\"(a)\")");
    rejects("replace in", "print(replace \"a\" in t to \"b\")\n", "E2-001", "use replace(text, p\"a\", \"b\")");
    rejects("replace in global", "print(replace \"a\" in t to \"b\" g)\n", "E2-001", "use replace_all(text, p\"a\", \"b\")");
    rejects("split by", "print(split t by \",\")\n", "E2-001", "use split(text, p\",\")");
    rejects("count in", "print(count \"a\" in t)\n", "E2-001", "use count(text, p\"a\")");
    rejects("dynamic find from", "print(find pat from t)\n", "E2-001", "use find(text, pat)");
    rejects("change to global", "func f() do:\n    change x to global\nend\n", "E2-001", "write `global x` on its own line");
    rejects("replace statement", "replace xs[1] to 2\n", "E2-001", "use `change xs[...] to value`");
    rejects("IS", "print(t IS \"a\")\n", "E2-001", "use `matches p\"a\"i` for a case-insensitive pattern test");
    rejects("and and", "print(a && b)\n", "E2-001", "write 'and'");
    rejects("or or", "print(a || b)\n", "E2-001", "write 'or'");
    rejects("single ampersand", "print(a & b)\n", "E2-001", "write 'and'");
    rejects("single pipe", "print(a | b)\n", "E2-001", "write 'or'");

    // ---- the reserved words ---------------------------------------------------------------------------------------
    // `global`, `add`, `from`, `in`, `to` and `not` can still be used where a name is expected.
    const char *reserved[] = {"set", "change", "constant", "remove", "if", "else", "loop", "repeat", "while", "each",
                              "stop", "skip", "return", "fail", "await", "use", "do", "end", "func", "class",
                              "extends", "with", "async", "pure", "private", "or_else", "self", "super", "is", "matches", "isa",
                              "and", "or", "true", "false", "empty"};
    for (const char *word : reserved)
        rejects(std::string("reserved word ") + word + " as a variable", std::string("set number ") + word + " to 1\n");
    const char *free[] = {"global", "add", "from", "in", "to", "not", "as", "init", "by", "count", "find", "list", "map", "match", "number", "pattern", "replace", "split", "str",
                          "boolean", "object", "returnable", "IS", "to_str", "copy", "Error"};
    for (const char *word : free)
        accepts(std::string("free word ") + word + " as a variable", std::string("set number ") + word + " to 1\nprint(" + word + ")\n");

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
