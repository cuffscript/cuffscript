// Behaviour that follows from how a run is organised rather than from any one statement: what has been printed when an
// error ends a run. The success cases in tests/cases/phenomena/ cover the rest of docs/guide/phenomena.md; this file
// covers what a script cannot show about itself, which is what reaches the output before the failure.
#include "engine/CuffEngine.h"
#include <iostream>
#include <sstream>
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

    struct Run
    {
        std::string printed;
        std::string error;
        bool success = false;
    };

    Run run(const std::string &source)
    {
        std::ostringstream sink;
        std::streambuf *old = std::cout.rdbuf(sink.rdbuf());
        CuffEngine::Result r = CuffEngine::execute(source, ".", CuffEngine::Options());
        std::cout.rdbuf(old);
        Run out;
        out.printed = sink.str();
        out.error = r.error;
        out.success = r.success;
        return out;
    }

    bool has(const std::string &hay, const std::string &needle)
    {
        return hay.find(needle) != std::string::npos;
    }
}

int main()
{
    // A program is read completely before any of it runs, so a syntax error on the last line stops the first line too.
    {
        Run r = run("print(\"first line\")\nprint(\"second line\")\nprint(1 +\n");
        check(!r.success && has(r.error, "[E2-001]"), "a syntax error on the last line fails the run: " + r.error.substr(0, 100));
        check(r.printed.empty(), "...and nothing was printed before it: '" + r.printed + "'");
    }

    // A runtime error, on the other hand, comes when its line is reached, and what came before it has been printed.
    {
        Run r = run("print(\"first line\")\nprint(\"second line\")\nprint(1 / 0)\nprint(\"never\")\n");
        check(!r.success && has(r.error, "[E4-006]"), "a runtime error fails the run: " + r.error.substr(0, 100));
        check(r.printed == "first line\nsecond line\n", "...and the lines before it were printed: '" + r.printed + "'");
    }

    // A class or function body is parsed with the rest, so a mistake inside one that is never called still stops the run.
    {
        Run r = run("print(\"first line\")\nfunc never_called() do:\n    print(1 +\nend\n");
        check(!r.success && r.printed.empty(), "a syntax error inside a function that is never called still stops the run");
    }

    // An un-awaited async call is still waiting when the program's own lines fail, and it never runs.
    {
        Run r = run("async func later() do:\n    print(\"later\")\nend\nlater()\nprint(1 / 0)\n");
        check(!r.success && !has(r.printed, "later"), "a waiting async call is dropped when the program fails: '" + r.printed + "'");
    }

    // A line break inside a string literal is one "\n", whether the file ends its lines with LF or CRLF.
    {
        Run lf = run("use DLC:string\nstr s = \"ab\ncd\"\nprint(length(s))\n");
        Run crlf = run("use DLC:string\r\nstr s = \"ab\r\ncd\"\r\nprint(length(s))\r\n");
        check(lf.success && lf.printed == "5\n", "LF literal length: '" + lf.printed + "'");
        check(crlf.success && crlf.printed == "5\n", "CRLF literal length: '" + crlf.printed + "' " + crlf.error.substr(0, 100));
    }

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
