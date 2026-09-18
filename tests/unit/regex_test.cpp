#include "engine/regex/RegexEngine.h"
#include <iostream>
#include <cassert>

using namespace cuff::regex;
using cuff::SourceLocation;

int pass = 0, fail = 0;
void check(bool cond, const std::string& msg) {
    if (cond) { pass++; }
    else { fail++; std::cout << "FAIL: " << msg << "\n"; }
}

int main() {
    RegexEngine eng;
    SourceLocation loc;

    // full match basics
    {
        auto p = eng.compile("[num]4", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "1234", false, loc, out), "num4 matches 1234");
        check(!eng.fullMatch(p, "123", false, loc, out), "num4 rejects 123");
        check(!eng.fullMatch(p, "12345", false, loc, out), "num4 rejects 12345 (full match)");
    }

    // quantifiers
    {
        auto p = eng.compile("[let]+", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "Hello", false, loc, out), "let+ matches Hello");
        check(!eng.fullMatch(p, "Hello1", false, loc, out), "let+ rejects Hello1");
    }
    {
        auto p = eng.compile("https?", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "http", false, loc, out), "https? matches http");
        check(eng.fullMatch(p, "https", false, loc, out), "https? matches https");
        check(!eng.fullMatch(p, "httpss", false, loc, out), "https? rejects httpss");
    }
    {
        auto p = eng.compile("colou?r", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "color", false, loc, out), "colou?r matches color");
        check(eng.fullMatch(p, "colour", false, loc, out), "colou?r matches colour");
    }
    {
        auto p = eng.compile("(AB)3", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "ABABAB", false, loc, out), "(AB)3 matches ABABAB");
        check(!eng.fullMatch(p, "ABAB", false, loc, out), "(AB)3 rejects ABAB");
    }
    {
        auto p = eng.compile("([num]2-[let]2)+", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "12-ab34-cd", false, loc, out), "([num]2-[let]2)+ matches 12-ab34-cd");
    }

    // ranges
    {
        auto p = eng.compile("[num]2~4", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "12", false, loc, out), "2~4 matches 2 digits");
        check(eng.fullMatch(p, "1234", false, loc, out), "2~4 matches 4 digits");
        check(!eng.fullMatch(p, "1", false, loc, out), "2~4 rejects 1 digit");
        check(!eng.fullMatch(p, "12345", false, loc, out), "2~4 rejects 5 digits");
    }
    {
        auto p = eng.compile("[num]~3", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "", false, loc, out), "~3 matches 0 digits");
        check(eng.fullMatch(p, "123", false, loc, out), "~3 matches 3 digits");
        check(!eng.fullMatch(p, "1234", false, loc, out), "~3 rejects 4 digits");
    }
    {
        auto p = eng.compile("[num]3~", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "12345", false, loc, out), "3~ matches 5 digits");
        check(!eng.fullMatch(p, "12", false, loc, out), "3~ rejects 2 digits");
    }

    // one-of
    {
        auto p = eng.compile("[str]+\\.[one:jpg|png|gif]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "photo.jpg", false, loc, out), "one-of matches photo.jpg");
        check(eng.fullMatch(p, "photo.png", false, loc, out), "one-of matches photo.png");
        check(!eng.fullMatch(p, "photo.bmp", false, loc, out), "one-of rejects photo.bmp");
    }

    // custom sets / negation
    {
        auto p = eng.compile("[a-z]+", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "hello", false, loc, out), "a-z matches hello");
        check(!eng.fullMatch(p, "Hello", false, loc, out), "a-z rejects Hello (case sensitive)");
    }
    {
        auto p = eng.compile("[!num]+", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "abc", false, loc, out), "!num matches abc");
        check(!eng.fullMatch(p, "ab1", false, loc, out), "!num rejects ab1");
    }

    // case-insensitive
    {
        auto p = eng.compile("hello", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "HELLO", true, loc, out), "IS case-insensitive matches HELLO");
        check(!eng.fullMatch(p, "HELLO", false, loc, out), "is case-sensitive rejects HELLO");
    }

    // literal dot (not wildcard)
    {
        auto p = eng.compile("photo.jpg", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "photo.jpg", false, loc, out), "literal dot matches photo.jpg");
        check(!eng.fullMatch(p, "photoXjpg", false, loc, out), "literal dot rejects photoXjpg (dot is NOT wildcard)");
    }

    // groups + named captures via search
    {
        auto p = eng.compile("SN-([num]4)-([num]+)", loc);
        MatchOutcome out;
        bool found = eng.search(p, "Serial: SN-1234-5678 end", 0, Flags{}, loc, out);
        check(found, "group search finds SN pattern");
        if (found) {
            check(out.positional.size() == 2, "2 groups captured");
            check(out.positional[0] == "1234", "group1 = 1234, got " + (out.positional.size()>0?out.positional[0]:"?"));
            check(out.positional[1] == "5678", "group2 = 5678, got " + (out.positional.size()>1?out.positional[1]:"?"));
        }
    }
    {
        auto p = eng.compile("<year:[num]4>-<month:[num]2>-<day:[num]2>", loc);
        MatchOutcome out;
        bool found = eng.search(p, "Date: 2026-03-31 something", 0, Flags{}, loc, out);
        check(found, "named group search finds date");
        if (found) {
            check(out.named["year"] == "2026", "year=2026 got " + out.named["year"]);
            check(out.named["month"] == "03", "month=03 got " + out.named["month"]);
            check(out.named["day"] == "31", "day=31 got " + out.named["day"]);
        }
    }

    // log line example from spec section 36
    {
        auto p = eng.compile("<date:[num]4-[num]2-[num]2> \\[[one:INFO|WARN|ERROR]\\] <ip:[num]1~3\\.[num]1~3\\.[num]1~3\\.[num]1~3> - <msg:[any]+>", loc);
        MatchOutcome out;
        std::string line = "2026-03-31 [ERROR] 192.168.0.1 - DB Connection Lost";
        bool found = eng.search(p, line, 0, Flags{}, loc, out);
        check(found, "log line pattern matches");
        if (found) {
            check(out.named["date"] == "2026-03-31", "date captured: " + out.named["date"]);
            check(out.named["ip"] == "192.168.0.1", "ip captured: " + out.named["ip"]);
            check(out.named["msg"] == "DB Connection Lost", "msg captured: " + out.named["msg"]);
        }
    }

    // find with g flag
    {
        auto p = eng.compile("[num]3", loc);
        auto all = eng.searchAll(p, "T-123 U-456 V-789", Flags::parse("g"), loc);
        check(all.size() == 3, "find g finds 3 matches, got " + std::to_string(all.size()));
    }

    // replace
    {
        auto p = eng.compile("[num]4-[num]4", loc);
        std::string s = eng.replace(p, "phone 010-1234-5678 call", "****-****", Flags{}, loc);
        check(s == "phone 010-****-**** call", "replace works, got: " + s);
    }
    {
        auto p = eng.compile("[num]", loc);
        std::string s = eng.replace(p, "a1b2c3", "X", Flags::parse("g"), loc);
        check(s == "aXbXcX", "replace g works, got: " + s);
    }

    // split
    {
        auto p = eng.compile(",[sp]*", loc);
        auto parts = eng.split(p, "apple, banana,cherry", loc);
        check(parts.size() == 3, "split into 3 parts, got " + std::to_string(parts.size()));
        if (parts.size() == 3) {
            check(parts[0] == "apple", "part0 = apple, got " + parts[0]);
            check(parts[1] == "banana", "part1 = banana, got " + parts[1]);
            check(parts[2] == "cherry", "part2 = cherry, got " + parts[2]);
        }
    }

    // count
    {
        auto p = eng.compile("[num]+", loc);
        size_t c = eng.count(p, "a1 b22 c333", Flags{}, loc);
        check(c == 3, "count = 3, got " + std::to_string(c));
    }

    // email / phone / url presets
    {
        auto p = eng.compile("[str]+@[str]2~10\\.[let]2~5", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "user@example.com", false, loc, out), "custom email-ish pattern matches");
    }
    {
        auto p = eng.compile("[email]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "test.user@example.co.kr", false, loc, out), "[email] preset matches");
        check(!eng.fullMatch(p, "not-an-email", false, loc, out), "[email] preset rejects garbage");
    }
    {
        auto p = eng.compile("[phone]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "010-1234-5678", false, loc, out), "[phone] preset matches mobile");
        check(eng.fullMatch(p, "02-123-4567", false, loc, out), "[phone] preset matches Seoul short");
    }
    {
        auto p = eng.compile("[url]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "https://cufflang.dev", false, loc, out), "[url] preset matches https url");
    }
    {
        auto p = eng.compile("[int]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "-42", false, loc, out), "[int] preset matches -42");
        check(eng.fullMatch(p, "42", false, loc, out), "[int] preset matches 42");
    }
    {
        auto p = eng.compile("[float]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "3.14", false, loc, out), "[float] preset matches 3.14");
        check(eng.fullMatch(p, "-0.05", false, loc, out), "[float] preset matches -0.05");
        check(!eng.fullMatch(p, "42", false, loc, out), "[float] preset rejects plain int");
    }

    // syntax errors
    {
        bool threw = false;
        try { eng.compile("[num", loc); } catch (cuff::RegexSyntaxError&) { threw = true; }
        check(threw, "unclosed bracket throws RegexSyntaxError");
    }
    {
        bool threw = false;
        try { eng.compile("[num]++", loc); } catch (cuff::RegexSyntaxError&) { threw = true; }
        check(threw, "stacked quantifier throws RegexSyntaxError");
    }
    {
        bool threw = false;
        try { eng.compile("[num]5~2", loc); } catch (cuff::RegexSyntaxError&) { threw = true; }
        check(threw, "reversed range throws RegexSyntaxError");
    }
    {
        bool threw = false;
        try { eng.compile("[one :a|b]", loc); } catch (cuff::RegexSyntaxError&) { threw = true; }
        check(threw, "space before colon throws RegexSyntaxError");
    }

    // ReDoS guard (should throw RegexRuntimeError, not hang)
    {
        bool threw = false;
        try {
            auto p = eng.compile("([any]+)+end", loc);
            MatchOutcome out;
            eng.fullMatch(p, std::string(40, 'a'), false, loc, out);
        } catch (cuff::RegexRuntimeError&) { threw = true; }
        check(threw, "catastrophic backtracking pattern is caught by step/time limit");
    }


    // ---- Unicode / codepoint-based matching ----
    {
        auto p = eng.compile("[any]5", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", false, loc, out),
              "[any]5 matches 5 Korean codepoints (15 bytes)");
    }
    {
        auto p = eng.compile("[any]15", loc);
        MatchOutcome out;
        check(!eng.fullMatch(p, "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", false, loc, out),
              "[any]15 rejects 5 Korean codepoints (not byte-based)");
    }
    {
        auto p = eng.compile("\xEC\x95\x88\xEB\x85\x95", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "\xEC\x95\x88\xEB\x85\x95", false, loc, out), "literal multi-byte pattern matches");
        check(!eng.fullMatch(p, "\xEC\x95\x88", false, loc, out), "literal multi-byte pattern rejects prefix");
    }
    {
        auto p = eng.compile("[!num]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "\xEC\x95\x88", false, loc, out), "negated set matches a non-ASCII codepoint");
    }
    {
        auto p = eng.compile("[let]+", loc);
        MatchOutcome out;
        check(!eng.fullMatch(p, "\xEC\x95\x88\xEB\x85\x95", false, loc, out), "[let] stays ASCII-only per spec");
    }
    {
        auto p = eng.compile("[one:\xEC\x82\xAC\xEA\xB3\xBC|\xEB\xB0\xB0]", loc);
        MatchOutcome out;
        check(eng.fullMatch(p, "\xEC\x82\xAC\xEA\xB3\xBC", false, loc, out), "[one:...] multi-byte alternative");
    }
    {
        auto p = eng.compile("[any]", loc);
        auto all = eng.searchAll(p, "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4", Flags::parse("g"), loc);
        check(all.size() == 3, "searchAll over Korean yields 3 codepoints, got " + std::to_string(all.size()));
    }
    {
        auto p = eng.compile("[edge]\xEC\x95\x88\xEB\x85\x95[edge]", loc);
        MatchOutcome out;
        check(eng.search(p, "\xEC\x95\x88\xEB\x85\x95 \xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", 0, Flags{}, loc, out),
              "[edge] treats non-ASCII letters as word characters");
    }

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
