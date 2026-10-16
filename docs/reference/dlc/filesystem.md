# `DLC:filesystem`

```cuff
use DLC:filesystem
```

Reads and writes files inside the folder of the running script, or inside the host's configured root
(`--root`). The boundary is the same one `use ... from` has.

| Function | Result |
| :--- | :--- |
| `file_exist(path)` | Whether the regular file exists. |
| `file_size(path)` | Its size in bytes, or `empty` on failure. |
| `file_read(path)` | Its text, or `empty` on failure. |
| `file_readlines(path)` | Its lines as a list, or `empty` on failure. |
| `file_write(path, text)` | Overwrites the file, or creates it. Gives `true` on success. |
| `file_add(path, text)` | Appends text, creating the file if needed. Gives `true` on success. |
| `file_remove(path)` | Deletes a regular file and gives whether it succeeded. A directory, even an empty one, is never removed. |

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

- A path that leaves the allowed folder (an absolute path, `../`) is refused with `E5-008`, before anything is
  touched. Symbolic links that point outside it are refused too.
- Ordinary failures, such as a missing file or no permission, are **not** errors: the function returns `empty` or
  `false` as the table says. Use `file_exist` first when the difference matters.
- File names may be written in any language; they are read as UTF-8 on every platform.
- A host can switch the library off (`--no-filesystem`), after which `use DLC:filesystem` is `E5-005`.

What the library can reach, and what it deliberately does not protect against, is described in the
[security policy](../../../SECURITY.md).
