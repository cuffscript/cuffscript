// A program that hosts the engine may set the locale of the process (setlocale(LC_ALL, "") is common), and the C
// library then reads and writes numbers and changes letter case by that locale. A script must mean the same in every
// locale: these run scripts under a locale that writes the decimal point as ',' and under one in which the upper
// case of 'i' is not 'I', where the machine has them.
#include "engine/CuffEngine.h"
#include <cctype>
#include <clocale>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>

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

    std::string run(const std::string &source)
    {
        std::ostringstream out;
        std::streambuf *old = std::cout.rdbuf(out.rdbuf());
        cuff::Session session;
        const auto outcome = session.run(source);
        std::cout.rdbuf(old);
        return outcome.success ? out.str() : "ERROR " + outcome.error;
    }

    // The first of `names` that the machine has and that passes `fits`, set for the whole process; empty when none is.
    template <class Fits>
    std::string useLocale(std::initializer_list<const char *> names, Fits fits)
    {
        for (const char *name : names)
            if (std::setlocale(LC_ALL, name) && fits())
                return name;
        std::setlocale(LC_ALL, "C");
        return std::string();
    }

    bool writesComma()
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.1f", 0.5);
        return std::string(buf) == "0,5";
    }

    void numbers()
    {
        const std::string name = useLocale({"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR",
                                            "German_Germany.1252", "German", "de-DE"},
                                           writesComma);
        if (name.empty())
        {
            std::cout << "  (no locale that writes the decimal point as ',' is installed: number checks skipped)\n";
            return;
        }
        const auto same = [&](const std::string &source, const std::string &expected, const std::string &what)
        {
            const std::string got = run(source);
            check(got == expected, what + " under " + name + ": got " + got);
        };
        same("print(1.5 + 1)\n", "2.5\n", "a number literal with a fraction is read with its fraction");
        same("print(1.5 is 1)\n", "false\n", "1.5 is not 1");
        same("print(0.25, 1 / 3, 10 ** 20, 0 - 2.5)\n", "0.25 0.333333333333333 1e+20 -2.5\n", "numbers are written with '.'");
        same("print(f\"{1 / 4}\")\nprint(to_str(0.5))\n", "0.25\n0.5\n", "f-strings and to_str write '.'");
        same("use DLC:convert\nprint(to_number(\"2.75\"), to_number(\" -1.5e2 \"), to_number(\"3.\"))\n", "2.75 -150 3\n",
             "to_number reads '.'");
        same("use DLC:convert\nnumber n = to_number(\"2,75\") or_else as Error e do:\n    print(e.code)\n    n = 0\nend\n",
             "E4-025\n", "to_number does not take the locale's ',' for a decimal point");
        same("use DLC:json\nprint(from_json(\"[3.5, -0.125e2]\"), to_json(4.5), to_json([0.1]))\n", "[3.5, -12.5] 4.5 [0.1]\n",
             "JSON reads and writes '.'");
        same("use DLC:math\nprint(math_round(1.25, 1), math_sqrt(2.25))\n", "1.3 1.5\n", "library results are written with '.'");
        std::setlocale(LC_ALL, "C");
    }

    bool upperOfIIsNotI()
    {
        return std::toupper(static_cast<unsigned char>('i')) != 'I';
    }

    void letterCase()
    {
        const std::string name = useLocale({"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "Turkish_Turkey.1254", "Turkish", "tr-TR"},
                                           upperOfIIsNotI);
        if (name.empty())
        {
            std::cout << "  (no locale in which toupper('i') is not 'I' is installed: letter case checks skipped)\n";
            return;
        }
        const std::string got = run("use DLC:string\nprint(str_upper(\"istanbul\"), str_lower(\"ISTANBUL\"), \"title\".upper())\n"
                                    "print(\"Tiny\" matches p\"[word]+\", str_upper(\"\xC3\xA9t\xC3\xA9\"))\n");
        check(got == "ISTANBUL istanbul TITLE\ntrue \xC3\x89T\xC3\x89\n", "letter case is the same under " + name + ": got " + got);
        std::setlocale(LC_ALL, "C");
    }
}

int main()
{
    numbers();
    letterCase();
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
