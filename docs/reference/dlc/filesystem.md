# `DLC:filesystem`

```cuff
use DLC:filesystem
```

Reads and writes files inside the folder of the running script, or inside the host's configured root (`--root`). The
boundary is the same one `use ... from` has. In the interactive shell the folder is the one you started it in.

## Functions

| Function | Result |
| :--- | :--- |
| `file_exist(path)` | Whether the regular file exists. A folder gives `false`. |
| `file_size(path)` | Its size in bytes, or `empty` on failure (including a folder). |
| `file_read(path)` | Its text, or `empty` on failure. The bytes are returned exactly as stored, so a `\r\n` line ending stays two characters. |
| `file_readlines(path)` | Its lines as a list, or `empty` on failure. Lines end at a line feed, and a carriage return before it is dropped, so Windows and Unix files give the same list. A last line without an ending is included; a file that ends with a line ending does not get an extra empty line; an empty file is `[]`. Blank lines in the middle are kept as `""`. |
| `file_write(path, text)` | Overwrites the file, or creates it. Gives `true` on success, `false` when the file could not be written (for example when its folder does not exist). |
| `file_add(path, text)` | Appends text, creating the file if needed. Gives `true` on success, `false` otherwise. |
| `file_remove(path)` | Deletes a regular file and gives whether it succeeded. A folder, even an empty one, is never removed, and a file that does not exist gives `false`. |

The path and the text must be strings (`E4-005` otherwise), and the number of arguments must match (`E4-010`).

```cuff
use DLC:filesystem
print(file_write("notes.txt", "one\ntwo\n"))
print(file_add("notes.txt", "three\n"))
print(file_readlines("notes.txt"))
print(file_exist("notes.txt"), file_size("notes.txt"))
print(file_remove("notes.txt"), file_exist("notes.txt"))
```

```output
true
true
["one", "two", "three"]
true 14
true false
```

## What counts as a failure

- A path that leaves the allowed folder (an absolute path, or one that climbs out with `../`) is refused with `E5-008`, before anything is
  touched. Symbolic links that point outside it are refused too. This error is never reported as `false` or `empty`, because it is
  a mistake in the script, not a condition of the world.
- Ordinary failures, such as a missing file or no permission, are **not** errors: the function returns `empty` or
  `false` as the table says. Use `file_exist` first when the difference matters.
- A file larger than the [string limit](../limits.md) cannot be read into a string, and a file with more lines than the
  [collection limit](../limits.md) cannot be read into a list (`E4-026`).
- Folders are not created: `file_write("reports/today.txt", ...)` gives `false` when `reports` does not exist. The library has no way to
  list, create or remove folders.
- File names may be written in any language; they are read as UTF-8 on every platform.

```cuff
use DLC:filesystem
set boolean saved to file_write("missing_folder/out.txt", "x")
print(saved)
set str text to file_read("not_there.txt")
if text is empty do:
    print("could not read the file")
end
```

```output
false
could not read the file
```

```cuff error E5-008
use DLC:filesystem
print(file_read("../secrets.txt"))
```

## Reading a file line by line

```cuff
use DLC:filesystem
use DLC:string
file_write("scores.txt", "ana 31\nben 27\n")
set list lines to file_readlines("scores.txt")
loop repeat i to 1 ~ length(lines) do:
    print(i, str_upper(lines[i]))
end
file_remove("scores.txt")
```

```output
1 ANA 31
2 BEN 27
```

## When the library is switched off

A host can switch the library off with `--no-filesystem`; every call then raises `E5-005`, as described for
[all libraries](README.md#libraries-that-reach-outside-the-program).

What the library can reach, and what it deliberately does not protect against, is described in the
[security policy](../../../SECURITY.md).
