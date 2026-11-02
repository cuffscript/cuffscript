# Frequently asked questions

Short answers to the questions people ask first. Each answer links to the page that explains it fully. Every example that has its output shown under it is run by the documentation checks.

## Getting started

**What do I need to run CuffScript?** A C++17 compiler and `make`; there is nothing else to install, because the engine is a set of headers. See [Installation](installation.md).

**What is the file extension?** `.cuff`. A program is plain UTF-8 text, and `cuffc` reads it from a file or from standard input.

**Is it compiled or interpreted?** Interpreted. The engine reads a program, builds a syntax tree and walks it; there is no separate compile step and no bytecode. See [Architecture](../internals/architecture.md).

**Can I run it in a browser?** Yes: the engine builds to WebAssembly, and the `cuffscript-wasm` npm package ships the result. See [Embedding the engine](embedding.md#webassembly-and-npm).

**Where can I try a line quickly?** In the interactive shell, `cuffsh`. Variables and functions stay defined between entries. See the [`cuffsh` reference](../reference/cuffsh.md).

## The language

**Why `to` and not `=`?** It reads like a sentence, and it keeps `=` free of the two meanings it has in other languages (assigning and comparing). Writing `=` gives an error that says to use `to`.

**How do I compare two values?** With `is` and `is not`. There is no `==` or `!=`. Values of different types are never equal, so `"1" is 1` is false. `is` always compares values, whatever is on its right.

**How do I test whether a text has a certain shape?** With `matches` and a pattern: `code matches p"[up]2-[num]4"`. See [text and patterns](text-and-patterns.md).

**Why is `count is "5"` false when `count` is 5?** A number and a string are different types and are never equal. Compare like with like: `count is 5`, or `to_str(count) is "5"`.

```cuff
set number count to 5
print(count is 5, to_str(count) is "5")
```

```output
true true
```

**Why is `1 < 2 < 3` an error?** Comparisons do not chain. It is read as `(1 < 2) < 3`, and that compares a boolean with a number. Write `1 < 2 and 2 < 3`.

**How do I loop over a list?** With `loop each`. It visits every element, and an empty list simply runs the body no times:

```cuff
set list items to []
loop each item in items do:
    print(item)
end
print("nothing to show")
loop each name in ["tea", "milk"] do:
    print(name)
end
```

```output
nothing to show
tea
milk
```

**Why does a `loop repeat` over an empty list run twice?** A `repeat` range counts down when the start is larger, so `1 ~ length(items)` with an empty list is `1 ~ 0`, which runs twice. Use `loop each` for the elements of a list, and keep `loop repeat` for counting.

**How do I loop over a map?** `loop each` visits its keys, in the order they were added:

```cuff
set map prices to {"tea": 3, "milk": 2}
loop each name in prices do:
    print(name, prices[name])
end
```

```output
tea 3
milk 2
```

**Is there a `continue`?** Yes, it is called `skip`: it ends the current pass of the nearest loop and starts the next one. `stop` ends the loop.

**How do I get the last element?** With a negative position: `items[-1]`. Positions start at 1, and position `0` is an error.

**Why did changing one list change another?** Lists and maps are shared, not copied: `set list b to a` gives `b` the same list. Make a copy with `copy`:

```cuff
set list a to [1, 2]
set list same to a
set list copy to copy(a)
add 3 to a
print(same, copy)
```

```output
[1, 2, 3] [1, 2]
```

**Can I add to a list inside another list?** Yes. `add`, `change` and `remove` take any place, however deep:

```cuff
set list grid to [[1], [2]]
add 5 to grid[1]
change grid[2][1] to 9
print(grid)
```

```output
[[1, 5], [9]]
```

**Can I return several values?** Return a list or a map: `return [low, high]`.

**Can I pass a function to another function?** No. A function is not a value, so there is nothing to store in a variable or hand to a call, and there are no closures. A function can call other functions by name.

**What is `empty`?** The value that means "nothing": the result of `find` when there is no match, of a function that returns nothing, and of `null` in JSON. Test for it with `is empty`.

**How do I read a number from the keyboard?** Read the text and convert it, with a fallback for text that is not a number:

```cuff
set number age to to_number(input("Age? ")) or_else do:
    print("that was not a number")
    change age to 0
end
```

**How do I print a number with two decimals?** Numbers do not keep trailing zeros, so `2.50` prints as `2.5`. Round with `math_round(x, 2)` to limit the digits, and join text yourself if you need a fixed width; `str_pad_right` helps.

## Classes

**When do I need a class?** When several values belong together and several functions work on them, such as a player with a name, a score and things it can do. A map and a few functions work too; a class adds fixed field names (a misspelt one is an error) and a place for the functions. See [Classes and objects](classes-and-objects.md).

**Why does a method say "undefined variable" for a field?** Inside a method a field is always written `self.name`; a bare `name` is a variable of the method. (Inside a [`with` block](../reference/classes.md#the-with-block) the plain name of a member works.)

**Why does reading a field give E4-033?** The class does not declare a field of that name, and the field set of a class is fixed. The message lists the fields the class has; the usual cause is a misspelling.

**Why is my object changed when I changed another one?** Objects are shared, not copied, like lists: `set Player b to a` gives `b` the same object. Use `a.copy()` for a separate one. See [objects are values](../reference/classes.md#objects-are-values).

**Can I pass a method to another function?** No. Methods, like functions, are not values: `player.sayhello` without parentheses is an error. Pass the object and call the method inside.

## Errors

**What does "undefined function 'length'" mean?** `length`, `contains` and `index_of` are library functions. Add `use DLC:string` (or `DLC:list`, or `DLC:map` for the first two) at the top. See [built-in functions](../reference/builtins.md).

**What does "global variable 'x' is not visible inside a function" mean?** A function sees only its own variables. Name the global one first with `global x`. See [scope](../reference/language.md#7-scope).

```cuff
set number total to 0
func add_to_total(amount) do:
    global total
    change total to total + amount
end
add_to_total(5)
add_to_total(7)
print(total)
```

```output
12
```

**What does "'return' with a value can only be used inside a function" mean?** A `return` with a value was written outside any function, at the top level of the program. Put the code in a function, or show the value with `print`.

**Why does `or_else` not run when `match` finds nothing?** Finding nothing is a normal result (`empty`), not an error. Use `is empty`. `or_else` is for errors; see [errors and debugging](errors-and-debugging.md).

**Why does my pattern match the wrong thing?** Two common causes: a misspelled token such as `[nun]` is read as a set of letters instead of being rejected, and `matches` needs the **whole** text to fit (use `find` to look inside). See [text and patterns](text-and-patterns.md#mistakes-to-watch-for).

**Why does `p"<b>"` give a pattern error?** `<` and `>` open and close a named capture. Escape them as `\<` and `\>` to match angle brackets: `p"\<b\>"`.

**An error message points at the wrong line.** The line and column in a report are where the engine gave up, which for a missing `end` is the end of the file; the hint names the line that opened the block. See [reading an error report](errors-and-debugging.md#reading-an-error-report).

## Libraries and the outside world

**How do I read a file?** `use DLC:filesystem`, then `file_read` or `file_readlines`. Files must be inside the script's folder. See [`DLC:filesystem`](../reference/dlc/filesystem.md).

**How do I make an HTTP request?** `use DLC:network`, then `network_get` or `network_post`. It is plain HTTP only: there is no TLS, so `https://` addresses are refused. See [`DLC:network`](../reference/dlc/network.md).

**How do I wait or sleep?** There is no sleep function. `DLC:time` gives the clock (`time_now`) and nothing more.

**Is the random generator suitable for passwords?** No. See [`DLC:random`](../reference/dlc/random.md).

## Running other people's scripts

**Is it safe to run a script I did not write?** The engine is built so that a script can fail but not crash the host, and file and network access are confined or can be switched off. It does **not** stop a script from running forever unless you tell it to. Give `--max-steps` and `--timeout`, switch off what the script does not need with
`--no-network` and `--no-filesystem`, and read [Security](../../SECURITY.md).

**My script hangs.** A loop that never ends. Run it with `--timeout 2000` and it ends with an error that shows where it was.

## The shell

**Shift+Enter does not insert a new line.** Many terminals cannot tell Shift+Enter from Enter. Use **Alt+Enter**. See [Does Shift+Enter work in my terminal?](../reference/cuffsh.md#does-shiftenter-work-in-my-terminal)

**`cuffsh` says it has no console in Git Bash and exits.** Git Bash (mintty) gives Windows programs no console. Run it from cmd, PowerShell or Windows Terminal, or with `winpty`.
