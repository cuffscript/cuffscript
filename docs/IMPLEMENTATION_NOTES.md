# Implementation Notes

`docs/SPEC.md` and `docs/REGEX.md` define most of the language's syntax and meaning, but
some points are deliberately (or accidentally) left unspecified. This document records how
the actual interpreter (`engine/interpreter/`) implements those points, and why it was
decided that way. Anything where the spec and the real behavior might read differently should
be here — if you find a new one, please add it to this document.

## 1. The `async` / `await` execution model

`SPEC.md` (section 6.5) states the model; this item records how the engine implements it and why
(improved in v0.2.0 — see "change history" below).

- When an `async` function is **called with `await`**, it runs to completion right there,
  synchronously, with no event loop or thread, and the value comes back immediately.
- When an `async` function is **called without `await`**, it does not run now. The call goes
  into an internal queue and runs, in the order queued (FIFO), **after all of the top-level
  script's synchronous code has finished**. The return value of a queued call cannot be
  observed (you did not use `await`, so there is no way to receive the value) — `empty` is
  always returned immediately.
    - The queue is drained exactly once, when the whole script finishes. Draining midway (for
      example at the end of each top-level statement) was considered, but then "the async job
      finished before the line right after the one that queued it even ran", which felt no
      different from immediate execution. The rule "it runs after the synchronous code is
      all done" is far easier to understand and actually shows that it runs "later".
      (See `examples/10_async_ordering.cuff`.)
    - No real threads or parallelism are used at all — the interpreter's shared state
      (`Environment`, the function table, the regex cache, etc.) is not thread-safe, so
      introducing true concurrency would be a big job that means wrapping all of it in
      mutexes. Putting stability first, only "cooperative deferred execution" was implemented.
- Using `await` on a function that is not `async` raises an `AwaitOnNonAsync` runtime error —
  this way `await` still works as documentation saying "this function is asynchronous".
- `async` and `returnable` are separate modifiers, so they can be used together
  (`set async returnable func ...`), as `SPEC.md` section 6.4 specifies for all modifiers.

## 2. What `.` (the period) means in regex

The escapable-symbol list in `docs/REGEX.md` includes `.`, which can make it read as if `.`
had a special meaning ("any one character", as in other regex languages). But CuffScript
already provides an explicit `[any]` token for that role, and "eliminating the complicated
special symbols entirely" is the language's core design philosophy. Therefore:

- **`.` is a literal character by default** (it is treated like any other character). This
  choice keeps behavior from changing unexpectedly when you compare ordinary strings in which
  periods commonly appear — emails, file names, version strings — with `is`.
- `\.` also produces a literal period in exactly the same way (allowed but not required).

## 3. Quotes inside an f-string

An f-string is wrapped in outer double quotes (`"`). If you need a string literal inside a
`{...}` expression (for example to access a key of a map/match result), you **must use single
quotes (`'...'`).** If you use double quotes inside, the tokenizer takes the f-string to end at
that point. This is why the `f"Year: {res['year']}"` example in `docs/REGEX.md` section 23
deliberately uses single quotes — this engine actually enforces that convention.

`{{` and `}}` print as a literal `{` and `}` respectively.

## 4. Type checking at declaration time

`set <type> <name> to <value>` checks that the value's runtime type matches the declared type
**only at the moment of declaration**. `change` can later reassign a value of any type (the
type is not re-checked) — this matches the language's overall "dynamic typing style"
philosophy. The one exception: an `empty` value is always allowed regardless of the declared
type. This is so that the `empty` returned when `find`/`match` fails can be stored as is in a
variable of the type you want, and then handled with `or_else` or an `is empty` check.

## 5. When constant names are checked

Both "a constant name that is not all uppercase" and "a `change` on a constant" are checked
**at runtime, when that statement executes** (not at parse time). A constant declaration
inside code that only runs conditionally is not verified until that branch actually executes.

## 6. How the target of a collection `remove` is decided

For `remove <value> from <collection>` on a list: if the value is a **number**, it is treated
as a 1-based index; for any other value, the engine looks for the **value itself** in the list
and removes it (if it is not there, `ElementNotFoundError`, which `or_else` can catch). On a
map it removes by string key, and deleting a key that does not exist quietly does nothing
(the same as the conventional map-delete behavior of other languages).

## 7. `loop match` removed

There was once a third loop form, `loop match <target> is/IS <state> do: ... end`, but the
interpreter implemented it exactly like `loop while` — only as re-checking the condition on
every iteration (even the parser code that parses the condition expression was the same). It
was a complete duplicate that nobody used and that only caused confusion, so it was removed
from the language. `LoopStmt::LoopKind` now has only the two values `Repeat`/`While`. If you
need the same effect, just use `loop while [condition] do: ... end`.

## 8. Module merging (`use ... from ...`)

- A module file is resolved at the path `running script's directory / <path> / <name>.cuff`.
- A module's top-level functions are registered in the interpreter's global function table and
  become visible automatically.
- A module's top-level variables are copied, after it runs, into the scope that called `use`.
  Since a function is not given a view of global variables on its own (see item 40), a module
  function that reads one of its module's top-level variables must bridge it with
  `change name to global` first; the bridge resolves against the importing script's globals,
  where the module's variables were copied.
- If the same module is loaded more than once (including diamond or circular imports), the
  second and later loads quietly do nothing — this prevents infinite loops while handling the
  common cases safely.

## 9. The `DLC:*` built-in libraries

`SPEC.md` (sections 9 and 10) names the libraries and what they offer. This table is the complete
list of functions this engine registers for each.

| Library           | Functions provided                                                                                                                                                                                                                                                           |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `DLC:math`        | `math_sqrt`, `math_abs`, `math_pow`, `math_round`, `math_floor`, `math_ceil`, `math_trunc`, `math_sign`, `math_min`, `math_max`, `math_clamp`, `math_mod`, `math_log`, `math_log2`, `math_log10`, `math_exp`, `math_sin`, `math_cos`, `math_tan`, `math_asin`, `math_acos`, `math_atan`, `math_atan2`, `math_pi`, `math_e` |
| `DLC:string`      | `str_upper`, `str_lower`, `str_trim`, `str_trim_start`, `str_trim_end`, `length`, `contains`, `index_of`, `str_starts_with`, `str_ends_with`, `str_repeat`, `str_pad_left`, `str_pad_right`, `str_char_code`, `str_from_char_code`                                          |
| `DLC:time`        | `time_now`, `time_timestamp`                                                                                                                                                                                                                                                   |
| `DLC:random`      | `random_float`, `random_int`, `random_seed`, `random_choice`, `random_shuffle`                                                                                                                                                                                                 |
| `DLC:list`        | `list_sort`, `list_reverse`, `list_join`, `list_unique`, `list_sum`, `list_average`, `list_flatten`, `list_range`, `length`, `contains`, `index_of` — all return a new value without changing the original                                                                    |
| `DLC:map`         | `map_keys`, `map_values`, `map_has_key`, `map_entries`, `map_merge`, `length`, `contains` — see items 22–24                                                                                                                                                                    |
| `DLC:convert`     | `to_number`, `to_str`, `to_boolean` — explicit type conversion (also core built-ins, so they are always usable without `use`)                                                                                                                                                  |
| `DLC:json`        | `to_json`, `from_json` — see item 19 below                                                                                                                                                                                                                                      |
| `DLC:network`     | `network_get`, `network_post` — a real HTTP/1.1 client. See the "DLC:network" entry in `SECURITY.md`                                                                                                                                                                            |
| `DLC:filesystem`  | `file_exist`, `file_size`, `file_read`, `file_readlines`, `file_write`, `file_add`, `file_remove` — see item 30 below and the "DLC:filesystem" entry in `SECURITY.md`                                                                                                           |

Only `length`/`contains`/`index_of` are exceptions with no prefix — they are deliberately
designed to share the same polymorphic implementation (`nativeLength`/`nativeContains`/
`nativeIndexOf`) across the str/list/map types, and splitting them into `str_length`/
`list_length`/`map_length` would instead lose the advantage of these functions that "accept
any value". `to_json`/`from_json`/`to_number`/`to_str`/`to_boolean` were also left as they are,
because their names already show their direction/domain. All other function names were
unified to the `library_verb` form (for example `math_sqrt`, `str_upper`, `list_sort`) —
in earlier versions they had no prefix at all, like `sqrt`, `upper`, `sort`, so you could not
tell which library a function belonged to from the name alone, and there was also a risk of
(unintended) collisions when several libraries registered the same name. **This is a
backward-incompatible change** — existing scripts have to rename every function to the new
names.

What was found while adding `DLC:list`/`DLC:convert`: when a library name overlapped with a
language reserved word such as `list`, `count` or `find`, there was a bug where `use DLC:list`
did not even parse (because `ImportParser` accepted only `IDENTIFIER` tokens as names). It is
now fixed so that any "token made of letters", whether IDENTIFIER or reserved word, is
accepted as a name — so the problem will not recur no matter which DLC/module names are added
in the future. It later turned out that the same problem existed across variable/function/
parameter/loop-variable name declarations in general (`add`, `count`, `find`, `split`,
`replace`, `match`, `in`, `by`, `not` and `global` could not be used in that position), which
item 28 covers as a whole.

## 10. Safety nets for regex matching

The "maximum match step count (Step Limit) and time limit (Timeout)" that `docs/REGEX.md`
requires are implemented directly (`engine/regex/RegexMatcher.h`): the defaults are 200,000
steps, 500 ms, and a recursion depth of 3,000. If any of the three is exceeded, matching
stops and a recoverable `RegexRuntimeError` is thrown (the script does not hang, and can
handle it with `or_else`).

## 11. Lists and maps are reference types

Lists and maps are implemented with `shared_ptr`, so when assigned to a variable or passed as
a function argument the value is not copied and the same storage is shared (the same as
Python/JS). Numbers, strings, booleans and `empty` are copied by value.

Because they are reference-counted, a list or map that contains itself (`add a to a`) is never
freed: the reference cycle keeps it alive until the process exits. Printing and comparing such
structures is safe (see `tests/cases/value_semantics.cuff`), and a one-shot `cuffc` run is
unaffected, but a long-lived host that runs many scripts in one process should be aware that a
script which builds cycles leaks that memory.

## 12. Functions are not values

Since the spec explicitly says that closures and nested functions are not supported, a function
is not a kind of `Value`; it is managed in a separate name table inside the interpreter
(`userFunctions_`). A function declaration that is not at the top level (nested inside another
function) throws a `NestedFunctionNotSupported` error.

(During v1.7.1 development this restriction was once lifted and closures treated as values
were introduced, but the capture was implemented as "by value, once at definition time" — the
only approach that could be implemented safely without the far bigger change of turning the
stack-based `Environment` into a heap-based one — and as a result a closure that captured a
scalar such as a number or string did not accumulate state between calls. The counter
pattern, the "classic example" of closures, did not work; a half-finished feature like that
did not fit the direction of this language (for beginners, easy and light), so it was removed
and this item was restored.)

## 13. `return`/`stop` are not C++ exceptions (performance + correctness)

In v0.1.x, `return`/`stop` were implemented as C++ exceptions (`ReturnSignal`/`StopSignal`)
and caught with `catch` at function-call/loop boundaries. That had **two problems**.

- **Performance**: throwing and catching a C++ exception costs microseconds. Because that cost
  was paid every time a function `return`ed, recursion-heavy scripts spent most of their run
  time on it. Measured: for `fib(27)` (about 630,000 calls) it **improved from 2.75 s to
  0.26 s** (about 10.7×).
- **Correctness**: the `catch` that caught `stop` existed only around loop bodies, so using
  `stop` inside a function (without a loop of its own in that function) passed straight through
  the function-call boundary and had a bug that **cut off the loop of whoever called that
  function**. On top of that, using `stop` or `return` at the top level with no loop/function
  let an exception that nobody could catch escape out of `main()`, and **the process died**
  (`std::terminate`, SIGABRT).

Now `execStatement`/`execBlock`/`execIf`/`execLoop` all return an ordinary value called
`ExecOutcome` (`Normal`/`Return`/`Stop` + value + location) and pass it upward
(`engine/interpreter/Signals.h`). `catch` is used only for real `CuffError`s (actual errors).
As a result:

- `stop` is explicitly consumed by `execLoop` and never leaks out to whoever called the
  function. Using `stop` without a loop inside a function is now a clean `StopOutsideLoop`
  runtime error (not a crash).
- Using `return`/`stop` at the top level is handled cleanly as a `ReturnOutsideFunction`/
  `StopOutsideLoop` error (again, not a crash).

When you add new control flow (for example `continue`, `break with label`), add a new case to
`ExecOutcome` and handle that signal explicitly at the point that should "consume" it (a loop?
a function?) — do not bring exceptions back, or the performance problem eliminated this time
comes right back.

## 14. String indexing/slicing is by UTF-8 code point

`str[i]`, `str[i~j]` and `length()` (DLC:string) work by **UTF-8 code point**, not by byte
(`engine/common/Utf8.h`). A precomposed Hangul syllable is a single code point, so
`"안녕하세요"[1]` correctly returns "안" — previously it was byte-based, so indexing/slicing a
Hangul string cut through the middle of a multi-byte sequence and produced broken bytes.

Grapheme clusters (jamo composition, emoji ZWJ sequences, etc.) are not handled — code points
suffice because, as long as Hangul is typed as precomposed (NFC) rather than composed jamo,
each character is already a single code point. `contains`/`starts_with`/`ends_with`/`+`
(concatenation) were left byte-based as they always were — UTF-8 is a self-synchronizing
encoding, so byte-wise search/concatenation never crosses a code point boundary and already
works safely.

The regex engine also works per code point — see item 17 below.

## 15. Type conversion (`to_number`/`to_str`/`to_boolean`) is built in by default

They can be used right away without `use DLC:convert` (`registerBuiltins` calls
`registerConvertDLC` internally). They still work if you write `use DLC:convert` — it only
registers the same functions once more, which is harmless. The other DLC functions
(`math`/`string`/`list`/`random`/`time`) still need `use` — converting a string received from
`input()` into a number is a very common pattern, so it was moved into the defaults, but making
everything else built in as well would leave `use` itself meaningless, so that was not done.

## 16. Regression tests (`tests/`)

`bash tests/run.sh` runs all of `tests/cases/` (success cases, output diff), `tests/errors/`
(failure cases, error code check), `examples/` and `examples/error_cases/`. When you add a
feature or fix a bug, add the related case to `tests/` as well (see `tests/README.md`). It runs
automatically on every push/PR from `.github/workflows/build-and-test.yaml`.

Two real bugs were found and fixed while building these tests:

**The regex recursion depth limit was too high, causing a real stack overflow (SIGSEGV).**
Matching a pattern that recurses deeply and linearly, such as `[any]+`, against a 25,000
character string killed the process — `depthLimit` had been set to 20,000, but on measurement
the real crash with an 8 MB stack already happened near ~19,500, so the real stack blew before
the safety net I had built (throwing an exception) could even trigger. `depthLimit` was
lowered to 3000 (more than 6× headroom against the observed crash point), and it was
confirmed that everything from 100 characters up to 1,000,000 characters either terminates
normally without a crash or cleanly throws `RegexRecursionLimitExceeded` (E3-103). Lesson: even
a number you thought was "conservative enough" cannot be trusted without measuring.

**The range of cases where a reserved word cannot be used as an identifier was wider than
expected (recorded here when found; fixed later — see item 28).** `set number add to 5` and
`set returnable func add(...) do:` both produced parse errors — common words used as syntax
keywords, such as `add`/`count`/`find`/`split`/`replace`/`match`/`in`/`by`/`not`/`global`,
could not be used as variable or function names at all. It has the same cause as the DLC-name
problem in `ImportParser` (a strict check of only `TokenType::IDENTIFIER`), but this time it was
spread across every point that declares a name, such as `DeclarationParser`/`FunctionParser`, so
the scope was larger. `tests/errors/argument_count_mismatch.cuff` had originally tried to use
the function name `add`, ran into this problem, and worked around it with `combine`.

## 17. Regex matching is codepoint-based (not byte-based)

_(Notes from here on are written in English.)_

The matcher walks the subject string one **UTF-8 codepoint** at a time, not one byte.
Positions are still stored as byte offsets internally — that keeps `substr()` on captures
free and avoids building an index table per match — but every place that _advances_ a
position now consumes a whole codepoint (`engine/common/Utf8.h`).

What this changes, concretely:

- `[any]` matches one codepoint. `"안녕하세요" is "[any]5"` is now true (it was false
  before, because the string is 15 bytes).
- A literal non-ASCII character written directly in a pattern (`"안녕"`) is compiled into a
  single multi-byte literal node and compared as one unit, instead of byte-by-byte.
- Negated sets (`[!num]`, `[!a-z]`) match a non-ASCII codepoint. This is the one case where
  a named class _can_ apply to multi-byte text, and it's handled by testing the whole
  codepoint rather than each byte.
- `search()` only ever starts an attempt at a codepoint boundary, and `[one:...]`
  alternatives must also _end_ on one — otherwise a match could span a partial character
  and `substr()` would produce mojibake.
- `[edge]` treats any non-ASCII codepoint as a word character, so `[edge]안녕[edge]` works.
  This follows the spec's own definition (REGEX.md section 17: the boundary between a word
  and whitespace/punctuation/string-start/end).

What deliberately stays ASCII-only, per the spec's explicit wording:

- `[let]` (English alphabet), `[low]`, `[up]`, `[str]` (English letters + digits),
  `[word]` (English letters + digits + underscore — an _identifier_ class), `[num]`, `[hex]`, and the
  `[int]`/`[float]`/`[email]`/`[phone]`/`[url]` presets. `"안녕" is "[let]+"` is false.
- `[any]` is the token for "any character in any language" — REGEX.md describes it as
  "every letter and symbol in the world except a line feed".

So `[word]` excluding Korean while `[edge]` includes it is not an inconsistency: `[word]` is
documented as an identifier class, `[edge]` as text segmentation. They serve different jobs.

Case-insensitive matching (`IS`, flag `i`) remains ASCII-only folding — correct Unicode case
folding needs a real Unicode table, and it is a no-op for scripts without case (Hangul, CJK,
Thai, ...), which is the common case here. Grapheme clusters (combining jamo, emoji ZWJ
sequences) are likewise out of scope, same reasoning as section 14.

## 18. Indices and range bounds must be whole numbers

`list[i]`, `str[i]`, `x[i~j]`, and `loop repeat i to A ~ B` all reject a fractional value
with `FractionalIndex` (E4-024) instead of silently rounding it, which is what the engine
used to do. A computed index that lands on `2.5` almost always means the _calculation_ is
wrong; rounding it hides the bug and produces a plausible-looking wrong answer.

Whole-valued doubles still work, since CuffScript has a single `number` type and `6 / 2`
legitimately produces `3.0` — only genuinely fractional values are rejected. Round
explicitly (`round`/`floor`/`ceil` from `DLC:math`) when that's what you mean.

## 19. `DLC:json`

JSON maps onto the value model almost exactly, so the mapping is the obvious one:
object ↔ `map`, array ↔ `list`, string ↔ `str`, number ↔ `number`, `true`/`false` ↔
`boolean`, `null` ↔ `empty`. `to_json(value)` produces compact output;
`to_json(value, indent)` pretty-prints with `indent` spaces (0-10).

Parsing is deliberately **strict** (RFC 8259): trailing commas, single-quoted strings,
unquoted keys, leading zeros, `NaN`/`Infinity`, and trailing content after the value are all
rejected. Being lenient here is how malformed data gets into a system unnoticed — a parse
error at the boundary is much cheaper than a wrong value deep inside a program. `\uXXXX`
escapes are decoded, including surrogate pairs, so `"\ud83d\ude00"` round-trips to a real
emoji rather than two broken halves. Output leaves UTF-8 bytes unescaped, keeping Korean and
emoji readable instead of `\uXXXX` soup.

Serializing a `match` result, `NaN`, or `Infinity` fails rather than inventing a
representation JSON doesn't have. Nesting is capped at 200 levels so a hostile input can't
overflow the stack during parsing.

## 20. Error-code hygiene

Two conventions, both checked during a full audit of every throw site:

- **Message capitalization is uniform**: every error message starts lowercase, because it's
  always rendered after a `...at line N, column M: ` prefix. (Parser messages used to start
  uppercase while runtime messages started lowercase.)
- **`ArgumentError` means the wrong _number_ of arguments; `ValueError`
  (`InvalidArgumentValue`, E4-025) means the right number but a value the function can't
  use** — `sqrt(-1)`, `to_number("abc")`, malformed `from_json` input, `random_int(5, 1)`.
  These previously all reported as E4-010 `ArgumentCountMismatch`, which was simply the wrong
  code for them.

When adding a builtin, pick the code by what actually went wrong, and keep the message
lowercase. Add a hint only when there's a concrete next action for the reader — a hint that
just restates the message is noise.

**Tag format.** Codes are printed as `E<category>-<number>` with a 3-digit zero-padded number
(`errorCodeTag()`, `ErrorCodes.h`): `4005` → `E4-005`, `2001` → `E2-001`. The enum values
themselves (`ErrorCode::DivisionByZero = 4006`, ...) are unchanged — only the printed tag is
split into category and number, so the 4005-style value in C++ and the `E4-005` seen by users
are the same code. The dash is what keeps the tag unambiguous when a category number needs
more than one digit (a 10th category would print as `E10-001`, where the old style would have
printed `E10001`). Capacity within a category is still 999 numbers. Every `.expected_code`
file, `note: E####` header comment, and doc example was converted; `tests/run.sh`/`run.ps1`
match the bracketed tag as a plain substring, so they needed no change.

## 21. Interpreter performance work

Two structural changes, both driven by `gprof` profiles of a recursion-heavy benchmark
rather than guesswork. The lesson from both: the bottleneck was not where it seemed.

**Operators are enums, not strings.** `BinaryOp`/`UnaryOp` used to store the operator as a
`std::string` (`"+"`, `"is"`, ...) and `evalBinaryOp` dispatched with an
`if (op == "is") ... else if (op == "+")` chain. The profile showed **16.2 million string
comparisons** for a 635k-call benchmark — roughly 25 per call, by far the single largest
cost in the engine. The chain is now a `switch` over `BinOp`/`UnOp` enums assigned at parse
time. Measured on `fib(27)`: 0.241s → 0.149s (38% faster).

**Variable and function names are interned to integer IDs** (`engine/common/NameInterner.h`).
The parser assigns each identifier a dense `uint32_t`; `Environment` stores and compares
those instead of strings, so a scope lookup is an int compare over a contiguous vector. A
benchmark with 400 globals went from 0.059s to 0.027s. Names are still kept alongside the
IDs for error messages, and a string-taking `declare` overload remains for the cold module-
merge path.

Where it stands now, versus before any of this session's work:

| benchmark                       | before |  after |
| :------------------------------ | -----: | -----: |
| `fib(27)` (635k calls)          | 0.241s | 0.140s |
| 2M-iteration loop with `change` | 0.287s | 0.173s |
| 400 globals, 200k lookups       | 0.059s | 0.027s |
| 3-argument call overhead        |  252ns | ~145ns |

**What would come next, and why it hasn't been done.** The profile is now dominated by
`evalExpr` itself — AST node dispatch and `Value` copies — which is the intrinsic cost of a
tree-walking interpreter. Getting substantially past this means compiling to bytecode and
running a stack VM, which is a rewrite of the execution core rather than a refactor of it.
That is a reasonable next step if a real workload demands it; it is not worth the risk on
speculation, and the current engine is in the same performance range as CPython on
call-heavy code.

## 22. Recursion safety and resource limits

Every native-stack recursion in the engine is now bounded, so no script can crash the
process; hostile or accidental input ends in an ordinary error code instead. All numeric
limits live in one file, `engine/common/Limits.h`.

**Parser.** `ParseDepthScope` (in `ParserCore.h`) counts native recursion across all parsers,
including the nested parser used for f-string expressions, and fails with `E2-008` beyond
256 levels of nested parentheses, lists, maps, unary chains or blocks. Chains that the parser
builds iteratively (`1+1+1+...`, `a[1][1]...`) are bounded by `Expr::height`, computed when
each node is constructed (`E2-008` beyond 10,000). Source larger than 16 MiB is rejected
(`E2-009`).

**Evaluator.** A depth counter alone cannot bound stack use: a function body with many
nested blocks uses far more stack per call than a flat one. So `evalExpr` and `execStatement`
compare the real stack pointer with a budget derived from the actual stack size (on Linux,
`pthread_getattr_np`; elsewhere a fixed default, `Interpreter::Config::stackBudgetBytes` to
override). Exceeding it raises the catchable `E4-017`, the same code as the call-depth limit
(1,000 calls, unchanged). Regex matching checks a hard floor below that budget, so it fails
with `E3-103` rather than overflowing when the interpreter is already deep.

**Values.** Nested lists/maps are destroyed iteratively (`dismantleValues`), and `is` compares
iteratively, so depth never matters; circular structures of the same shape compare equal.
Printing marks cycles (`[...]`, `{...}`) and stops at depth 1,000. `to_json`/`from_json` stop at
depth 200.

**Sizes.** Strings are capped at 128 MiB and lists/maps at 32M items (`E4-026`), enforced where
they can grow: `+`, f-strings, `join`, `repeat_str`, padding, `range`, `flatten`, `merge`,
regex `replace`/`find`/`split` results and the async task queue. Running out of memory anyway
is reported as `E6-003` instead of terminating the process.

**Execution budget (opt-in).** `--max-steps N` and `--timeout MS` (or `CuffEngine::Options`)
bound loop iterations plus function calls, and wall-clock time. Both are off by default. They
raise `E6-001`/`E6-002`, which live in the 6000 range and are deliberately **not** catchable by
`or_else`, so a script cannot swallow its own kill switch.

**Regex.** Group nesting is capped at 64 and quantifier counts at 100,000; a pattern over
64 KiB is rejected. Each find/replace/split/count also has an overall time allowance
(5 s plus 2 s per MiB of input) on top of the existing per-attempt limits, and the compile
cache is bounded (512 entries).

Tuning: the numbers were chosen so that ordinary programs never notice them (10M-element
lists, 1,500-term expressions, 2M regex matches and 990-deep recursion with rich bodies all
still work). Raise them in `Limits.h` if a workload needs more.

## 23. Module sandbox

`use name from path` may only load files inside a root directory: the running script's
directory by default, or `--root <dir>` / `CuffEngine::Options::rootDir`. Absolute paths and
paths that resolve outside the root (`..`, or a symlink pointing out, checked on the
canonicalized path) fail with `E5-006`. A module must be a regular file no larger than the source
limit, and imports may nest 64 levels (`E5-007`). Errors show the path as written in the script
rather than the host's absolute path. A failed import no longer marks the module as loaded,
and its AST is kept alive even if its body fails, so functions it registered can never dangle.

## 24. Built-in functions and performance

**Added.** `type_of` (always available); `DLC:math` `mod` (floored), `clamp`, `sign`, `trunc`,
`log`, `log2`, `log10`, `exp`, trig functions, `pi`, `e`, `round(x, digits)`; `DLC:string`
`trim_start`, `trim_end`, `index_of`, `repeat_str`, `pad_left`, `pad_right`, `char_code`,
`from_char_code`; `DLC:list` `sum`, `average`, `flatten`, `range`; `DLC:random` `random_seed`,
`choice`, `shuffle`; and a new `DLC:map` with `keys`, `values`, `has_key`, `entries`, `merge`.
`length`, `contains` and `index_of` work on strings, lists and maps and are available from
whichever of `DLC:string`/`DLC:list`/`DLC:map` is imported. `min`/`max` also accept a list.

**Hardened.** `sort`, `min`, `max` and `clamp` reject NaN instead of misordering (sorting NaN
was undefined behavior). `pow`, `log`, `asin`, `acos`, `mod` reject domain errors and results that
are not finite. `to_number` accepts only plain decimal text (no `nan`, `inf`, hex floats or
trailing garbage). Whole-number arguments are range-checked to +/-2^53 (`random_int` used a
32-bit `long` under WebAssembly). `unique` is O(n) for numbers and strings. `upper`/`lower`
map Latin, Greek and Cyrillic letters, not just ASCII. Number formatting no longer builds an
`ostringstream` per value.

**Performance.** Strings are immutable and shared (`StrData`): reading a variable or passing a
string never copies its text, literals are built once at parse time, and ASCII-ness, codepoint
count and a codepoint cursor are cached, so indexing and `length` are O(1) for ASCII text and
amortized O(1) for sequential access on other text (a 1 MB string read 20,000 times went from
19.7 s to 6 ms; a 20,000-character index loop from 0.55 s to 12 ms). Environments keep their
name index up to date incrementally (20,000 globals: 3.8 s to 0.05 s). Native and user
functions are looked up by interned id; cold error paths are out of line, which shrinks the
hot recursion frames and the stack used per call by about a quarter. Tokens are moved rather
than copied between pipeline stages.

## 25. v2.0.0: nesting ceiling, tuples, `pure`, `func`, and DLC:network

**Nesting depth raised to 512.** `limits::kMaxParseDepth` (256 -> 512) governs source-level
nesting: parentheses, list/map literals, and `if`/`loop`/`or_else` blocks. Raising the counter
alone would have been unsafe on a small native stack (the parser recurses on the real C++
stack with no bytecode to unwind into) — measured empirically, 512 levels of nested
parentheses can need over 1 MiB of native stack, more than Windows' 1 MiB default thread
stack. The parser now also primes a stack-pointer floor at the start of every `Parser::parse()`
call (`ParseStackFloorScope`/`parseStackFloor()` in `ParserCore.h`), sized from the real
available stack (`availableStackBytes()`, extended below) and checked by every
`ParseDepthScope`, mirroring the interpreter's own stack guard. Other depth-like limits
(`kMaxRegexNesting`, `kMaxImportDepth`, `kMaxValueDepth`) were left as they were — this change
is scoped to source nesting only, not the size/collection/regex limits from v1.6.0.

**`availableStackBytes()` is now cross-platform** (`engine/common/Attributes.h`): Linux via
`pthread_getattr_np`, macOS via `pthread_get_stackaddr_np`/`pthread_get_stacksize_np`, Windows
via `GetCurrentThreadStackLimits`, and Emscripten via `emscripten_stack_get_free()`. Previously
Linux-only; both the parser's and the interpreter's stack guards now adapt to the real stack
everywhere instead of guessing on three of the four shipped targets.

**`constant list` (tuple).** `constant` now works on `list` as well as scalars, giving a
Python-tuple-style read-only list — no new value type: `ValueList` gained one field,
`isConstant`, checked at every mutation site (`add`/`remove`, and the final container in
`resolveContainerSlot`, which is also what `change x[i] to v` and CollectionOp's `Replace` go
through). Declaring one (`execDeclaration`'s `freezeList`) always copies the initializer into a
fresh `ValueList` before freezing it, specifically so `set constant list T to existing_var`
can't reach back and silently freeze `existing_var`'s own list — `List` is a reference type
(`engine/interpreter/Value.h`), so without a defensive copy the two names would share one
backing object. The immutability travels with that object, not with any one variable name: an
alias or a function argument bound to the same frozen list is blocked too (`execChange`'s
existing name-based constant check was narrowed to whole-variable reassignment only; an
indexed write now falls through to the value-level check, which is what makes both the alias
case and shallow nesting — mutating a plain list found _inside_ a frozen tuple is allowed, only
the tuple's own slots are frozen — work correctly). `constant map` was not added; the ALL-CAPS
naming rule is unchanged and applies to constants of every type as before.

**`function` renamed to `func`.** A straight keyword replacement (`lexer/KeywordClassifier.h`),
not an alias — existing scripts using `function` need a one-word find-and-replace. Every
example, test fixture, and doc code sample was updated.

**`pure` functions.** A new independent modifier (`async`/`returnable`/`pure` combine in any
order): `set pure func f() do: ... end`. Enforcement lives entirely in `Interpreter`, not
`Environment` — `FrameGuard` now also carries `isPure`, and the three places that resolve a
name against an `Environment` (`evalExpr`'s identifier case, `execChange`, `execCollectionOp`)
each check `currentFunctionPure_ && look.owner == &globalEnv_` and raise the new
`PureFunctionGlobalAccess` (E4-027) before the read/write happens; `change x to global` is
rejected at the bridge itself, before it can even register. The restriction is on that
function's own body, not the whole call graph: a pure function calling a non-pure one is fine,
and the callee's own purity (or lack of it) governs its own body, exactly as `returnable` and
`async` already worked per-call via the same `FrameGuard`. Removing `pure` is a complete,
literal escape hatch, by construction — there's no separate override flag to keep in sync.
_(Update, item 40: the three per-site checks described above were replaced by the single
failure path `throwUnresolved()`, which raises the same `PureFunctionGlobalAccess` error.)_

**`DLC:network`.** A dependency-free HTTP/1.1 client, `engine/net/HttpClient.h`: POSIX sockets
on Linux/macOS, Winsock2 on Windows (the one file in the engine with `#ifdef _WIN32` socket
code). Plain HTTP only — no TLS, so `https://` fails with a clear error rather than silently
talking plaintext to an HTTPS port. `get(url)` and `post(url, body[, content_type])` return
`{"status", "ok", "body"}`; connection/DNS/timeout failures raise `NetworkRequestFailed` (E4-028),
catchable via `or_else` like any other runtime error. Handles chunked transfer-encoding,
`Content-Length` framing, and 301/302/303/307/308 redirects (303, and 301/302 for an original
POST, downgrade to GET per common client behavior; 307/308 preserve method and body), each
capped (`limits::kHttpMaxRedirects`). Every resolved address is checked against loopback/
private/link-local ranges (`net::isPrivateOrLoopback`, IPv4 and IPv6, checked on the concrete
`sockaddr` right before connecting, not the hostname text) and rejected unless
`CuffEngine::Options::allowPrivateNetworkTargets` opts in — the same "safe by default, widen
explicitly" shape as the module sandbox's `rootDir`. `networkEnabled` (default true) lets an
embedder turn `DLC:network` off entirely for untrusted scripts. Both are also CLI flags
(`--allow-private-network`, `--no-network`). See `SECURITY.md` for the reasoning.

**Windows build fix.** Cross-compiling with `x86_64-w64-mingw32-g++` surfaced a real bug:
`<windows.h>` (pulled in by `Attributes.h` for `GetCurrentThreadStackLimits`) `#define`s `TRUE`,
`FALSE` and `IN` as bare macros, silently mangling `TokenType::TRUE`/`FALSE`/`IN` wherever
`Attributes.h` was included first (`TokenType::TRUE` would macro-expand to `TokenType::1`,
failing to compile). Fixed with `WIN32_LEAN_AND_MEAN`/`NOMINMAX` plus targeted `#undef`s right
after the `windows.h` include. The Windows build was not previously exercised with a real
toolchain; it now compiles cleanly under mingw-w64 (`x86_64-w64-mingw32-g++`, `-lws2_32`).
Actually running the cross-compiled binary could not be verified in this environment (no
working Windows/Wine runtime available) — only compilation was confirmed.

**Performance.** Two changes, both measured, both kept because neither regresses anything:
`std::get<T>` calls in the `evalExpr`/`execStatement` dispatch were replaced with
`std::get_if<T>` (avoids the exception-handling path `std::get` carries even though the kind
check already guarantees the right alternative), and `evalCall`/`invokeAwaited`/
`callUserFunction` now recycle their argument vector and the callee `Environment`'s variable
storage across calls (`Interpreter::argsPool_`/`envVarsPool_`, capped, RAII-returned on every
exit path including exceptions) instead of allocating fresh each call. Two other ideas were
tried and measured to have no real benefit on this toolchain (glibc 2.39), so were **not**
adopted as load-bearing optimizations: replacing `shared_ptr`'s atomic refcounting with a
non-atomic one (glibc's `__libc_single_threaded` fast path already makes single-threaded
`shared_ptr` copies non-atomic in practice) and a broader allocation-pooling scheme beyond the
call path above (glibc's per-thread tcache already caches same-size small allocations about as
well as a hand-rolled pool would). Net effect: recursive/call-heavy code (`fib(30)`) is
modestly faster (~7%); pure arithmetic loops, which make no allocations to begin with, are
unchanged — a tree-walking evaluator's per-node cost was already close to its practical floor
after v1.6.0's work, and closing that gap further would mean the bytecode VM this project is
deliberately deferring, not a tuning pass.

---

## 26. Pure-function hardening: call-graph propagation

`pure` originally only checked a function's own body (see item 25). That left a trivial
escape hatch: `set pure func f() do: g() end` where `g` touches globals — `f` never
touches a global *directly*, so the old check passed, but calling `f` still reached a
global through `g`. `checkPureCallAllowed()` (`Interpreter.h`) now runs at every call
site (`evalCall`, `invokeAwaited`) and rejects a pure caller invoking a non-pure
*user-defined* function, with a new error, `PureFunctionImpureCall` (E4-029).
Native/DLC functions are still exempt — they never touch a CuffScript `Environment` at
all, so they can't reach a global through this route regardless. Recursion and
pure-calling-pure remain unrestricted, since `decl.isPure` is checked per callee, not
per call depth. This is a **behavior change**: `tests/cases/pure_functions.cuff`'s old
"pure calling non-pure is fine" example no longer holds and was rewritten; the same
scenario now lives in `tests/errors/pure_func_call_impure.cuff` as an E4-029 case.


## 27. Global-bridge bug: `Environment::resolve()` checked the bridge before the local

`change x to global` (item 25, and the earlier feature it hardens) had a real, reproducible
bug: `resolve()` checked whether a name was in the current function scope's
`globalDeclared_` list *before* checking for an actual local (`findLocal`) at each level.
Once a name was bridged, any later *local* redeclaration of the same name in that same
call — most visibly a `loop repeat x to 1 ~ 3` reusing a bridged `x` as its loop
variable — became permanently invisible to reads for the rest of that call: every read
saw the stale global instead of the loop's own counter, silently, with no error. The fix
is a one-line reordering: check `findLocal` first at every level (including the function
scope itself), and only fall back to `globalDeclared_`/true-global when no local exists
— restoring the same "innermost declaration always wins" rule the rest of the language
already follows. This also incidentally fixes the same class of misattribution in the
`pure` global-access check (`currentFunctionPure_ && look.owner == &globalEnv_`), which
could previously misreport an ordinary shadowed local as forbidden global access.
Regression test: `tests/cases/global_bridge_shadowing.cuff` (loop-variable shadowing and
the `or_else` declaration-recovery placeholder, which hits the identical bug from a
different angle).

_(Update, item 40: the "fall back to the true global when no local exists" step described above
was itself the cause of a second bug — a function could read and write globals without any
bridge — and was removed. A function now resolves only locals plus the names it bridged.)_

## 28. Reserved words as identifiers

`add`, `count`, `find`, `split`, `replace`, `match`, `in`, `by`, `not`, and `global`
could not be used as a variable, function, parameter, or `loop repeat` variable name —
every name-declaring/-referencing spot checked `TokenType::IDENTIFIER` specifically,
which a reserved word never is, even though the exact same problem for DLC/module names
was already fixed by relaxing that check to `isWordLikeToken()` (item 9). The fix does the
same thing everywhere else names are declared: `DeclarationParser` (`set` variable name,
`change` target), `FunctionParser` (function name, parameter names), `CollectionOpParser`
(the collection name in `add`/`replace`/`remove`), `LoopParser` (the `repeat` loop
variable), and `RegexExprParser::looksLikeCollectionReplace` (the 2-token lookahead that
tells the statement and expression forms of `replace` apart) all now accept any
word-shaped token via the shared `isWordLikeToken()` (moved from `ImportParser.h` to the
more central `ParserCore.h`).

Reading one of these names back in an *expression* needed separate handling in
`LiteralParser::parsePrimary` (`ExpressionParser.h`), since that's the one place a
keyword's token type is dispatched on to decide what to parse:
- `add`, `to`, `in`, `by`, `global`, `not`, `from` have no meaning anywhere in expression
  grammar — every existing use of each is consumed via an explicit `p.consume(...)` at a
  fixed grammar point, never checked as "does an expression start here". They're always
  safe as a bare identifier reference (`isBareIdentifierKeyword()`).
- `match`/`find`/`replace`/`split`/`count` are different: each already has its own
  primary-position construct (`match X from Y`, `count "p" in y`, ...). Disambiguating
  "the construct" from "a bare name of the same word" uses one token of lookahead
  (`canStartExpressionToken()`/`looksLikeConstructContinuation()`): if what follows can't
  plausibly start that construct's required sub-expression, it's a name instead. `(` and
  `[` are deliberately excluded from that lookahead even though they can start an
  expression, because `split(...)`/`count[0]` need to mean "call/index the
  variable/function named split/count" (handled by ordinary postfix parsing after
  `parsePrimary` returns a plain identifier) — including them would make it impossible to
  ever call or index something with one of these five names again.

This is necessarily a judgment call about which collisions are safe to resolve
mechanically, not a complete lifting of every keyword restriction — see the note on
`SPEC.md` section 1.5 (Names and reserved words) for exactly which words are covered. `not`'s only other grammar role is the
`is not` comparison (`parseComparison`'s own `p.check(TokenType::NOT)`, unrelated to
`parsePrimary`), so there's no interaction between using `not` as a variable and using it
in an `is not` comparison. Regression test: `tests/cases/reserved_words_as_identifiers.cuff`.

## 29. Caret (`^`) marker in error messages

Errors now show the offending source line with a `^` under the exact column, e.g.:

```
[E2-001] Syntax Error at line 4, column 10: unexpected token ')' in expression
    print(x +)
             ^
```

`CuffError`/`SourceLocation` themselves are unchanged — a raw `CuffError::what()` still
renders message+hint only, with no source access. The snippet is built one layer up, in
`CuffEngine::execute()`/`run()`, exactly where the original source text is still in scope
when a `CuffError` is caught: `buildCaretSnippet()` finds the line via `loc.offset` (a
byte offset already kept in sync with the tokenizer's `line`/`column`, see
`ScanState.h`), and `renderErrorWithSnippet()` reconstructs the full message from the
error's own public fields with the snippet spliced in between message and hint, rather
than parsing `what()`'s already-flattened string. The caret's horizontal offset is
counted in **codepoints**, not bytes — `column`/`offset` are byte-based (one UTF-8
continuation byte = one column), so a Korean/emoji/etc. string literal earlier on the
same line would otherwise push the caret roughly 3x too far right; the byte-prefix up to
the error's offset is measured with `utf8::length()` instead. Regression test:
`tests/unit/error_snippet_test.cpp` (ASCII, UTF-8-alignment, and a runtime-error case).

An error inside an f-string's `{...}` used to report line 1 of the *fragment* (the
expression text is tokenized on its own, so its positions start over at 1:1), which the
snippet then rendered as the wrong source line. `LiteralParser::parseEmbeddedExpression`
now pins every token location in the fragment, and any `CuffError` thrown while tokenizing or
parsing it, to the f-string literal's own location, so the snippet shows the line the
f-string is on (the caret sits at the literal's start, not at the exact column inside the
braces — the fragment has no reliable mapping back once escapes have been decoded).
Two extra cases in `error_snippet_test.cpp` cover this.

## 30. `DLC:filesystem`

Real local file access: `file_exist`, `file_size`, `file_read`, `file_readlines`,
`file_write`, `file_add`, `file_remove` (`registerFilesystemDLC`, `engine/dlc/FilesystemDLC.h`).
Every path is resolved relative to, and confined inside, the exact same sandbox root
`use ... from` module imports already use (`Interpreter::moduleRoot_`, i.e.
`CuffEngine::Options::rootDir` or the script's own directory) — there's no separate,
filesystem-specific root to configure. `isInsideRoot()` (the containment check module
loading already had) moved from a private `Interpreter` method to a shared
`engine/common/PathSandbox.h` so both features use the identical check rather than two
copies that could drift. An absolute path, or a relative path that escapes the root
(`../../etc/passwd`), is rejected with a new error, `FilesystemAccessDenied` (E5-008),
*before* touching the filesystem — a script trying to reach outside its sandbox is worth
surfacing loudly. An ordinary OS-level failure once a path clears that check (file
doesn't exist, permission denied) is reported the quiet way each function's own contract
promises (`empty`/`false`), not by throwing — `file_exist()` is how a script is expected
to check first. `file_read`/`file_readlines` check the file's stat'd size against the
engine's normal string-size ceiling *before* allocating or reading anything (rather than
slurping the whole file first and rejecting after), so an oversized file on the sandboxed
filesystem can't force an oversized allocation just to get rejected. New host controls,
mirroring `DLC:network`'s existing shape exactly: `CuffEngine::Options::filesystemEnabled`
(default true) / `--no-filesystem`. See `SECURITY.md`'s "DLC:filesystem" section for the
full trust-boundary discussion. Regression tests: `tests/cases/dlc_filesystem.cuff`
(success paths, self-cleaning), `tests/errors/dlc_filesystem_disabled.cuff` (E5-005),
`tests/errors/filesystem_sandbox_escape.cuff` (E5-008).

## 31. DLC function naming convention (`library_verb`)

Every DLC function name (except the ones noted below) now carries its library as a
prefix — `sqrt` → `math_sqrt`, `upper` → `str_upper`, `sort` → `list_sort`, `now` →
`time_now`, `random` → `random_float`, `choice` → `random_choice`, `keys` → `map_keys`,
`get`/`post` → `network_get`/`network_post`, and so on (see the table in item 9 for the
complete mapping) — matching the `file_*` shape `DLC:filesystem` (item 30) introduced.
Reasoning: before this, a script reading `sort(x)` or `get(url)` had no way to tell which
`use`d library it came from without cross-referencing every `use` line, and two libraries
registering the same bare name (all of them share one flat `natives_` map) could silently
shadow each other with no warning. Three exceptions, deliberately not renamed:
`length`/`contains`/`index_of`, which are intentionally polymorphic across str/list/map
(registered identically under all three — see item 11) and would lose that "works on
anything" property if split into `str_length`/`list_length`/`map_length`; and
`to_json`/`from_json`/`to_number`/`to_str`/`to_boolean`, whose names already encode
their domain and direction without a prefix. **This is a breaking rename** — every
example, test, and doc snippet using the old bare names was updated in this repository,
but any script outside it needs the same mechanical find-and-replace per function.

## 32. `loop match` removed

`LoopStmt::LoopKind` had three values — `Repeat`, `While`, `Match` — but `execLoop`
(`Interpreter.h`) implemented `Match` with the exact same code path as `While` (re-check
a boolean condition every iteration), and `LoopParser` parsed `loop match target is/IS
state do: ... end`'s condition the same way `while`'s condition is parsed. It was a
complete, confusing duplicate with no functional difference and no use in this
repository's examples or tests, so it was removed outright rather than kept as sugar:
`LoopKind` now has just `Repeat`/`While`, `loop match ... do:` is a plain syntax error
("expected 'repeat' or 'while' after 'loop'"), and `ASTPrinter`'s debug dump for loops
was simplified to match. `loop while [condition] do: ... end` covers the exact same case.

## 33. Async: reasoned decision not to add OS threads, plus a light pass

The brief for this release asked for "real" async concurrency and left the choice between
non-blocking I/O and multithreading — and the resulting thread-safety work — to be
decided here. The two honestly available options, given this is a recursive tree-walking
evaluator with no coroutine/continuation support (no way to suspend a call mid-statement
and resume it later on the same native call stack):

1. **OS threads**, with a GIL-style lock serializing all interpreter execution and
   released only around a queued task's raw blocking socket I/O — the only way to get
   genuine wall-clock overlap between multiple queued tasks' network calls without a
   deeper rewrite. This was fully designed (thread-local per-call-stack frame state to
   stop concurrently-running tasks from corrupting each other's `pure`/`returnable`/
   call-depth bookkeeping; a FIFO-preserving spawn order so scripts with no I/O keep
   today's exact sequential output; exception propagation across worker threads).
2. **Leave the current cooperative model as-is** and be transparent about its actual
   shape rather than overselling it as concurrency it doesn't have.

**Decision: option 2.** Real, safe concurrent execution fundamentally needs either OS
threads or turning the evaluator into a resumable state machine; the latter is a rewrite
far beyond this change's scope, and the former is a permanent complexity and correctness
tax on every future change to `Interpreter.h` — every field on that class becomes a
question of "is this per-call-stack state that now needs to be thread-local", for a
feature (overlapping *network* I/O specifically, since that's the only thing in this
engine slow enough for overlap to matter) that most scripts written in this language will
never exercise. That tradeoff cuts directly against this language's own stated design
goals — simple, lightweight, easy to reason about — more than any other single change in
this release; see the more general version of this argument given directly to the user
alongside it. **What "async" means here remains unchanged and is now documented as such
rather than implied otherwise**: `f()` on an `async` function queues it; the queue drains,
strictly in FIFO order, once the top-level script's synchronous code finishes (self-queued
tasks included, see `examples/10_async_ordering.cuff`); `await f()` runs it immediately,
synchronously, and returns its value. This is cooperative scheduling, not concurrency —
useful for controlling *when* code runs relative to the rest of the script, not for
making two things run at the same wall-clock time.

A later, smaller pass over this same area (after the architectural question above was
already settled) found two real, if minor, bugs and locked in two behaviors that were
already correct but untested:
- Two error hints quoted the wrong keyword — `'set async function NAME(...) do:'` and
  `'set returnable function'` — when the actual declaration keyword is `func`, not
  `function` (`throwAwaitOnNonAsync`, `throwReturnInVoidFunction`). Fixed; these are the
  only two spots that quoted the syntax literally rather than using "function" as the
  English word for it.
- An error thrown by a queued task now has a regression test confirming it stops the
  drain the same way an error mid-script stops synchronous execution — tasks queued after
  the failing one don't run (`tests/errors/async_error_stops_queue.cuff`) — this was
  already the behavior (an uncaught exception simply unwinds out of `drainTaskQueue`), just
  previously unverified by a test.
- Self-queuing chains (a queued task queuing another queued task queuing another, `A` →
  `B` → `C`) already drained correctly in the documented FIFO order; now has a permanent
  regression test (`tests/cases/async_self_queue_chain.cuff`).

## 34. `NativeFunctions.h` split into `engine/dlc/`

Every DLC's implementation used to live together in one 1,748-line
`engine/interpreter/NativeFunctions.h`. Split into `engine/dlc/`, one file per
library (`MathDLC.h`, `StringDLC.h`, `TimeDLC.h`, `RandomDLC.h`, `ListDLC.h`, `MapDLC.h`,
`ConvertDLC.h`, `NetworkDLC.h`, `FilesystemDLC.h`, `JsonDLC.h`), plus `DLCCommon.h` for
what several of them share: the `expectArgCount`/`expectNumber`/`expectStr`/`expectList`/
`expectMap`/`expectWhole` argument-checking helpers, `ensureStringSize`/`ensureItemCount`,
and the `textutil` UTF-8 namespace (used by both `StringDLC.h` and `JsonDLC.h`).
`NativeFunctions.h` itself is now ~100 lines: the always-on core builtins
(`registerBuiltins` — `print`/`input`/`type_of`, no `use` needed) and `registerDLC()`,
the single dispatcher every `use DLC:name` calls into, which now just `#include`s all
eleven files. `nativeLength`/`nativeContains`/`nativeIndexOf` (the three polymorphic
functions shared across str/list/map, see items 9 and 31) live in `DLCCommon.h` rather than
being duplicated per file, for the same reason they were never split by library to begin
with. Every new file compiles standalone (checked by hand, one `#include "X.h"` +
empty `main()` per file) rather than silently depending on include order from whatever
happened to be pulled in first — the kind of hidden coupling splitting a file is supposed
to remove, not just relocate. Purely a file-organization change: no declaration moved
namespace, gained a new name, or changed behavior, and the full suite (unit tests,
`tests/cases`, `tests/errors`, `examples`) passes unchanged before and after.

## 35. `use DLC:a, DLC:b` — several libraries in one statement

`use DLC:math, DLC:string, DLC:convert` loads all three; one `use` per line still works and
gives the same result. `UseStmt` now carries `dlcs` (a list of `{name, loc}`) for the DLC form —
each name keeps its own location so an unknown library in the middle of the list (`E5-004`) is
pointed at directly — and `name`/`path` only for the `use X from path` form. `ImportParser`
requires every item to repeat the `DLC:` prefix (`use DLC:math, string` is a syntax error, since a
bare `string` is indistinguishable from a module name), and gives a specific message for a
dangling comma and for a forgotten comma (`use DLC:math DLC:string`; without that check the
second `DLC` would parse as the start of a new statement and fail with a confusing `unexpected
token ':'`). `SPEC.md` section 9.1 requires a `use` statement to stay on a single line, so a list cannot be
continued onto the next line — separate `use` lines are the multi-line form.
Tests: `tests/cases/use_multiple_dlcs.cuff`, `tests/errors/use_multiple_*.cuff`.

## 36. Typed function parameters

`set func add_num(number n1, number n2) do:` — each parameter is `name` or `type name` with
`number`/`str`/`boolean`/`list`/`map`/`match`; typed and untyped can be mixed. `FunctionDecl` gets
`paramTypes` (parallel to `params`, `ParamType::Any` = untyped) and `hasTypedParams`; the call
path only does any checking when `hasTypedParams` is true, so untyped functions pay one
predictable branch. `Interpreter::checkParamTypes` runs in `evalCall`/`invokeAwaited` *before*
dispatch — for an `async` function that is before queueing, so the error points at the call
rather than surfacing later when the queue drains. A mismatch is `ParameterTypeMismatch`
(`E4-030`) located at the call site. `empty` passes every type, matching `set`'s rule, since a
failed `find`/`file_read` returning `empty` should be something a function can receive and
handle. A type keyword counts as a type only when a name follows it, so `(number)` and
`(number, x)` stay legal untyped parameters named `number`. Arity is still checked separately,
by `callUserFunction`. Tests: `tests/cases/typed_parameters.cuff`,
`tests/errors/parameter_type_mismatch*.cuff`.

## 37. Performance pass: measured, mostly nothing to change

Benchmarks (`fib(30)`, a 3M-iteration loop, 1M user-function calls, string/list/map work,
f-strings, a 66k-line script) against the original codebase: this release's engine is at parity
(fib 449→429 ms, everything else within noise), so none of the semantic changes above cost
speed. Then looked for real wins: no profiler beyond `gprof` was available and its profile was
flat (`evalExpr`, `evalBinaryOp`, scope lookup), consistent with the earlier notes (21, 24) that
the tree-walker is near its floor. Tried and **not adopted**: profile-guided optimization
(0–4%, inside run-to-run noise, and it needs a two-step build), `-O2` instead of `-O3` (same),
static `libstdc++` (startup 2.2→1.7 ms, but the binary grows 2.4×, against "light as a
feather"), dropping `-fstack-protector`/`_FORTIFY_SOURCE` (≈0–8%, not worth weakening a
sandboxing interpreter). A broad micro-suite (200k-element sort, unique, split of 150k
parts, replace/find/count over 850 KB, 50k-object JSON round trip, 50k-key map) found no
quadratic outlier. **Adopted:** stripping the release binary (`-s`, 1.18 MB → 670 KB, no speed
change; skipped on macOS where Apple's linker ignores it with a warning).

## 38. `tests/run.ps1` fixes

The first `run.ps1` used `ProcessStartInfo.ArgumentList`, which does not exist on .NET
Framework, so on Windows PowerShell 5.1 (the default on Windows) every test died with a
null-method error. Rewritten to build the argument string itself (CommandLineToArgvW quoting),
and fixed along the way: output is decoded as UTF-8 (the OEM code page garbled Korean output,
failing every diff); `\r\n` is normalized before comparing; stdin is closed so `input()` sees
EOF instead of waiting for the timeout; g++ is run through the same helper instead of `& g++ 2>`
(under `$ErrorActionPreference='Stop'`, 5.1 turned any compiler warning on stderr into a
terminating error), with a 600 s build timeout instead of 10 s; the unit tests link with
`-lws2_32` and an 8 MiB stack on Windows, as `Makefile.win` does (without them
`limits_test.cpp` could not link, or overflowed Windows' 1 MiB default stack); and the binary
name is `cuffc.exe` on Windows, `./cuffc` elsewhere, so it also runs under PowerShell 7 on
Linux/macOS. Still not executed on a real Windows machine — none was available here.

## 39. Comment cleanup in `engine/`

Of the 657 comment blocks in `engine/` (1,534 comment lines out of 12,529), about 90 short
ones remain (92 lines out of 11,247). What stays is what the code can't say itself:
lifetime and ordering invariants (`Environment` is stack-local and referenced by raw pointer; the
module AST must outlive its functions), platform quirks (`windef.h` macros, Emscripten's shadow
stack, `SIGPIPE` handling, `pthread_get_stackaddr_np`), stack-safety limits, the reasoning behind
non-obvious checks (zero-width regex guard, codepoint-boundary matching, ASCII-only case
folding, "a local binding always wins over a `global` bridge"), and the public option fields in
`CuffEngine::Options`. Longer explanations that used to live in comments are in this file and in
`SPEC.md`. The cleanup was mechanical and checked mechanically: a string/char-literal-aware
scanner located comments, the preprocessed output of `main.cpp` was compared before and after
(identical once whitespace is ignored, so only comments and blank lines changed), every string and
char literal was compared byte-for-byte, and the build plus the full suite pass unchanged.
`main.cpp`, `wasm/bindings.cpp` and `tests/` were left as they were.

## 40. A function no longer sees globals without a `change x to global` bridge

**The bug.** `Environment::resolve()` walked the scope chain up to the function scope and then,
when nothing was found, fell back to the real global scope. So a function could *read* any
global, and — worse — `change g to 99` inside a function silently overwrote the global with no
`global` declaration at all, which made `change g to global` pointless (`SPEC.md`, section 7 "Scope",
says that is how you reach a global from a function).

**The rule now.** A function scope resolves only its own locals (and the block scopes inside it)
plus the names it bridged with `change x to global`; everything else is unresolved. This
applies to reads, to `change`, and to collection statements (`add`/`remove`/`change x[i]`), and
to constants as much as to variables — there is no read-only exemption for `constant` globals.
A local declared with `set` still always wins over a bridge of the same name (item 27). Top-level
code and the block scopes inside it are unaffected, since they live in the global scope itself.

**Errors.** `Interpreter::throwUnresolved()` is the one place that reports an unresolved name.
If the name exists as a global and the running function has not bridged it, the message says so
and hints at `change <name> to global`, instead of a bare "undefined variable"
(`UndefinedVariable`, E4-001, still recoverable with `or_else`). Inside a `pure` function the
same situation raises `PureFunctionGlobalAccess` (E4-027) exactly as before — a pure function
cannot bridge, so it can never reach a global — which is why the three
`look.owner == &globalEnv_` checks from item 25 are gone: they could no longer fire.

**Knock-on changes.** `examples/02_comprehensive_demo.cuff` (and the SPEC code that mirrors it)
reads the constant `MAX_LEVEL` inside a function, and `examples/lib/greetings.cuff` reads a
module-level constant, so both now begin with `change <NAME> to global`. Module functions follow
the same rule as any function (item 8).

**Tests.** `tests/cases/global_bridge_required.cuff` (read/write/collection op blocked without a
bridge, allowed with one, local shadowing, the global untouched afterwards) and
`tests/errors/global_read_without_bridge.cuff` / `global_write_without_bridge.cuff` (E4-001).

### Other fixes in the same pass

- **HTTP chunked bodies could exceed the response cap.** The chunk-size check added the declared
  size to the bytes received so far, so a hostile `ffffffffffffffff` chunk size wrapped around and
  passed; the client then buffered everything the server sent until the 15 s deadline (measured:
  642 MiB against an 8 MiB cap). It now compares against the remaining budget, so the response is
  rejected immediately. (No automated test: it needs a live hostile server.)
- **`http://[::]/` bypassed the private-address block.** On Linux the IPv6 unspecified address
  connects to the local host, but `isPrivateOrLoopback()` only blocked `::1`. `::` is now blocked
  too (`tests/unit/dlc_guards_test.cpp`).
- **`file_remove` also deleted empty directories** (`std::filesystem::remove` does both). It now
  removes regular files only, matching `file_exist()` and the SPEC table.
- **`[int]` could match a bare `+` or `-`.** Its backtracking tried lengths down to 1, which is
  just the sign, so `"+5" is "[int][one:5]"` was true. It now always keeps at least one digit.
- **An error at the very first character of a file lost the first character of the quoted source
  line** and put the caret at the end of the line. `buildCaretSnippet()` computed the line start as
  `0 + 1` when the offset was 0 (`tests/unit/error_snippet_test.cpp`).
- **An unexpected non-ASCII character was reported as its first byte alone**, which is not valid
  UTF-8, so a diagnostic about a Hangul identifier came out garbled. The whole character is shown now.
- `list_join()`'s error told users to call `convert:to_str()`, a form that no longer exists; it says
  `to_str()`.
- Dead code removed: the unused `KeywordClassifier` class (its `keywords_` member was declared but
  never defined; `KeywordClassifierImpl` is now simply `KeywordClassifier`), an f-string brace
  counter in `StringScanner` that nothing read, and a stale comment. `cuffsh` gained the
  `--no-filesystem` flag that `cuffc` already had.

### Performance check

Re-measured against the previous build (`fib(30)`, a 3M-iteration loop, string/list/map work,
regex in a loop): everything is at parity, and calling a function that bridges a global got about
35% faster, because a lookup no longer falls through to the global scope. One outlier turned up:
`remove <key> from <map>` re-hashed every later key after each removal (about 3 ms per removal on
a 60,000-key map). It now shifts the stored positions by walking the index when many keys move,
and still re-indexes only the tail when few do, so removing the last key stays O(1) — roughly
2× faster for removals near the front. It is still O(n) per removal, because a map keeps its keys
and values in dense, insertion-ordered vectors that a dozen call sites read directly; making
removal O(1) would mean tombstones and touching all of them (`tests/cases/map_remove_reindex.cuff`
pins the ordering behavior either way).

### Regex: a digit run after a literal is text, not a count

`applyQuantifier()` used to read a run of digits after *any* atom as an exact repeat count, so
`"v2"` meant `v` twice, `"abc123" is "abc123"` was false, and `"010-[num]4-[num]4"` — the
phone-number example in `SPEC.md` and `REGEX.md` — began with `0` repeated ten times and could
never match a real number. A digit run is now a count only after an atom that consumes characters:
a bracket token or set, `[one:...]`, a group or a named capture. After a plain or escaped literal
(`RNode::isLiteral`) or a zero-width anchor (`[start]`, `[end]`, `[edge]`) it is ordinary text.
`+`, `*`, `?` and `~M` still apply after literals, exactly as before. The price is that a literal
digit straight after a token reads as part of the count, so four digits then a `5` is
`[num]4[one:5]`. A pattern that relied on a literal followed by a count (`ab2` for `abb`)
changes meaning; that form was never documented. Tests: `tests/unit/regex_test.cpp` and
`tests/cases/regex_literal_digits.cuff`.

### Verifying with sanitizers

```bash
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
g++ -std=c++17 -O1 -g $SAN -o cuffc-asan main.cpp
ASAN_OPTIONS=detect_leaks=0 CUFFC=./cuffc-asan UNIT_FLAGS="-O1 -g $SAN" bash tests/run.sh
```

With leak detection off the whole suite is clean under ASan + UBSan (no memory errors, no
undefined behavior). With leak detection on, the only reports are the deliberate reference cycles
in `tests/cases/value_semantics.cuff` and `tests/unit/limits_test.cpp` (item 11), which is why
the command above sets `detect_leaks=0`.

## 41. Extension points and portability

**One row per DLC.** `dlcTable()` in `engine/interpreter/NativeFunctions.h` is the only list of
`use DLC:<name>` libraries. `registerDLC()` walks it, and the "unknown library" hint is built from
it, so adding a library means writing its header, including it there and adding one row — no
`if` chain and no second list of names to keep in sync. Host settings that a library reads when it
loads travel in `DLCOptions` (one member per library that has any).

**How a host flag reaches a library.** `CuffEngine::Options` (the public, flat API that `cuffc`,
`cuffsh` and the WASM bindings fill in) → `Interpreter::Config` → `DLCOptions`, built in
`Interpreter::execUse()` → the library's own options struct. The first two stay flat on purpose:
they are what the CLI and the bindings already set, and nesting them would break those callers.
A new host-controlled library therefore needs its own options struct, a `DLCOptions` member, a
`Config` field, one line in `execUse()`, and a `CuffEngine::Options` field with its copy line —
six small edits in four files, all compiler-checked.

**Leaving out the socket client.** Building with `-DCUFF_DISABLE_NETWORK`
(`make EXTRA_CXXFLAGS=-DCUFF_DISABLE_NETWORK`, or the same variable for `make wasm`) drops
`engine/net/HttpClient.h` and its platform headers (`getaddrinfo` is no longer imported). `use
DLC:network` still loads, and `network_get`/`network_post` fail with `DLCFeatureUnavailable`
(E5-005), the same error as when a host turns the library off at run time. The four
`tests/errors/network_*.cuff` cases test the client itself, so they are expected to fail in such a
build; everything else passes. The default builds, WASM included, are unchanged. `DLC:filesystem`
has no such switch: it needs only `std::filesystem`, which module loading uses anyway.

**UTF-8 paths.** A path that comes from a script (`use ... from <path>`, `<name>.cuff`, and the
argument of every `file_*` function) is converted with `std::filesystem::u8path()`. Plain
construction would read the string in the ANSI code page on Windows and garble non-ASCII names,
Hangul included. Strings the host passes in (`scriptDir`, `rootDir`) are used as given — encoding
them is the host's job. `u8path` is deprecated from C++20 on, so keep `-std=c++17` (the Makefiles
do) or replace it with the `char8_t` constructor when the project moves up.

**Considered and left alone**, because each one reaches into every library or every statement
kind: giving native functions a context parameter so `print`/`input` could use host-supplied
streams (the WASM host gets them through Emscripten's `print`/`stdin` callbacks instead), generating the token enum, its names and
the keyword table from one macro list, and a visitor over the AST in place of the per-kind switches
in the parser, `ASTPrinter` and interpreter. `docs/EXTENDING.md` lists exactly which files each
kind of addition touches.
