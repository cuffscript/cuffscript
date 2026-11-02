# `tools/`

| File | Purpose |
| :--- | :--- |
| `docs.py` | Generates the parts of the documentation that come from the code, and checks the rest against it. |
| `introspect.cpp` | Asks the engine what it contains (libraries, limits, error codes, built-in methods). Built and run by `docs.py`; not meant to be built by hand. |
| `migrate_3_to_4.py` | Rewrites programs and the code blocks of Markdown files from the 3.x syntax to the 4.0 syntax. |
| `migrate_3_to_4_test.py` | The tests of the migration tool. |

```sh
python3 tools/docs.py generate    # rewrite the generated parts of docs/
python3 tools/docs.py check       # fail if the docs are stale or broken (what CI runs)
```

```sh
python3 tools/migrate_3_to_4.py --check path/        # list what would change, change nothing
python3 tools/migrate_3_to_4.py --write path/        # rewrite the files in place
python3 tools/migrate_3_to_4_test.py                 # the tool's own tests
```

The migration tool reads `.cuff` files and the `cuff` code blocks of `.md` files, rewrites what it can do safely, and reports what needs a person: `renamed:` lines are changes it made, `manual:` lines are places to look at (for example an `add ... to NAME[i]` whose meaning changed). A file that is already in the 4.0 syntax is left alone.

Python 3.8 or later, standard library only. What it checks, and the rules for writing documentation, are in
[Documentation](../docs/contributing/documentation.md).
