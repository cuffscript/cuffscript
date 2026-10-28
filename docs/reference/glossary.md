# Glossary

The words this documentation uses in a particular way, in alphabetical order. Each entry points to the page that defines the thing fully.

**async function.** A function declared `set async func`. Calling it does not run it at once: the call is queued and runs after the main program, in order, unless it is run on the spot with `await`. Nothing runs in parallel. See [the language tour](../guide/language-tour.md#functions-that-run-later).

**AST (abstract syntax tree).** The tree the parser builds from the tokens of a program, and that the interpreter walks to run it. `cuffc --ast` prints it. See [Architecture](../internals/architecture.md).

**await.** Runs an `async` call right where it is written instead of queueing it. Using it on a function that is not `async` is `E4-019`.

**bridge.** `change name to global` inside a function: it makes the global variable `name` visible there. Without it a function sees only its own variables. A constant needs the same bridge to be read. See [scope](language.md#7-scope).

**budget.** A limit a host puts on a run: `--max-steps` and `--timeout`. A run that exhausts it stops with `E6-001` or `E6-002`, which cannot be caught. Budgets are off unless set. See [`cuffc`](cuffc.md#options).

**builtin.** A function that is always available without `use`: `print`, `input`, `type_of`, `to_number`, `to_str` and `to_boolean`. See [built-in functions](builtins.md).

**capture.** The part of a text that a group in a pattern matched, kept by `match`. Numbered captures come from `( )` and are counted from 1; named captures come from `<name:...>`. See [pattern syntax](patterns.md#22-extracting-1-based-indexed-captures-match).

**catchable (recoverable).** An error that `or_else` can handle. Run-time errors (`E4-`), module errors (`E5-`) and the errors a pattern raises while matching (`E3-1`) are catchable; syntax errors, pattern syntax errors and budget errors are not. See the [error catalogue](errors.md).

**constant.** A variable declared `set constant`, whose name is written in capitals and which cannot be changed. A constant list is also read-only through every name. See [the language reference](language.md#33-constants).

**DLC library.** One of the libraries a program loads with `use DLC:name`: `math`, `string`, `list`, `map`, `convert`, `time`, `random`, `json`, `filesystem` and `network`. See [libraries](dlc/README.md).

**display form.** How a value is turned into text by `print`, `to_str` and f-strings. See [types and values](language.md#2-types-and-values).

**embedding (host).** Using the engine from another program. The program that does so is the host; it decides the options, the budgets and what happens to output. See [Embedding the engine](../guide/embedding.md).

**empty.** The value that means nothing: the result of a failed `find`, of a function that returns nothing, and `null` in JSON. It is also a type name, and falsy.

**engine.** The C++ headers under `engine/` that tokenize, parse and run CuffScript. `cuffc`, `cuffsh` and the WebAssembly package are front ends to it.

**f-string.** A string written `f"..."` in which `{expression}` is replaced by the display form of the expression's value. `{{` and `}}` are literal braces.

**falsy / truthy.** Every value can be used as a condition. `false`, `0`, `empty`, the empty string, the empty list and the empty map are falsy; everything else is truthy, including the string `"0"`.

**flag.** A letter after a pattern statement: `i` ignores case, `g` finds every match, `m` makes `[start]` and `[end]` work on each line. See [pattern syntax](patterns.md#28-the-flags-system).

**function scope / block scope.** A function has a scope of its own that cannot see its caller's variables; the body of an `if` or a loop does not open a scope, so what it declares stays visible after it. See [scope](language.md#7-scope).

**handler.** The block after `or_else`, which runs when the statement before it fails.

**hoisting.** Functions are known to the whole program before it starts, so a function can be called above the place where it is declared. If a name is declared twice, the last declaration wins.

**interpolation.** See f-string.

**list.** An ordered collection of values, written `[1, "a"]`, indexed from 1. Lists are shared, not copied, when assigned.

**literal.** A value written directly in the program: `42`, `"text"`, `true`, `[1, 2]`.

**map.** A collection of string keys and values that remembers the order the keys were added, written `{"a": 1}`.

**match (value).** The result of the `match` statement: the first place a pattern matched, with its captures. It prints as `<match "text">`. Declared with type `match`.

**module.** A `.cuff` file loaded with `use name from folder`. See [Modules](../guide/modules.md).

**native function.** A function implemented in C++ rather than in CuffScript: the builtins and the library functions.

**or_else.** Attaches a handler to a statement or a block that may fail. See [errors](language.md#8-errors-and-or_else).

**pattern.** A string read as a description of text by shape: `"[up]2-[num]4"`. See [pattern syntax](patterns.md).

**position.** A place in a list or string. Positions start at 1, and a negative position counts from the end. `0` is an error.

**pure function.** A function declared `set pure func` that may not use a global variable or call a function of the program that is not itself pure.

**quantifier.** The part of a pattern that says how many times the item before it repeats: `4`, `2~5`, `+`, `*`, `?`.

**REPL / shell.** `cuffsh`, the interactive shell: it reads an entry, runs it in one long-lived session and prints the result. See [`cuffsh`](cuffsh.md).

**returnable function.** A function declared `set returnable func`, which can give a value back with `return value`.

**sandbox.** The folder that modules and `DLC:filesystem` may reach: the script's own folder, or the `--root` the host chose. See [Modules](../guide/modules.md#the-sandbox).

**session.** A running interpreter that keeps its variables and functions between pieces of a program. `cuff::Session` in C++; `cuffsh` uses one. See [C++ API](cpp-api.md#cuffsession).

**slice.** A part of a list or string taken with `x[start~end]`, both ends included.

**step.** What `--max-steps` counts: loop iterations plus calls of the program's own functions. Library functions are not counted.

**token.** The smallest piece of a program the tokenizer produces: a word, a number, a string, an operator. In a pattern, a *token* is a word in brackets such as `[num]`.

**type.** One of `number`, `str`, `boolean`, `list`, `map`, `match` or `empty`. `set` needs the type of the variable it declares.
