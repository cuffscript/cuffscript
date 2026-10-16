# Text and patterns

CuffScript has a small language for describing text by its shape: a pattern. This page builds up the idea with
examples. The complete definition, with every token and flag, is the [pattern reference](../reference/patterns.md).

## Does the whole text fit a shape?

When the right-hand side of `is` is a string literal, it is read as a pattern, and the test is true if the **whole** text
fits it:

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

`[up]` is an uppercase letter and `[num]` a digit; a number after a token says how many. A literal without any special
characters means exactly that text, so ordinary comparisons keep working.

## Finding parts of a text

```cuff
set str log to "user=ann id=1042 user=ben id=2077"
print(find "[num]4" from log)
print(find "[num]4" from log g)
print(count "user" in log)
```

```output
1042
["1042", "2077"]
2
```

`find` gives the first match, or `empty` if there is none. Adding the flag `g` gives all of them, as a list. `count`
gives how many.

## Capturing, replacing and splitting

```cuff
set str text to "call 010-1234-5678 now"
print(replace "[num]4-[num]4" in text to "****-****")
print(split "a, b;c" by "[,;][sp]*")
set match m to match "SN-2024-0042" from "SN-([num]4)-([num]+)"
if m do:
    print("year and serial matched")
end
```

```output
call 010-****-**** now
["a", "b", "c"]
year and serial matched
```

Parentheses make a group, whose text can be read from the `match` result; the [pattern reference](../reference/patterns.md)
shows how.

## Case and other flags

`IS` is `is` with English letters compared without regard to case, and `i`, `g` and `m` flags can follow `find`,
`replace` and `count`. A pattern held in a variable is not read as a pattern by `is`: only a string literal on the right
is.

## Keeping patterns safe

Matching is bounded. A pattern has a size and a nesting limit (see [limits](../reference/limits.md)), and each match has
a budget of steps, time and recursion, so a pattern that would take forever ends with an error
([`E3-101`](../reference/errors.md#e3-101) and its neighbours) that `or_else` can catch.
