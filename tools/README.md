# `tools/`

| File | Purpose |
| :--- | :--- |
| `docs.py` | Generates the parts of the documentation that come from the code, and checks the rest against it. |
| `introspect.cpp` | Asks the engine what it contains (libraries, limits, error codes). Built and run by `docs.py`; not meant to be built by hand. |

```sh
python3 tools/docs.py generate    # rewrite the generated parts of docs/
python3 tools/docs.py check       # fail if the docs are stale or broken (what CI runs)
```

Python 3.8 or later, standard library only. What it checks, and the rules for writing documentation, are in
[Documentation](../docs/contributing/documentation.md).
