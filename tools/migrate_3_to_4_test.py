#!/usr/bin/env python3
"""Tests for tools/migrate_3_to_4.py:  python3 tools/migrate_3_to_4_test.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import migrate_3_to_4 as mig  # noqa: E402


def migrate(src):
    report = mig.Report()
    return mig.migrate_source(src, "t.cuff", report), report


class Rewrites(unittest.TestCase):
    def check(self, before, after):
        out, _ = migrate(before)
        self.assertEqual(out, after)

    def test_function_declarations(self):
        self.check("set returnable func f(a) do:\n    return a\nend\n", "func f(a) do:\n    return a\nend\n")
        self.check("set async pure returnable func f() do:\nend\n", "async pure func f() do:\nend\n")
        self.check("    set func g() do:\n    end\n", "    func g() do:\n    end\n")

    def test_global(self):
        self.check("set func f() do:\n    change total to global\nend\n", "func f() do:\n    global total\nend\n")

    def test_replace_statement(self):
        self.check("replace items[2] to 5\n", "change items[2] to 5\n")

    def test_text_constructs(self):
        self.check('set str a to find "[num]+" from text\n', 'set str a to find(text, p"[num]+")\n')
        self.check('set list a to find "[num]" from text g\n', 'set list a to find_all(text, p"[num]")\n')
        self.check('set match m to match text from "(a)(b)"\n', 'set match m to match(text, p"(a)(b)")\n')
        self.check('set str r to replace "a" in text to "b"\n', 'set str r to replace(text, p"a", "b")\n')
        self.check('set str r to replace "a" in text to "b" g\n', 'set str r to replace_all(text, p"a", "b")\n')
        self.check('set list p to split text by "[sp]+"\n', 'set list p to split(text, p"[sp]+")\n')
        self.check('set number n to count "a" in text i\n', 'set number n to count(text, p"a"i)\n')

    def test_dynamic_patterns_are_wrapped(self):
        self.check("set str a to find rule from text\n", "set str a to find(text, pattern(rule))\n")
        self.check("set str a to find a + b from text\n", "set str a to find(text, pattern(a + b))\n")

    def test_operands_follow_the_old_grammar(self):
        self.check('set str w to match text from "a" + "b"\n', 'set str w to match(text, p"a") + "b"\n')
        self.check('set number k to count "a" in t + u\n', 'set number k to count(t + u, p"a")\n')

    def test_backslashes_and_quotes(self):
        self.check('set str q to replace "\\"" in t to "x" g\n', 'set str q to replace_all(t, p"\\"", "x")\n')
        self.check('set str q to find "a\\\\b" from t\n', 'set str q to find(t, p"a\\b")\n')

    def test_is_against_a_string_literal(self):
        self.check('if code is "[up]2-[num]4" do:\nend\n', 'if code matches p"[up]2-[num]4" do:\nend\n')
        self.check('if answer IS "yes" do:\nend\n', 'if answer matches p"yes"i do:\nend\n')
        self.check('if a is not "[num]+" do:\nend\n', 'if !a matches p"[num]+" do:\nend\n')
        self.check('if a is "plain" do:\nend\n', 'if a is "plain" do:\nend\n')
        self.check('if a is not "plain" do:\nend\n', 'if a is not "plain" do:\nend\n')
        self.check('if n is 3 do:\nend\n', 'if n is 3 do:\nend\n')

    def test_logical_operators(self):
        self.check("set boolean b to a && c || d\n", "set boolean b to a and c or d\n")

    def test_fstrings(self):
        self.check("show f\"{find '[num]' from t}\"\n", "show f\"{find(t, pattern('[num]'))}\"\n")

    def test_comments_and_strings_are_untouched(self):
        src = 'note: set returnable func f() do:\nprint("set func find x from y")\n'
        self.check(src, src)

    def test_line_endings_and_layout(self):
        self.check("if x do:\n\tset returnable func f() do:\n\tend\nend\n", "if x do:\n\tfunc f() do:\n\tend\nend\n")


class Reports(unittest.TestCase):
    def test_reserved_words_are_renamed_and_reported(self):
        out, report = migrate("set number each to 1\nprint(each + 1)\n")
        self.assertEqual(out, "set number each_ to 1\nprint(each_ + 1)\n")
        self.assertEqual(len(report.renamed), 2)

    def test_collection_target_is_reported(self):
        out, report = migrate("add 5 to grid[1]\nremove 1 from grid[1]\nadd 5 to grid\n")
        self.assertEqual(out, "add 5 to grid[1]\nremove 1 from grid[1]\nadd 5 to grid\n")
        self.assertEqual(len(report.manual), 2)


class Idempotence(unittest.TestCase):
    def test_current_syntax_is_left_alone(self):
        src = (
            "class Player do:\n    set number x to 0\n    func go() do:\n        change self.x to self.x + 1\n    end\nend\n"
            'loop each item in [1] do:\n    skip\nend\nset pattern p to p"[num]+"\nprint("1" matches p)\n'
        )
        out, report = migrate(src)
        self.assertEqual(out, src)
        self.assertEqual(report.current, 1)

    def test_a_migrated_source_migrates_to_itself(self):
        before = 'set returnable func f(t) do:\n    return find "[num]" from t g\nend\nif f("a1") is "x[num]" do:\nend\n'
        once, _ = migrate(before)
        twice, _ = migrate(once)
        self.assertEqual(once, twice)

    def test_module_paths_are_not_a_sign_of_new_syntax(self):
        out, report = migrate("use helper from ./lib\nset returnable func f() do:\nend\n")
        self.assertEqual(out, "use helper from ./lib\nfunc f() do:\nend\n")
        self.assertEqual(report.current, 0)


class Markdown(unittest.TestCase):
    def test_only_cuff_blocks_change(self):
        text = "Text with `find x from y`.\n\n```cuff\nset returnable func f() do:\nend\n```\n\n```text\nset returnable func f() do:\n```\n"
        out = mig.migrate_markdown(text, "t.md", mig.Report())
        self.assertEqual(out, "Text with `find x from y`.\n\n```cuff\nfunc f() do:\nend\n```\n\n```text\nset returnable func f() do:\n```\n")


if __name__ == "__main__":
    unittest.main()
