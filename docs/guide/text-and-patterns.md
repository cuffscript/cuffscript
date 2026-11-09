# Text and patterns

CuffScript has a small language for describing text by its shape: a pattern. A pattern is written `p"..."` and is a value of its own (the type `pattern`). This page builds up the idea with examples, from a first test to capturing parts of a text. The complete
definition, with every token and flag, is the [pattern reference](../reference/patterns.md).

## Does the whole text fit a shape?

`text matches pattern` is true if the **whole** text fits the pattern:

```cuff
str code = "AB-1234"
if code matches p"[up]2-[num]4" do:
    print("looks like a code")
end
if "hello" matches p"hel" do:
    print("never printed: the pattern must fit the whole text")
end
```

```output
looks like a code
```

`[up]` is an uppercase letter and `[num]` a digit; a number after a token says how many. A pattern without any special characters means exactly that text. A pattern literal is checked when the program is read, so a mistake in it is reported before anything runs. `is` never reads a pattern: it compares values, so use `matches` for a shape.

## The building blocks

| Write | It matches |
| :--- | :--- |
| `[num]` `[let]` `[low]` `[up]` | A digit, an English letter, a lowercase letter, an uppercase letter |
| `[str]` `[word]` | An English letter or digit; the same plus `_` |
| `[sp]` `[nl]` | A space or tab; a line break character |
| `[any]` | Any one character except a line feed |
| `[abc]` `[a-z]` `[!abc]` | One of the listed characters; a range; anything but those |
| `[one:cat\|dog]` | One of the listed words, separated by a bar |
| `[int]` `[float]` `[email]` `[phone]` `[url]` | Common shapes: a whole number, a decimal number, an email address, a Korean phone number, a web address |
| `[edge]` `[start]` `[end]` | A word boundary; the start of the text; the end of the text |
| `(...)` | A group, to repeat or capture as a unit |

A token can be followed by how many times it repeats:

| Write | Repeats |
| :--- | :--- |
| `[num]4` | Exactly 4 times |
| `[num]2~4` | 2 to 4 times; `[num]2~` is 2 or more; `[num]~4` is up to 4 |
| `[num]+`, `[num]*`, `[num]?` | One or more, zero or more, zero or one |
| `[any]+?` | Add `?` after a repeat to make it take as little as it can |

```cuff
print("2026-10-24" matches p"[num]4-[num]2-[num]2")
print("cat" matches p"[one:cat|dog]", "bird" matches p"[one:cat|dog]")
print("ab12" matches p"[low]+[num]+", "AB12" matches p"[low]+[num]+")
print("a@b.com" matches p"[email]", "010-1234-5678" matches p"[phone]")
```

```output
true
true false
true false
true true
```

## Finding parts of a text

```cuff
str log = "user=ann id=1042 user=ben id=2077"
print(find(log, p"[num]4"))
print(find_all(log, p"[num]4"))
print(count(log, p"user"))
print(find(log, p"[num]9"))
```

```output
1042
["1042", "2077"]
2
empty
```

`find` gives the first match, or `empty` if there is none. `find_all` gives all of them, as a list (an empty list when nothing matches). `count` gives how many. A match is
text you can use like any other: `find` gives a `str` when it finds something, so a variable that may hold a failed result is declared `any`: `empty` is not a `str`.

`[edge]` stops a word from matching inside a longer one:

```cuff
str sentence = "The category of catalog is a cat"
print(find_all(sentence, p"cat"))
print(find_all(sentence, p"[edge]cat[edge]"))
```

```output
["cat", "cat", "cat"]
["cat"]
```

## Capturing parts of a match

Parentheses make a group whose text can be read afterwards. `match` searches for the first place the pattern fits and keeps the groups; they are numbered from 1.
A group can also be named with `<name:...>`, and then it is read by name:

```cuff
match order = match("order SN-2024-0042 shipped", p"SN-([num]4)-([num]+)")
print(order[1], order[2], order[-1])

match when = match("on 2026-12-25", p"<year:[num]4>-<month:[num]2>-<day:[num]2>")
print(when["day"] + "/" + when["month"] + "/" + when["year"])

any none = match("no digits", p"([num]+)")
if none is empty do:
    print("no match is empty, not an error")
end
```

```output
2024 0042 0042
25/12/2026
no match is empty, not an error
```

A `match` that found nothing is `empty`; ask with `is empty`. It is not an error, so `or_else` does not run for it.

## Replacing and splitting

```cuff
str text = "call 010-1234-5678 now"
print(replace(text, p"[num]4-[num]4", "****-****"))
print(replace_all("a   b    c", p"[sp]+", " "))
print(split("a, b;c", p"[,;][sp]*"))
print(split("2026-10-24", p"-"))
```

```output
call 010-****-**** now
a b c
["a", "b", "c"]
["2026", "10", "24"]
```

`replace` changes only the first match and `replace_all` changes every one. The replacement is plain text; it is never read as a pattern. `split` keeps empty pieces, so `split(",a,", p",")` is `["", "a", ""]`.

## Case and other flags

Two flags can follow the closing quote of a pattern: `i` (ignore case, for English letters) and `m` (`[start]` and `[end]` work on each line). They belong to the pattern, so they work with every function that takes it. There is no flag for "every match": that is what `find_all` and `replace_all` are for.

```cuff
print("Yes" matches p"yes"i, "Yes" matches p"yes")
print(count("Apple, apple, APPLE", p"apple"i))
str lines = "first\nsecond"
print(find(lines, p"[start]second"))
print(find(lines, p"[start]second"m))
```

```output
true false
3
empty
second
```

A pattern can be kept in a variable of the type `pattern`, and built while the program runs with `pattern(text)`. A plain string is never read as a pattern, so `find(log, "user")` is an error with a hint to write `p"user"`.

```cuff
pattern id_shape = pattern("[num]4")
print(find("id 1042", id_shape))
```

```output
1042
```

## Characters that mean something

Some characters have a meaning inside a pattern. To match one of them as itself, put a backslash in front.

| Character | Why it needs care | Write |
| :--- | :--- | :--- |
| `<` `>` | They open and close a named capture | `\<` and `\>` for angle brackets, as in HTML |
| `(` `)` | They make a group | `\(` and `\)` |
| `[` `]` | They make a token or a set | `\[` and `\]` |
| `~` | It makes a range | `\~` |
| `\` | It is the escape character | `\\` (two; a pattern literal is raw, so what you write is what the pattern reads) |
| `.` `\|` `:` `-` `{` `}` | Nothing: they match themselves | as they are |

```cuff
str html = "<b>one</b> and <b>two</b>"
print(find(html, p"\<b\>[any]+?\</b\>"))
print("a.b" matches p"a.b", "axb" matches p"a.b")
print("(1)" matches p"\([num]\)")
```

```output
<b>one</b>
true false
true
```

The dot is an ordinary character, and `|` is too: there is no way to write "this or that" with `|`; use `[one:this|that]`.

## Mistakes to watch for

- **A misspelled token is a set of letters.** `[nun]` is not an error: it matches one of `n`, `u`, `n`. Compare with a known good example when a pattern matches strangely.
- **`matches` needs the whole text.** To find a shape *inside* text, use `find`, `match` or `count`.
- **`find` gives text, not a `match`.** Declare a result that is known to exist as `str` for `find` and `match` for `match`, and one that may be missing as `any`, because `empty` fits neither type.
- **Digits after a token are a count.** `[num]4` is four digits. A digit after a plain character is just that digit (`"v2"` matches `v2`).
- **A pattern with a syntax error stops the program** with an `E3-` code before it runs, when the pattern is written as a `p"..."` literal. One made with `pattern(text)` stops the program when that call runs.

## Keeping patterns safe

Matching is bounded. A pattern has a size and a nesting limit (see [limits](../reference/limits.md)), and each match has a budget of steps, time and recursion, so a pattern that would take
forever ends with an error ([`E3-101`](../reference/errors.md#e3-101) and its neighbours) that `or_else` can catch.
