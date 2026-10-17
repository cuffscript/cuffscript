# Source locations and error reports

An error report names a place in the source and draws it. Getting that right across files, shell entries, tabs and
Korean text is the job of three small pieces: `SourceLocation`, `SourceRef` and `buildCaretSnippet`.

## SourceLocation

`engine/common/SourceLocation.h` holds three numbers, always consistent with one another:

| Field | Meaning |
| :--- | :--- |
| `offset` | The byte offset into the source text, from 0. |
| `line` | The line, from 1. A line ends at LF; a CR before it is part of the line ending. |
| `column` | The character on the line, from 1. Characters are counted, not bytes: the continuation bytes of a multi-byte character add nothing, and a tab is one column. |

A byte order mark at the start of the file is skipped before anything is counted, so it shifts neither the column nor the
offset's meaning. A unit test checks, over the shipped programs and hundreds of mutations of them, that for every error
the reported line and column are the ones the offset is really at.

## Where a token's location is

A location belongs to the **start** of a token, except for the tokens that end things:

- `NEWLINE` is located at the end of the statement it finishes: at the line break, after any trailing blanks. Not at the
  start of whatever follows. This is why `set number x to` followed by another statement is reported on its own line.
- The `DEDENT`s and `EOF` at the end of the program are located at the end of the last statement, not on an empty line
  past it.
- `INDENT` and `DEDENT` inside the program are located at the first character of the line they begin.

An f-string's `{...}` is parsed from a fragment of text that is scanned on its own, so its tokens start out with positions
relative to the fragment (line 1, column 1). They are then moved to where they really are in the program:

- The tokenizer stores a literal's value **after** escapes are decoded, so an offset into the value is not an offset into the source.
  `LiteralParser::buildValueOffsets` decodes the literal again from the program text, with the same table of escapes
  (`tokenizer/Escapes.h`, shared with the tokenizer), and records for every byte of the value the source offset it came from.
- `FragmentOrigin::map` turns a position in the fragment into one in the program: the offset through that table, then
  line and column counted forward from the literal's own location (`locationAfter`, the tokenizer's rules). So an escape such
  as `\t` or `\"` before or inside the braces takes its two source columns, and a literal that runs over several lines puts the
  expression on its own line.
- Errors raised while the fragment is scanned (relative to it) and while it is parsed (already real) are both reported at the real
  place, and say which expression they are in.

If the decoded text does not reproduce the literal's value exactly, or the parser has no program text (it was built without a
`SourceRef`), nothing in the fragment can be placed, and everything falls back to the start of the literal: coarse, but never on the
wrong line. `error_snippet_test` covers escapes, wide characters, several lines and the fallback.

## SourceRef: which text

An offset means nothing without the text it is an offset into. `SourceRef` (`common/SourceText.h`) is a shared pointer to
that text and, for a file loaded with `use`, its name.

1. `parseProgram` gives the `SourceRef` to the parser, and the parser stamps it on the program and on every
   `FunctionDecl`.
2. When a CuffError leaves a function body (`execFunctionBody`) or a module's top-level code, it is given the
   `SourceRef` of the function or module (`CuffError::attachSource`), unless something closer already did.
3. `Session::run` attaches the entry's own `SourceRef` to whatever is still unlabelled, so a parse error and an error in
   the entry's top-level code are drawn from the entry.

So an error is drawn from the text it was found in, however many files or shell entries lie between where it happened and
where it surfaced. A module that does not parse is wrapped as `E5-002` at the `use` statement, with the module's own
position and line in `CuffError::related`.

## Drawing the line

`buildCaretSnippet` (`common/ErrorFormat.h`) takes the text and a location and returns two lines: the source line and a caret
line. The caret line is built, not counted:

- each tab in the part of the line before the error is copied as a tab, so the caret lines up whatever the terminal's tab stops are;
- every other character contributes its display width: two for wide characters (Hangul, CJK, many emoji), none for
  combining marks, one otherwise. The width tables are in `common/Width.h`, shared with the shell's line editor;
- a CR left over from a CRLF line ending, and a byte order mark, are not drawn.

`renderErrorWithSnippet` then adds the headline (with `in 'file'` for an error from another file), the snippet, the
`related` block and the hint. `renderCurrentException` is the version for a `catch (...)` block and also turns
`std::bad_alloc` and unexpected exceptions into a report, so a front end never needs its own handler.

## When you add a construct that makes tokens

Take the location from the token that starts the construct, and put it on the AST node. If a new kind of error should be
reported somewhere other than where it is detected, move the location, not the message. Run the unit test
`error_snippet_test`, which will fail if a line and column ever disagree with their offset.
