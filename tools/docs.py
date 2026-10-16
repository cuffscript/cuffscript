#!/usr/bin/env python3
"""Keeps the documentation in step with the code.

    python3 tools/docs.py generate   rewrite the generated parts of the docs from the code
    python3 tools/docs.py check      fail if anything is out of date or broken (what CI runs)

What it does, and why it exists: a number or a list that is written down by hand in a document goes stale the
moment someone changes the code. So the facts that live in the code are taken from the code:

  * Limits (engine/common/Limits.h), error codes (engine/common/ErrorCodes.h) and the library functions each
    `use DLC:<name>` provides are read out of the engine and written into the "generated" parts of the pages
    under docs/reference/. A generated part sits between
        <!-- BEGIN GENERATED: <id> -->  and  <!-- END GENERATED: <id> -->
    and is never edited by hand.
  * The command-line help of cuffc and cuffsh is embedded the same way.

and what is written by hand is checked against the code:

  * every library function must be mentioned on its library's page, and a page may not mention a function the
    library does not have;
  * every relative link and heading anchor in every Markdown file must resolve;
  * every example in a ```cuff block that is followed by an ```output block is run and its output compared; a
    ```cuff error E4-006 block must fail with that error; a ```cuff run block must succeed.

`check` needs a C++17 compiler (to ask the engine what it contains) and, for the examples and the help text,
the built binaries (`make`, `cli/build.sh`). Without them it still does what it can and says what it skipped;
pass --require-binaries (CI does) to make that an error. Only the Python standard library is used.
"""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "engine"
DOCS = ROOT / "docs"

PROBLEMS = []
NOTES = []


def problem(message):
    PROBLEMS.append(message)


def read(path):
    return Path(path).read_text(encoding="utf-8")


# ---------------------------------------------------------------------------------------------------------------
# Reading the code
# ---------------------------------------------------------------------------------------------------------------

def parse_limits():
    """Every constant in Limits.h that has an @limit or @internal comment block above it."""
    entries = []
    pending = None
    seen = set()
    for raw in read(ENGINE / "common" / "Limits.h").splitlines():
        line = raw.strip()
        m = re.match(r"//\s*@(limit|internal)\s+(.*?)(?:\s*\|\s*(E\d-\d{3}))?\s*$", line)
        if m:
            pending = {"kind": m.group(1), "title": m.group(2), "error": m.group(3), "text": []}
            continue
        if pending is None:
            continue
        if line.startswith("//"):
            pending["text"].append(line[2:].strip())
            continue
        if line.startswith("#") or not line:
            continue
        m = re.match(r"constexpr\s+[\w:<>\s]+?\s+(k\w+)\s*=", line)
        if m:
            name = m.group(1)
            if name not in seen:
                seen.add(name)
                pending["name"] = name
                pending["description"] = " ".join(pending["text"])
                entries.append(pending)
            pending = None
        else:
            pending = None
    return entries


def parse_error_codes():
    """(name, description) for every enumerator of ErrorCode, in order."""
    out = []
    for line in read(ENGINE / "common" / "ErrorCodes.h").splitlines():
        m = re.match(r"\s+([A-Z]\w+)\s*=\s*(\d+),\s*//\s*(.*?)\s*$", line)
        if m:
            out.append((m.group(1), m.group(3)))
    return out


def error_is_raised(name):
    """Whether any engine source raises the code (the rest are reserved numbers)."""
    pattern = re.compile(r"ErrorCode::" + name + r"\b")
    for path in list(ENGINE.rglob("*.h")) + [ROOT / "main.cpp"]:
        if path.name == "ErrorCodes.h":
            continue
        if pattern.search(read(path)):
            return True
    return False


def find_cxx():
    for candidate in [os.environ.get("CXX"), "c++", "g++", "clang++"]:
        if candidate and shutil.which(candidate):
            return candidate
    return None


def ask_engine(limits, error_names):
    """Compiles tools/introspect.cpp and returns its records, or None (with a note) if that is not possible."""
    cxx = find_cxx()
    if cxx is None:
        NOTES.append("no C++ compiler found (set CXX): skipped the checks that ask the engine what it contains")
        return None
    with tempfile.TemporaryDirectory() as tmp:
        inc = []
        for e in limits:
            inc.append('    emitLimit("%s", static_cast<unsigned long long>(cuff::limits::%s));' % (e["name"], e["name"]))
        for name in error_names:
            inc.append("    emitError(cuff::ErrorCode::%s, \"%s\");" % (name, name))
        Path(tmp, "limits_dump.inc").write_text("\n".join(inc) + "\n", encoding="utf-8")
        exe = str(Path(tmp, "introspect.exe" if os.name == "nt" else "introspect"))
        cmd = [cxx, "-std=c++17", "-O0", "-I", tmp, str(ROOT / "tools" / "introspect.cpp"), "-o", exe]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            problem("could not build tools/introspect.cpp:\n" + done.stderr[-2000:])
            return None
        ran = subprocess.run([exe], capture_output=True, text=True, encoding="utf-8")
        if ran.returncode != 0:
            problem("tools/introspect.cpp failed to run")
            return None
        return [line.split("\t") for line in ran.stdout.splitlines() if line]


# ---------------------------------------------------------------------------------------------------------------
# The generated parts
# ---------------------------------------------------------------------------------------------------------------

BANNER = "_This table is generated from `{src}` by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._"


def fmt_value(name, value):
    value = int(value)
    if "Ms" in name:
        return "{:,} ms".format(value)
    if re.search(r"Bytes|Budget", name):
        mib, kib = 1024 * 1024, 1024
        if value >= mib and value % mib == 0:
            return "{} MiB".format(value // mib)
        if value >= kib and value % kib == 0:
            return "{} KiB".format(value // kib)
        return "{:,} bytes".format(value)
    if value >= (1 << 20) and value % (1 << 20) == 0 and "Items" in name:
        return "{} Mi ({:,})".format(value // (1 << 20), value)
    return "{:,}".format(value)


def link_code(tag):
    return "[`%s`](errors.md#%s)" % (tag, tag.lower()) if tag else ""


def block_limits(limits, records):
    values = {r[1]: r[2] for r in records if r[0] == "limit"}
    rows = ["| Limit | Value | Raises | What it is |", "| :--- | ---: | :--- | :--- |"]
    for e in limits:
        if e["kind"] != "limit":
            continue
        rows.append("| %s | %s | %s | %s |" % (e["title"], fmt_value(e["name"], values[e["name"]]),
                                              link_code(e["error"]), e["description"]))
    return BANNER.format(src="engine/common/Limits.h") + "\n\n" + "\n".join(rows)


def block_internal(limits, records):
    values = {r[1]: r[2] for r in records if r[0] == "limit"}
    rows = ["| Value | Setting | What it is |", "| :--- | ---: | :--- |"]
    for e in limits:
        if e["kind"] != "internal":
            continue
        rows.append("| %s (`%s`) | %s | %s |" % (e["title"], e["name"], fmt_value(e["name"], values[e["name"]]), e["description"]))
    return BANNER.format(src="engine/common/Limits.h") + "\n\n" + "\n".join(rows)


def block_errors(descriptions, records):
    info = {r[2]: r for r in records if r[0] == "error"}
    out = [BANNER.format(src="engine/common/ErrorCodes.h")]
    category = None
    for name, text in descriptions:
        tag, _, cat, recoverable = info[name][1], info[name][2], info[name][3], info[name][4]
        if cat != category:
            category = cat
            out += ["", "### " + (cat if cat.endswith("s") else cat + "s"), "",
                    "| Code | Meaning | `or_else` can catch it |", "| :--- | :--- | :---: |"]
        note = "" if error_is_raised(name) else " _Reserved: never raised._"
        out.append("| <a id=\"%s\"></a>`%s` | %s%s | %s |" % (tag.lower(), tag, text, note, "yes" if recoverable == "1" else "no"))
    return "\n".join(out)


LIBRARIES = ["math", "string", "time", "random", "list", "map", "convert", "json", "network", "filesystem"]


def functions_by_library(records):
    libs = {}
    for r in records:
        if r[0] == "fn":
            libs.setdefault(r[1], []).append(r[2])
    return libs


def block_function_index(records):
    libs = functions_by_library(records)
    builtins = [r[1] for r in records if r[0] == "builtin"]
    rows = ["| Function | Library |", "| :--- | :--- |"]
    entries = [(n, "always available", "builtins.md") for n in builtins]
    for lib in LIBRARIES:
        for n in libs.get(lib, []):
            entries.append((n, "`DLC:%s`" % lib, "dlc/%s.md" % lib))
    for n, where, page in sorted(entries):
        rows.append("| [`%s`](%s) | %s |" % (n, page, where))
    return BANNER.format(src="the engine's library tables") + "\n\n" + "\n".join(rows)


def block_dlc_table(records):
    libs = functions_by_library(records)
    rows = ["| Library | Load it with | Functions | Reference |", "| :--- | :--- | ---: | :--- |"]
    for lib in LIBRARIES:
        rows.append("| %s | `use DLC:%s` | %d | [%s](%s.md) |" % (lib, lib, len(libs.get(lib, [])), lib, lib))
    return BANNER.format(src="the engine's library tables") + "\n\n" + "\n".join(rows)


def run_help(binary):
    path = find_binary(binary)
    if path is None:
        return None
    done = subprocess.run([path, "--help"], capture_output=True, text=True, encoding="utf-8")
    text = (done.stdout or done.stderr).rstrip("\n")
    # The usage line prints the path the program was started with, which depends on where the repository is checked
    # out; keep only the program's name.
    text = re.sub(r"(Usage: )\S*?([^\s/\\]+?)(?:\.exe)?(?= )", r"\1\2", text)
    # A version number in the help would make every release look like out-of-date docs.
    text = re.sub(r"(Shell )\d+\.\d+\.\d+", r"\1<version>", text)
    return "```text\n" + text + "\n```"


def find_binary(name):
    candidates = {"cuffc": [ROOT / "cuffc", ROOT / "cuffc.exe"], "cuffsh": [ROOT / "cli" / "cuffsh", ROOT / "cli" / "cuffsh.exe"]}[name]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


def build_blocks(limits, descriptions, records):
    """{ (relative file, block id): text or None if it cannot be produced here }"""
    blocks = {}
    if records is not None:
        blocks[("docs/reference/limits.md", "limits-table")] = block_limits(limits, records)
        blocks[("docs/reference/limits.md", "internal-limits")] = block_internal(limits, records)
        blocks[("docs/reference/errors.md", "error-table")] = block_errors(descriptions, records)
        blocks[("docs/reference/functions.md", "function-index")] = block_function_index(records)
        blocks[("docs/reference/dlc/README.md", "dlc-table")] = block_dlc_table(records)
    blocks[("docs/reference/cuffc.md", "help-cuffc")] = run_help("cuffc")
    blocks[("docs/reference/cuffsh.md", "help-cuffsh")] = run_help("cuffsh")
    return blocks


def block_pattern(block_id):
    return re.compile(r"(<!-- BEGIN GENERATED: %s -->)(.*?)(<!-- END GENERATED: %s -->)" % (re.escape(block_id), re.escape(block_id)), re.S)


def apply_blocks(blocks, write, require_binaries):
    for (rel, block_id), text in blocks.items():
        path = ROOT / rel
        if not path.exists():
            problem("%s does not exist (it holds the generated block '%s')" % (rel, block_id))
            continue
        current = read(path)
        match = block_pattern(block_id).search(current)
        if not match:
            problem("%s has no <!-- BEGIN GENERATED: %s --> ... END block" % (rel, block_id))
            continue
        if text is None:
            msg = "%s: skipped block '%s' (needs a built binary)" % (rel, block_id)
            if require_binaries:
                problem(msg)
            else:
                NOTES.append(msg)
            continue
        updated = current[:match.start(2)] + "\n" + text + "\n" + current[match.end(2):]
        if updated != current:
            if write:
                path.write_text(updated, encoding="utf-8", newline="\n")
                print("updated", rel, "[" + block_id + "]")
            else:
                diff = "\n".join(list(difflib.unified_diff(current.splitlines(), updated.splitlines(), rel, rel + " (generated)", lineterm="", n=1))[:30])
                problem("%s is out of date (block '%s'); run: python3 tools/docs.py generate\n%s" % (rel, block_id, diff))


# ---------------------------------------------------------------------------------------------------------------
# Checks on what is written by hand
# ---------------------------------------------------------------------------------------------------------------

def check_library_pages(records):
    libs = functions_by_library(records)
    builtins = [r[1] for r in records if r[0] == "builtin"]
    pages = {lib: DOCS / "reference" / "dlc" / (lib + ".md") for lib in LIBRARIES}
    for lib, page in pages.items():
        if not page.exists():
            problem("docs/reference/dlc/%s.md is missing" % lib)
            continue
        text = read(page)
        words = set(re.findall(r"[A-Za-z_][A-Za-z_0-9]*", text))
        for fn in libs.get(lib, []):
            if fn not in words:
                problem("docs/reference/dlc/%s.md does not mention the function %s" % (lib, fn))
        prefix = {"string": "str", "filesystem": "file"}.get(lib, lib)
        for word in sorted(words):
            if re.match(r"^%s_[a-z0-9_]+$" % prefix, word) and word not in libs.get(lib, []):
                problem("docs/reference/dlc/%s.md mentions %s, which %s does not provide" % (lib, word, "DLC:" + lib))
    page = DOCS / "reference" / "builtins.md"
    if page.exists():
        words = set(re.findall(r"[A-Za-z_][A-Za-z_0-9]*", read(page)))
        for fn in builtins:
            if fn not in words:
                problem("docs/reference/builtins.md does not mention %s" % fn)
    else:
        problem("docs/reference/builtins.md is missing")


def check_limit_duplicates(limits, records):
    """A limit's value is written down in one place (limits.md, generated). Anywhere else it would go stale."""
    values = {r[1]: r[2] for r in records if r[0] == "limit"}
    needles = {}
    for e in limits:
        v = int(values[e["name"]])
        shown = fmt_value(e["name"], v)
        candidates = {shown.split(" (")[0]}
        if v >= 1000:
            candidates.add("{:,}".format(v))
        for c in candidates:
            # bare small numbers are too common ("5") to be searched for; sizes with a unit, and long numbers, are not
            if re.search(r"\d\s?(MiB|KiB|Mi|ms)$", c) or ("," in c):
                needles.setdefault(c, e["title"])
    exempt = {"docs/reference/limits.md", "docs/CHANGELOG.md", "docs/internals/design-notes.md"}
    generated = re.compile(r"<!-- BEGIN GENERATED.*?<!-- END GENERATED[^>]*-->", re.S)
    for path in markdown_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel in exempt:
            continue
        text = generated.sub("", read(path))
        for needle, title in needles.items():
            for m in re.finditer(r"(?<![\w.,])" + re.escape(needle) + r"(?![\w,])", text):
                line = text[:m.start()].count("\n") + 1
                problem("%s:%d writes '%s', the value of the limit '%s'; say what it limits and link to docs/reference/limits.md instead" % (rel, line, needle, title))


def slugify(heading):
    """How GitHub turns a heading into an anchor."""
    text = re.sub(r"<[^>]+>", "", heading)
    text = re.sub(r"[`*]", "", text).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def markdown_files():
    skip = {".git", "node_modules", "dist"}
    for path in ROOT.rglob("*.md"):
        if not any(part in skip for part in path.relative_to(ROOT).parts):
            yield path


def anchors_of(path, cache={}):
    if path not in cache:
        text = read(path)
        found = set(re.findall(r'<a id="([^"]+)"', text))
        counts = {}
        in_fence = False
        for line in text.splitlines():
            if line.startswith("```"):
                in_fence = not in_fence
            if not in_fence:
                m = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
                if m:
                    slug = slugify(m.group(1))
                    n = counts.get(slug, 0)
                    counts[slug] = n + 1
                    found.add(slug if n == 0 else "%s-%d" % (slug, n))
        cache[path] = found
    return cache[path]


def check_versions():
    """The version is written in two places that must agree: the shell's banner and the npm package."""
    header = re.search(r'kVersion\s*=\s*"([^"]+)"', read(ROOT / "cli" / "Version.h"))
    package = re.search(r'"version"\s*:\s*"([^"]+)"', read(ROOT / "npm" / "package.json"))
    if not header or not package:
        problem("could not read the version from cli/Version.h and npm/package.json")
    elif header.group(1) != package.group(1):
        problem("cli/Version.h says %s but npm/package.json says %s" % (header.group(1), package.group(1)))
    top = re.search(r"^## v(\d+\.\d+\.\d+)", read(DOCS / "CHANGELOG.md"), re.M)
    if top and header and top.group(1) != header.group(1):
        problem("docs/CHANGELOG.md starts with v%s but cli/Version.h says %s" % (top.group(1), header.group(1)))


def check_links():
    link = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
    for path in markdown_files():
        rel = path.relative_to(ROOT).as_posix()
        in_fence = False
        for number, line in enumerate(read(path).splitlines(), 1):
            if line.startswith("```"):
                in_fence = not in_fence
            if in_fence:
                continue
            for target in link.findall(re.sub(r"`[^`]*`", "", line)):
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                    continue
                file_part, _, anchor = target.partition("#")
                dest = (path.parent / file_part).resolve() if file_part else path
                if file_part and not dest.exists():
                    problem("%s:%d links to %s, which does not exist" % (rel, number, target))
                elif anchor and dest.suffix == ".md" and anchor not in anchors_of(dest):
                    problem("%s:%d links to %s, but that page has no heading '%s'" % (rel, number, target, anchor))


FENCE = re.compile(r"^```([^\n]*)\n(.*?)^```[ \t]*$", re.S | re.M)


def check_examples(require_binaries):
    binary = find_binary("cuffc")
    if binary is None:
        msg = "cuffc is not built (run make): the examples in the docs were not run"
        if require_binaries:
            problem(msg)
        else:
            NOTES.append(msg)
        return
    ran = 0
    for path in markdown_files():
        text = read(path)
        rel = path.relative_to(ROOT).as_posix()
        blocks = [(m.group(1).strip(), m.group(2), text[:m.start()].count("\n") + 1, m.end()) for m in FENCE.finditer(text)]
        for index, (info, body, line, end) in enumerate(blocks):
            words = info.split()
            if not words or words[0] != "cuff":
                continue
            following = blocks[index + 1] if index + 1 < len(blocks) else None
            adjacent = following is not None and text[end:following[3]].strip().startswith("```output")
            expect_error = words[1:2] == ["error"]
            plain_run = words[1:2] == ["run"]
            if not (adjacent or expect_error or plain_run):
                continue
            ran += 1
            with tempfile.TemporaryDirectory() as work:
                Path(work, "main.cuff").write_text(body, encoding="utf-8", newline="\n")
                try:
                    done = subprocess.run([binary, "--no-network", "main.cuff"], cwd=work, capture_output=True, timeout=30)
                except subprocess.TimeoutExpired:
                    problem("%s:%d: the example timed out" % (rel, line))
                    continue
                out = done.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
                err = done.stderr.decode("utf-8", "replace").replace("\r\n", "\n")
            where = "%s:%d" % (rel, line)
            if expect_error:
                code = words[2] if len(words) > 2 else ""
                if done.returncode == 0 or ("[" + code + "]") not in err:
                    problem("%s: the example should fail with %s but %s" % (where, code, "succeeded" if done.returncode == 0 else "failed with: " + err.strip().splitlines()[0]))
            elif done.returncode != 0:
                problem("%s: the example failed: %s" % (where, err.strip().splitlines()[0] if err.strip() else "exit code %d" % done.returncode))
            elif adjacent:
                expected = following[1].rstrip("\n")
                if out.rstrip("\n") != expected:
                    problem("%s: the example's output differs\n  expected: %r\n  actual:   %r" % (where, expected, out.rstrip("\n")))
    print("ran %d documentation examples" % ran)


# ---------------------------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Generate and check the documentation.")
    parser.add_argument("command", choices=["generate", "check"])
    parser.add_argument("--require-binaries", action="store_true", help="treat a missing cuffc/cuffsh or compiler as an error")
    args = parser.parse_args()
    os.chdir(ROOT)

    limits = parse_limits()
    descriptions = parse_error_codes()
    if not limits or not descriptions:
        problem("could not read the limits or the error codes from the engine headers")
    records = ask_engine(limits, [n for n, _ in descriptions])
    if records is None and args.require_binaries:
        problem("the engine could not be introspected (a C++ compiler is required)")

    apply_blocks(build_blocks(limits, descriptions, records), write=(args.command == "generate"), require_binaries=args.require_binaries)

    if args.command == "check":
        if records is not None:
            check_library_pages(records)
        if records is not None:
            check_limit_duplicates(limits, records)
        check_versions()
        check_links()
        check_examples(args.require_binaries)

    for note in NOTES:
        print("note:", note)
    if PROBLEMS:
        print("\n%d problem(s):" % len(PROBLEMS))
        for p in PROBLEMS:
            print(" -", p)
        return 1
    print("docs: ok" if args.command == "check" else "docs: generated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
