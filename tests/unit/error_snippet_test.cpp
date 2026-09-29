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

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
