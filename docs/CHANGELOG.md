## v1.5.0 - 2026-09-19

## Description

- **Operators are now enums instead of strings.** `BinaryOp`/`UnaryOp` stored the operator as a `std::string` and the interpreter
  dispatched through an `if (op == "is") ... else if (op == "+")` chain. Profiling showed this cost **16.2 million string comparisons** on a 635k-call benchmark — about 25 per call, the largest single cost in the engine. Now a `switch` over `BinOp`/`UnOp` assigned at parse time. `fib(27)`: 0.241s → 0.149s.

- **Variable and function names are interned to integer IDs** (`engine/common/NameInterner.h`), so scope lookups compare `uint32_t`s
  over a contiguous vector instead of strings. A 400-global lookup benchmark went 0.059s → 0.027s. Names are still stored alongside for error messages.

- **Interpreter scope-storage optimization.** Profiling showed parameter binding dominated call cost (~60ns per parameter, 72% of a
  3-argument call) because each one was an `unordered_map` insert: a hash plus a node allocation. Scopes are small in practice, so `Environment` now stores variables in a contiguous vector with linear lookup — one allocation per scope instead of one per variable — falling back to a lazily-built index once a scope exceeds 16 entries, which keeps large global scopes fast. Measured: 0-argument call overhead 72ns → 32ns, 3-argument 252ns → 164ns.

- Decided _against_ the larger slot-resolution refactor (pre-resolving every variable to an array index at parse time) and a bytecode VM
  for now. The measurements above captured most of the available win for a fraction of the risk, and the engine is now in the same performance range as CPython on call-heavy code. See `docs/IMPLEMENTATION_NOTES.md` section 21 for details.

- Net effect of optimizations this release: `fib(27)` 0.32s → 0.140s (~56% faster), a 2M-iteration loop 0.287s → 0.173s, 3-argument call
  overhead 252ns → ~145ns.

- Added `DLC:json` (`to_json`, `from_json`). Object/array/string/number/true/false/null map onto map/list/str/number/boolean/empty.
  `to_json(value, indent)` pretty-prints. The parser is strict per RFC 8259 — trailing commas, single quotes, unquoted keys, leading zeros, `NaN`/`Infinity`, and trailing content are rejected — and decodes `\uXXXX` escapes including surrogate pairs. See `docs/IMPLEMENTATION_NOTES.md` section 19.

- **Error-code audit and normalization.** `ArgumentError` (E4010 `ArgumentCountMismatch`) was being used both for wrong argument
  _counts_ and for bad argument _values_ (`sqrt(-1)`, `to_number("abc")`, malformed JSON). Added `ValueError` / `InvalidArgumentValue` (E4025) and rerouted the six value-problem sites to it. Also normalized message capitalization across every throw site: all messages now start lowercase, since they're always printed after a `...at line N, column M: ` prefix.

- **The regex matcher is now codepoint-based instead of byte-based.** `[any]` matches one character rather than one byte
  (`"안녕하세요" is "[any]5"` is now true, previously false); literal non-ASCII characters in a pattern compile to a single multi-byte literal node; negated sets (`[!num]`) match non-ASCII codepoints; `search()` only starts attempts on codepoint boundaries and `[one:...]` alternatives must end on one. `[edge]` treats non-ASCII letters as word characters, per REGEX.md section 17. `[let]`/`[str]`/`[word]`/`[num]`/`[hex]` stay ASCII-only per spec. See `docs/IMPLEMENTATION_NOTES.md` section 17.

- **Fractional indices and range bounds are now a runtime error** (`FractionalIndex`, E4024) instead of being silently rounded. Applies
  to `list[i]`, `str[i]`, slices, and `loop repeat` bounds. Whole-valued doubles (`2.0`, `6 / 2`) still work.

- Moved `Utf8.h` from `engine/interpreter/` to `engine/common/`,
  since the regex engine now uses it too.

- Added `tests/unit/` for standalone C++ unit tests, and moved the regex engine's own test suite there (78 cases, including new Unicode ones).
  `tests/run.sh` now builds and runs it first.

- Expanded integration tests: added `tests/cases/json.cuff` (plus 7 error cases), `tests/cases/regex_unicode.cuff`,
  `tests/cases/whole_number_indices.cuff`, and 3 fractional-index error cases. All 66 script tests and 78 regex unit tests pass unchanged.

---

## v1.4.0 - 2026-09-18

## Description

- Added an automated regression test suite (`tests/`, `bash tests/run.sh`): `tests/cases/`
  (exact output diff), `tests/errors/` (must fail with a specific error code), plus the
  existing `examples/`/`examples/error_cases/` are now checked the same way instead of only
  by hand. Wired into CI (`.github/workflows/build-and-test.yaml`).

- **Fixed a real crash (SIGSEGV) in the regex matcher**: a pattern with deep linear recursion
  (e.g. `[any]+` against a ~20k+ character string) overflowed the real C++ stack before the
  matcher's own recursion-depth guard could throw its safety exception — the guard's limit
  (20,000) was measured, empirically, to be _higher_ than where the actual crash occurs on an
  8MB stack (~19,500). Lowered the default depth limit to 3,000 (a large margin below the
  observed crash point) and verified no crash from 100 to 1,000,000 characters. Found by the
  new test suite.

- Found and documented (not yet fixed) that names using certain keywords — `add`, `count`,
  `find`, `split`, `replace`, `match`, `in`, `by`, `not`, `global`, etc. — can't be used as
  variable or function names anywhere (`set number add to 5` fails to parse). Same root cause
  as the `DLC:list` import-parsing bug fixed earlier, but spread across every place an
  identifier is declared. See `docs/IMPLEMENTATION_NOTES.md` section 16.

- Fixed `examples/06_error_recovery.cuff`, which incorrectly demonstrated `or_else` "catching"
  a `find` that simply returns `empty` on no match (not an exception) — added the correct
  `is empty` check alongside a genuine or_else-recoverable example (out-of-range index).

---

## v1.3.0 - 2026-09-18

## Description

- **String indexing/slicing/`length()` are now UTF-8 codepoint-based**, not byte-based.
  Previously `"안녕하세요"[1]` sliced into the middle of a multi-byte UTF-8 sequence and
  produced corrupted output — any non-ASCII string indexing was broken. Fixed via a small
  UTF-8 boundary scanner (`engine/interpreter/Utf8.h`); `contains`/`starts_with`/`ends_with`/
  `+` were already byte-safe (UTF-8 is self-synchronizing) and needed no change. The regex
  engine remains byte-oriented by design/scope — see `docs/IMPLEMENTATION_NOTES.md` sections
  14-15 for the exact boundaries and reasoning.

- `to_number`/`to_str`/`to_boolean` are now core builtins — no `use DLC:convert` needed.
  `use DLC:convert` still works (harmlessly re-registers the same functions).

- Fixed the Makefile using Windows-only batch syntax (`@if exist ... del`) for `clean`/`wasm`,
  which failed with a shell syntax error on Linux/macOS (and in CI). Restored portable
  `rm -f`/`mkdir -p`. Also fixed `wasm-clean` deleting the whole `npm/dist/` directory,
  including the hand-written `cuffscript.d.ts`, instead of just the compiled outputs.

- Restored `docs/EXTENDING.md`, which the changelog referenced but was missing from the repo.

---

## v1.2.1 - 2026-09-18

## Description

- Add npm package for web IDE

---

## v1.2.0 - 2026-09-18

## Description

- **Performance**: replaced the exception-based `return`/`stop` control-flow implementation
  with an explicit `ExecOutcome` value threaded through `execStatement`/`execBlock`/`execIf`/
  `execLoop` (`engine/interpreter/Signals.h`). C++ exceptions are reserved for genuine
  `CuffError` conditions again. Measured effect: a `fib(27)` recursion benchmark (~635k
  function calls) went from 2.75s to 0.26s (~10.7x). This also fixed two real bugs the
  exception-based version had: `stop` used inside a function with no loop of its own used to
  leak through the function-call boundary and terminate whatever loop was active in the
  _caller_; and `return`/`stop` used at the top level (outside any function/loop) used to
  crash the whole process with an uncaught exception. Both are now clean, catchable
  `CuffRuntimeError`s (`StopOutsideLoop`, `ReturnOutsideFunction`).

- **Async**: calling an `async` function _without_ `await` no longer runs it immediately —
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

## v1.1.0 - 2026-09-18

## Description

- Make `IMPLEMENTATION_NOTES.md` for language growth.

---

## v1.0.0 - 2026-09-18

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

## v0.1.0 - 2026-09-18

## Description

- Define initial grammar rules
- Set up basic engine skeleton and directory structure
- Add README and initial documentation

---
