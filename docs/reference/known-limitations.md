# Known limitations

What CuffScript does not do, and the behaviours that surprise people, collected in one place. Some are decisions the language made on purpose, because it is small and aimed at beginners; others are things a particular build or library
cannot do yet. Every item links to the page that defines the behaviour. The [design notes](../internals/design-notes.md) say why most of the language decisions were made, and [Phenomena](../guide/phenomena.md) lists the behaviours that follow from how the engine works.

## The language

- **No closures and no function values.** A function cannot be stored in a variable, passed to another function or declared inside another function (`E4-018`). Functions are called by name. See [functions](language.md#6-functions).
- **No variable-length parameters.** A call passes at least as many arguments as the function has parameters without a default, and at most as many as it declares (`E4-010`).
- **No `switch`.** Use `if` and `else if`.
- **One kind of number.** Every number is a double-precision float. Whole numbers are exact up to 2<sup>53</sup>, and a whole number of 10<sup>15</sup> or more prints in exponent form. There are no integer, bitwise or big-number operations. See [types and values](language.md#2-types-and-values).
- **Number literals are plain.** No exponent form (`1e5`), no leading or trailing point (`.5`, `5.`), no digit separators; a minus is an operator.
- **Strings cannot be changed in place**, there are no `\u` escapes, and an f-string has no format specifiers (`{x:2}` is a syntax error). Text is handled as Unicode code points, so a letter made of several code points counts as several characters.
- **Map keys are strings.** `loop each` over a map visits its keys, in the order they were added; use the key to read the value.
- **A slice and the result of a call are not places.** `change`, `add` and `remove` work on a name, an item or a field (`grid[2][3]`, `self.items`), not on `f()` or `x[1~2]`. See [changing places](language.md#51-changing-places-change-add-remove).
- **A `repeat` range is never empty.** `1 ~ 0` runs twice, counting down. See [loops](language.md#53-loops).
- **`async` is not concurrency.** Queued calls run one after another after the main program. Nothing runs at the same time, and there is no sleep or timer.
- **Chained comparisons are not supported**: `1 < 2 < 3` is a type error.
- **Reserved words** cannot be names, apart from the few the language reference lists. See [names and reserved words](language.md#15-names-and-reserved-words).

## Classes

- **No static members, abstract classes, interfaces, operator overloading or multiple inheritance.** A class has one optional parent, and an object has its fields and methods and nothing else. See [classes](classes.md).
- **No `protected`.** A member is public, or `private` to the class that declares it; a child class cannot see its parent's private members.
- **The fields of a class are fixed.** An object cannot gain a field that its class does not declare (`E4-033`), and a field's declared type is checked on its default value only.
- **A class is declared at the top level of a file** and cannot be declared in a block, in a function or in a method (`E4-036`).
- **Methods are not values**, like functions: `object.method` without the parentheses is an error (`E4-040`).
- **A cycle of objects is never freed.** Two objects that refer to each other, or an object that holds itself in a field, stay in memory until the process ends, as a list that contains itself does. There are no destructors.
- **`from_json` gives a map, not an object.** Build the object from the map in your own code.

## Patterns

- **No alternation outside `[one:...]`, no look-around and no back-references.** `|` outside `[one:...]` is an ordinary character, and a replacement is plain text.
- **A word in brackets that is not a token is read as a set of letters**, so a misspelled token is not reported.
- **`]` cannot be a member of a set**, and a set has no escapes.
- **`<` and `>` always open and close a named capture**; matching them literally needs `\<` and `\>`.
- **Case folding is for English letters only** (the `i` flag); `str_upper` and `str_lower` cover English, Latin-1, Latin Extended-A, Greek and Cyrillic, one character to one character.
- **`[email]`, `[phone]` and `[url]` are shape checks**, not validation: `[phone]` is the Korean number shape, and `[url]` only knows `http://` and `https://`. See [pattern syntax](patterns.md#19-real-world-format-preset-tokens-email-phone-url).
- Patterns are bounded by a step, time and recursion budget; a pattern that backtracks badly ends with a catchable error rather than running forever.

## Libraries

- **`DLC:network` is plain HTTP.** There is no TLS, no custom headers, no cookies and no access to response headers; `https://` is refused. A redirect chain longer than the limit ends with the last redirect response. See [`DLC:network`](dlc/network.md).
- **`DLC:filesystem` works on files only.** It cannot list, create or remove folders, and it stays inside the sandbox folder. See [`DLC:filesystem`](dlc/filesystem.md).
- **`DLC:random` is not secure and not private to a program.** It is one generator for the whole process, and it must not be used for secrets. See [`DLC:random`](dlc/random.md).
- **`DLC:time` is the wall clock** and has no calendar or formatting.
- **`DLC:json` writes and reads UTF-8 text**, rejects what the standard rejects (comments, trailing commas, `NaN`), and cannot write a `match` or a `pattern`. An object is written as its public fields.

## Running it

- **There is no execution budget unless one is set.** `cuffc` and an embedding host run a program for as long as it wants unless `--max-steps` or `--timeout` is given, and nothing limits total memory. See [Security](../../SECURITY.md).
- **A list, map or object that refers to itself is never freed.** Values are reference counted, so a cycle stays in memory until the process ends. See the [design notes](../internals/design-notes.md).
- **`--max-steps` counts loop iterations and calls of the program's own functions**, not calls of library functions, so a loop of library calls is bounded by the iterations only.
- **The WebAssembly binding runs with the default options**: no step or time budget, and no way to switch libraries off. A browser cannot make ordinary network connections. See [Embedding the engine](../guide/embedding.md#webassembly-and-npm).
- **Paths handed to the engine are in the platform's narrow encoding.** On Windows that is the ANSI code page; the shell converts its UTF-8 paths first.
