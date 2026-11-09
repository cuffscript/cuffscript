# Built-in functions

These functions are always available. No `use` is needed.

| Function | Result |
| :--- | :--- |
| `print(a, b, ...)` | Writes its arguments to standard output separated by single spaces, then a line feed. A `str` is written as it is and any other value in its [display form](language.md#2-types-and-values). With no arguments it writes an empty line. It gives `empty`. |
| `input([prompt])` | Writes the optional prompt (a `str`), then reads one line from standard input and returns it as a `str` without its line ending (a line feed, or a carriage return and a line feed). At the end of the input it returns the empty string. See [printing and input](language.md#54-printing-and-input). |
| `type_of(x)` | The type name: `"number"`, `"str"`, `"boolean"`, `"list"`, `"map"`, `"match"`, `"pattern"` or `"empty"`; for an object, the name of its class. |
| `to_number(x)` | A number is returned as it is, a boolean becomes `1` or `0`, and a string is read as plain decimal text (`E4-025` if it is not one). Any other type is `E4-005`. The accepted text is listed on the [`DLC:convert`](dlc/convert.md#what-to_number-accepts) page. |
| `to_str(x)` | The display form of any value: the text `print` would write. |
| `to_boolean(x)` | The truthiness of any value: `false`, `0`, `empty` and an empty string, list or map are false; everything else is true. |
| `copy(x)` | A new list, map or object with the same elements or fields. The copy is shallow: a list or object inside it is shared with the original. Any other value is `E4-005`. |
| `pattern(text)`, `pattern(text, flags)` | A `pattern` made from a string while the program runs. `flags` is a string made of `i` (ignore case) and `m` (lines). A syntax error in the text is an `E3-0xx` error. For a pattern known in advance, write the literal `p"..."` instead. |

## Text functions

Seven functions find and change text by shape. The text is the first argument and a [pattern](patterns.md) the second; a string in the pattern's place is an error (`E4-005`).

| Function | Result |
| :--- | :--- |
| `find(text, pattern)` | The first piece of the text that fits the pattern, as a `str`, or `empty`. |
| `find_all(text, pattern)` | Every piece that fits, as a `list` of `str` (empty when there is none). |
| `match(text, pattern)` | A `match` value for the first fit, with its captures, or `empty`. |
| `replace(text, pattern, with)` | The text with the first fit replaced by the string `with`. |
| `replace_all(text, pattern, with)` | The text with every fit replaced. |
| `split(text, pattern)` | The pieces of the text between the fits, as a `list`; empty pieces are kept. |
| `count(text, pattern)` | How many times the pattern fits, as a `number`. |

The details, including how matches are found and what a `match` holds, are in the [pattern reference](patterns.md).

`to_number`, `to_str` and `to_boolean` are also listed under [`DLC:convert`](dlc/convert.md); both ways of reaching them are
the same functions.

```cuff
print(type_of(1), type_of("a"), type_of([1]), type_of({}), type_of(empty), type_of(p"a"))
print(to_number("42") + 1, to_str(3) + "!")
print(find("room 101", p"[num]+"), count("banana", p"a"))
```

```output
number str list map empty pattern
43 3!
101 3
```

Most of these can also be written as methods of the value they work on: `"room 101".find(p"[num]+")`, `scores.copy()`. See [method calls on built-in values](language.md#104-method-calls-on-built-in-values).

## What `print` writes

```cuff
print("a", 1, true, empty)
print([1, "two", [3]], {"k": "v"})
print()
print("done")
```

```output
a 1 true empty
[1, "two", [3]] {"k": "v"}

done
```

Strings inside a list or a map are shown with quotes; the string on its own is not. Use `to_str` when you need the display text as a value, for
example to join it to other text: `"total: " + to_str(42)`.

## Not built in

`length`, `contains` and `index_of` are not built in; they come with the [string](dlc/string.md), [list](dlc/list.md) and [map](dlc/map.md)
libraries. Without a `use DLC:...` line that loads them, calling one is `E4-002`. Between them they cover strings, lists and maps, with one gap: `index_of` has no meaning for a
map and is not available from `DLC:map` alone.

## Defining a function with a built-in's name

A function that your program declares with the same name as a built-in or a library function replaces it for the whole program:

```cuff
func print_twice(str text) do:
    print(text)
    print(text)
end
print_twice("hi")
```

```output
hi
hi
```

Declaring `func print(...)` yourself works the same way, which is rarely what you want; choose a different name unless that is the point. This also holds for the names of the text functions: a variable called `count` or `find` is fine, since variables and functions have separate name spaces.
