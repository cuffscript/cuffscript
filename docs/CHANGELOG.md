## v1.5.0 - 2026-09-19

## Description

- **Operators are now enums instead of strings.** / stored the operator as a  and the interpreter dispatched through an  chain. Profiling showed this cost **16.2 million string comparisons** on a 635k-call benchmark — about 25 per call, the largest single cost in the engine. Now a  over / assigned at parse time. : 0.241s → 0.149s.
- **Variable and function names are interned to integer IDs** (), so scope lookups compare s over a contiguous vector instead of strings. A 400-global lookup benchmark went 0.059s → 0.027s. Names are still stored alongside for error messages.
- **Interpreter scope-storage optimization.** Profiling showed parameter binding dominated call cost (~60ns per parameter, 72% of a 3-argument call) because each one was an  insert: a hash plus a node allocation. Scopes are small in practice, so  now stores variables in a contiguous vector with linear lookup — one allocation per scope instead of one per variable — falling back to a lazily-built index once a scope exceeds 16 entries, which keeps large global scopes fast. Measured: 0-argument call overhead 72ns → 32ns, 3-argument 252ns → 164ns.
- Decided _against_ the larger slot-resolution refactor (pre-resolving every variable to an array index at parse time) and a bytecode VM for now. The measurements above captured most of the available win for a fraction of the risk, and the engine is now in the same performance range as CPython on call-heavy code. See  section 21 for details.
- Net effect of optimizations this release:  0.32s → 0.140s (~56% faster), a 2M-iteration loop 0.287s → 0.173s, 3-argument call overhead 252ns → ~145ns.
- Added  (, ). Object/array/string/number/true/false/null map onto map/list/str/number/boolean/empty.  pretty-prints. The parser is strict per RFC 8259 — trailing commas, single quotes, unquoted keys, leading zeros, /, and trailing content are rejected — and decodes  escapes including surrogate pairs. See  section 19.
- **Error-code audit and normalization.**  (E4010 ) was being used both for wrong argument _counts_ and for bad argument _values_ (, , malformed JSON). Added  /  (E4025) and rerouted the six value-problem sites to it. Also normalized message capitalization across every throw site: all messages now start lowercase, since they're always printed after a  prefix.
- **The regex matcher is now codepoint-based instead of byte-based.**  matches one character rather than one byte ( is now true, previously false); literal non-ASCII characters in a pattern compile to a single multi-byte literal node; negated sets () match non-ASCII codepoints;  only starts attempts on codepoint boundaries and  alternatives must end on one.  treats non-ASCII letters as word characters, per REGEX.md section 17. //// stay ASCII-only per spec. See  section 17.
- **Fractional indices and range bounds are now a runtime error** (, E4024) instead of being silently rounded. Applies to , , slices, and  bounds. Whole-valued doubles (, ) still work.
- Moved  from  to , since the regex engine now uses it too.
- Added  for standalone C++ unit tests, and moved the regex engine's own test suite there (78 cases, including new Unicode ones).  now builds and runs it first.
- Expanded integration tests: added  (plus 7 error cases), , , and 3 fractional-index error cases. All 66 script tests and 78 regex unit tests pass unchanged.


---

## v1.4.0 - 2026-09-18

## Description

- Added an automated regression test suite (, ): 
  (exact output diff),  (must fail with a specific error code), plus the
  existing / are now checked the same way instead of only
  by hand. Wired into CI ().
- **Fixed a real crash (SIGSEGV) in the regex matcher**: a pattern with deep linear recursion
  (e.g.  against a ~20k+ character string) overflowed the real C++ stack before the
  matcher's own recursion-depth guard could throw its safety exception — the guard's limit
  (20,000) was measured, empirically, to be *higher* than where the actual crash occurs on an
  8MB stack (~19,500). Lowered the default depth limit to 3,000 (a large margin below the
  observed crash point) and verified no crash from 100 to 1,000,000 characters. Found by the
  new test suite.
- Found and documented (not yet fixed) that names using certain keywords — , ,
  .
./.git
./.git/hooks
./.git/hooks/pre-merge-commit.sample
./.git/hooks/push-to-checkout.sample
./.git/hooks/applypatch-msg.sample
./.git/hooks/pre-push.sample
./.git/hooks/pre-commit.sample
./.git/hooks/pre-applypatch.sample
./.git/hooks/pre-receive.sample
./.git/hooks/post-update.sample
./.git/hooks/fsmonitor-watchman.sample
./.git/hooks/sendemail-validate.sample
./.git/hooks/update.sample
./.git/hooks/commit-msg.sample
./.git/hooks/prepare-commit-msg.sample
./.git/hooks/pre-rebase.sample
./.git/ORIG_HEAD
./.git/logs
./.git/logs/HEAD
./.git/logs/refs
./.git/logs/refs/heads
./.git/logs/refs/heads/main
./.git/logs/refs/heads/chore
./.git/logs/refs/heads/chore/update-changelog-v1.4.0
./.git/logs/refs/remotes
./.git/logs/refs/remotes/origin
./.git/logs/refs/remotes/origin/main
./.git/info
./.git/info/exclude
./.git/index
./.git/HEAD
./.git/config
./.git/config.worktree
./.git/FETCH_HEAD
./.git/description
./.git/refs
./.git/refs/heads
./.git/refs/heads/main
./.git/refs/heads/chore
./.git/refs/heads/chore/update-changelog-v1.4.0
./.git/refs/tags
./.git/refs/tags/v1.2.1
./.git/refs/tags/v1.1.0
./.git/refs/tags/v1.0.0
./.git/refs/tags/v1.2.0
./.git/refs/tags/v1.4.0
./.git/refs/tags/v0.1.0
./.git/refs/tags/v1.3.0
./.git/refs/remotes
./.git/refs/remotes/origin
./.git/refs/remotes/origin/main
./.git/objects
./.git/objects/info
./.git/objects/pack
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.idx
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.pack
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.rev
./.devcontainer
./.devcontainer/devcontainer.json
./tests
./tests/cases
./tests/cases/truthiness.cuff
./tests/cases/indexing_boundaries.cuff
./tests/cases/comparison_operators.cuff
./tests/cases/truthiness.expected
./tests/cases/indexing_boundaries.expected
./tests/cases/equality_semantics.cuff
./tests/cases/equality_semantics.expected
./tests/cases/arithmetic_operators.expected
./tests/cases/arithmetic_operators.cuff
./tests/cases/number_formatting.cuff
./tests/cases/number_formatting.expected
./tests/cases/comparison_operators.expected
./tests/errors
./tests/errors/regex_recursion_limit.cuff
./tests/errors/regex_stacked_quantifier.expected_code
./tests/errors/type_mismatch_arithmetic.cuff
./tests/errors/regex_unexpected_character.cuff
./tests/errors/is_string_literal_requires_str_lhs.expected_code
./tests/errors/key_not_found.cuff
./tests/errors/unexpected_token.expected_code
./tests/errors/regex_colon_spacing.expected_code
./tests/errors/undefined_function.expected_code
./tests/errors/regex_recursion_limit.expected_code
./tests/errors/regex_unclosed_group.cuff
./tests/errors/return_outside_function.expected_code
./tests/errors/undefined_function.cuff
./tests/errors/regex_empty_token.cuff
./tests/errors/is_string_literal_requires_str_lhs.cuff
./tests/errors/dlc_feature_unavailable.cuff
./tests/errors/undefined_variable.expected_code
./tests/errors/element_not_found.cuff
./tests/errors/regex_unexpected_character.expected_code
./tests/errors/element_not_found.expected_code
./tests/errors/module_parse_failed.cuff
./tests/errors/unexpected_token.cuff
./tests/errors/fixtures
./tests/errors/fixtures/broken_module.cuff
./tests/errors/regex_stacked_quantifier.cuff
./tests/errors/argument_count_mismatch.expected_code
./tests/errors/regex_invalid_quantifier_range.expected_code
./tests/errors/module_not_found.expected_code
./tests/errors/key_not_found.expected_code
./tests/errors/module_parse_failed.expected_code
./tests/errors/regex_invalid_escape.expected_code
./tests/errors/dlc_feature_unavailable.expected_code
./tests/errors/invalid_global_declaration.cuff
./tests/errors/regex_empty_token.expected_code
./tests/errors/argument_count_mismatch.cuff
./tests/errors/type_mismatch_arithmetic.expected_code
./tests/errors/regex_dangling_quantifier.cuff
./tests/errors/regex_dangling_quantifier.expected_code
./tests/errors/stop_outside_loop.expected_code
./tests/errors/regex_invalid_quantifier_range.cuff
./tests/errors/undefined_variable.cuff
./tests/errors/return_outside_function.cuff
./tests/errors/module_not_found.cuff
./tests/errors/regex_colon_spacing.cuff
./tests/errors/regex_invalid_escape.cuff
./tests/errors/regex_unclosed_group.expected_code
./tests/errors/invalid_global_declaration.expected_code
./tests/errors/stop_outside_loop.cuff
./tests/README.md
./tests/run.sh
./SECURITY.md
./npm
./npm/dist
./npm/dist/cuffscript.d.ts
./npm/package.json
./npm/.gitignore
./npm/README.md
./npm/LICENSE
./main.cpp
./temp.md
./examples
./examples/05_scoping_and_globals.cuff
./examples/02_comprehensive_demo.cuff
./examples/07_functions_async.cuff
./examples/10_async_ordering.expected
./examples/06_error_recovery.cuff
./examples/lib
./examples/lib/greetings.cuff
./examples/11_utf8_strings.expected
./examples/06_error_recovery.expected
./examples/01_hello.expected
./examples/10_async_ordering.cuff
./examples/04_collections.cuff
./examples/error_cases
./examples/error_cases/await_on_nonasync.cuff
./examples/error_cases/await_on_nonasync.expected_code
./examples/error_cases/declaration_type_mismatch.cuff
./examples/error_cases/regex_redos_guard.cuff
./examples/error_cases/zero_index.expected_code
./examples/error_cases/stack_overflow.cuff
./examples/error_cases/index_out_of_range.expected_code
./examples/error_cases/constant_reassignment.cuff
./examples/error_cases/constant_reassignment.expected_code
./examples/error_cases/bad_constant_name.cuff
./examples/error_cases/return_from_nonreturnable.cuff
./examples/error_cases/zero_index.cuff
./examples/error_cases/regex_syntax_error.cuff
./examples/error_cases/stack_overflow.expected_code
./examples/error_cases/division_by_zero.cuff
./examples/error_cases/declaration_type_mismatch.expected_code
./examples/error_cases/regex_syntax_error.expected_code
./examples/error_cases/unknown_dlc.cuff
./examples/error_cases/regex_redos_guard.expected_code
./examples/error_cases/index_out_of_range.cuff
./examples/error_cases/nested_function.expected_code
./examples/error_cases/bad_constant_name.expected_code
./examples/error_cases/division_by_zero.expected_code
./examples/error_cases/return_from_nonreturnable.expected_code
./examples/error_cases/nested_function.cuff
./examples/error_cases/unknown_dlc.expected_code
./examples/05_scoping_and_globals.expected
./examples/07_functions_async.expected
./examples/11_utf8_strings.cuff
./examples/maps
./examples/maps/core_engine
./examples/maps/core_engine/stage_data.cuff
./examples/04_collections.expected
./examples/08_dlc_libraries.cuff
./examples/09_modules_demo.expected
./examples/03_pattern_matching.cuff
./examples/README.md
./examples/02_comprehensive_demo.expected
./examples/03_pattern_matching.expected
./examples/09_modules_demo.cuff
./examples/01_hello.cuff
./CODE_OF_CONDUCT.md
./engine
./engine/debug
./engine/debug/ASTPrinter.h
./engine/interpreter
./engine/interpreter/NativeFunctions.h
./engine/interpreter/Environment.h
./engine/interpreter/Interpreter.h
./engine/interpreter/Utf8.h
./engine/interpreter/Value.h
./engine/interpreter/Signals.h
./engine/CuffEngine.h
./engine/parser
./engine/parser/LiteralParser.h
./engine/parser/ExpressionParser.h
./engine/parser/CollectionOpParser.h
./engine/parser/ControlFlowParser.h
./engine/parser/ASTNodes.h
./engine/parser/Parser.h
./engine/parser/DeclarationParser.h
./engine/parser/OrElseParser.h
./engine/parser/RegexExprParser.h
./engine/parser/ImportParser.h
./engine/parser/LoopParser.h
./engine/parser/ParserCore.h
./engine/parser/FunctionParser.h
./engine/parser/StatementParser.h
./engine/lexer
./engine/lexer/ColonValidator.h
./engine/lexer/KeywordClassifier.h
./engine/lexer/Lexer.h
./engine/tokenizer
./engine/tokenizer/ScanState.h
./engine/tokenizer/OperatorScanner.h
./engine/tokenizer/NumberScanner.h
./engine/tokenizer/CommentScanner.h
./engine/tokenizer/Tokenizer.h
./engine/tokenizer/IdentifierScanner.h
./engine/tokenizer/CharUtils.h
./engine/tokenizer/StringScanner.h
./engine/regex
./engine/regex/RegexAst.h
./engine/regex/RegexMatcher.h
./engine/regex/RegexEngine.h
./engine/regex/RegexParser.h
./engine/common
./engine/common/ErrorCodes.h
./engine/common/Token.h
./engine/common/CuffError.h
./engine/common/SourceLocation.h
./engine/common/TokenTypes.h
./.editorconfig
./CONTRIBUTING.md
./wasm
./wasm/bindings.cpp
./.gitattributes
./docs
./docs/IMPLEMENTATION_NOTES.md
./docs/CHANGELOG.md
./docs/SPEC.md
./docs/REGEX.md
./.gitignore
./Makefile
./README.md
./assets
./assets/cuffscript_vertical.png
./assets/cuffscript_symbol_512.png
./assets/cuffscript_horiz.svg
./assets/cuffscript_vertical.svg
./assets/cuffscript_symbol.svg
./assets/cuffscript_horiz.png
./README_ko.md
./.github
./.github/workflows
./.github/workflows/update-changelog.yaml
./.github/workflows/PR-conflict-check.yaml
./.github/pull_request_template.md
./LICENSE, , , , , , , , etc. — can't be used as
  variable or function names anywhere ( fails to parse). Same root cause
  as the  import-parsing bug fixed earlier, but spread across every place an
  identifier is declared. See  section 16.
- Fixed , which incorrectly demonstrated  catching
  a .
./.git
./.git/hooks
./.git/hooks/pre-merge-commit.sample
./.git/hooks/push-to-checkout.sample
./.git/hooks/applypatch-msg.sample
./.git/hooks/pre-push.sample
./.git/hooks/pre-commit.sample
./.git/hooks/pre-applypatch.sample
./.git/hooks/pre-receive.sample
./.git/hooks/post-update.sample
./.git/hooks/fsmonitor-watchman.sample
./.git/hooks/sendemail-validate.sample
./.git/hooks/update.sample
./.git/hooks/commit-msg.sample
./.git/hooks/prepare-commit-msg.sample
./.git/hooks/pre-rebase.sample
./.git/ORIG_HEAD
./.git/logs
./.git/logs/HEAD
./.git/logs/refs
./.git/logs/refs/heads
./.git/logs/refs/heads/main
./.git/logs/refs/heads/chore
./.git/logs/refs/heads/chore/update-changelog-v1.4.0
./.git/logs/refs/remotes
./.git/logs/refs/remotes/origin
./.git/logs/refs/remotes/origin/main
./.git/info
./.git/info/exclude
./.git/index
./.git/HEAD
./.git/config
./.git/config.worktree
./.git/FETCH_HEAD
./.git/description
./.git/refs
./.git/refs/heads
./.git/refs/heads/main
./.git/refs/heads/chore
./.git/refs/heads/chore/update-changelog-v1.4.0
./.git/refs/tags
./.git/refs/tags/v1.2.1
./.git/refs/tags/v1.1.0
./.git/refs/tags/v1.0.0
./.git/refs/tags/v1.2.0
./.git/refs/tags/v1.4.0
./.git/refs/tags/v0.1.0
./.git/refs/tags/v1.3.0
./.git/refs/remotes
./.git/refs/remotes/origin
./.git/refs/remotes/origin/main
./.git/objects
./.git/objects/info
./.git/objects/pack
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.idx
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.pack
./.git/objects/pack/pack-122fb0168536df40d1cc4a6b8f237fd62005adef.rev
./.devcontainer
./.devcontainer/devcontainer.json
./tests
./tests/cases
./tests/cases/truthiness.cuff
./tests/cases/indexing_boundaries.cuff
./tests/cases/comparison_operators.cuff
./tests/cases/truthiness.expected
./tests/cases/indexing_boundaries.expected
./tests/cases/equality_semantics.cuff
./tests/cases/equality_semantics.expected
./tests/cases/arithmetic_operators.expected
./tests/cases/arithmetic_operators.cuff
./tests/cases/number_formatting.cuff
./tests/cases/number_formatting.expected
./tests/cases/comparison_operators.expected
./tests/errors
./tests/errors/regex_recursion_limit.cuff
./tests/errors/regex_stacked_quantifier.expected_code
./tests/errors/type_mismatch_arithmetic.cuff
./tests/errors/regex_unexpected_character.cuff
./tests/errors/is_string_literal_requires_str_lhs.expected_code
./tests/errors/key_not_found.cuff
./tests/errors/unexpected_token.expected_code
./tests/errors/regex_colon_spacing.expected_code
./tests/errors/undefined_function.expected_code
./tests/errors/regex_recursion_limit.expected_code
./tests/errors/regex_unclosed_group.cuff
./tests/errors/return_outside_function.expected_code
./tests/errors/undefined_function.cuff
./tests/errors/regex_empty_token.cuff
./tests/errors/is_string_literal_requires_str_lhs.cuff
./tests/errors/dlc_feature_unavailable.cuff
./tests/errors/undefined_variable.expected_code
./tests/errors/element_not_found.cuff
./tests/errors/regex_unexpected_character.expected_code
./tests/errors/element_not_found.expected_code
./tests/errors/module_parse_failed.cuff
./tests/errors/unexpected_token.cuff
./tests/errors/fixtures
./tests/errors/fixtures/broken_module.cuff
./tests/errors/regex_stacked_quantifier.cuff
./tests/errors/argument_count_mismatch.expected_code
./tests/errors/regex_invalid_quantifier_range.expected_code
./tests/errors/module_not_found.expected_code
./tests/errors/key_not_found.expected_code
./tests/errors/module_parse_failed.expected_code
./tests/errors/regex_invalid_escape.expected_code
./tests/errors/dlc_feature_unavailable.expected_code
./tests/errors/invalid_global_declaration.cuff
./tests/errors/regex_empty_token.expected_code
./tests/errors/argument_count_mismatch.cuff
./tests/errors/type_mismatch_arithmetic.expected_code
./tests/errors/regex_dangling_quantifier.cuff
./tests/errors/regex_dangling_quantifier.expected_code
./tests/errors/stop_outside_loop.expected_code
./tests/errors/regex_invalid_quantifier_range.cuff
./tests/errors/undefined_variable.cuff
./tests/errors/return_outside_function.cuff
./tests/errors/module_not_found.cuff
./tests/errors/regex_colon_spacing.cuff
./tests/errors/regex_invalid_escape.cuff
./tests/errors/regex_unclosed_group.expected_code
./tests/errors/invalid_global_declaration.expected_code
./tests/errors/stop_outside_loop.cuff
./tests/README.md
./tests/run.sh
./SECURITY.md
./npm
./npm/dist
./npm/dist/cuffscript.d.ts
./npm/package.json
./npm/.gitignore
./npm/README.md
./npm/LICENSE
./main.cpp
./temp.md
./examples
./examples/05_scoping_and_globals.cuff
./examples/02_comprehensive_demo.cuff
./examples/07_functions_async.cuff
./examples/10_async_ordering.expected
./examples/06_error_recovery.cuff
./examples/lib
./examples/lib/greetings.cuff
./examples/11_utf8_strings.expected
./examples/06_error_recovery.expected
./examples/01_hello.expected
./examples/10_async_ordering.cuff
./examples/04_collections.cuff
./examples/error_cases
./examples/error_cases/await_on_nonasync.cuff
./examples/error_cases/await_on_nonasync.expected_code
./examples/error_cases/declaration_type_mismatch.cuff
./examples/error_cases/regex_redos_guard.cuff
./examples/error_cases/zero_index.expected_code
./examples/error_cases/stack_overflow.cuff
./examples/error_cases/index_out_of_range.expected_code
./examples/error_cases/constant_reassignment.cuff
./examples/error_cases/constant_reassignment.expected_code
./examples/error_cases/bad_constant_name.cuff
./examples/error_cases/return_from_nonreturnable.cuff
./examples/error_cases/zero_index.cuff
./examples/error_cases/regex_syntax_error.cuff
./examples/error_cases/stack_overflow.expected_code
./examples/error_cases/division_by_zero.cuff
./examples/error_cases/declaration_type_mismatch.expected_code
./examples/error_cases/regex_syntax_error.expected_code
./examples/error_cases/unknown_dlc.cuff
./examples/error_cases/regex_redos_guard.expected_code
./examples/error_cases/index_out_of_range.cuff
./examples/error_cases/nested_function.expected_code
./examples/error_cases/bad_constant_name.expected_code
./examples/error_cases/division_by_zero.expected_code
./examples/error_cases/return_from_nonreturnable.expected_code
./examples/error_cases/nested_function.cuff
./examples/error_cases/unknown_dlc.expected_code
./examples/05_scoping_and_globals.expected
./examples/07_functions_async.expected
./examples/11_utf8_strings.cuff
./examples/maps
./examples/maps/core_engine
./examples/maps/core_engine/stage_data.cuff
./examples/04_collections.expected
./examples/08_dlc_libraries.cuff
./examples/09_modules_demo.expected
./examples/03_pattern_matching.cuff
./examples/README.md
./examples/02_comprehensive_demo.expected
./examples/03_pattern_matching.expected
./examples/09_modules_demo.cuff
./examples/01_hello.cuff
./CODE_OF_CONDUCT.md
./engine
./engine/debug
./engine/debug/ASTPrinter.h
./engine/interpreter
./engine/interpreter/NativeFunctions.h
./engine/interpreter/Environment.h
./engine/interpreter/Interpreter.h
./engine/interpreter/Utf8.h
./engine/interpreter/Value.h
./engine/interpreter/Signals.h
./engine/CuffEngine.h
./engine/parser
./engine/parser/LiteralParser.h
./engine/parser/ExpressionParser.h
./engine/parser/CollectionOpParser.h
./engine/parser/ControlFlowParser.h
./engine/parser/ASTNodes.h
./engine/parser/Parser.h
./engine/parser/DeclarationParser.h
./engine/parser/OrElseParser.h
./engine/parser/RegexExprParser.h
./engine/parser/ImportParser.h
./engine/parser/LoopParser.h
./engine/parser/ParserCore.h
./engine/parser/FunctionParser.h
./engine/parser/StatementParser.h
./engine/lexer
./engine/lexer/ColonValidator.h
./engine/lexer/KeywordClassifier.h
./engine/lexer/Lexer.h
./engine/tokenizer
./engine/tokenizer/ScanState.h
./engine/tokenizer/OperatorScanner.h
./engine/tokenizer/NumberScanner.h
./engine/tokenizer/CommentScanner.h
./engine/tokenizer/Tokenizer.h
./engine/tokenizer/IdentifierScanner.h
./engine/tokenizer/CharUtils.h
./engine/tokenizer/StringScanner.h
./engine/regex
./engine/regex/RegexAst.h
./engine/regex/RegexMatcher.h
./engine/regex/RegexEngine.h
./engine/regex/RegexParser.h
./engine/common
./engine/common/ErrorCodes.h
./engine/common/Token.h
./engine/common/CuffError.h
./engine/common/SourceLocation.h
./engine/common/TokenTypes.h
./.editorconfig
./CONTRIBUTING.md
./wasm
./wasm/bindings.cpp
./.gitattributes
./docs
./docs/IMPLEMENTATION_NOTES.md
./docs/CHANGELOG.md
./docs/SPEC.md
./docs/REGEX.md
./.gitignore
./Makefile
./README.md
./assets
./assets/cuffscript_vertical.png
./assets/cuffscript_symbol_512.png
./assets/cuffscript_horiz.svg
./assets/cuffscript_vertical.svg
./assets/cuffscript_symbol.svg
./assets/cuffscript_horiz.png
./README_ko.md
./.github
./.github/workflows
./.github/workflows/update-changelog.yaml
./.github/workflows/PR-conflict-check.yaml
./.github/pull_request_template.md
./LICENSE that simply returns  on no match (not an exception) — added the correct
   check alongside a genuine or_else-recoverable example (out-of-range index).


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
