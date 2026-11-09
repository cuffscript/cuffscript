// Tests for the error report: the headline, the source line with its "^" marks and the message beside them, and
// which text that line is taken from (see common/ErrorFormat.h and common/SourceText.h).
//
// What is pinned down here:
//  - the marks are placed by display width (tabs kept, Hangul two columns), not by byte count, and cover the
//    whole token the error is about;
//  - an incomplete statement is reported on its own line, and an unclosed block at the end of the code;
//  - a byte order mark at the start of the file changes nothing;
//  - an error that comes from an imported module, or from a function an earlier piece of a Session declared,
//    is drawn against that text and not against whatever happened to be running;
//  - for any error, the reported line and column agree with the offset the snippet is drawn from.
#include "engine/CuffEngine.h"
#include <cstdint>
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

    bool contains(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

    CuffEngine::Result runSource(const std::string &src)
    {
        return CuffEngine::execute(src, ".", CuffEngine::Options());
    }

    // Whether the report draws this source line, numbered, with these marks (the padding and the "^"s) under it:
    //
    //       |
    //     3 | print(a / b)
    //       |         ^ division by zero
    bool hasSnippet(const std::string &error, const std::string &sourceLine, const std::string &marks)
    {
        const std::string head = " | " + sourceLine + "\n";
        for (size_t at = error.find(head); at != std::string::npos; at = error.find(head, at + 1))
        {
            const size_t lineStart = error.rfind('\n', at) == std::string::npos ? 0 : error.rfind('\n', at) + 1;
            const std::string under = std::string(at - lineStart, ' ') + " | " + marks;
            if (error.compare(at + head.size(), under.size(), under) == 0)
                return true;
        }
        return false;
    }

    // Asserts the error output draws this source line with these marks under it.
    void expectSnippet(const std::string &name, const std::string &src,
                       const std::string &expectedSourceLine, const std::string &expectedMarks)
    {
        auto r = runSource(src);
        check(!r.success && hasSnippet(r.error, expectedSourceLine, expectedMarks),
              name + ": expected to find:\n" + expectedSourceLine + "\n" + expectedMarks + "\ngot:\n" + r.error);
    }

    // The line and column (counted in characters) the offset really is at: what a SourceLocation must agree with.
    void positionOf(const std::string &src, size_t offset, int &line, int &column)
    {
        const bool bom = src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
                         static_cast<unsigned char>(src[1]) == 0xBB && static_cast<unsigned char>(src[2]) == 0xBF;
        line = 1;
        column = 1;
        for (size_t i = bom ? 3 : 0; i < offset && i < src.size(); ++i)
        {
            if (src[i] == '\n')
            {
                ++line;
                column = 1;
            }
            else if ((static_cast<unsigned char>(src[i]) & 0xC0) != 0x80)
                ++column;
        }
    }

    // Parses and runs `src` directly (so the CuffError itself can be inspected) and checks that wherever it
    // fails, the location's line and column are the ones its offset is at.
    void checkLocationAgrees(const std::string &name, const std::string &src)
    {
        cuff::Interpreter::Config cfg;
        cfg.maxSteps = 20000;
        cfg.timeoutMs = 2000;
        cfg.networkEnabled = false;
        cfg.filesystemEnabled = false;
        try
        {
            auto program = cuff::parseProgram(cuff::makeSourceRef(src));
            cuff::Interpreter interp(cfg);
            interp.run(*program, ".");
            ++pass;
        }
        catch (const cuff::CuffError &e)
        {
            int line = 0, column = 0;
            positionOf(src, static_cast<size_t>(e.location.offset), line, column);
            check(line == e.location.line && column == e.location.column,
                  name + ": reported " + e.location.toString() + " but offset " + std::to_string(e.location.offset) +
                      " is line " + std::to_string(line) + ", column " + std::to_string(column) + "\n" + e.what());
        }
    }

    // A directory holding the files a test needs, removed afterwards.
    struct TempDir
    {
        std::filesystem::path path;
        TempDir()
        {
            path = std::filesystem::temp_directory_path() / ("cuff_snippet_test_" + std::to_string(std::rand()));
            std::filesystem::create_directories(path / "lib");
        }
        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
        void write(const std::string &rel, const std::string &text) const
        {
            std::ofstream(path / rel, std::ios::binary) << text;
        }
    };
}

int main()
{
    // ---- placement of the caret ---------------------------------------------------------------------------

    // Plain ASCII: the mark lands directly under the offending ')'.
    expectSnippet("ascii caret", "number x = 5\nprint(x +)\n",
                  "print(x +)", "         ^");

    // Hangul takes two terminal columns per character, so the caret after a two-character string literal sits
    // four columns (not two, and not six bytes) to the right of where the quotes begin.
    expectSnippet("wide characters", "print(\"\xEC\x95\x88\xEB\x85\x95\" +)\n",
                  "print(\"\xEC\x95\x88\xEB\x85\x95\" +)", "              ^");

    // Zero-width combining marks add no column: "e" + U+0301 draws as one character.
    expectSnippet("combining mark", "print(\"e\xCC\x81\" +)\n",
                  "print(\"e\xCC\x81\" +)", "           ^");

    // A tab in front of the error is copied into the caret line, so the two stay aligned whatever the tab stops.
    expectSnippet("tab indentation", "if 1 is 1 do:\n\tprint(1/0)\nend\n",
                  "\tprint(1/0)", "\t       ^");

    // Runtime errors get a snippet too, not just parse errors.
    expectSnippet("runtime error caret", "number a = 5\nnumber b = 0\nprint(a / b)\n",
                  "print(a / b)", "        ^");

    // An error inside an f-string's {...} is reported where it really is: on the f-string's own line, and at the
    // character inside the braces, not at line 1 of the fragment it was parsed from and not at the start of the
    // literal. (A call is reported at its name, as it is outside an f-string, and the marks cover the whole name.)
    expectSnippet("f-string runtime error", "print(\"ok\")\nprint(f\"v: {nope(1)}\")\n",
                  "print(f\"v: {nope(1)}\")", "            ^^^^");
    expectSnippet("f-string syntax error", "print(\"ok\")\nprint(f\"v: {1 +}\")\n",
                  "print(f\"v: {1 +}\")", "               ^");
    expectSnippet("f-string, same error outside the f-string", "print(nope(1))\n", "print(nope(1))", "      ^^^^");

    // The case that was reported: a call inside an f-string that fails when its argument is checked.
    {
        auto r = runSource("func fib(str n) do:\n    return n\nend\nprint(f\"fib(10) = {fib(10)}\")\n");
        check(!r.success && contains(r.error, "[E4-030] Runtime Error at line 4, column 20") &&
                  hasSnippet(r.error, "print(f\"fib(10) = {fib(10)}\")", std::string(19, ' ') + "^^^"),
              "a failing call inside an f-string is reported at the call:\n" + r.error);
    }

    // Escapes before and inside the braces shift the source but not the result: the position is still the one in
    // the file, where "\t" and "\"" take two columns each.
    expectSnippet("f-string after escapes", "print(f\"a\\tb \\\"q\\\" {1 / 0}\")\n",
                  "print(f\"a\\tb \\\"q\\\" {1 / 0}\")", "                      ^");
    expectSnippet("f-string with an escape inside the braces",
                  "map m = {\"k\": 1}\nprint(f\"{m[\\\"k\\\"] / 0}\")\n",
                  "print(f\"{m[\\\"k\\\"] / 0}\")", "                  ^");

    // Wide characters before the braces: the headline counts characters, the caret counts columns.
    {
        auto r = runSource("print(f\"\xED\x95\x9C\xEA\xB8\x80 {1 / 0}\")\n");
        check(!r.success && contains(r.error, "line 1, column 15") &&
                  hasSnippet(r.error, "print(f\"\xED\x95\x9C\xEA\xB8\x80 {1 / 0}\")", std::string(16, ' ') + "^"),
              "f-string after Hangul:\n" + r.error);
    }

    // A literal that runs over several lines: the expression is on the line it is written on.
    expectSnippet("f-string over two lines", "print(f\"first line\nsecond {1 / 0}\")\n", "second {1 / 0}\")", "          ^");
    {
        auto r = runSource("print(f\"first line\nsecond {1 / 0}\")\n");
        check(contains(r.error, "line 2, column 11"), "f-string over two lines, position:\n" + r.error);
    }

    // With no program text to measure against (a parser built without a SourceRef), the position falls back to the
    // start of the literal, which is coarse but never wrong about the line.
    {
        const std::string src = "print(f\"v: {nope(1)}\")\n";
        try
        {
            cuff::Tokenizer tokenizer(src);
            cuff::Lexer lexer(tokenizer.tokenize());
            cuff::Parser parser(lexer.lex());
            auto program = parser.parse();
            cuff::Interpreter interp{cuff::EngineOptions()};
            interp.run(*program, ".");
            check(false, "the program should fail");
        }
        catch (const cuff::CuffError &e)
        {
            check(e.location.line == 1 && e.location.column == 7, "without source text: the literal's start, got " + e.location.toString());
        }
    }

    // Text left over after an f-string expression is an error, not silently dropped.
    {
        auto r = runSource("print(f\"v: {1 2}\")\n");
        check(!r.success && contains(r.error, "[E2-001]") && contains(r.error, "after the expression"),
              "f-string with trailing text is rejected:\n" + r.error);
    }

    // An error on the very first character of the file (offset 0) must show the whole line, not drop
    // its first character.
    expectSnippet("error at offset 0", "print(nothing)\n",
                  "print(nothing)", "      ^^^^^^^");
    expectSnippet("a statement that fails on its first character", "nothing = 1\n",
                  "nothing = 1", "^^^^^^^");

    // A non-ASCII character where a token must start is reported whole, so the message stays valid UTF-8.
    {
        auto r = runSource("number \xEB\xB3\x80\xEC\x88\x98 = 1\n");
        check(!r.success && contains(r.error, "Unexpected character '\xEB\xB3\x80'"),
              "unexpected multibyte character is shown whole:\n" + r.error);
    }

    // Columns count characters, so the headline agrees with where the caret is drawn.
    {
        auto r = runSource("str s = \"\xED\x95\x9C\xEA\xB8\x80\" + 5\n");
        check(!r.success && contains(r.error, "column 14"), "column is counted in characters:\n" + r.error);
    }

    // ---- the shape of the report ------------------------------------------------------------------------------

    {
        // The headline names the place, the message sits beside the marks, the hint follows after a blank line.
        auto r = runSource("number x = 1\nif x is 1 do:\nprint(x)\nend\n");
        check(!r.success &&
                  r.error == "[E2-001] Syntax Error at line 3, column 1\n"
                             "  |\n"
                             "3 | print(x)\n"
                             "  | ^^^^^ expected the lines of the block to be indented (got 'print')\n"
                             "\n"
                             "  hint: indent the body one level deeper than the line that ends with 'do:'",
              "the report has the headline, the line, the marks with the message, and the hint:\n" + r.error);
    }
    {
        // The line number sets the width of the gutter.
        std::string src;
        for (int i = 0; i < 11; ++i)
            src += "number n" + std::to_string(i) + " = " + std::to_string(i) + "\n";
        auto r = runSource(src + "print(\"abc\" + 12.5)\n");
        check(!r.success && contains(r.error, "\n   |\n12 | print(\"abc\" + 12.5)\n   | ") &&
                  hasSnippet(r.error, "print(\"abc\" + 12.5)", "            ^"),
              "a two-digit line number widens the gutter:\n" + r.error);
    }
    expectSnippet("the marks cover the whole name of an unknown function", "print(undefined_function(1))\n",
                  "print(undefined_function(1))", "      ^^^^^^^^^^^^^^^^^^");
    expectSnippet("the marks cover a whole method name", "str s = \"a\"\nprint(s.nomethod())\n",
                  "print(s.nomethod())", "        ^^^^^^^^");
    expectSnippet("an unknown class is reported at its name", "numbr x = 1\n", "numbr x = 1", "^^^^^");
    {
        // An error with no source to draw keeps the message on the headline.
        cuff::CuffError e(cuff::ErrorCode::DivisionByZero, "division by zero", cuff::SourceLocation(3, 9, -1));
        check(cuff::renderErrorWithSnippet(e, "") == "[E4-006] Runtime Error at line 3, column 9: division by zero",
              "without a source line the message stays on the headline: " + cuff::renderErrorWithSnippet(e, ""));
    }

    // ---- which line an error is on --------------------------------------------------------------------------

    // The statement that is incomplete is the one reported, not the line after it.
    expectSnippet("unfinished statement", "number x =\nprint(1)\n", "number x =", "          ^");
    expectSnippet("unfinished statement before blank lines", "number x = 1 +\n\n\nprint(2)\n",
                  "number x = 1 +", "              ^");
    expectSnippet("unclosed parenthesis", "print(1)\nnumber y = (1 + 2\nprint(3)\n",
                  "number y = (1 + 2", "                 ^");
    expectSnippet("unfinished statement, CRLF", "number x = 1\r\nnumber y =\r\nprint(2)\r\n",
                  "number y =", "          ^");

    // A block with no 'end' is reported where the code stops, and says where the block was opened.
    {
        auto r = runSource("number x = 1\nif x is 1 do:\n    print(x)\n");
        check(!r.success && contains(r.error, "at line 3") && contains(r.error, " |     print(x)\n") &&
                  contains(r.error, "hint: the block opened at line 2, column 1 has no matching 'end'"),
              "missing 'end' is reported at the last line, naming the block's start:\n" + r.error);
    }
    {
        auto r = runSource("loop repeat number i = 1 ~ 3 do:\n    print(i)\n\n\n");
        check(!r.success && contains(r.error, "opened at line 1"),
              "missing 'end' with trailing blank lines:\n" + r.error);
    }

    // ---- byte order mark, line endings ----------------------------------------------------------------------

    {
        auto r = runSource("\xEF\xBB\xBF" "print(1)\n");
        check(r.success, "a byte order mark at the start of the file is ignored:\n" + r.error);
    }
    expectSnippet("error after a byte order mark", "\xEF\xBB\xBF" "print(1/0)\n", "print(1/0)", "       ^");
    {
        auto r = runSource("\xEF\xBB\xBF" "print(1/0)\n");
        check(contains(r.error, "line 1, column 8"), "a byte order mark does not shift the column:\n" + r.error);
    }
    {
        auto r = runSource("number a = 1\r\nprint(a / 0)\r\n");
        check(!r.success && hasSnippet(r.error, "print(a / 0)", "        ^") && !contains(r.error, "\r"),
              "CRLF source: the snippet has no carriage return:\n" + r.error);
    }

    // ---- which text the line is taken from ------------------------------------------------------------------

    {
        TempDir dir;
        dir.write("lib/boom.cuff", "func boom(any n) do:\n    number pad = 1\n    return n / 0\nend\n");
        const std::string main = "number a = 1\nnumber b = 2\nuse boom from ./lib\nprint(boom(5))\n";
        auto r = CuffEngine::execute(main, dir.path.string());
        check(!r.success && contains(r.error, "Runtime Error in './lib/boom.cuff' at line 3, column 14") &&
                  contains(r.error, " |     return n / 0\n"),
              "an error inside a module's function is drawn from the module:\n" + r.error);
        check(!contains(r.error, "print(boom(5))"), "...and not from the importing file:\n" + r.error);

        dir.write("lib/top.cuff", "note: one\nnote: two\nnumber t = 1 / 0\n");
        r = CuffEngine::execute("use top from ./lib\n", dir.path.string());
        check(!r.success && contains(r.error, "in './lib/top.cuff' at line 3") && contains(r.error, " | number t = 1 / 0\n"),
              "an error in a module's top-level code is drawn from the module:\n" + r.error);

        dir.write("lib/bad.cuff", "number z = 1 +\nprint(1)\n");
        r = CuffEngine::execute("use bad from ./lib\n", dir.path.string());
        check(!r.success && contains(r.error, "[E5-002]") && hasSnippet(r.error, "use bad from ./lib", "^^^") &&
                  contains(r.error, "in './lib/bad.cuff' [E2-001] at line 1, column 15:") &&
                  contains(r.error, " | number z = 1 +\n"),
              "a module that does not parse shows the use statement and the module's own line:\n" + r.error);
    }

    // A function declared by an earlier piece of a Session fails while a later piece runs: it is drawn from the
    // piece that declared it, which is what the interactive shell relies on.
    {
        cuff::Session session;
        auto first = session.run("func half(any n) do:\n    number pad = 1\n    return n / 0\nend\n");
        check(first.success, "declaring the function works:\n" + first.error);
        auto second = session.run("print(half(2))\n");
        check(!second.success && contains(second.error, "at line 3, column 14") &&
                  contains(second.error, " |     return n / 0\n") && !contains(second.error, "print(half(2))"),
              "an error in a function from an earlier entry is drawn from that entry:\n" + second.error);
        auto third = session.run("print(1 + 1)\n");
        check(third.success, "the session is still usable after an error:\n" + third.error);
    }

    // ---- a failed async job drops the rest of the queue ------------------------------------------------------

    {
        cuff::Session session;
        auto first = session.run("async func a() do:\n    print(\"A\")\nend\n"
                                 "async func b() do:\n    number z = 1 / 0\nend\n"
                                 "async func c() do:\n    print(\"C ran\")\nend\n"
                                 "a()\nb()\nc()\n");
        check(!first.success, "the failing job reports its error");
        auto second = session.run("print(\"next\")\n");
        check(second.success, "the next entry runs:\n" + second.error);
    }

    // ---- line and column always agree with the offset -------------------------------------------------------

    const char *corpus[] = {
        "number x = 1\nprint(x)\n",
        "str s = \"\xED\x95\x9C\xEA\xB8\x80\xED\x95\x9C\xEA\xB8\x80\"\nprint(s + 1)\n",
        "if 1 is 1 do:\n\tprint(1/0)\nend\n",
        "list a = [1, 2, 3]\nloop repeat number i = 1 ~ 3 do:\n    print(a[10])\nend\n",
        "note: a comment\nnote: another\nprint(undefined_name)\n",
        "number n = 3\nprint(f\"v {n} and {n / 0}\")\n",
        "func f(any a, any b) do:\n    return a + b\nend\nprint(f(1))\n",
        "map m = {\"a\": 1}\nprint(m[\"b\"])\n",
        "print(\"line one\nline two\" + 1)\n",
        ("\xEF\xBB\xBF" "number x = 2\r\nprint(x / 0)\r\n"),
        "number y = 1 +\n",
        "number n = 3\nprint(f\"tab\\t and \\\"quote\\\" {n / 0}\")\n",
        "str s = \"\xED\x95\x9C\xEA\xB8\x80\"\nprint(f\"\xED\x95\x9C\xEA\xB8\x80 {s + 1} {s}\")\n",
        "print(f\"first\nsecond {1 +}\")\n",
        "map m = {\"k\": 1}\nprint(f\"{m[\\\"z\\\"]} {m[\\\"k\\\"]}\")\n",
        "print(f\"{{literal}} {nope(2)} done\")\n",
        "print(f\"{1 2}\")\n",
        "print(f\"{\")\n",
        "loop repeat number i = 1 ~ 2 do:\n    print(i)\n",
    };
    uint32_t rng = 12345;
    auto next = [&]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    for (const char *base : corpus)
    {
        const std::string src = base;
        checkLocationAgrees("corpus", src);
        for (int trial = 0; trial < 60; ++trial)
        {
            std::string mutated = src;
            const size_t pos = next() % (mutated.size() + 1);
            switch (next() % 3)
            {
            case 0:
                if (pos < mutated.size())
                    mutated.erase(pos, 1 + next() % 3);
                break;
            case 1:
                mutated.insert(pos, std::string(1, "()[]{}\"+-/ \n\t:,'#"[next() % 17]));
                break;
            default:
                if (pos < mutated.size())
                    mutated[pos] = "()[]{}\"+-/ \n\t:,'#"[next() % 17];
                break;
            }
            checkLocationAgrees("mutated", mutated);
        }
    }

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
