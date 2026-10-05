## v3.2.0 - 2026-10-05

## Description

- **New: several libraries in one `use` statement.** `use DLC:math, DLC:string, DLC:convert` loads all three; one `use` per line still works and gives the same result. Every item in the list needs its own `DLC:` prefix (`use DLC:math, string` is a syntax error). A dangling comma and a forgotten comma (`use DLC:math DLC:string`) each get a specific message instead of a confusing `unexpected token ':'`. An unknown library in the middle of a list (`E5-004`) is pointed at directly. `SPEC.md` still requires a module-load command to fit on one line, so a list cannot be continued onto the next line. `UseStmt` now holds a list of `{name, loc}` for the DLC form.

- **New: typed function parameters.** `set func add_num(number n1, number n2) do:`. Each parameter is `name` or `type name`, with `number`, `str`, `boolean`, `list`, `map` or `match`. Typed and untyped parameters can be mixed. Passing a value of another type is a runtime error, `ParameterTypeMismatch` (`E4-030`), reported at the call site. For `async` functions the check runs before the call is queued, so the error points at the call. `empty` is accepted by every type, the same rule as `set`. A type keyword counts as a type only when a name follows it, so `(number)` and `(number, x)` remain valid untyped parameters. Functions with no typed parameter skip the check entirely.

- **Changed: error codes print as `E<category>-<number>`.** `E4005` is now `E4-005`, `E2001` is `E2-001`, and so on. The enum values in C++ are unchanged; only the printed tag is split, by `errorCodeTag()`. The dash keeps a tag unambiguous if a category ever needs two digits (`E10-001`). Capacity within a category is still 999 numbers. 158 test/example files (`.expected_code` files and `note: E####` headers) and all current docs were converted. Entries in this changelog from earlier releases keep the old format, as a record of what was printed then.

- **Fixed: errors inside an f-string `{...}` pointed at the wrong source line.** The expression is tokenized separately, so its positions started over at line 1, column 1, and the source snippet added in the previous release then displayed line 1 of the file for any error inside an f-string. Locations for tokens in the fragment, and for any error thrown while tokenizing or parsing it, are now pinned to the f-string literal, so the snippet shows the line the f-string is on. The caret sits at the start of the literal, not at the exact column inside the braces. Two cases added to `tests/unit/error_snippet_test.cpp`.

- **Fixed: `tests/run.ps1` did not work on Windows PowerShell 5.1.** It used `ProcessStartInfo.ArgumentList`, which does not exist on .NET Framework, so every test failed with a null-method error. Rewritten, and these were fixed on the way: output is decoded as UTF-8 (the OEM code page garbled Korean output and failed every diff); `\r\n` is normalized before comparing; stdin is closed so `input()` sees EOF instead of waiting for the timeout; g++ runs through the same helper rather than `& g++ 2>`, since under `$ErrorActionPreference='Stop'` PowerShell 5.1 turns any compiler warning on stderr into a terminating error, and the build timeout is 600 s instead of 10 s; unit tests link with `-lws2_32` and an 8 MiB stack on Windows, as `Makefile.win` does; the binary is `cuffc.exe` on Windows and `./cuffc` elsewhere, so it also runs under PowerShell 7 on Linux/macOS. Not executed on a real Windows machine (none available here).

- **Release binary is now stripped.** `-s` is added to `Makefile` and `Makefile.win`: 1.18 MB → 670 KB with no change in speed. It is skipped on macOS, where Apple's linker ignores the flag with a warning.

- **Performance: measured, nothing else worth changing.** Against the original engine: `fib(30)`, a 3M-iteration loop, 1M user-function calls, string/list/map work, f-strings and a 66k-line script are all at parity (differences inside run-to-run noise). The only profiler available was `gprof`, and its profile was flat (`evalExpr`, `evalBinaryOp`, scope lookup). Tried and not adopted: profile-guided optimization (0–4%, inside noise, needs a two-step build), `-O2` instead of `-O3` (same), static `libstdc++` (startup 2.2 → 1.7 ms but a 2.4× larger binary), and dropping `-fstack-protector`/`_FORTIFY_SOURCE` (about 0–8%, not worth weakening a sandboxing interpreter). A micro-suite (200k-element sort, `list_unique`, `split` into 150k parts, `replace`/`find`/`count` over 850 KB, a 50k-object JSON round trip, a 50k-key map) found no quadratic behavior.

- **Comments in `engine/` cut from 1,534 lines to 92.** Of 657 comment blocks, about 90 short ones remain: lifetime and ordering invariants, platform quirks (`windef.h` macros, Emscripten's shadow stack, `SIGPIPE`, `pthread_get_stackaddr_np`), stack-safety limits, the reason behind non-obvious checks, and the public `CuffEngine::Options` fields. Longer explanations are in `docs/IMPLEMENTATION_NOTES.md`. Checked mechanically: a string/char-literal-aware scanner found the comments; the preprocessed output of `main.cpp` is identical before and after once whitespace is ignored; every string and char literal in `engine/` is byte-identical; build and suite pass unchanged. `main.cpp`, `wasm/bindings.cpp` and `tests/` were not touched.

- **Docs.** `SPEC.md`: typed parameters (§9), comma-separated `use` (§13), new §14 on the error-code format and categories. `README.md` / `README_ko.md`: the `pure` paragraph still said the restriction covers only the function's own body, which was wrong since the call-graph hardening in the previous release, and is now corrected; typed parameters and the `use` list are documented; the error example shows the caret and the new tag format; the directory tree now lists `dlc/` and `net/`, which were missing. `docs/IMPLEMENTATION_NOTES.md`: new §35–39, plus the tag format in §20 and the f-string fix in §29. `docs/EXTENDING.md`: new §12–14 (parameter syntax, `use` syntax, comment policy). `SECURITY.md`, `tests/README.md` and `examples/README.md` updated; `examples/08_dlc_libraries.cuff` shows both `use` forms.

- Tests: 8 new checks (`typed_parameters`, `use_multiple_dlcs`, and the error cases `parameter_type_mismatch`, `parameter_type_mismatch_async`, `use_multiple_unknown_dlc`, `use_multiple_missing_prefix`, `use_multiple_missing_comma`, `use_multiple_dangling_comma`), and the f-string cases above. The suite is 124 checks, up from 116.

- **Verification.** Clean build, 124/124 passing. Also run clean: AddressSanitizer + UBSan over the whole suite, with a leak check on the new-feature scripts; libstdc++ debug mode (`_GLIBCXX_DEBUG`); `-pedantic -Wall -Wextra -Werror`; each header under `engine/dlc`, `engine/interpreter`, `engine/parser` and `engine/common` compiled standalone. The one known LeakSanitizer finding (the intentionally circular lists in `value_semantics.cuff`) is the same pre-existing one. Not done: nothing here was compiled or run on Windows or macOS, since no such toolchain exists in this environment. The new engine code is standard C++17 with no platform-specific API. `run.ps1` is reviewed by hand only.


---

## v3.1.0 - 2026-10-01

## Description

- **Removed: closures / nested function values.** The nested-function-as-closure feature added in the previous release has been reverted. Implementing capture the only way that didn't require rewriting `Environment`'s stack-allocated lifetime model — by value, once, at definition time — meant a closure over a scalar never accumulates state across separate calls (the textbook "counter closure" returning 1, 2, 3, ... on successive calls did not work; every call returned the same first value), while a closure over a list/map behaved as expected (shared by reference, like an ordinary argument). That asymmetry was judged too confusing for this language's intended audience to justify keeping a half-correct version of the feature. `set func` inside another function's body is once again a hard error (`NestedFunctionNotSupported`, `E4018`), exactly as before closures were introduced. `ValueType::Function`/`Closure` (`Value.h`), `Environment::collectCapturable()`, `callClosure`/`findClosure` (`Interpreter.h`), and the `set func NAME to EXPR` closure-typed-variable syntax are all gone. No other feature from the same release (pure-function hardening, the `change x to global` fix, reserved words as identifiers, the caret marker, `DLC:filesystem`, the DLC naming convention, or the `loop match` removal) is affected.

- **Async: two real bugs fixed, two behaviors locked in with tests — no architectural change.** Two error-hint messages quoted a keyword that doesn't exist in this language: `'set async function NAME(...) do:'` and `'set returnable function'` both said `function` where the actual declaration keyword is `func` (`throwAwaitOnNonAsync`, `throwReturnInVoidFunction`). Fixed. Separately, two behaviors that were already correct but had no regression test now do: a queued task that itself queues another queued task (`A` queues `B` queues `C`) still drains in the order queued (`tests/cases/async_self_queue_chain.cuff`), and an error thrown by a queued task still stops the drain the same way an error mid-script stops synchronous execution — tasks queued after the failing one don't run (`tests/errors/async_error_stops_queue.cuff`). The cooperative, non-concurrent scheduling model itself (documented in the previous release) is unchanged.

- **`engine/interpreter/NativeFunctions.h` split into `engine/dlc/`.** The single 1,748-line file holding every library's implementation is now one file per library — `MathDLC.h`, `StringDLC.h`, `TimeDLC.h`, `RandomDLC.h`, `ListDLC.h`, `MapDLC.h`, `ConvertDLC.h`, `NetworkDLC.h`, `FilesystemDLC.h`, `JsonDLC.h` — plus `DLCCommon.h` for what several of them share (the `expectArgCount`/`expectNumber`/`expectStr`/`expectList`/`expectMap` argument-checking helpers, and the `textutil` UTF-8 namespace). `NativeFunctions.h` is now ~100 lines: the always-on core builtins (`print`/`input`/`type_of`) and `registerDLC()`, the dispatcher every `use DLC:name` calls into. Every new file was verified to compile standalone (one `#include` + an empty `main()` each) rather than silently depending on include order. Pure reorganization — no declaration changed namespace, name, or behavior; full suite passes unchanged before and after.

- **New: `tests/run.ps1`.** A PowerShell equivalent of `tests/run.sh` for running the suite on Windows without WSL or Git Bash — same section headers, same pass/fail summary line, same exit-code convention. Not executed against a real Windows/PowerShell environment during development (none available); reviewed by hand instead, same caveat as the platform notes in the previous release.

- Tests: 2 new cases (`async_self_queue_chain`, and the error case `async_error_stops_queue`); 4 removed (`closures`, `closure_pure_leak`, `closure_call_non_function`, `nested_async_not_supported` — all specific to the now-reverted closure feature, including the one-off `NestedAsyncFunctionNotSupported` error code, which reverts back to the original `NestedFunctionNotSupported` under the same number, `E4018`). Net: 116 checks (previous release's 119, minus 5 closure-only checks — one of the four removed files had covered more than one assertion — plus 2 new async ones).

- **Verification.** Full suite rebuilt clean and passes (116/116) after both the revert and the file split; re-checked under AddressSanitizer + UBSan with the same clean result as the previous release (the one pre-existing, documented exception — intentional circular-structure tests triggering LeakSanitizer — is unchanged and unaffected by anything in this release). `use DLC:math` / `use DLC:string` / `use DLC:convert` together, in one script, was specifically re-tested on a report that it might not work — it does, both before and after this release's changes, so nothing needed fixing there.

---

## v3.0.0 - 2026-09-29

## Description

- **Pure-function hardening: the restriction now propagates through the call graph.** Previously `pure` only checked a function's own body, so `set pure func f() do: g() end` where `g` touches a global was allowed — `f` never touched a global *directly*. `checkPureCallAllowed()` now runs at every call site and rejects a pure caller invoking a non-pure user-defined function or closure, with a new error, `PureFunctionImpureCall` (`E4029`). Native/DLC functions remain exempt (they can't reach a CuffScript global through this route at all). **Behavior change**: `tests/cases/pure_functions.cuff`'s old "pure calling non-pure is fine" example no longer holds; it now lives in `tests/errors/pure_func_call_impure.cuff` as an `E4029` case.

- **Fixed: a real bug in `change x to global`.** `Environment::resolve()` checked whether a name was bridged to global *before* checking for an actual local at each scope level. Once bridged, any later local redeclaration of the same name in that call — most visibly a `loop repeat x to 1 ~ 3` reusing a bridged `x` as its own loop counter — became permanently invisible: every read silently returned the stale global instead of the loop's own value, with no error. Fixed by checking local-first everywhere, matching the "innermost declaration wins" rule the rest of the language already follows. Same bug also affected the `pure` global-access check, which could previously misreport a shadowed local as forbidden global access.

- **Reserved words can now be used as identifiers.** `add`, `count`, `find`, `split`, `replace`, `match`, `in`, `by`, `not`, `global` can now be used as variable, function, parameter, and loop-variable names, and read back in expressions. `match`/`find`/`replace`/`split`/`count` (which have their own expression syntax) use one token of lookahead to tell "the construct" from "just a name" apart, without breaking the ability to call or index something with one of those names.

- **New: caret (`^`) marker in error messages.** Errors now show the source line with a `^` under the exact column (codepoint-aligned, so multibyte UTF-8 text earlier on the line — Korean, emoji, ... — doesn't throw off the alignment).

- **New: `DLC:filesystem`.** Real local file access: `file_exist`, `file_size`, `file_read`, `file_readlines`, `file_write`, `file_add`, `file_remove`. Every path is confined to the exact same sandbox root `use ... from` module imports already use — an absolute path or a `../`-escape is rejected with a new error, `FilesystemAccessDenied` (`E5008`), before touching the filesystem. Symlinks pointing outside the root are also rejected (verified by hand). New host controls, mirroring `DLC:network`'s existing shape: `CuffEngine::Options::filesystemEnabled` / `--no-filesystem`. See `SECURITY.md`.

- **DLC function naming convention: `library_verb`.** Every DLC function name now carries its library as a prefix — `sqrt` → `math_sqrt`, `upper` → `str_upper`, `sort` → `list_sort`, `get`/`post` → `network_get`/`network_post`, and so on — so a script can tell which library a call came from without cross-referencing every `use` line, and two libraries can no longer silently shadow each other's bare names in the shared native-function table. Exceptions: `length`/`contains`/`index_of` (intentionally polymorphic across str/list/map) and `to_json`/`from_json`/`to_number`/`to_str`/`to_boolean` (already self-describing). **This is a breaking rename** — every example, test, and doc in this repository was updated; external scripts need the same mechanical rename per function (full mapping in `docs/IMPLEMENTATION_NOTES.md` §9/§31).

- **`loop match` removed.** It was implemented identically to `loop while` (same condition-recheck-every-iteration code path, same parsing) — a complete, confusing duplicate with no functional difference and no use anywhere in this repository. `loop while [condition] do: ... end` covers the same case. `LoopStmt::LoopKind` now has just `Repeat`/`While`.

- **New: nested functions / closures.** `set func` inside another function's body no longer errors — it creates a closure: a first-class value that can be assigned to a variable (`set func NAME to EXPR`, a new type-keyword use), passed as an argument, returned, and called by name, including recursively. Capture is **by value, once, at definition time** — not shared upvalues: a captured number/str/boolean is independent from that point on (a closure does *not* accumulate state across separate calls — the classic "counter closure" pattern does not work here), while a captured list/map is still shared by reference, exactly like an ordinary function argument. A closure created inside a `pure` function is automatically forced pure regardless of its own `pure` keyword, closing the same escape hatch the call-graph hardening above closes for named functions. Top-level function declarations are unaffected (still not values, still the fast named registry). Nested `async` functions are explicitly rejected for now with a clear error (`NestedAsyncFunctionNotSupported`, reusing error code `E4018`) rather than silently running wrong or dropping their capture. Calling something that isn't a function now says so specifically (`'x' is a number, not a function`) instead of a generic "undefined function".

- **Async concurrency: reasoned decision not to add OS threads.** Evaluated a full design (GIL-style lock + worker-thread pool, releasing the lock only around a queued task's blocking network I/O, with per-call-stack state made thread-local so concurrently-running tasks can't corrupt each other's bookkeeping) and chose not to ship it. Genuine concurrent execution needs either OS threads or turning the evaluator into a resumable state machine; the rewrite is out of scope, and the threading approach is a permanent complexity and correctness tax on every future change to the interpreter, for a feature (overlapping network I/O) most scripts will never exercise. No code changed for this item — `f()` on an `async` function still queues it for after the top-level script's synchronous code finishes (strict FIFO order, self-queued tasks included); `await f()` still runs it immediately and synchronously. This is cooperative scheduling, not concurrency, and is now documented as such.

- Added error codes: `E4029` (`PureFunctionImpureCall`), `E5008` (`FilesystemAccessDenied`). Reused (repurposed, previously unused after the closures change): `E4018`, now `NestedAsyncFunctionNotSupported` instead of the old blanket `NestedFunctionNotSupported`.

- **Fixed: a flaky test-runner bug, unrelated to the engine.** `tests/run.sh`'s error-code check piped a captured error message into `grep -q` under `bash -o pipefail`; `grep -q` exits at its first match, which can make the upstream `echo` die of `SIGPIPE` — pipefail then reports the whole pipeline as failed even though the message matched. Measured at roughly 1 false failure in 1,500 runs on a ~450-byte message, worse on longer ones. Switched to a here-string (`grep -q "..." <<<"$actual"`), which has no pipe to race; confirmed at 0 failures in 6,000 stress-runs of the old failure pattern.

- Tests: 4 new script cases (`closures`, `dlc_filesystem`, `global_bridge_shadowing`, `reserved_words_as_identifiers`), 7 new error cases, and a new C++ unit test file (`error_snippet_test.cpp`, 3 checks for the caret marker). The suite now has 119 checks (was 106).

- **Verification.** Full suite passes clean under AddressSanitizer + UBSan, with one caveat: `tests/cases/value_semantics.cuff` intentionally builds circular list/map structures (`add a to a`), and `shared_ptr` reference cycles never reach a zero refcount, so LeakSanitizer correctly flags them — this is a pre-existing architectural tradeoff (predates this change; the same construct leaked before it too), not a new bug, and every other script in the suite is leak-clean including all new closure and filesystem code (closure self-recursion in particular was specifically checked, since it could easily have introduced its own reference cycle — it doesn't). Also run clean: `-pedantic`, and libstdc++'s debug-iterator mode (`_GLIBCXX_DEBUG`). One real, if minor, regression was caught and fixed during ASan verification: identifier-token handling inlined into the parser's hot recursive-descent path enlarged that function's stack frame enough to measurably reduce how deeply `((((...))))`-style nesting could go before the existing stack-depth guard trips (462 vs. 483 levels under ASan's larger frames) — moved into a dedicated `noinline` helper, which recovered it (506 vs. 483, so no worse than before, if anything slightly better from the refactor). Windows: not cross-compiled this round (no `mingw-w64` toolchain available in this environment); the new code is all standard C++17 + `<filesystem>` (already an existing, working dependency in this codebase's module-loader), introduces no new platform-specific APIs, and follows the same patterns as the existing, already-Windows-tested code — reviewed by hand rather than compiled. macOS: not cross-compiled or reviewed against real Windows/macOS toolchains either — same caveat as prior releases' entries for this project.

---

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

## v1.3.0 - 2026-09-15

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

## v1.2.1 - 2026-09-12

## Description

- Add npm package for web IDE

---

## v1.2.0 - 2026-09-09

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

## v1.1.0 - 2026-09-08

## Description

- Make `IMPLEMENTATION_NOTES.md` for language growth.

---

## v1.0.0 - 2026-09-08

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
