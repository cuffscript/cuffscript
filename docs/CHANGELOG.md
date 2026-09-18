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
