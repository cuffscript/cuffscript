## v1.2.1 - 2026-09-18

# Description

- Add npm package for web IDE



---

## v1.2.0 - 2026-09-18

# Description

- **Performance**: replaced the exception-based / control-flow implementation
  with an explicit  value threaded through ///
   (). C++ exceptions are reserved for genuine
   conditions again. Measured effect: a  recursion benchmark (~635k
  function calls) went from 2.75s to 0.26s (~10.7x). This also fixed two real bugs the
  exception-based version had:  used inside a function with no loop of its own used to
  leak through the function-call boundary and terminate whatever loop was active in the
  _caller_; and / used at the top level (outside any function/loop) used to
  crash the whole process with an uncaught exception. Both are now clean, catchable
  s (, ).

- **Async**: calling an  function _without_  no longer runs it immediately —
  it's queued and runs once the entire top-level script's synchronous code has finished,
  in the order it was queued (FIFO).  is unchanged (still runs immediately and
  returns the value). This is cooperative, single-threaded deferral — not real concurrency —
  chosen for safety (the interpreter's shared state isn't thread-safe). See
   section 1 and .

- **DLC libraries**: added  (, , ,  — all
  non-mutating) and  (, , ).

- Fixed a parser bug uncovered while adding : / only accepted an  token for the name, so any name colliding with a
  reserved word (like ) failed to parse at all.  now accepts any
  word-shaped token (identifier or keyword) as a name.

- Minor interpreter micro-optimizations:  pre-sizes its variable table for a
  function call's known parameter count, and arguments are moved rather than copied into
  parameter bindings.

- Added  and expanded  to
  cover the new libraries.


---

## v1.1.0 - 2026-09-18

# Description

- Make  for language growth.


---

## v1.0.0 - 2026-09-18

# Description

- Implemented a full tree-walking interpreter (): Value/Environment
  model, Python-style function-level scoping with explicit  bridging,
  1-based indexing/slicing with negative-index support, collections (///
  indexed , insertion-ordered maps), functions (/ in any
  combination),  error recovery, and // module loading.

- Implemented CuffScript's custom pattern-matching engine from scratch ():
  a dedicated compiler + backtracking matcher for the full  dialect
  (character classes, presets, , named/numbered captures, quantifiers including
   ranges and lazy variants, anchors, flags), wired into /, ,
  , , , and  — including a
  step-count + time-limit safety guard against catastrophic backtracking.

- Introduced a systematic, extensible error hierarchy (,
  ): every error carries a stable numeric code, category,
  location, message, and optional hint, and is marked recoverable/non-recoverable so
   catches exactly the right things.

-  now runs the program by default;  reproduces the previous
  tokens/AST-dump behavior for engine development.

- Fixed several defects that prevented the previously-committed engine from compiling or
  running at all (broken relative includes, a use-before-complete-type cycle between the
  statement/expression sub-parsers, a real dangling-copy bug in  parsing, a missing
  ), added single-quoted string literals (needed for map/match key access
  inside f-strings per ), fixed f-string / escaping and nested-brace
  handling, and corrected  to bind looser than comparison ( now means
  , matching the spec's own examples).

- Added  documenting every place the language spec leaves
  runtime behavior unspecified and the choice this engine makes there (async model, DLC
  function list, module-merge semantics, etc.), and  as a guide for
  adding new builtins/DLC libraries/statements/expressions.


---

## v0.1.0 - 2026-09-18

# Description

- Define initial grammar rules
- Set up basic engine skeleton and directory structure
- Add README and initial documentation


---
