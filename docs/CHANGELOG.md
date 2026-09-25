## v2.0.0 - 2026-09-25

## Description

- **Nesting depth ceiling raised to 512** (from 256, introduced in v1.6.0). Scoped specifically to source-level nesting — parentheses, list/map literals, `if`/`loop`/`or_else` blocks — not the string/collection/size limits from the previous release, which are unchanged. Raising the counter alone would have been unsafe on a small stack: measured empirically, 512 levels of nested parentheses can need over 1 MiB of native stack, more than Windows' 1 MiB default thread stack, since the parser recurses on the real C++ stack. The parser now also carries a stack-pointer safety net (primed fresh at the start of every parse, including a module's own parse) mirroring the interpreter's existing one, so it fails cleanly with the same `E2008` instead of crashing on a small stack, on any platform. `availableStackBytes()` (the real-stack-size query this relies on) is now implemented on Windows and macOS as well as Linux, not just Linux — a general robustness improvement, not just a v1.7.0 side effect.

- **New: `constant list` — a read-only, Python-tuple-style list.** `set constant list NAME to [...]` freezes a list: adding, removing, or index-assigning into it is a runtime error (`ConstantReassignment`), and — unlike a naive implementation — this can't be bypassed by assigning it to another variable or passing it into a function, since the immutability lives on the list value itself, not on one variable's name. Declaring one always makes an independent copy first, so `set constant list A to existing_list` freezes only `A`, never `existing_list`. Matches Python tuples exactly in one respect worth knowing: it's *shallow* — a plain list or map found inside a frozen one is still fully mutable through its own reference. No new value type was added; `list` gained one internal flag. `constant` continues to require ALL-CAPS names for every type, as before; `constant map` was not added.

- **Keyword: `function` → `func`.** A straight rename, not an alias — `set function x() do:` no longer parses; it's `set func x() do:` now. Every script in this repository (examples, tests, docs) was updated; existing external scripts need the same one-word find-and-replace.

- **New: `pure` functions — no global-scope access.** `set pure func f() do: ... end` (freely combinable with `returnable`/`async` in any order). A `pure` function's body cannot read or write a top-level global variable — including via the `change x to global` bridge — and fails immediately with a new error, `PureFunctionGlobalAccess` (`E4027`), that names the variable and reminds you removing `pure` is the fix. The restriction is on that function's own body only: a `pure` function calling a non-`pure` one is fine, and the callee's own purity governs its own body, the same per-call scoping `returnable` and `async` already had.

- **New: `DLC:network` — real HTTP (not a stub anymore).** `get(url)` and `post(url, body[, content_type])`, returning `{"status", "ok", "body"}`. A from-scratch, dependency-free HTTP/1.1 client (POSIX sockets on Linux/macOS, Winsock2 on Windows) handling chunked encoding, `Content-Length` framing, and redirects. Plain HTTP only — `https://` fails with a clear error rather than silently talking plaintext to an HTTPS port. Connection/DNS/timeout failures raise `NetworkRequestFailed` (`E4028`), catchable with `or_else` like any other runtime error. **Security-relevant defaults, please read `SECURITY.md`'s new "DLC:network" section before deploying:** network access is *on* by default (unlike the module sandbox); every request is checked against loopback/private/link-local addresses (including `169.254.169.254`, the common cloud-metadata address) and blocked unless explicitly widened. New: `CuffEngine::Options::networkEnabled` / `--no-network` (turn `DLC:network` off entirely — do this before hosting untrusted scripts) and `allowPrivateNetworkTargets` / `--allow-private-network` (widen the address check). Response size, connect timeout, total timeout, and redirect count are all capped (`engine/common/Limits.h`).

- **Windows build fix (found via this release's new mingw-w64 cross-compile check — see Verification below).** `<windows.h>` `#define`s `TRUE`, `FALSE`, and `IN` as plain macros, which was silently corrupting `TokenType::TRUE`/`FALSE`/`IN` wherever the stack-introspection header was included first, breaking the Windows build outright. Fixed with `WIN32_LEAN_AND_MEAN`/`NOMINMAX` and targeted `#undef`s. This means the Windows build had not actually been verified against a real toolchain before now.

- **Performance.** Two changes, kept because both are measured improvements with no downside: dispatch in the interpreter's hot paths uses `std::get_if` instead of `std::get` (skips exception-handling machinery the kind-check already makes unnecessary), and function calls now recycle their argument vector and the callee's local-variable storage across calls instead of allocating fresh each time. Net effect is modest — recursive/call-heavy code is about 7% faster; pure arithmetic loops (no allocations to begin with) are unchanged. In the interest of not overclaiming: two more aggressive ideas (non-atomic reference counting; broader allocation pooling) were built and benchmarked, then **not** kept, because on this toolchain (glibc 2.39) both turned out to duplicate optimizations glibc already does transparently (single-threaded `shared_ptr` is already non-atomic in practice; the allocator's per-thread cache already does the pooling). A tree-walking evaluator's per-node cost was already close to its practical floor after v1.6.0; going further would mean the bytecode VM this project is deliberately deferring, not a tuning pass.

- Added error codes: `E4027` (`PureFunctionGlobalAccess`), `E4028` (`NetworkRequestFailed`).

- Tests: 12 new checks added directly to `tests/unit/limits_test.cpp` (the raised depth ceiling, both at and near the new ceiling), 2 new script cases (`pure_functions`, `constant_list_tuple`) and 10 new error cases (constant-list mutation via direct name/index/alias/argument, `pure` read/write violations, and `DLC:network`'s SSRF/HTTPS/malformed-URL/disabled-network error paths). `tests/run.sh` gained support for a per-test `.args` file (extra CLI flags), used by the network-disabled test. The suite now has 106 checks (was 94). `DLC:network`'s success paths (GET, POST, redirects, chunked decoding, JSON round-trip) were verified manually against a local test server, including under AddressSanitizer + UBSan, but aren't part of the automated suite, which shouldn't depend on live network access.

- **Verification.** Full suite + a battery of hostile/stress scripts run clean under AddressSanitizer + UBSan (176 files, 0 findings), including the new socket-handling code. Windows: this release added a real `x86_64-w64-mingw32-g++` cross-compile check (previously the Windows build was never actually compiled), which is how the macro-collision bug above was caught; the cross-compile is clean, but running the resulting binary could not be verified in this environment (no working Windows/Wine runtime available) — compilation only. macOS: not cross-compiled (no toolchain available here); the new platform-specific code follows the same POSIX APIs already used on Linux, with the two known Linux/macOS differences (`SO_NOSIGPIPE` vs `MSG_NOSIGNAL`, and `pthread_get_stackaddr_np`/`pthread_get_stacksize_np` vs `pthread_getattr_np`) handled explicitly, but this is unverified by actual compilation or execution.


---

## v1.6.0 - 2026-09-21

## Description

- **Fixed: deeply nested input could crash the process.** Nested parentheses, lists, maps, unary chains (`----1`, `!!!!true`), `if`/`loop`/`or_else` blocks, `await` chains, index chains and f-string expressions recursed on the native stack with no bound, so a hostile (or generated) script killed the process with a segfault. Parser recursion is now counted across every sub-parser, including the nested parser used for f-string expressions, and fails with the new `E2008` beyond 256 levels. Left-associative chains that the parser builds iteratively (`1+1+1+...`, `a[1][1]...`) are bounded by an AST height computed as each node is built (10,000). Source over 16 MiB is rejected with `E2009`. A number literal too large for a double is now a clean `E1003` instead of `Internal error: stod`.

- **Fixed: runtime stack exhaustion.** The 1,000-call limit did not protect functions whose bodies nest blocks: 40 nested `if`s inside a function recursing 990 deep segfaulted. The evaluator now compares the real stack pointer against a budget derived from the actual stack size and raises the catchable `E4017` (the same code as the call-depth limit) before it can overflow. Regex matching honors a hard floor below that budget and fails with `E3103` instead of overflowing on small stacks. Programs that ran before still run: a 990-deep recursion with six nested blocks per call and 10M-element lists work as before.

- **Fixed: circular and very deep values crashed.** `add a to a` followed by `print(a)`, `a is b` or `to_json(a)` recursed forever, and destroying a deeply nested list recursed once per level. Nested lists/maps are now destroyed iteratively; `is` compares iteratively (any depth, and circular structures of the same shape compare equal); printing marks cycles as `[...]`/`{...}` and stops at depth 1,000; `to_json` reports `E4025` instead of overflowing.

- **Regex hardening.** Group nesting is capped at 64 (`E3011`), quantifier counts at 100,000 (`E3004`; a huge count used to escape as `Internal error: stoi`), patterns at 64 KiB, and the compile cache at 512 entries. Every find/replace/split/count now has an overall time allowance (5 s plus 2 s per MiB of input) on top of the existing per-attempt limits, `replace` cannot build a result beyond the string limit, and `find ... g` no longer materializes capture groups for every match.

- **Size limits.** Strings are capped at 128 MiB and lists/maps at 32M items (`E4026`), checked where they can grow (`+`, f-strings, `join`, `repeat_str`, padding, `range`, `flatten`, `merge`, regex results, the async task queue). Running out of memory anyway is reported as `E6003` instead of terminating the process. All limits are in `engine/common/Limits.h`.

- **New opt-in execution budget.** `--max-steps <n>` (loop iterations + function calls) and `--timeout <ms>`, also available as `CuffEngine::Options`. Off by default. They raise `E6001`/`E6002`, a new 6000 "Resource" range that `or_else` deliberately cannot catch.

- **Module sandbox (behavior change).** `use name from path` may only load files inside the script's directory, or inside `--root <dir>` / `Options::rootDir`. Absolute paths and paths that resolve outside the root (`..`, or a symlink pointing out) fail with `E5006`; oversized or too deeply nested imports fail with `E5007`. Error messages show the path as written instead of the host's absolute path. A failed import no longer marks the module as loaded, and a module's AST now outlives a failure in its body, so functions it registered can no longer dangle. Scripts that import from a parent directory need `--root`.

- **New built-ins.** `type_of` (always available). `DLC:math`: `mod` (floored), `clamp`, `sign`, `trunc`, `log`, `log2`, `log10`, `exp`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pi`, `e`, and `round(x, digits)`. `DLC:string`: `trim_start`, `trim_end`, `index_of`, `repeat_str`, `pad_left`, `pad_right`, `char_code`, `from_char_code`. `DLC:list`: `sum`, `average`, `flatten`, `range`. `DLC:random`: `random_seed`, `choice`, `shuffle`. New `DLC:map`: `keys`, `values`, `has_key`, `entries`, `merge` (maps previously had no way to enumerate their keys). `length`, `contains` and `index_of` work on strings, lists and maps. `min`/`max` accept a single list.

- **Hardened built-ins (behavior changes).** `sort`, `min`, `max` and `clamp` reject NaN (sorting a list containing NaN was undefined behavior). `pow` reports domain errors and results too large to represent (previously `Infinity`/`NaN`); `log`, `asin`, `acos` and `mod` reject out-of-domain input. `to_number` accepts only plain decimal text: `"nan"`, `"inf"`, hex floats and trailing garbage are now `E4025`. Whole-number arguments and indices are range-checked to +/-2^53 (an index like `1e30` used to be undefined behavior; `random_int` used a 32-bit `long` under WebAssembly). `unique` is O(n) for numbers and strings. `upper`/`lower` map Latin, Greek and Cyrillic letters, not just ASCII. `await` now resolves a name the same way a plain call does (user function first).

- **Performance.** Strings are now immutable and shared: reading a variable or passing a string never copies its text, literals are built once at parse time, and ASCII-ness, codepoint count and a codepoint cursor are cached. Reading a 1 MB string variable 20,000 times: 19.7 s → 6 ms. A 20,000-character `s[i]` loop: 0.55 s → 12 ms. Declaring 20,000 globals: 3.8 s → ~0.1 s (the scope index is now maintained incrementally instead of rebuilt). Native and user functions are looked up by interned id, and cold error paths are out of line, which shrinks the stack used per call by about a quarter. Tokens are moved rather than copied between pipeline stages. Raw arithmetic speed (`fib(27)`, tight loops) is unchanged; the bytecode VM remains deferred (see section 21 of `docs/IMPLEMENTATION_NOTES.md`).

- Added error codes: `E1003` now covers oversized number literals; new `E2008`, `E2009`, `E3011`, `E4026`, `E5006`, `E5007`, `E6001`, `E6002`, `E6003`. `main.cpp` no longer syncs iostreams with stdio.

- Tests: new `tests/unit/limits_test.cpp` (45 checks: hostile nesting, cycles, size and step/time limits, module sandbox, and large legitimate inputs that must keep working), 6 new script cases (`builtins_*`, `strings_utf8_access`, `value_semantics`) and 21 new error cases. The suite now has 94 checks (was 66). Also run clean under AddressSanitizer + UBSan.

- Docs: `docs/IMPLEMENTATION_NOTES.md` sections 22 (limits and recursion safety), 23 (module sandbox) and 24 (built-ins and performance); `SECURITY.md` gained a "Running untrusted scripts" section; READMEs list the new CLI options and `DLC:map`. `Makefile.win` now links with an 8 MiB stack.


---

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
