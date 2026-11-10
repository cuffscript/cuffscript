# `DLC:filesystem`

```cuff
use DLC:filesystem
```

Reads, writes, copies and moves files, and lists, makes and removes folders, inside the folder of the running script, or
inside the host's configured root (`--root`). The boundary is the same one `use ... from` has. In the interactive shell the folder is the one you started it in.

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
| `file_isdir(path)` | Whether the folder exists. A file gives `false`. |
| `file_list(path)` | The names of what is in the folder, as a list of strings in byte order, or `empty` when the path is not a folder or cannot be read. A name is not a path: join it with the folder (`"reports/" + name`) to use it. Files and folders are mixed, and `file_isdir` tells them apart. An empty folder is `[]`. |
| `file_makedir(path)` | Makes the folder and the missing folders on the way to it. `true` when the folder exists afterwards, also when it was already there; `false` otherwise, for example when a file is in the way. |
| `file_removedir(path)` | Removes an empty folder. `false` for a folder with anything in it, for a file, for a path that does not exist, and for the allowed folder itself. |
| `file_copy(from, to)` | Copies a regular file. It never replaces anything: `false` when `to` already exists, when `from` is not a regular file, and when the folder of `to` does not exist. |
| `file_move(from, to)` | Renames or moves a file or a folder within the allowed folder. It never replaces anything: `false` when `to` exists or `from` does not, and the allowed folder itself cannot be moved. A move to another disk can fail and gives `false`. |

The paths and the text must be strings (`E4-005` otherwise), and the number of arguments must match (`E4-010`).

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
- `file_write("reports/today.txt", ...)` gives `false` when `reports` does not exist; make the folder first with `file_makedir`.
- Nothing is replaced or deleted by surprise: `file_copy` and `file_move` stop at an existing destination, `file_removedir` takes only an
  empty folder, and no function removes a folder with something in it. Clearing a folder is a loop over `file_list` that removes each file.
- File names may be written in any language; they are read as UTF-8 on every platform.

```cuff
use DLC:filesystem
boolean saved = file_write("missing_folder/out.txt", "x")
print(saved)
any text = file_read("not_there.txt")
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

## Folders

```cuff
use DLC:filesystem
file_makedir("export/old")
file_write("export/a.txt", "A")
file_write("export/b.txt", "B")
print(file_move("export/a.txt", "export/old/a.txt"))
print(file_copy("export/b.txt", "export/old/b.txt"), file_copy("export/b.txt", "export/old/b.txt"))
print(file_list("export"))
print(file_list("export/old"))
loop each str name in file_list("export") do:
    if file_isdir("export/" + name) do:
        print(name, "is a folder")
    end
end
file_remove("export/old/a.txt")
file_remove("export/old/b.txt")
file_remove("export/b.txt")
print(file_removedir("export"), file_removedir("export/old"), file_removedir("export"))
```

```output
true
true false
["b.txt", "old"]
["a.txt", "b.txt"]
old is a folder
false true true
```

## Reading a file line by line

```cuff
use DLC:filesystem
use DLC:string
file_write("scores.txt", "ana 31\nben 27\n")
list lines = file_readlines("scores.txt")
loop repeat number i = 1 ~ length(lines) do:
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
