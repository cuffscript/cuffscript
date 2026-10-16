# Getting started

This page takes you from nothing to running your first CuffScript program. It needs about five minutes.

## What you need

- A C++17 compiler: GCC, Clang or, on Windows, MinGW-w64 (the [platforms table](../contributing/building.md#platforms) lists what is tested)
- `make` (`mingw32-make` on Windows)
- Optional: Python 3.8 or later, to run the documentation and shell checks

## Build

On Linux and macOS:

```sh
make
```

On Windows with MinGW-w64:

```powershell
mingw32-make -f Makefile.win
```

The result is `cuffc` (`cuffc.exe` on Windows), the program that runs scripts. The interactive shell, `cuffsh`, is built
separately: `./cli/build.sh` on Linux and macOS, `cli\build.bat` on Windows. [Building](../contributing/building.md)
describes every target and the build options.

## Run a script

```sh
./cuffc examples/01_hello.cuff
```

A script is plain UTF-8 text. `cuffc` reads it from a file, or from standard input when no file is given:

```sh
echo 'print("hello")' | ./cuffc
```

## Your first program

Save this as `greet.cuff`:

```cuff
set str name to "World"

set returnable func greet(person) do:
    return f"Hello, {person}!"
end

print(greet(name))
```

```output
Hello, World!
```

Line by line:

- `set str name to "World"` declares a variable. `set` is followed by a type (`number`, `str`, `boolean`, `list`,
  `map`), the name, the word `to`, and the value. The word is `to`, never `=`.
- `set returnable func greet(person) do:` starts a function that gives a value back. A block ends with `end`, and
  the body is indented.
- `f"Hello, {person}!"` is an f-string: whatever is inside the braces is evaluated and inserted.
- `print(...)` writes its arguments to the screen.

## Try it interactively

```text
$ ./cli/cuffsh
>> set number x to 6
>> print(x * 7)
42
```

Variables and functions stay defined between entries. Press Shift+Enter or Alt+Enter to continue an entry on the
next line. Everything about the shell is in the [`cuffsh` reference](../reference/cuffsh.md).

## When something goes wrong

A program that fails prints a report with the line, a caret under the position, and often a hint:

```text
ERROR: [E4-006] Runtime Error at line 2, column 9: division by zero
    print(1 / 0)
            ^
```

[Reading an error report](errors-and-debugging.md) explains each part and what to do about the common ones.

## Where to go next

- [A tour of the language](language-tour.md) covers everything in the language, with examples you can run.
- [The language reference](../reference/language.md) is the complete definition.
- [`examples/`](../../examples/README.md) holds runnable programs, each with its expected output.
- [Libraries](../reference/dlc/README.md) lists what `use DLC:math` and friends provide.
