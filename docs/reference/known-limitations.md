# Known limitations

What CuffScript does not do, and the behaviours that surprise people, collected in one place. Some are decisions the language made on purpose, because it is small and aimed at beginners; others are things a particular build or library
cannot do yet. Every item links to the page that defines the behaviour. The [design notes](../internals/design-notes.md) say why most of the language decisions were made.

## The language

- **No closures and no function values.** A function cannot be stored in a variable, passed to another function or declared inside another function (`E4-018`). Functions are called by name. See [functions](language.md#6-functions).
- **No default or variable-length parameters.** A call must pass exactly as many arguments as the function declares (`E4-010`).
- **No `continue`, no `switch`, no classes.** `stop` ends the nearest loop; use `if` to skip the rest of a body.
- **One kind of number.** Every number is a double-precision float. Whole numbers are exact up to 2<sup>53</sup>, and a whole number of 10<sup>15</sup> or more prints in exponent form. There are no integer, bitwise or big-number operations. See [types and values](language.md#2-types-and-values).
- **Number literals are plain.** No exponent form (`1e5`), no leading or trailing point (`.5`, `5.`), no digit separators; a minus is an operator.
- **Strings cannot be changed in place**, there are no `\u` escapes, and an f-string has no format specifiers (`{x:2}` is a syntax error). Text is handled as Unicode code points, so a letter made of several code points counts as several characters.
- **Map keys are strings.** A map is not iterated directly; loop over `map_keys`.
- **`add` and `remove` need a variable name**, so `add 5 to grid[1]` adds to `grid` itself. See [collections](language.md#51-collections-add-change-remove).
- **A `repeat` range is never empty.** `1 ~ 0` runs twice, counting down. See [loops](language.md#53-loops).
- **`async` is not concurrency.** Queued calls run one after another after the main program. Nothing runs at the same time, and there is no sleep or timer.
- **Chained comparisons are not supported**: `1 < 2 < 3` is a type error.
- **Reserved words** cannot be names, apart from the few the language reference lists. See [names and reserved words](language.md#15-names-and-reserved-words).

## Patterns

- **No alternation outside `[one:...]`, no look-around and no back-references.** `|` outside `[one:...]` is an ordinary character, and a replacement is plain text.
- **A word in brackets that is not a token is read as a set of letters**, so a misspelled token is not reported.
- **`]` cannot be a member of a set**, and a set has no escapes.
- **`<` and `>` always open and close a named capture**; matching them literally needs `\<` and `\>`.
- **Case folding is for English letters only** (`i`, `IS`); `str_upper` and `str_lower` cover English, Latin-1, Latin Extended-A, Greek and Cyrillic, one character to one character.
- **`[email]`, `[phone]` and `[url]` are shape checks**, not validation: `[phone]` is the Korean number shape, and `[url]` only knows `http://` and `https://`. See [pattern syntax](patterns.md#19-real-world-format-preset-tokens-email-phone-url).
- Patterns are bounded by a step, time and recursion budget; a pattern that backtracks badly ends with a catchable error rather than running forever.

## Libraries

- **`DLC:network` is plain HTTP.** There is no TLS, no custom headers, no cookies and no access to response headers; `https://` is refused. A redirect chain longer than the limit ends with the last redirect response. See [`DLC:network`](dlc/network.md).
- **`DLC:filesystem` works on files only.** It cannot list, create or remove folders, and it stays inside the sandbox folder. See [`DLC:filesystem`](dlc/filesystem.md).
- **`DLC:random` is not secure and not private to a program.** It is one generator for the whole process, and it must not be used for secrets. See [`DLC:random`](dlc/random.md).
- **`DLC:time` is the wall clock** and has no calendar or formatting.
- **`DLC:json` writes and reads UTF-8 text**, rejects what the standard rejects (comments, trailing commas, `NaN`), and cannot write a `match`.

## Running it

- **There is no execution budget unless one is set.** `cuffc` and an embedding host run a program for as long as it wants unless `--max-steps` or `--timeout` is given, and nothing limits total memory. See [Security](../../SECURITY.md).
- **A list or map that contains itself is never freed.** Values are reference counted, so a cycle stays in memory until the process ends. See the [design notes](../internals/design-notes.md).
- **`--max-steps` counts loop iterations and calls of the program's own functions**, not calls of library functions, so a loop of library calls is bounded by the iterations only.
- **The WebAssembly binding runs with the default options**: no step or time budget, and no way to switch libraries off. A browser cannot make ordinary network connections. See [Embedding the engine](../guide/embedding.md#webassembly-and-npm).
- **Paths handed to the engine are in the platform's narrow encoding.** On Windows that is the ANSI code page; the shell converts its UTF-8 paths first.
