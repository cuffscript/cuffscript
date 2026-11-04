#!/usr/bin/env python3
"""What the tests leave out.

Asks the engine what it contains (the same way tools/docs.py does) and reports what no test touches:

  * error codes     every code the engine can raise must be the expected code of a test in tests/errors/ or
                    examples/error_cases/, or be named in a unit test or an expected output
  * functions       every always-available function and every library function must be called in some test or example
  * methods         every built-in method must be called as `.name(` in some test or example
  * grammar         every production in docs/reference/grammar.md must be listed in the map in tests/README.md, and
                    every area in that map must have its directories in tests/
  * phenomena       every example in docs/guide/phenomena.md must be a test program word for word, with the output the
                    test expects, and every test in tests/cases/phenomena/ must be shown there

Run it from anywhere:   python3 tests/coverage.py            (exit status 1 when something is left out)
                        python3 tests/coverage.py --list     (also prints what is covered, and by which tests)

A gap that cannot be closed (a code that cannot be reached from a script, say) goes into tests/coverage_exceptions.txt
with the reason; the checker then reports it as accepted instead of failing.
"""

import argparse
import importlib.util
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"

spec = importlib.util.spec_from_file_location("cuff_docs", ROOT / "tools" / "docs.py")
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)


def text_of(path):
    return path.read_text(encoding="utf-8", errors="replace")


def script_files():
    """Every program the engine is tested with."""
    out = []
    for base in (TESTS / "cases", TESTS / "errors", ROOT / "examples"):
        out += sorted(base.rglob("*.cuff"))
    return out


def rel(path):
    return path.relative_to(ROOT).as_posix()


def load_exceptions():
    accepted = {}
    path = TESTS / "coverage_exceptions.txt"
    if path.exists():
        for line in text_of(path).splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 2)
            if len(parts) < 3:
                print("coverage_exceptions.txt: a line needs 'kind name reason': " + line)
                sys.exit(2)
            accepted[(parts[0], parts[1])] = parts[2]
    return accepted


def error_code_coverage(records, descriptions):
    tags = {r[2]: r[1] for r in records if r[0] == "error"}  # Name -> tag
    where = defaultdict(list)
    sources = list((TESTS / "errors").rglob("*.expected_code")) + list((ROOT / "examples" / "error_cases").glob("*.expected_code"))
    sources += sorted((TESTS / "unit").glob("*.cpp"))
    sources += sorted((TESTS / "cases").rglob("*.expected")) + sorted((ROOT / "examples").glob("*.expected"))
    for src in sources:
        for tag in set(re.findall(r"E\d-\d{3}", text_of(src))):
            where[tag].append(rel(src))
    result = {}
    for name, _ in descriptions:
        if not docs.error_is_raised(name):
            continue  # a reserved number: nothing raises it
        tag = tags.get(name)
        if tag:
            result[tag] = where.get(tag, [])
    return result


def function_coverage(records, scripts):
    corpus = {p: text_of(p) for p in scripts}
    corpus.update({p: text_of(p) for p in sorted((TESTS / "unit").glob("*.cpp"))})
    names = {}
    for r in records:
        if r[0] == "builtin":
            names[r[1]] = "builtin"
        elif r[0] == "fn":
            names[r[2]] = "DLC:" + r[1]
    result = {}
    for name, origin in sorted(names.items()):
        pattern = re.compile(r"(?<![\w.])" + re.escape(name) + r"\s*\(")
        result[(origin, name)] = [rel(p) for p, t in corpus.items() if pattern.search(t)]
    return result


def method_coverage(records, scripts):
    corpus = {p: text_of(p) for p in scripts}
    result = {}
    for r in records:
        if r[0] != "method":
            continue
        name = r[1]
        pattern = re.compile(r"\." + re.escape(name) + r"\s*\(")
        result[name] = [rel(p) for p, t in corpus.items() if pattern.search(t)]
    return result


def phenomena_guide():
    """Every example in docs/guide/phenomena.md is a test, word for word, and every test of that folder is in the guide."""
    problems = []
    page = text_of(ROOT / "docs" / "guide" / "phenomena.md").replace("\r\n", "\n")
    folder = TESTS / "cases" / "phenomena"
    seen = set()
    for m in re.finditer(r"<!-- test: (\S+) -->\n(.*?)(?=\n#|\Z)", page, re.S):
        target, body = m.group(1), m.group(2)
        if target.endswith(".cpp"):
            if not (ROOT / target).exists():
                problems.append("phenomena.md names %s, which does not exist" % target)
            continue
        name = target.split("/", 1)[1]
        seen.add(name)
        script, expected = folder / (name + ".cuff"), folder / (name + ".expected")
        if not script.exists():
            problems.append("phenomena.md names tests/cases/%s, which does not exist" % target)
            continue
        blocks = re.findall(r"```(\S*)\n(.*?)\n```", body, re.S)
        sources = [b for info, b in blocks if info == "cuff"]
        results = [b for info, b in blocks if info in ("output", "text")]
        if not sources or sources[-1] != text_of(script).replace("\r\n", "\n").rstrip("\n"):
            problems.append("the example for %s in phenomena.md is not the test program word for word" % name)
        if not results or results[-1] != text_of(expected).replace("\r\n", "\n").rstrip("\n"):
            problems.append("the output shown for %s in phenomena.md is not the test's expected output" % name)
    for script in sorted(folder.glob("*.cuff")):
        if script.stem not in seen:
            problems.append("tests/cases/phenomena/%s.cuff is not shown in docs/guide/phenomena.md" % script.stem)
    return problems


MAP_BEGIN = "<!-- coverage-map:begin -->"
MAP_END = "<!-- coverage-map:end -->"


def read_area_map():
    """Rows of the table between the markers in tests/README.md: (area, productions, notes)."""
    body = text_of(TESTS / "README.md")
    if MAP_BEGIN not in body or MAP_END not in body:
        return None
    table = body.split(MAP_BEGIN, 1)[1].split(MAP_END, 1)[0]
    rows = []
    for line in table.splitlines():
        line = line.strip()
        if not line.startswith("|") or set(line) <= set("|:- "):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if cells[0].lower() == "area":
            continue
        rows.append(cells)
    return rows


def grammar_productions():
    out = []
    for line in text_of(ROOT / "docs" / "reference" / "grammar.md").splitlines():
        m = re.match(r"^([a-z_]+)\s+=", line)
        if m:
            out.append(m.group(1))
    return out


def main():
    parser = argparse.ArgumentParser(description="Report what the tests leave out.")
    parser.add_argument("--list", action="store_true", help="also list what is covered, and by which tests")
    args = parser.parse_args()

    limits = docs.parse_limits()
    descriptions = docs.parse_error_codes()
    records = docs.ask_engine(limits, [n for n, _ in descriptions])
    if records is None:
        print("coverage: the engine could not be introspected (a C++ compiler is required)")
        for note in docs.NOTES + docs.PROBLEMS:
            print("  " + note)
        return 2
    accepted = load_exceptions()
    scripts = script_files()
    failed = 0

    def report(kind, title, table):
        nonlocal failed
        gaps = [k for k, v in table.items() if not v]
        name_of = (lambda k: k) if kind == "error" else (lambda k: k if isinstance(k, str) else k[1])
        covered = len(table) - len(gaps)
        print("%-9s %d of %d covered" % (title, covered, len(table)))
        for k in gaps:
            key = (kind, name_of(k))
            if key in accepted:
                print("   accepted: %s %s — %s" % (kind, name_of(k), accepted[key]))
            else:
                failed += 1
                label = k if isinstance(k, str) else "%s (%s)" % (k[1], k[0])
                print("   MISSING:  %s" % label)
        if args.list:
            for k, v in table.items():
                if v:
                    label = k if isinstance(k, str) else "%s (%s)" % (k[1], k[0])
                    print("   %-28s %s%s" % (label, ", ".join(v[:2]), " ..." if len(v) > 2 else ""))

    report("error", "errors", error_code_coverage(records, descriptions))
    report("function", "functions", function_coverage(records, scripts))
    report("method", "methods", method_coverage(records, scripts))

    area_rows = read_area_map()
    if area_rows is None:
        print("grammar   MISSING: tests/README.md has no map between %s and %s" % (MAP_BEGIN, MAP_END))
        failed += 1
    else:
        listed = set()
        for area, productions, _ in [(r + ["", ""])[:3] for r in area_rows]:
            area = area.strip("`")
            for p in re.findall(r"[a-z_]+", productions):
                listed.add(p)
            if not any(any((TESTS / sub / area).glob("*.cuff")) for sub in ("cases", "errors") if (TESTS / sub / area).is_dir()):
                print("   MISSING:  area '%s' has no tests in tests/cases/%s/ or tests/errors/%s/" % (area, area, area))
                failed += 1
        productions = grammar_productions()
        missing = [p for p in productions if p not in listed]
        print("grammar   %d of %d productions are assigned to an area" % (len(productions) - len(missing), len(productions)))
        for p in missing:
            print("   MISSING:  production '%s' is in no area of the map in tests/README.md" % p)
            failed += 1
        extra = sorted(listed - set(productions))
        for p in extra:
            print("   MISSING:  the map names '%s', which is not a production in docs/reference/grammar.md" % p)
            failed += 1

    problems = phenomena_guide()
    print("phenomena %s" % ("every example in the guide is a test" if not problems else "the guide and its tests differ"))
    for problem in problems:
        print("   MISSING:  " + problem)
        failed += 1

    if failed:
        print("\ncoverage: %d gap(s)" % failed)
        return 1
    print("\ncoverage: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
