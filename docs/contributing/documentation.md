# Documentation

The documentation is part of the product, and it is kept honest by tooling rather than by discipline. This page describes how
it is organised and what `tools/docs.py` enforces.

## Layout

| Folder | For | Read it when |
| :--- | :--- | :--- |
| [`docs/guide/`](../guide/getting-started.md) | Learning: tutorials and how-tos | You are new, or doing a task |
| [`docs/reference/`](../reference/language.md) | Facts: the language, the libraries, the options, the errors, the limits | You need the exact answer |
| [`docs/internals/`](../internals/architecture.md) | How the engine works and why | You are changing it |
| `docs/contributing/` | How to build, test, extend, document and release | You are contributing |

Each source folder also has a short `README.md` that says what the folder is for and points here; it holds no facts that
belong elsewhere. [`docs/CHANGELOG.md`](../CHANGELOG.md) is written at release time (see [Releasing](releasing.md)).

## The rule: one fact, one home

If the same fact is written in two places, one of them goes stale. So every fact has one home and everything else links to it:

- **Limits** live in `engine/common/Limits.h`; the page [limits](../reference/limits.md) is generated from it. No other page writes a
  limit's value; it says what is limited and links.
- **Error codes** live in `engine/common/ErrorCodes.h`, with a one-line description beside each; [errors](../reference/errors.md) is
  generated.
- **Library functions** are discovered from the engine; the [function index](../reference/functions.md) is generated and each library
  page must cover its functions.
- **Command-line help** is the program's own `--help`, embedded in [`cuffc`](../reference/cuffc.md) and [`cuffsh`](../reference/cuffsh.md).
- **The language** is defined once, in the [language reference](../reference/language.md); guides teach it and link to it.

A guide, an FAQ or a summary page may state a fact again in a sentence when the reader needs it there to follow the text. What it must not carry is the
detail: the full rule, a table or a number stays in the home, and the sentence links to it. Before finishing a change to behaviour, search the
documentation for the words that describe it (`grep -rn` over `docs`, the root pages and the READMEs), so that a restatement is not left saying the old thing.

## Generated parts

A generated part sits between two markers and is never edited by hand:

```text
<!-- BEGIN GENERATED: error-table -->
...
<!-- END GENERATED: error-table -->
```

After changing a limit, an error code, a library function or a command-line option, run

```sh
python3 tools/docs.py generate
```

and commit the result. CI runs `python3 tools/docs.py check`, which fails if a generated part is stale.

## Runnable examples

A fenced block tagged `cuff` is shown to the reader; what follows the tag decides whether the checker runs it:

| Written as | The checker |
| :--- | :--- |
| A `cuff` block followed immediately by an `output` block | runs it and compares the output exactly |
| `cuff error E4-006` | runs it and requires it to fail with that code |
| `cuff run` | runs it and requires it to succeed (use this when the output varies) |
| a plain `cuff` block | does nothing; use it for fragments and for code that needs other files |

Blocks that need the network are not run. Each example runs alone, in a scratch folder, so it can create files.

## Links and headings

Every relative link and heading anchor must resolve; the checker follows GitHub's rules for turning a heading into an anchor.
Link to a page, not to a line. When you rename or move a file, `tools/docs.py check` lists what broke.

## Writing

- Plain, present-tense English, addressed to the reader ("you"), with short sentences.
- Say what something is for before how it works. Put the commonest case first.
- Examples that run beat examples that look right. Use the real output.
- Describe behaviour, not history. History belongs in the [changelog](../CHANGELOG.md) and in the [design notes](../internals/design-notes.md).
- Do not copy a table from the code or from another page. Generate it, or link to it.

## When you change something

| You changed | Update |
| :--- | :--- |
| A limit | `Limits.h`, then `generate` |
| An error code | `ErrorCodes.h` (with the description), then `generate` |
| A library function | The library's page under `docs/reference/dlc/` |
| An option or flag | The code only; the help text is the documentation, then `generate` |
| The syntax or meaning of the language | The [language reference](../reference/language.md), and the [pattern reference](../reference/patterns.md) for patterns. The [grammar](../reference/grammar.md) lists what the parser accepts, so a change to the shape of a statement or an expression updates it too |
| A word with a special meaning in these pages | The [glossary](../reference/glossary.md) |
| Something the language cannot do, or a behaviour that surprises | [Known limitations](../reference/known-limitations.md) and, for beginners, the [FAQ](../guide/faq.md) |
| The public C++ API (`CuffEngine`, `Session`, the options) | The [C++ API reference](../reference/cpp-api.md), and the options table in [Embedding the engine](../guide/embedding.md#options) |
| What a CI workflow does | [Continuous integration](ci.md) |
| Where a source file lives, or what it is for | The [source map](../internals/source-map.md) |
| How the engine works | The page under [`docs/internals/`](../internals/architecture.md) that describes that part |

`tools/docs.py check` needs a C++ compiler and the built binaries; with `--require-binaries` (as in CI) a missing one is an error,
and without it the checks that need it are skipped with a note.
