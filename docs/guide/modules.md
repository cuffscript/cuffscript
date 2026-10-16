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

## The rules

- A path starts with `./` or `../` and is resolved against the folder of the **running script**, wherever the `use`
  appears: a module that loads another module writes the path as if it were in the script's own folder.
- A `use` statement stays on one line.
- A module's code runs once, at the first `use` that loads it. Later `use` statements for it, including circular
  ones (two modules that load each other), do nothing and are not an error.
- A module's functions are available to the whole program. Its top-level variables are copied into the scope that
  wrote `use`, after the module has run, so `set number secret_a to 1` in a module is visible as `secret_a` in
  the file that loaded it.
- Modules may load modules; the nesting is limited (see [limits](../reference/limits.md)).

## The sandbox

By default a module may be loaded only from the folder of the script, or from below it. An absolute path, or a
path that climbs out of that folder, is refused with [`E5-006`](../reference/errors.md#e5-006) before any file is
touched. `--root <dir>` (or `CuffEngine::Options::rootDir`) widens the allowed area to everything under `<dir>`:

```sh
cuffc --root /srv/shared app/main.cuff
```

The same root also confines [`DLC:filesystem`](../reference/dlc/filesystem.md), so widening one widens both.
Symbolic links that lead out of the root are refused. When a script comes from standard input, the current
directory takes the place of the script's folder.

## When a module fails

| Problem | Error |
| :--- | :--- |
| The file does not exist | [`E5-001`](../reference/errors.md#e5-001) |
| The file does not parse | [`E5-002`](../reference/errors.md#e5-002) |
| The path leaves the allowed folder | [`E5-006`](../reference/errors.md#e5-006) |
| Modules nested too deeply, or a file that is too large | [`E5-007`](../reference/errors.md#e5-007) |

An error inside a module is reported against that module's file and line; see
[errors inside an imported file](errors-and-debugging.md#errors-inside-an-imported-file).
