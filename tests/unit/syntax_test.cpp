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

    const std::string kClass = "class Box do:\n    number size = 1\n    func grow() do:\n        self.size = self.size + 1\n    end\nend\n";
}

int main()
{
    // ---- declaration --------------------------------------------------------------------------------------------
    accepts("number", "number n = 1\n");
    accepts("constant", "constant number LIMIT = 3\n");
    accepts("every builtin type", "str a = \"x\"\nboolean b = true\nlist c = []\nmap d = {}\n"
                                  "pattern e = p\"a\"\nmatch f = empty\nobject g = empty\nempty h = empty\n");
    accepts("class type", kClass + "Box b = Box()\n");
    rejects("no initial value", "number n\n", "E2-001", "to declare 'n' write `number n = value`");
    accepts("type names as variables", "number list = 1\nnumber map = 2\nnumber str = 3\nnumber number = 4\n"
                                       "number pattern = 5\nnumber count = 6\nnumber find = 7\nprint(list + map + str + number + pattern + count + find)\n");

    // ---- place: assignment, add, remove -----------------------------------------------------------------------------
    accepts("assign name", "number n = 1\nn = 2\n");
    accepts("assign element", "list g = [[1]]\ng[1][1] = 2\n");
    accepts("assign field", kClass + "Box b = Box()\nb.size = 2\nb.size = b.size + 1\n");
    accepts("assign deep", "map m = {}\nm[\"a\"][\"b\"] = 1\n");
    accepts("add to element", "list g = [[1]]\nadd 5 to g[1]\n");
    accepts("add to field", kClass + "Box b = Box()\nadd 5 to b.size\n");
    accepts("remove from element", "list g = [[1, 2]]\nremove 1 from g[1]\n");
    accepts("remove from field", kClass + "Box b = Box()\nremove 1 from b.size\n");
    rejects("assign to a call", "f() = 1\n", "E2-007");
    rejects("assign to a slice", "list g = [1]\ng[1~2] = 1\n", "E2-007");
    rejects("assign to a literal", "5 = 1\n", "E2-001", "`=` stores a value in a variable; to compare two values write `is`");
    rejects("add to a call", "add 1 to f()\n", "E2-007");
    accepts("assign self field in a method", "class A do:\n    number n = 0\n    func f() do:\n        self.n = 1\n        add 1 to self.n\n    end\nend\n");
    rejects("assign self itself", "class A do:\n    func f() do:\n        self = 1\n    end\nend\n", "E2-007");

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
    accepts("loop repeat", "loop repeat number i = 1 ~ 3 do:\n    print(i)\nend\n");
    accepts("loop while", "loop while false do:\n    print(1)\nend\n");
    accepts("loop each", "loop each number x in [1] do:\n    print(x)\nend\n");
    accepts("loop each one line", "loop each number x in [1] do: print(x) end\n");
    rejects("loop each without in", "loop each x [1] do:\n    print(x)\nend\n");
    rejects("loop each without a name", "loop each in [1] do:\n    print(1)\nend\n");
    rejects("loop without a kind", "loop do:\n    print(1)\nend\n");
    accepts("with", kClass + "Box b = Box()\nwith b do:\n    grow()\n    print(.size)\n    size = 3\nend\n");
    accepts("nested with", kClass + "Box b = Box()\nwith b do:\n    with b do:\n        print(.size)\n    end\nend\n");
    rejects("with without do", "with 1\nend\n");
    rejects("dot outside with", "print(.size)\n");
    rejects("dot call outside with", ".grow()\n");
    rejects("dot place outside with", ".size = 1\n");
    accepts("or_else", "print(1) or_else do:\n    print(2)\nend\n");
    accepts("or_else as", "print(1) or_else as Error err do:\n    print(err.message)\nend\n");
    rejects("or_else as Error without a name", "print(1) or_else as Error do:\n    print(2)\nend\n");
    accepts("as is an ordinary name", "number as = 1\nprint(as)\n");

    // ---- indentation: the body of a block on its own lines is one level deeper than the header --------------------
    const std::string indentHint = "indent the body one level deeper than the line that ends with 'do:'";
    rejects("if body not indented", "if true do:\nprint(1)\nend\n", "E2-001", indentHint);
    rejects("else body not indented", "if true do:\n    print(1)\nelse do:\nprint(2)\nend\n", "E2-001", indentHint);
    rejects("loop body not indented", "loop while false do:\nprint(1)\nend\n", "E2-001", indentHint);
    rejects("func body not indented", "func f() do:\nreturn 1\nend\n", "E2-001", indentHint);
    rejects("class body not indented", "class A do:\nnumber n = 1\nend\n", "E2-001", indentHint);
    rejects("or_else body not indented", "print(1) or_else do:\nprint(2)\nend\n", "E2-001", indentHint);
    rejects("nested body at the level of its header", "if true do:\n    if true do:\n    print(1)\n    end\nend\n", "E2-001", indentHint);
    rejects("a line indented deeper than the body", "if true do:\n    print(1)\n        print(2)\nend\n", "E2-001",
            "every line of a block is indented the same, one level deeper than the line that ends with 'do:'");
    accepts("empty bodies need no indentation", "if true do:\nend\nif false do:\nelse do:\nend\nfunc f() do:\nend\nclass A do:\nend\n");
    accepts("tabs indent too", "if true do:\n\tprint(1)\nend\n");

    // ---- functions ------------------------------------------------------------------------------------------------
    accepts("func", "func f(any a, any b) do:\n    return a + b\nend\n");
    accepts("func modifiers", "async func a() do:\n    print(1)\nend\npure func b() do:\n    return 1\nend\nasync pure func c() do:\n    return 1\nend\npure async func d() do:\n    return 1\nend\n");
    accepts("one-line func", "func f() do: return 1 end\n");
    accepts("typed and default parameters", "func f(number a,    str b = \"x\", any c = 1, list d = []) do:\n    return a\nend\n");
    accepts("parameter named like a type", "func f(any list) do:\n    return list\nend\nfunc g(list list) do:\n    return list\nend\n");
    accepts("class typed parameter", kClass + "func f(Box b, object o) do:\n    return b\nend\n");
    rejects("set func", "set func f() do:\nend\n", "E2-001", "write `func name(...) do:` without 'set'");
    rejects("set returnable func", "set returnable func f() do:\n    return 1\nend\n", "E2-001", "write `func name(...) do:` without 'set'; return works in every function");
    rejects("set async func", "set async func f() do:\nend\n", "E2-001", "write `func name(...) do:` without 'set'");
    rejects("returnable after the name", "func f() returnable do:\n    return 1\nend\n");
    rejects("repeated modifier", "async async func f() do:\nend\n");
    rejects("func without a body", "func f()\n");
    rejects("required parameter after a default", "func f(any a = 1, any b) do:\n    return a\nend\n");
    rejects("private outside a class", "private func f() do:\nend\n");

    // ---- class ----------------------------------------------------------------------------------------------------
    accepts("class", kClass);
    accepts("class extends", kClass + "class Crate extends Box do:\nend\n");
    accepts("class members", "class A do:\n    str s = \"\"\n    constant number N = 1\n    private list l = []\n"
                             "    func init(any x) do:\n        self.s = x\n    end\n    private func hidden() do:\n        return 1\n    end\n"
                             "    async func later() do:\n        print(1)\n    end\n    pure func calm() do:\n        return self.N\n    end\nend\n");
    accepts("empty class", "class Empty do:\nend\n");
    rejects("class with a statement", "class A do:\n    print(1)\nend\n");
    rejects("class with an assignment", "class A do:\n    x = 1\nend\n");
    rejects("class without end", "class A do:\n    number n = 1\n");
    rejects("class extends nothing", "class A extends do:\nend\n");
    rejects("class in a block", "if true do:\n    class A do:\n    end\nend\n", "E4-036");
    rejects("class in a function", "func f() do:\n    class A do:\n    end\nend\n", "E4-036");
    rejects("self in a field default", "class A do:\n    number a = 1\n    number b = self.a\nend\n");

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
    accepts("indexing and slices", "list a = [1, 2, 3]\nprint(a[1], a[-1], a[1 ~ 2], a[2 ~ 3][1])\n");
    accepts("literals", "print([1, 2,], {\"a\": 1,}, [], {}, \"s\", 'single', f\"x{1 + 1}\", empty, true, false)\n");
    accepts("calls", "func f(any a) do:\n    return a\nend\nprint(f(1), f(f(2)))\n");
    accepts("method call on literals", "use DLC:string\nprint(\"a\".upper(), [1].length(), 5.sqrt())\n");
    accepts("keyword as method name", "use DLC:string\nprint(\"a\".repeat(2))\n");
    accepts("await", "async func f() do:\n    return 1\nend\nnumber r = await f()\nawait f()\n");
    rejects("await a non-call", "number r = await 5\n");
    accepts("grouping", "print((1 + 2) * 3)\n");

    // ---- pattern literals -----------------------------------------------------------------------------------------
    accepts("pattern literal", "pattern p = p\"[num]+\"\n");
    accepts("pattern flags", "pattern a = p\"a\"i\npattern b = p\"a\"m\npattern c = p\"a\"im\n");
    accepts("pattern with an escaped quote", "pattern p = p\"a\\\"b\"\n");
    accepts("pattern with a backslash at the end", "pattern p = p\"a\\\\\"\n");
    rejects("pattern with the g flag", "pattern p = p\"a\"g\n");
    rejects("pattern syntax error", "pattern p = p\"[num\"\n", "E3-002");
    rejects("empty token in a pattern", "pattern p = p\"[]\"\n", "E3-006");
    rejects("unterminated pattern", "pattern p = p\"abc\n");
    accepts("p as a name", "number p = 1\nprint(p + 1)\n");

    // ---- removed syntax, with the hint that says what to write ----------------------------------------------------
    rejects("find from", "print(find \"[num]\" from t)\n", "E2-001", "use find(text, p\"[num]\")");
    rejects("find from global", "print(find \"[num]\" from t g)\n", "E2-001", "use find_all(text, p\"[num]\")");
    rejects("match from", "print(match t from \"(a)\")\n", "E2-001", "use match(text, p\"(a)\")");
    rejects("replace in", "print(replace \"a\" in t to \"b\")\n", "E2-001", "use replace(text, p\"a\", \"b\")");
    rejects("replace in global", "print(replace \"a\" in t to \"b\" g)\n", "E2-001", "use replace_all(text, p\"a\", \"b\")");
    rejects("split by", "print(split t by \",\")\n", "E2-001", "use split(text, p\",\")");
    rejects("count in", "print(count \"a\" in t)\n", "E2-001", "use count(text, p\"a\")");
    rejects("dynamic find from", "print(find pat from t)\n", "E2-001", "use find(text, pat)");
    rejects("replace statement", "replace xs[1] to 2\n", "E2-001", "use `xs[...] = value`");
    rejects("IS", "print(t IS \"a\")\n", "E2-001", "use `matches p\"a\"i` for a case-insensitive pattern test");
    rejects("and and", "print(a && b)\n", "E2-001", "write 'and'");
    rejects("or or", "print(a || b)\n", "E2-001", "write 'or'");
    rejects("single ampersand", "print(a & b)\n", "E2-001", "write 'and'");
    rejects("single pipe", "print(a | b)\n", "E2-001", "write 'or'");

    // ---- the reserved words ---------------------------------------------------------------------------------------
    // Every keyword is reserved: none of them can be used where a name is expected.
    const char *reserved[] = {"constant", "global", "add", "remove", "if", "else", "loop", "repeat", "while", "each",
                              "stop", "skip", "return", "fail", "await", "use", "from", "in", "to", "at", "do", "end", "func",
                              "class", "extends", "with", "async", "pure", "private", "or_else", "self", "super", "is",
                              "matches", "isa", "not", "and", "or", "true", "false", "empty"};
    for (const char *word : reserved)
        rejects(std::string("reserved word ") + word + " as a variable", std::string("number ") + word + " = 1\n");
    const char *free[] = {"set", "change", "as", "init", "by", "count", "find", "list", "map", "match", "number", "pattern", "replace",
                          "split", "str", "boolean", "object", "any", "returnable", "IS", "to_str", "copy", "Error"};
    for (const char *word : free)
        accepts(std::string("free word ") + word + " as a variable", std::string("number ") + word + " = 1\nprint(" + word + ")\n");

    // ---- one name, one declaration per function --------------------------------------------------------------------
    rejects("declared twice", "number x = 1\nnumber x = 2\n", "E2-010");
    rejects("declared twice with another type", "number x = 1\nstr x = \"a\"\n", "E2-010");
    rejects("declared again in a nested block", "number x = 1\nif true do:\n    number x = 2\nend\n", "E2-010");
    rejects("a parameter declared again", "func f(number a) do:\n    number a = 1\nend\n", "E2-010");
    rejects("a loop variable declared again", "number i = 0\nloop repeat number i = 1 ~ 3 do:\n    print(i)\nend\n", "E2-010");
    rejects("a loop element declared again", "str c = \"\"\nloop each str c in \"ab\" do:\n    print(c)\nend\n", "E2-010");
    rejects("an error name declared again", "any e = 0\nprint(1) or_else as Error e do:\n    print(e)\nend\n", "E2-010");
    accepts("a name is free again after the block that declared it", "if true do:\n    number x = 1\nend\nnumber x = 2\n");
    accepts("sibling blocks reuse a name", "if true do:\n    number x = 1\nelse do:\n    number x = 2\nend\n"
                                           "loop repeat number i = 1 ~ 2 do:\n    print(i)\nend\n"
                                           "loop repeat number i = 1 ~ 2 do:\n    print(i)\nend\n");
    accepts("every function starts with fresh names", "number x = 1\nfunc f(number x) do:\n    number y = x\n    return y\nend\n"
                                                      "func g() do:\n    number x = 2\n    number y = x\n    return y\nend\n");
    accepts("a name used before its type is not a declaration", "number x = 1\nx = 2\nx = x + 1\n");

    // ---- the old spelling of declarations and assignments --------------------------------------------------------
    rejects("set number", "set number x to 1\n", "E2-001", "write the type, the name, '=' and the value, for example: number x = 1");
    rejects("set a name", "set x to 1\n", "E2-001", "write the type, the name, '=' and the value, for example: number x = 1");
    rejects("set constant", "set constant number N to 1\n", "E2-001", "write the type, the name, '=' and the value, for example: number x = 1");
    rejects("change a name", "number x = 1\nchange x to 2\n", "E2-001", "write the name, '=' and the value, for example: x = 1");
    rejects("change an element", "list g = [1]\nchange g[1] to 2\n", "E2-001", "write the name, '=' and the value, for example: x = 1");
    rejects("assignment with to", "number x = 1\nx to 2\n");
    rejects("loop repeat without a type", "loop repeat i to 1 ~ 3 do:\n    print(i)\nend\n", "E2-001",
            "write the type and then the name, for example: loop repeat number i = 1 ~ 3 do:");
    rejects("loop each without a type", "list xs = [1]\nloop each x in xs do:\n    print(x)\nend\n", "E2-001",
            "write the type and then the name, for example: loop each str item in items do:");
    rejects("or_else as without a type", "print(1) or_else as e do:\n    print(e)\nend\n", "E2-001",
            "write the type and then the name, for example: or_else as Error e do:");
    rejects("a parameter without a type", "func f(a) do:\nend\n", "E2-001", "write the type and then the name, for example: any a");
    rejects("a statement that starts with the word set", "number set = 1\nset 5\n");

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
