# `DLC:csv`

```cuff
use DLC:csv
```

Comma-separated text (RFC 4180) to rows and back. Every field read is a `str`: the library never guesses that `007` is a number,
so what a program reads is what the file says, and [`to_number`](../builtins.md) turns a field into a number where one is wanted.
The separator, the last argument of each function, is one character and defaults to `,`; a quote or a line break cannot be one
(`E4-025`). A wrong type is `E4-005`, and the wrong number of arguments is `E4-010`.

## Functions

| Function | Result |
| :--- | :--- |
| `csv_parse(text [, separator])` | A list of rows, each a list of fields. |
| `csv_parse_records(text [, separator])` | A list of maps: the first row names the fields, and every other row maps those names to its fields. A field name given twice, or a row with more or fewer fields than the first, is `E4-025` with the row's number. |
| `csv_write(rows [, separator])` | The rows as text, one line each. |
| `csv_write_records(records [, separator])` | A header row of every key, in the order the keys first appear, then a row for each map; a key that a map does not have is an empty field. |

How text is read:

- A field in double quotes may hold the separator, line breaks and quotes, a quote being written twice (`"say ""hi"""`). After the
  closing quote only a separator or the end of the line may follow (`E4-025`). A quote inside a field that does not begin with
  one is an ordinary character.
- A quoted field that is never closed is `E4-025`, naming the line it begins on.
- Lines may end in LF, CRLF or CR. A blank line is no row, and a UTF-8 byte order mark at the start is skipped.

How rows are written:

- A field is written as it is, or in quotes when it holds the separator, a quote or a line break, or begins or ends with a space.
- A `str` is written as it is, a number as `print` writes it, a boolean as `true` or `false`, and `empty` as an empty field. Any
  other value is `E4-005`.
- Every line, the last one too, ends in a line feed. A row of one empty field is written `""`, so that it does not read back as
  a blank line.

```cuff
use DLC:csv
str text = "name,points\nAnn,12\n\"Bob, Jr.\",7\n"
list records = csv_parse_records(text)
number total = 0
loop each map record in records do:
    total = total + to_number(record["points"])
end
print(records[2]["name"], total)
print(csv_write([["item", "note"], ["tea", "say \"hi\""], [3.5, empty]]))
print(csv_parse("a\tb\n", "\t"))
```

```output
Bob, Jr. 19
item,note
tea,"say ""hi"""
3.5,

[["a", "b"]]
```

The library reads text that is already in memory: read a file with [`file_read`](filesystem.md) first, and write the result with
`file_write`.
