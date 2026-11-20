# `DLC:path`

```cuff
use DLC:path
```

File paths as text. Nothing here looks at the disk (that is [`DLC:filesystem`](filesystem.md)), so every function gives the same
answer on every machine, and a path need not exist. A wrong type is `E4-005`, and the wrong number of arguments is `E4-010`.

The separator is `/`, as in the paths a program writes for `use` and for `DLC:filesystem`; a backslash is an ordinary character.
A path is **absolute** when it begins with `/`, or with a drive letter, a colon and `/` (`C:/data`); that beginning is its root.

## Functions

| Function | Result |
| :--- | :--- |
| `path_join(part, ...)` | The parts joined with `/`. A part that is absolute starts the path again; an empty part adds nothing. |
| `path_normalize(path)` | The path with `.` parts and doubled slashes removed and each `..` taking away the part before it. A relative path keeps the `..` it cannot resolve; at a root there is nothing above, so `..` there is dropped. An empty path is `.`. |
| `path_basename(path)` | The last part, without trailing slashes; `""` for a root. |
| `path_dirname(path)` | Everything before the last part: `.` when there is nothing before it, the root for a part just below it. |
| `path_extension(path)` | The last part from its last dot (`.gz` of `a.tar.gz`), or `""`. A dot that only dots come before does not start an extension, so `.bashrc` has none. |
| `path_stem(path)` | The last part without its extension. |
| `path_with_extension(path, extension)` | The path with its extension replaced, or added; `""` removes it. An extension other than `""` starts with a dot and has no `/` (`E4-025`), and the path must end in a name (`E4-025`). |
| `path_split(path)` | The parts as a list, the root first when there is one. |
| `path_is_absolute(path)` | Whether the path begins with a root. |
| `path_relative(from, to)` | The path that leads from the folder `from` to `to`, both normalized first. Both are absolute with the same root, or both relative (`E4-025` otherwise); a `from` that climbs out of where it starts with `..` cannot be followed back (`E4-025`). |

```cuff
use DLC:path
str file = "reports/2026/../2025/./summary.final.txt"
print(path_normalize(file))
print(path_basename(file), path_extension(file), path_stem(file))
print(path_join("data", "raw", "input.csv"), path_with_extension("notes.txt", ".md"))
print(path_relative("/srv/app/logs", "/srv/data/x.db"), path_split("/usr/local"))
```

```output
reports/2025/summary.final.txt
summary.final.txt .txt summary.final
data/raw/input.csv notes.md
../../data/x.db ["/", "usr", "local"]
```
