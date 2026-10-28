# Modules

A program can be split into files. `use name from ./folder` loads `./folder/name.cuff` and runs it once; what it
declares becomes available to the file that loaded it.

```text
project/
    main.cuff
    lib/
        ratio.cuff
```

`lib/ratio.cuff`:

```cuff
set returnable func ratio(a, b) do:
    return a / b
end
```

`main.cuff`:

```cuff
use ratio from ./lib
print(ratio(10, 4))
```

Running `cuffc main.cuff` prints `2.5`.

The same thing in one runnable piece, which writes the module file first (see [`DLC:filesystem`](../reference/dlc/filesystem.md)) and loads it from the folder of
the script, written as `.`:

```cuff
use DLC:filesystem
file_write("greeter.cuff", "set returnable func greet(name) do:\n    return \"Hello, \" + name + \"!\"\nend\nset number visits to 0\n")

use greeter from .
print(greet("Ann"), visits)
```

```output
Hello, Ann! 0
```

## Writing the path

`use [name] from [folder]` has a bare module name, the word `from`, and the folder that holds the file.

- The module name is the file name without `.cuff`: `use ratio from ./lib` loads `ratio.cuff`. Writing the extension (`use ratio.cuff from ...`) is a syntax error.
- The folder is written with forward slashes on every platform; a backslash is a syntax error. A trailing slash is fine, and the folder `.` is the script's own folder.
- The folder is resolved against the folder of the **running script**, wherever the `use` appears: a module that loads another module writes the path as if it were in the
  script's own folder, not its own. `./` at the front is the usual way to write it, but `use ratio from lib` means the same as `use ratio from ./lib`.
- The path may contain `..` as long as the place it ends up in is still inside the allowed area (see the sandbox below).
- A `use` statement stays on one line.

## What loading does

- A module's code runs once, at the first `use` that loads it. Later `use` statements for it, including circular ones (two modules that load each other), do nothing and are not an error:

  ```cuff
  use DLC:filesystem
  file_write("counter.cuff", "print(\"loading counter\")\nset number start to 10\n")
  use counter from .
  use counter from .
  print(start)
  ```

  ```output
  loading counter
  10
  ```

- A module's functions are available to the whole program, so a module can load another module and use its functions directly:

  ```cuff
  use DLC:filesystem
  file_write("a.cuff", "set returnable func fa() do:\n    return 1\nend\n")
  file_write("b.cuff", "use a from .\nset returnable func fb() do:\n    return fa() + 1\nend\n")
  use b from .
  print(fb())
  ```

  ```output
  2
  ```

- A module's top-level variables are copied into the scope that wrote `use`, after the module has run, so `set number secret_a to 1` in a module is visible as `secret_a` in the file that
  loaded it. A function of the module that wants to read such a variable has to bridge it first, like any function (`change secret_a to global`); the bridge finds the copy in the file that loaded the module.
- Modules may load modules; the nesting is limited (see [limits](../reference/limits.md)).
- The libraries a module loads with `use DLC:...` stay available to the whole program, because a library is loaded into the program, not into one file.

## The sandbox

By default a module may be loaded only from the folder of the script, or from below it. An absolute path, or a path that climbs out of that folder, is refused with
[`E5-006`](../reference/errors.md#e5-006) before any file is touched. `--root <dir>` (or `CuffEngine::Options::rootDir`) widens the allowed area to everything under `<dir>`:

```sh
cuffc --root /srv/shared app/main.cuff
```

The same root also confines [`DLC:filesystem`](../reference/dlc/filesystem.md), so widening one widens both. Symbolic links that lead out of the root are refused. When a script comes from
standard input, or in the interactive shell, the current directory takes the place of the script's folder.

## When a module fails

| Problem | Error |
| :--- | :--- |
| The file does not exist | [`E5-001`](../reference/errors.md#e5-001) |
| The file does not parse | [`E5-002`](../reference/errors.md#e5-002) |
| The path leaves the allowed folder, or is absolute | [`E5-006`](../reference/errors.md#e5-006) |
| Modules nested too deeply, or a file larger than the source limit | [`E5-007`](../reference/errors.md#e5-007) |

An error inside a module is reported against that module's file and line; see
[errors inside an imported file](errors-and-debugging.md#errors-inside-an-imported-file). These errors can be caught with `or_else`, like any run-time error.
