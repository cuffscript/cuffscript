# Known limitations

What CuffScript does not do, and the behaviours that surprise people, collected in one place. Some are decisions the language made on purpose, because it is small and aimed at beginners; others are things a particular build or library
cannot do yet. Every item links to the page that defines the behaviour. The [design notes](../internals/design-notes.md) say why most of the language decisions were made, and [Phenomena](../guide/phenomena.md) lists the behaviours that follow from how the engine works.

## The language

- **No closures and no function values.** A function cannot be stored in a variable, passed to another function or declared inside another function (`E4-018`). Functions are called by name. See [functions](language.md#6-functions).
- **No variable-length parameters.** A call passes at least as many arguments as the function has parameters without a default, and at most as many as it declares (`E4-010`).
- **No `switch`.** Use `if` and `else if`.
- **One kind of number.** Every number is a double-precision float. Whole numbers are exact up to 2<sup>53</sup>, and a whole number of 10<sup>15</sup> or more prints in exponent form. There are no integer or big-number types. [`DLC:math`](dlc/math.md#bits-and-bases) has bit operations on the whole numbers from 0 to 2<sup>53</sup> - 1, and none on a negative number or on a larger one. See [types and values](language.md#2-types-and-values).
- **Number literals are decimal.** No hexadecimal, binary or octal form, no leading or trailing point (`.5`, `5.`), no digit separators; a minus is an operator. The exponent form (`1e5`, `2.5e-3`) is accepted.
- **Strings cannot be changed in place**, there are no `\u` escapes, and an f-string has no format specifiers (`{x:2}` is a syntax error). Text is handled as Unicode code points, so a letter made of several code points counts as several characters.
- **Map keys are strings.** `loop each` over a map visits its keys, in the order they were added; use the key to read the value.
- **A slice and the result of a call are not places.** An assignment, `add` and `remove` work on a name, an item or a field (`grid[2][3]`, `self.items`), not on `f()` or `x[1~2]`. See [places](language.md#51-places-assignment-add-remove).
- **A `repeat` range is never empty.** `1 ~ 0` runs twice, counting down. See [loops](language.md#53-loops).
- **Tasks take turns; they do not run at the same time.** A task runs until it waits (`await`, `time_sleep`, a network request) or ends, and nothing interrupts one that does neither: a long computation in one task holds the others up. Waiting overlaps, computing does not, and there are no threads. A task cannot be cancelled. See [tasks](language.md#65-asynchronous-functions-and-tasks).
- **Chained comparisons are not supported**: `1 < 2 < 3` is a type error.
- **Reserved words** cannot be names, apart from the few the language reference lists. See [names and reserved words](language.md#15-names-and-reserved-words).

## Classes

- **No static members, abstract classes, interfaces, operator overloading or multiple inheritance.** A class has one optional parent, and an object has its fields and methods and nothing else. See [classes](classes.md).
- **No `protected`.** A member is public, or `private` to the class that declares it; a child class cannot see its parent's private members.
- **The fields of a class are fixed.** An object cannot gain a field that its class does not declare (`E4-033`).
- **A class is declared at the top level of a file** and cannot be declared in a block, in a function or in a method (`E4-036`).
- **Methods are not values**, like functions: `object.method` without the parentheses is an error (`E4-040`).
- **There are no destructors.** An object is freed when nothing refers to it any more (a cycle of objects too, a little later), and nothing of the program runs when that happens.
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

- **A network request looks up its host name without waiting the way it waits for the answer.** The lookup holds the program (and its other tasks) up until the system answers; the connection and the answer do not.
- **`DLC:network` is plain HTTP.** There is no TLS, no custom headers, no cookies and no access to response headers; `https://` is refused. A redirect chain longer than the limit ends with the last redirect response. See [`DLC:network`](dlc/network.md).
- **`DLC:filesystem` stays inside the sandbox folder and never replaces or deletes by surprise.** It cannot remove a folder that has something in it, copy a folder, or read file times and permissions, and `file_copy` and `file_move` stop at an existing destination. See [`DLC:filesystem`](dlc/filesystem.md).
- **`DLC:hash` is not an audited cryptographic library.** Its hashes are written for the engine and checked against the published test vectors and against another implementation, but nobody has reviewed them as security code. It makes no random numbers, so a salt or a key must come from elsewhere; only `hash_equal` compares in constant time; SHA-1 and MD5 are there for compatibility, not security. See [`DLC:hash`](dlc/hash.md).
- **`DLC:csv` reads and writes text that is in memory**, and every field it reads is a `str`. It knows nothing of types, sheets or encodings other than UTF-8. See [`DLC:csv`](dlc/csv.md).
- **`DLC:path` only knows `/`.** A backslash is an ordinary character, so a Windows path written with backslashes is one name. See [`DLC:path`](dlc/path.md).
- **`DLC:hardware` is only what the host gives.** The engine drives no device itself, so a part works as the host's function for it does; `cuffc --hardware` gives the battery and the temperature on Linux, the battery on Windows, and a beep (on Linux and macOS the terminal's bell, whose pitch and length the terminal decides). Reading a part does not wait the way tasks do: a host function that takes long holds the program up. A serial line is read as what has arrived, without waiting for more. See [`DLC:hardware`](dlc/hardware.md).
- **`DLC:random` is not secure and not private to a program.** It is one generator for the whole process, and it must not be used for secrets. See [`DLC:random`](dlc/random.md).
- **`DLC:time` is the wall clock and a sleep** and has no calendar, formatting or timers that call back; the calendar is in `DLC:datetime`. In WebAssembly a sleep keeps the processor busy.
- **`DLC:datetime` has no time zone names.** A zone is an offset in minutes that the script gives (or `datetime_offset`, which reads the machine's), so it knows nothing about daylight saving rules, month or weekday names, or years outside 1 to 9999. See [`DLC:datetime`](dlc/datetime.md).
- **`DLC:json` writes and reads UTF-8 text**, rejects what the standard rejects (comments, trailing commas, `NaN`), and cannot write a `match` or a `pattern`. An object is written as its public fields.

## Running it

- **There is no execution budget unless one is set.** `cuffc` and an embedding host run a program for as long as it wants unless `--max-steps` or `--timeout` is given, and nothing limits total memory. See [Security](../../SECURITY.md).
- **A cycle is freed a little later than other values.** Values are counted, and a list, map or object that refers to itself waits for the engine to look for cycles, which it does after it has made a good number of new ones ([memory](../internals/how-it-works.md#memory)). A cycle that a host still holds when its engine is destroyed is not freed.
- **`--max-steps` counts loop iterations and calls of the program's own functions**, not calls of library functions, so a loop of library calls is bounded by the iterations only.
- **The WebAssembly binding runs with the default options**: no step or time budget, and no way to switch libraries off. A browser cannot make ordinary network connections. See [Embedding the engine](../guide/embedding.md#webassembly-and-npm).
- **In WebAssembly, work that nests inside the engine stops sooner.** The JavaScript engine's stack is far smaller than a native thread's and cannot be measured, so the WebAssembly build counts instead: a repeated group in a pattern stops with `E3-103` at about a tenth of the native depth (`(a)+` over about 100 characters), and runs of the machine inside one another (an object's `to_str` that shows another object, a field's default value that makes an object whose default makes another) with `E4-017` after 100 levels. Up to 6.3.0 both ended in a JavaScript `RangeError` thrown out of `cuffRun`. A program's own recursion and a repeated single character, such as `[any]*`, nest nothing and are not affected.
- **On Windows, an ANSI path that is also valid UTF-8 is read as UTF-8.** The paths a host hands the engine (`scriptDir`, `rootDir`) are UTF-8, and one that is not UTF-8 is read in the ANSI code page, as every path was before 6.2.0. A name in the ANSI code page whose bytes happen to be valid UTF-8 (rare: `Ã©`, for instance, in code page 1252) is read as the UTF-8 text. See [Embedding the engine](../guide/embedding.md#options).
- **Running out of memory while `file_list` reads a folder ends the process.** The C++ library that lists folders on Linux (GCC's) ends the process when it cannot get memory, instead of reporting it; everywhere else running out of memory is `E6-003`, and the engine can be used again.
