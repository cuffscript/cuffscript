## v1.2.0 - 2026-09-09

## Description

- **Performance**: replaced the exception-based `return`/`stop` control-flow implementation
  with an explicit `ExecOutcome` value threaded through `execStatement`/`execBlock`/`execIf`/
  `execLoop` (`engine/interpreter/Signals.h`). C++ exceptions are reserved for genuine
  `CuffError` conditions again. Measured effect: a `fib(27)` recursion benchmark (~635k
  function calls) went from 2.75s to 0.26s (~10.7x). This also fixed two real bugs the
  exception-based version had: `stop` used inside a function with no loop of its own used to
  leak through the function-call boundary and terminate whatever loop was active in the
  *caller*; and `return`/`stop` used at the top level (outside any function/loop) used to
  crash the whole process with an uncaught exception. Both are now clean, catchable
  `CuffRuntimeError`s (`StopOutsideLoop`, `ReturnOutsideFunction`).

- **Async**: calling an `async` function *without* `await` no longer runs it immediately —
  it's queued and runs once the entire top-level script's synchronous code has finished,
  in the order it was queued (FIFO). `await` is unchanged (still runs immediately and
  returns the value). This is cooperative, single-threaded deferral — not real concurrency —
  chosen for safety (the interpreter's shared state isn't thread-safe). See
  `docs/IMPLEMENTATION_NOTES.md` section 1 and `examples/10_async_ordering.cuff`.

- **DLC libraries**: added `DLC:list` (`sort`, `reverse`, `join`, `unique` — all
  non-mutating) and `DLC:convert` (`to_number`, `to_str`, `to_boolean`).

- Fixed a parser bug uncovered while adding `DLC:list`: `use DLC:<name>`/`use <name> from
  ...` only accepted an `IDENTIFIER` token for the name, so any name colliding with a
  reserved word (like `list`) failed to parse at all. `ImportParser` now accepts any
  word-shaped token (identifier or keyword) as a name.

- Minor interpreter micro-optimizations: `Environment` pre-sizes its variable table for a
  function call's known parameter count, and arguments are moved rather than copied into
  parameter bindings.

- Added `examples/10_async_ordering.cuff` and expanded `examples/08_dlc_libraries.cuff` to
  cover the new libraries.


---

## v1.1.0 - 2026-09-08

## Description

- Implemented a full tree-walking interpreter (`engine/interpreter/`): Value/Environment
  model, Python-style function-level scoping with explicit `change x to global` bridging,
  1-based indexing/slicing with negative-index support, collections (`add`/`remove`/`replace`/
  indexed `change`, insertion-ordered maps), functions (`returnable`/`async` in any
  combination), `or_else` error recovery, and `use`/`from`/`DLC:*` module loading.

- Implemented CuffScript's custom pattern-matching engine from scratch (`engine/regex/`):
  a dedicated compiler + backtracking matcher for the full `docs/REGEX.md` dialect
  (character classes, presets, `[one:...]`, named/numbered captures, quantifiers including
  `N~M` ranges and lazy variants, anchors, flags), wired into `is`/`IS`, `match ... from`,
  `find ... from`, `replace ... in ... to`, `split ... by`, and `count ... in` — including a
  step-count + time-limit safety guard against catastrophic backtracking.

- Introduced a systematic, extensible error hierarchy (`engine/common/ErrorCodes.h`,
  `engine/common/CuffError.h`): every error carries a stable numeric code, category,
  location, message, and optional hint, and is marked recoverable/non-recoverable so
  `or_else` catches exactly the right things.

- `./cuffc program.cuff` now runs the program by default; `--ast` reproduces the previous
  tokens/AST-dump behavior for engine development.

- Fixed several defects that prevented the previously-committed engine from compiling or
  running at all (broken relative includes, a use-before-complete-type cycle between the
  statement/expression sub-parsers, a real dangling-copy bug in `await` parsing, a missing
  `-Wunused-variable`), added single-quoted string literals (needed for map/match key access
  inside f-strings per `docs/REGEX.md`), fixed f-string `{{`/`}}` escaping and nested-brace
  handling, and corrected `!` to bind looser than comparison (`!lvl is MAX_LEVEL` now means
  `!(lvl is MAX_LEVEL)`, matching the spec's own examples).

- Added `docs/IMPLEMENTATION_NOTES.md` documenting every place the language spec leaves
  runtime behavior unspecified and the choice this engine makes there (async model, DLC
  function list, module-merge semantics, etc.), and `docs/EXTENDING.md` as a guide for
  adding new builtins/DLC libraries/statements/expressions.

---

## v0.1.0 - 2026-09-05

## Description

- Define initial grammar rules
- Set up basic engine skeleton and directory structure
- Add README and initial documentation


---
