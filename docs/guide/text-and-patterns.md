# Text and patterns

CuffScript has a small language for describing text by its shape: a pattern. This page builds up the idea with examples, from a first test to capturing parts of a text. The complete
definition, with every token and flag, is the [pattern reference](../reference/patterns.md).

## Does the whole text fit a shape?

When the right-hand side of `is` is a string literal, it is read as a pattern, and the test is true if the **whole** text fits it:

```cuff
set str code to "AB-1234"
if code is "[up]2-[num]4" do:
    print("looks like a code")
end
if "hello" is "hel" do:
    print("never printed: the pattern must fit the whole text")
end
```

```output
looks like a code
```

`[up]` is an uppercase letter and `[num]` a digit; a number after a token says how many. A literal without any special characters means exactly that text, so ordinary comparisons keep working.

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
print("2026-10-24" is "[num]4-[num]2-[num]2")
print("cat" is "[one:cat|dog]", "bird" is "[one:cat|dog]")
print("ab12" is "[low]+[num]+", "AB12" is "[low]+[num]+")
print("a@b.com" is "[email]", "010-1234-5678" is "[phone]")
```

```output
true
true false
true false
true true
```

## Finding parts of a text

```cuff
set str log to "user=ann id=1042 user=ben id=2077"
print(find "[num]4" from log)
print(find "[num]4" from log g)
print(count "user" in log)
print(find "[num]9" from log)
```

```output
1042
["1042", "2077"]
2
empty
```

`find` gives the first match, or `empty` if there is none. Adding the flag `g` gives all of them, as a list (an empty list when nothing matches). `count` gives how many. A match is
text you can use like any other: `find` gives a `str`, so declare its result with `set str`.

`[edge]` stops a word from matching inside a longer one:

```cuff
set str sentence to "The category of catalog is a cat"
print(find "cat" from sentence g)
print(find "[edge]cat[edge]" from sentence g)
```

```output
["cat", "cat", "cat"]
["cat"]
```

## Capturing parts of a match

Parentheses make a group whose text can be read afterwards. `match` searches for the first place the pattern fits and keeps the groups; they are numbered from 1.
A group can also be named with `<name:...>`, and then it is read by name:

```cuff
set match order to match "order SN-2024-0042 shipped" from "SN-([num]4)-([num]+)"
print(order[1], order[2], order[-1])

set match when to match "on 2026-12-25" from "<year:[num]4>-<month:[num]2>-<day:[num]2>"
print(when["day"] + "/" + when["month"] + "/" + when["year"])

set match none to match "no digits" from "([num]+)"
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
set str text to "call 010-1234-5678 now"
print(replace "[num]4-[num]4" in text to "****-****")
print(replace "[sp]+" in "a   b    c" to " " g)
print(split "a, b;c" by "[,;][sp]*")
print(split "2026-10-24" by "-")
```

```output
call 010-****-**** now
a b c
["a", "b", "c"]
["2026", "10", "24"]
```

Without `g`, `replace` changes only the first match. The replacement is plain text; it is never read as a pattern. `split` keeps empty pieces, so `split ",a," by ","` is `["", "a", ""]`.

## Case and other flags

`IS` is `is` with English letters compared without regard to case. The flags `i` (ignore case), `g` (every match) and `m` (`[start]` and `[end]` work on each line) follow `find`, `match`, `count` and
`replace`; `split` takes none.

```cuff
print("Yes" IS "yes", "Yes" is "yes")
print(count "apple" in "Apple, apple, APPLE" i)
set str lines to "first\nsecond"
print(find "[start]second" from lines)
print(find "[start]second" from lines m)
```

```output
true false
3
empty
second
```

A pattern held in a variable is not read as a pattern by `is`: only a string literal on the right is. `find`, `match`, `replace`, `split` and `count` accept a pattern from a variable.

## Characters that mean something

Some characters have a meaning inside a pattern. To match one of them as itself, put a backslash in front.

| Character | Why it needs care | Write |
| :--- | :--- | :--- |
| `<` `>` | They open and close a named capture | `\<` and `\>` for angle brackets, as in HTML |
| `(` `)` | They make a group | `\(` and `\)` |
| `[` `]` | They make a token or a set | `\[` and `\]` |
| `~` | It makes a range | `\~` |
| `\` | It is the escape character | `\\\\` (four, because a string literal also reads backslashes) |
| `.` `\|` `:` `-` `{` `}` | Nothing: they match themselves | as they are |

```cuff
set str html to "<b>one</b> and <b>two</b>"
print(find "\<b\>[any]+?\</b\>" from html)
print("a.b" is "a.b", "axb" is "a.b")
print("(1)" is "\([num]\)")
```

```output
<b>one</b>
true false
true
```

The dot is an ordinary character, and `|` is too: there is no way to write "this or that" with `|`; use `[one:this|that]`.

## Mistakes to watch for

- **A misspelled token is a set of letters.** `[nun]` is not an error: it matches one of `n`, `u`, `n`. Compare with a known good example when a pattern matches strangely.
- **`is` needs the whole text.** To find a shape *inside* text, use `find`, `match` or `count`.
- **`find` gives text, not a `match`.** Use `set str` for `find` and `set match` for `match`.
- **Digits after a token are a count.** `[num]4` is four digits. A digit after a plain character is just that digit (`"v2"` matches `v2`).
- **A pattern with a syntax error stops the program** with an `E3-` code before it runs, when the pattern is written as a string literal.

## Keeping patterns safe

Matching is bounded. A pattern has a size and a nesting limit (see [limits](../reference/limits.md)), and each match has a budget of steps, time and recursion, so a pattern that would take
forever ends with an error ([`E3-101`](../reference/errors.md#e3-101) and its neighbours) that `or_else` can catch.
