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
4 runtime, 5 module, 6 resource limit, 7 program, 9 internal. `errorCodeTag` writes it as `E4-006`; `errorCodeRecoverable` is the one
place that decides which kinds `or_else` can catch (regex matching, runtime, module and program errors). The catalogue in
[errors](../reference/errors.md) is generated from `ErrorCodes.h`, including a one-line description written next to each
enumerator and a note on which codes nothing raises.

Syntax problems are all reported as `E2-001`; the other lexical and syntax numbers are reserved and kept so that the code
space does not have to be renumbered.

## Errors as objects

A program sees an error as an object of the built-in class `Error` (`message`, `code`, `line`, `column`, `hint`).
Two kinds of error become one:

- An error the program raises with `fail` is a `ProgramError` (`interpreter/ProgramError.h`, code `E7-001`). It carries the
  `Error` object that was raised, which may be of a class the program wrote, and a handler written `or_else as Error e` receives that
  very object. `fail e` inside a handler raises it again with its class intact.
- An error the engine raises (a recoverable `CuffError`) is turned into a new `Error` object at the handler, from its code,
  message, location and hint, so a handler can read the same fields whichever kind it caught. `fail e` of such an object raises
  the original error again, so its report is unchanged.

A `ProgramError` that nothing catches is reported like any error, as `[E7-001] Program Error at line N, column M` with the message beside
the marks, and the class name before the message when the object is not a plain `Error`.

## Who catches what

- `or_else` (the interpreter) catches errors whose code is recoverable, and runs its block. With `as NAME` the block's scope gets the error object first.
- Everything else propagates out of `Interpreter::run`. Before it leaves, `run` empties the queue of `async` calls that
  have not started, so a failed run leaves nothing half done.
- `Session::run` and `CuffEngine::execute` catch what escapes, through `renderCurrentException`. It renders a `CuffError`
  as a report, and turns `std::bad_alloc` and any other exception into a report as well. They never throw.

## The report

`renderErrorWithSnippet` is the only function that formats an error for a person:

```text
[E4-006] Runtime Error in './lib/ratio.cuff' at line 3, column 17
  |
3 |     return base / n
  |                 ^ division by zero

  hint: ...
```

`in '...'` appears only for an error from another file. The marks cover the token the location starts at (`tokenEnd` in
`ErrorFormat.h` finds where a name, number, string or operator ends), so an error is placed by where it starts and the
report works out how wide it is. When there is no source line to draw, the message goes on the headline instead.
`CuffError::what()` is a shorter form without the source line, for logs. Front ends print the rendered text, prefixed
with `ERROR: `, and nothing else; they do not look inside it.

## Adding an error code

1. Add an enumerator to `ErrorCodes.h` in the right range, with its one-line description after it.
2. Add `errorCategoryName`/`errorCodeRecoverable` handling only if you start a new range.
3. Throw it where the problem is found, from the subclass that matches its kind, with a message and, where one helps, a hint.
4. Add a case to `tests/errors/<area>/` that must fail with that code (see [Testing](../contributing/testing.md)).
5. Run `python3 tools/docs.py generate`, which adds the code to the catalogue.
