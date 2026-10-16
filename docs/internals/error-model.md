# The error model

## What an error is

Every failure the engine reports is a `CuffError` (`engine/common/CuffError.h`), thrown as a C++ exception and caught by the
front end. It carries:

| Field | Meaning |
| :--- | :--- |
| `code` | An `ErrorCode`, numbered by kind (see below). |
| `category` | The kind's name, from the code. |
| `message` | What went wrong, in one sentence. |
| `location` | A [`SourceLocation`](source-locations.md). |
| `hint` | An optional suggestion, shown last. |
| `recoverable` | Whether `or_else` may catch it, from the code. |
| `source`, `related` | The text the location points into, and an optional extra block; see [source locations](source-locations.md). |

There are subclasses (`SyntaxError`, `RuntimeError`, `ModuleError`, ...) that exist to make throw sites short and to fix the
code's category; they add no fields.

## Codes

A code is a number, and its thousands digit is its kind: 1 lexical, 2 syntax, 3 regex (3000s syntax, 3100s matching),
4 runtime, 5 module, 6 resource limit, 9 internal. `errorCodeTag` writes it as `E4-006`; `errorCodeRecoverable` is the one
place that decides which kinds `or_else` can catch (regex matching, runtime and module errors). The catalogue in
[errors](../reference/errors.md) is generated from `ErrorCodes.h`, including a one-line description written next to each
enumerator and a note on which codes nothing raises.

Syntax problems are all reported as `E2-001`; the other lexical and syntax numbers are reserved and kept so that the code
space does not have to be renumbered.

## Who catches what

- `or_else` (the interpreter) catches errors whose code is recoverable, and runs its block.
- Everything else propagates out of `Interpreter::run`. Before it leaves, `run` empties the queue of `async` calls that
  have not started, so a failed run leaves nothing half done.
- `Session::run` and `CuffEngine::execute` catch what escapes, through `renderCurrentException`. It renders a `CuffError`
  as a report, and turns `std::bad_alloc` and any other exception into a report as well. They never throw.

## The report

`renderErrorWithSnippet` is the only function that formats an error for a person:

```text
[E4-006] Runtime Error in './lib/ratio.cuff' at line 3, column 17: division by zero
        return base / n
                    ^
    hint: ...
```

`in '...'` appears only for an error from another file. `CuffError::what()` is a shorter form without the source line,
for logs. Front ends print the rendered text, prefixed with `ERROR: `, and nothing else; they do not look inside it.

## Adding an error code

1. Add an enumerator to `ErrorCodes.h` in the right range, with its one-line description after it.
2. Add `errorCategoryName`/`errorCodeRecoverable` handling only if you start a new range.
3. Throw it where the problem is found, from the subclass that matches its kind, with a message and, where one helps, a hint.
4. Add a case to `tests/errors/` that must fail with that code (see [Testing](../contributing/testing.md)).
5. Run `python3 tools/docs.py generate`, which adds the code to the catalogue.
