// Regression tests for the "^" caret snippet CuffEngine splices into error
// messages (see buildCaretSnippet()/renderErrorWithSnippet() in CuffEngine.h).
// The snippet's horizontal offset is measured in *codepoints*, not bytes, so
// these specifically probe that multibyte UTF-8 content earlier on the same
// line doesn't push the caret out of alignment.
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

    CuffEngine::Result runSource(const std::string &src)
    {
        return CuffEngine::execute(src, ".", CuffEngine::Options());
    }

    // Asserts the error output contains exactly this source line followed by
    // exactly this caret line (both on their own, "    "-indented lines).
    void expectSnippet(const std::string &name, const std::string &src,
                       const std::string &expectedSourceLine, const std::string &expectedCaretLine)
    {
        auto r = runSource(src);
        std::string needle = "    " + expectedSourceLine + "\n    " + expectedCaretLine;
        check(!r.success && r.error.find(needle) != std::string::npos,
              name + ": expected to find:\n" + needle + "\ngot:\n" + r.error);
    }
}

int main()
{
    // Plain ASCII: caret lands directly under the offending ')'.
    expectSnippet("ascii caret", "set number x to 5\nprint(x +)\n",
                  "print(x +)", "         ^");

    // A multibyte (3-byte-per-codepoint) Korean string literal precedes the
    // error on the same line -- the caret must still land under ')', counted
    // in codepoints, not bytes (which would push it well past ')' instead,
    // since "ìë" is 2 codepoints but 6 bytes).
    expectSnippet("utf8 caret alignment", "print(\"\xEC\x95\x88\xEB\x85\x95\" +)\n",
                  "print(\"\xEC\x95\x88\xEB\x85\x95\" +)", "            ^");

    // Runtime errors get a snippet too, not just parse errors.
    expectSnippet("runtime error caret", "set number a to 5\nset number b to 0\nprint(a / b)\n",
                  "print(a / b)", "        ^");

    // An error inside an f-string's {...} must point at the f-string's own line
    // in the file, not at line 1 of the fragment it was parsed from.
    expectSnippet("f-string runtime error line", "print(\"ok\")\nprint(f\"v: {nope(1)}\")\n",
                  "print(f\"v: {nope(1)}\")", "      ^");
    expectSnippet("f-string syntax error line", "print(\"ok\")\nprint(f\"v: {1 +}\")\n",
                  "print(f\"v: {1 +}\")", "      ^");

    // An error on the very first character of the file (offset 0) must show the whole line, not drop
    // its first character.
    expectSnippet("error at offset 0", "change nothing to 1\n",
                  "change nothing to 1", "^");

    // A non-ASCII character where a token must start is reported whole, so the message stays valid UTF-8.
    {
        auto r = runSource("set number \xEB\xB3\x80\xEC\x88\x98 to 1\n");
        check(!r.success && r.error.find("Unexpected character '\xEB\xB3\x80'") != std::string::npos,
              "unexpected multibyte character is shown whole:\n" + r.error);
    }

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
