// The built-in functions that touch the process's streams or its shared state: input() and DLC:random.
#include "engine/CuffEngine.h"
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

    struct Outcome
    {
        bool success;
        std::string out;
        std::string error;
    };

    // Runs a program with `input` as standard input and returns what it printed.
    Outcome run(const std::string &source, const std::string &input = std::string())
    {
        std::istringstream in(input);
        std::ostringstream out;
        std::streambuf *oldIn = std::cin.rdbuf(in.rdbuf());
        std::streambuf *oldOut = std::cout.rdbuf(out.rdbuf());
        cuff::Session session;
        const auto outcome = session.run(source);
        std::cout.rdbuf(oldOut);
        std::cin.rdbuf(oldIn);
        return {outcome.success, out.str(), outcome.error};
    }

    // What input() returns for `input`, shown as the program prints it: the text between brackets, or "<eof>".
    std::string firstLine(const std::string &input)
    {
        const auto r = run("set str s to input()\nprint(\"[\" + s + \"]\")\n", input);
        return r.success ? r.out : "ERROR " + r.error;
    }

    void inputTests()
    {
        check(firstLine("abc\n") == "[abc]\n", "input() returns the line without its line feed");
        check(firstLine("abc\r\n") == "[abc]\n", "input() drops a carriage return that precedes the line feed");
        check(firstLine("abc") == "[abc]\n", "input() returns a last line that has no line ending");
        check(firstLine("abc\r") == "[abc]\n", "input() drops a carriage return that ends the input");
        check(firstLine("a\rb\r\n") == "[a\rb]\n", "input() keeps a carriage return inside the line");
        check(firstLine("\r\n") == "[]\n", "input() reads an empty CRLF line as the empty string");
        check(firstLine("\n") == "[]\n", "input() reads an empty line as the empty string");
        check(firstLine("") == "[]\n", "input() at the end of the input returns the empty string");
        check(firstLine("first\r\nsecond\r\n") == "[first]\n", "input() reads only the first line");

        const auto two = run("print(input() + \"|\" + input() + \"|\" + input() + \"|\")\n", "x\r\ny\r\n");
        check(two.success && two.out == "x|y||\n", "successive input() calls read successive lines, then the empty string");

        const auto prompt = run("set str s to input(\"name? \")\nprint(s)\n", "Ann\r\n");
        check(prompt.success && prompt.out == "name? Ann\n", "input(prompt) writes the prompt, then reads the line");

        const auto bad = run("print(input(5))\n");
        check(!bad.success && bad.error.find("E4-005") != std::string::npos, "input() with a prompt that is not a string is E4-005");
    }

    void randomTests()
    {
        const std::string seeded = "use DLC:random\nuse DLC:list\n"
                                   "random_seed(12345)\n"
                                   "print(random_int(1, 1000), random_int(-5, 5), random_float(), random_choice([10, 20, 30]), random_shuffle([1, 2, 3, 4, 5]))\n";
        const auto a = run(seeded);
        const auto b = run(seeded);
        check(a.success && !a.out.empty() && a.out == b.out, "the same seed gives the same numbers");

        const auto other = run("use DLC:random\nrandom_seed(54321)\nprint(random_int(1, 1000000), random_int(1, 1000000))\n");
        const auto same = run("use DLC:random\nrandom_seed(12345)\nprint(random_int(1, 1000000), random_int(1, 1000000))\n");
        check(other.success && same.success && other.out != same.out, "different seeds give different numbers");

        const auto range = run("use DLC:random\nset boolean ok to true\n"
                               "loop repeat i to 1 ~ 2000 do:\n"
                               "    set number n to random_int(-3, 3)\n"
                               "    set number f to random_float()\n"
                               "    if n < -3 or n > 3 or f < 0 or f >= 1 do:\n"
                               "        change ok to false\n"
                               "    end\n"
                               "end\n"
                               "print(ok, random_int(7, 7))\n");
        check(range.success && range.out == "true 7\n", "random_int stays within its bounds, both included, and random_float within [0, 1)");

        const auto errors = run("use DLC:random\nprint(random_int(5, 1))\n");
        check(!errors.success && errors.error.find("E4-025") != std::string::npos, "random_int with low above high is E4-025");
        const auto empty = run("use DLC:random\nprint(random_choice([]))\n");
        check(!empty.success && empty.error.find("E4-025") != std::string::npos, "random_choice of an empty list is E4-025");

        // One generator per process: seeding in one session decides what another session draws next.
        cuff::Session first, second;
        std::ostringstream sink;
        std::streambuf *oldOut = std::cout.rdbuf(sink.rdbuf());
        first.run("use DLC:random\nrandom_seed(99)\n");
        second.run("use DLC:random\nprint(random_int(1, 1000000))\n");
        first.run("random_seed(99)\n");
        second.run("print(random_int(1, 1000000))\n");
        std::cout.rdbuf(oldOut);
        const std::string drawn = sink.str();
        const size_t newline = drawn.find('\n');
        check(newline != std::string::npos && drawn.substr(0, newline) == drawn.substr(newline + 1, drawn.size() - newline - 2),
              "seeding in one session reseeds the generator every session draws from");
    }
}

int main()
{
    inputTests();
    randomTests();
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
