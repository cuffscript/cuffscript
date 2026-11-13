# Pattern syntax

The full definition of CuffScript's pattern language: the tokens, quantifiers, groups, anchors and flags
that pattern literals (`p"..."`), `matches` and the text functions `match`, `find`, `find_all`, `replace`, `replace_all`, `split` and `count` understand. [Text and
patterns](../guide/text-and-patterns.md) is a gentler introduction; the [quick
reference](#39-token-quick-reference) at the end of this page lists every token on one screen.

---

## 1. Purpose

CuffScript patterns (its regular expressions) keep the text validation and processing power
of JavaScript, but drop the complicated, cryptic special symbols (`\d`, `\w`, `\s`,
`(?<=...)`, `\1`, ...) in favor of intuitive word-style tokens (`[num]`, `[str]`,
...) and natural-language syntax.

The goal is to let programming beginners understand a pattern at a glance and apply
it right away when they write scripts for everyday task automation, web data
collection and data cleanup.

---

## 2. Using patterns: pattern literals and `matches`

A pattern is a value of its own, of the type `pattern`. A **pattern literal** is written `p"..."`, and a
pattern can be tested against a whole text with the comparison operator `matches`:

```cuff
str user_code = "AB-1234"

if user_code matches p"[up]2-[num]4" do:
    print("The code matches the format.")
end
```

```output
The code matches the format.
```

A literal is checked for syntax when the program is read, before anything runs, so a mistake in a pattern is found even in code that never executes. The text of a literal is
raw: a backslash is not the escape of an ordinary string, so the `\.` of the pattern language is written just so (section 20). The only thing
the literal itself reads is `\"`, which stands for a quote.

A pattern that is built while the program runs comes from the function `pattern(text)` (or `pattern(text, flags)`); it can be kept in a variable of the type `pattern`, passed to a function and used wherever a literal can be:

```cuff
str shape = "[num]3"
pattern three_digits = pattern(shape)
print("123" matches three_digits, "12a" matches three_digits)
```

```output
true false
```

A string is never read as a pattern by itself. `"abc" matches "abc"` is an error (`E4-005`), with a hint to write `p"abc"` or `pattern("abc")`. In the same way `is` always compares values, and `"abc" is p"abc"` is false because a `str` and a `pattern` are different types.

Every form that takes a pattern uses the same syntax:

| Form | What it does | Result |
| :--- | :--- | :--- |
| `text matches pattern` | The whole text fits the pattern | `boolean` |
| `match(text, pattern)` | Finds the first place the pattern fits and keeps its captures | a `match`, or `empty` when nothing fits |
| `find(text, pattern)` | Finds the first fit | a `str`, or `empty` |
| `find_all(text, pattern)` | Finds every fit | a `list` of `str` |
| `replace(text, pattern, replacement)` | Replaces the first fit | a `str` |
| `replace_all(text, pattern, replacement)` | Replaces every fit | a `str` |
| `split(text, pattern)` | Cuts the text at every fit | a `list` of `str` |
| `count(text, pattern)` | Counts the non-overlapping fits | a `number` |

The seven functions are always available and take the text first. A text that is not a `str`, or a pattern that is not a `pattern`, is `E4-005`. They can also be written as methods of the text: `text.find(p"[num]+")`.

---

## 3. Case sensitivity (the `i` flag)

A pattern distinguishes upper and lower case English letters unless it is given the `i` flag, written right after the closing quote of the literal.

```cuff
str answer = "Yes"

note: only lowercase yes is expected, so this is false
if answer matches p"yes" do:
    print("lowercase match")
end

note: with the i flag, upper and lower case are the same
if answer matches p"yes"i do:
    print("passes regardless of case")
end
```

```output
passes regardless of case
```

The flags are listed in section 28. To compare two strings for equality ignoring case, compare them after changing the case (`str_lower` in `DLC:string`).

---

## 4. Full match is the default

`matches` checks whether the **entire target string fits from start to end**.

So, unlike regex in other languages, you do not need a start anchor (`^`) or an end
anchor (`$`). The functions that look *inside* a text (`find`, `match`, `replace`, `count`, ...) do not need the whole text to fit.

```cuff
str pin = "1234"

note: it is true only when exactly 4 digits fit the whole string.
if pin matches p"[num]4" do:
    print("Valid PIN")
end
```

```output
Valid PIN
```

- `"1234"` → true (`true`)
- `"123"` → false (`false`)
- `"A123"` → false (`false`)
- `"12345"` → false (`false`)

---

## 5. Basic single-character tokens

Instead of hard-to-memorize escape symbols, intuitive word-style tokens wrapped in
brackets are provided.

| Pattern token | Meaning                    | What it matches                                   |
| :------------ | :------------------------- | :------------------------------------------------ |
| `[num]`       | One digit                  | `0` ~ `9`                                         |
| `[let]`       | One English letter         | `a` ~ `z`, `A` ~ `Z`                              |
| `[low]`       | One lowercase English letter | `a` ~ `z`                                       |
| `[up]`        | One uppercase English letter | `A` ~ `Z`                                       |
| `[sp]`        | One whitespace character   | space, tab (`\t`)                                 |
| `[nl]`        | One line-break character   | `\n` or `\r` (a Windows `\r\n` pair is two characters: use `[nl]2` or `[nl]+`) |
| `[any]`       | Any one character          | every letter and symbol in the world except a line feed (`\n`) |

> **Unicode handling (implementation note)**
> Matching is done per **code point (character)**, not per byte. So `[any]5` matches
> the Hangul string `"안녕하세요"` (15 bytes) as expected, and Hangul written directly
> inside a pattern (`"안녕"`, `[one:사과|배]`) is compared one character at a time.
> Negated sets such as `[!num]` also match Hangul. On the other hand, the tokens in
> the table above and `[str]`/`[word]` below are **English-only**, exactly as
> defined, so `"안녕" matches p"[let]+"` is false — use `[any]` to accept every character,
> Hangul included. For the exact boundaries see item 17 in
> `docs/internals/design-notes.md`.

---

## 6. The `[str]` token (English letters and digits)

`[str]` stands for one character of the most commonly checked kind in programming:
an **English letter or a digit**.

- Matches: `A-Z`, `a-z`, `0-9`
- Special characters (`_`, `-`, `@`, `.`, etc.) are not included.

```cuff
str id = "user2026"

if id matches p"[str]8" do:
    print("8 English letters/digits matched")
end
```

---

## 7. The `[word]` token (identifiers and word characters)

`[word]` means one word-forming character: English letters and digits, plus the
**underscore (`_`)**.

- Matches: `A-Z`, `a-z`, `0-9`, `_`
- Mainly used when `_` should be allowed, such as in variable names and account IDs.

```cuff
str account = "admin_cuff_01"

if account matches p"[word]+" do:
    print("Valid account identifier format.")
end
```

---

## 8. Exact repeat count (`N`)

Putting a positive integer after a token means it **repeats exactly that many
times**. You do not need braces (`{N}`).

```cuff
note: 6 digits (birth date format)
"[num]6"

note: 3 uppercase English letters
"[up]3"

note: 4 English letters/digits
"[str]4"
```

> **When a digit run is a count and when it is text.** A run of digits is a repeat
> count only when it directly follows an atom that consumes characters: a bracket
> token or set (`[num]4`, `[a-z]3`), a `[one:...]` choice, a `(...)` group or a
> `<name:...>` capture. After a plain or escaped literal character, or after a
> zero-width anchor (`[start]`, `[end]`, `[edge]`), digits are ordinary text. So
> `"v2"` matches the text `v2`, `"010-[num]4-[num]4"` matches `010-1234-5678`, and
> `"[start]2026"` matches text that begins with `2026`. The one thing this rules
> out is a literal digit straight after a token, because the digit would be read
> as part of the count: write `[num]4[one:5]` for four digits followed by a `5`.

---

## 9. Minimum-repeat quantifiers (`+`, `*`)

- `+` : the preceding atom must appear **one or more times** (at least once)
- `*` : the preceding atom must appear **zero or more times** (it may be absent or repeated)

```cuff
note: one or more consecutive whitespace characters
"[sp]+"

note: no digits at all, or any number of consecutive digits
"[num]*"
```

---

## 10. Optional quantifier (`?`)

`?` means the preceding atom appears **zero times or once**. (It may be there or not.)

```cuff
note: allow both http and https
if protocol matches p"https?" do:
    print("Web protocol confirmed")
end

note: matches both color (American) and colour (British)
if text matches p"colou?r" do:
    print("Word matched")
end
```

---

## 11. Tilde range quantifier (`~`)

The tilde (`~`), unique to CuffScript, specifies a minimum and a maximum count
intuitively.

- `N~M` : at least N and at most M (inclusive)
- `N~` : at least N (no upper limit)
- `~M` : at most M (from a minimum of 0 up to M)

```cuff
note: a password of 8 to 16 English letters/digits
if password matches p"[str]8~16" do:
    print("Password of a suitable length")
end

note: 2 or more English letters, no upper limit
if code matches p"[let]2~" do:
    print("Passed")
end
```

---

## 12. Greedy and lazy matching

The basic quantifiers (`+`, `*`, `~`) are **greedy**: they swallow as long a string
as possible.

Adding `?` right after a quantifier switches it to **lazy** matching, which matches
as short a string as possible.

```cuff
str html = "<b>one</b> and <b>two</b>"

note: Greedy: swallows everything from the first <b> to the very last </b>
print(find(html, p"\<b\>[any]+\</b\>"))

note: Lazy: the match completes as soon as the nearest </b> is found
print(find(html, p"\<b\>[any]+?\</b\>"))
```

```output
<b>one</b> and <b>two</b>
<b>one</b>
```

`<` and `>` open and close a named capture ([section 23](#23-named-capture-), so a
literal angle bracket must be escaped as `\<` and `\>` ([section 20](#20-escaping-special-symbols-)).

---

## 13. Parenthesis groups (`(...)`)

Use parentheses `()` to bundle several pattern tokens into one, to apply a
quantifier to the bundle or to make a unit of operation.

```cuff
note: the word set "AB" repeated 3 times ("ABABAB")
if code matches p"(AB)3" do:
    print("Repeated code passed")
end

note: one or more linked "2 digits-2 letters" bundles
if token matches p"([num]2-[let]2)+" do:
    print("Composite token matched")
end
```

---

## 14. Word choice token (`[one:...]`)

When the text must match **exactly one** of several candidate words or symbols, use
the structure `[one:candidate1|candidate2|...]`.

The rule that forbids a space before the colon applies.

```cuff
note: file extension check
if ext matches p"[one:jpg|png|gif|webp]" do:
    print("A supported image format.")
end

note: payment method check
if payment matches p"[one:card|cash|point]" do:
    print("Valid payment method")
end
```

---

## 15. User-defined character sets (`[...]`)

To specify a character range or a set of characters yourself, list the characters
inside brackets.

- `[abc]` : one character out of `a`, `b`, `c`
- `[a-z]` : one character in the lowercase English range
- `[0-9]` : one character in the digit range (same as `[num]`)
- `[A-Z]` : one character in the uppercase English range

```cuff
note: the digit that identifies gender in a resident registration number (one of 1, 2, 3, 4)
if gender_code matches p"[1234]" do:
    print("Valid gender identification digit")
end
```

Inside a set every character stands for itself and nothing is escaped, apart from `a-z` style ranges (the start of a range may not come after its end: `[z-a]` is `E3-004`). A few consequences:

- A word that is not one of the named tokens is a set of its letters. `[nope]` is not an error and not a token: it matches one of `n`, `o`, `p` or `e`. A misspelled token such as `[nun]` therefore quietly matches the wrong thing.
- `^` is an ordinary member, not negation: `[^a]` matches `^` or `a`. Negation is `!` ([section 16](#16-negated-character-sets-)).
- A set ends at the first `]`, so `]` itself cannot be a member. Match it outside a set, as `\]`.

---

## 16. Negated character sets (`[!...]`)

If you put an exclamation mark (`!`) as the first character of a bracket character
set, it matches **every character except those**.

- `[!0-9]` : any one character that is not a digit
- `[!a-z]` : any one character that is not a lowercase English letter
- `[!sp]` : any one character that is not whitespace

```cuff
note: one or more characters with no whitespace at all
if input_data matches p"[!sp]+" do:
    print("A valid word with no whitespace")
end
```

---

## 17. Word boundary token (`[edge]`)

This corresponds to JavaScript's `\b` and means the **boundary point** between a
word and a non-word (whitespace, punctuation, the start or end of the text).

Using a word boundary blocks false positives where the text is just a substring of a
longer word.

```cuff
str sentence = "The category of catalog is books"

note: search only for the standalone word 'cat' (category and catalog are excluded)
list result = find_all(sentence, p"[edge]cat[edge]")

print(result) note: an empty result (there is no standalone word cat)
```

---

## 18. Numeric preset tokens (`[int]`, `[float]`, `[hex]`)

Advanced shortcut tokens are provided so that common number formats can be used
right away, without assembling them from regex symbols each time.

| Preset token | Description                              | Example matches         |
| :----------- | :--------------------------------------- | :---------------------- |
| `[int]`      | An optional `+` or `-`, then one or more digits | `100`, `-25`, `+7` |
| `[float]`    | An optional `+` or `-`, one or more digits, a `.`, then one or more digits | `3.14`, `-0.05`, `10.0` |
| `[hex]`      | One hexadecimal character (`0-9`, `a-f`, `A-F`) | `A`, `f`, `9`    |

`[float]` needs digits on both sides of the point and has no exponent, so `10`, `.5`, `5.` and `1e5` are not floats. To accept both whole and decimal numbers, test twice: `value matches p"[int]" or value matches p"[float]"`. A bare sign is never an `[int]`.

```cuff
str coordinate = "-12.54"

if coordinate matches p"[float]" do:
    print("A valid coordinate value.")
end
```

---

## 19. Real-world format preset tokens (`[email]`, `[phone]`, `[url]`)

Validation of the structured data that beginners write most often can be finished
with a single shortcut token.

| Preset token | Description                              | Format matched                          |
| :----------- | :--------------------------------------- | :-------------------------------------- |
| `[email]`    | A standard internet email address        | `account@domain.top-level-domain`       |
| `[phone]`    | South Korean landline/mobile number format | `010-XXXX-XXXX`, `02-XXX-XXXX`, etc.  |
| `[url]`      | Web address protocol format              | an address starting with `http://` or `https://` |

The exact shapes are:

- `[email]`: a local part of one or more English letters, digits, `.`, `_`, `%`, `+` or `-`; an `@`; and a domain of English letters, digits, `.` and `-` whose last dot-separated label is two or more English letters. `a@b.co` and `a.b+c@sub.example.org` match; `a@b.c`, `a@b` and an address with Hangul in it do not.
- `[phone]`: digits and hyphens in one of four shapes, `3-4-4`, `2-3-4`, `2-4-4` and `3-3-4` digits (`010-1234-5678`, `02-123-4567`, `02-1234-5678`, `031-123-4567`). It checks the shape only: the area or carrier prefix is not validated, and a number without hyphens does not match.
- `[url]`: `http://` or `https://` followed by one or more English letters, digits or any of `. - _ / ? = & % # ~ : +`. A space ends the address, so an address that contains a space does not match as a whole.

```cuff
str user_contact = "010-8888-9999"

if user_contact matches p"[phone]" do:
    print("A valid South Korean phone number format.")
end
```

---

## 20. Escaping special symbols (`\`)

To treat a character that has syntactic meaning inside a pattern as just the plain
character itself, put a backslash (`\`) in front of it.

- Escapable symbols: `[`, `]`, `(`, `)`, `{`, `}`, `<`, `>`, `+`, `*`, `?`, `~`, `|`, `.`, `\`, `:`, `!`
- Any other backslash sequence (`\d`, `\w`, `\n`, ...) is `E3-008`, and so is a backslash at the very end of a pattern.

```cuff
note: escaping is recommended when checking for a literal dot (.) character.
if filename matches p"[str]+\.png" do:
    print("A PNG file extension.")
end

note: when checking for the parenthesis characters themselves
if math_exp matches p"\([num]+\)" do:
    print("A number wrapped in parentheses.")
end
```

**Backslashes in source.** A pattern literal is raw, so what you write between the quotes is what the pattern parser reads: `p"\."` is the pattern `\.`, and a single backslash is matched by `p"\\"`. The only thing the literal itself reads is `\"`, a quote inside the pattern. A backslash always takes the character after it, so `p"a\\"` is the pattern `a\\` and the literal ends at the next quote. When a pattern is built from an ordinary string with `pattern(text)`, the string's own escapes apply first, so the same single-backslash pattern is written `pattern("\\\\")`.

```cuff
str path = "C:\\temp"
print(path matches p"C:\\temp", path matches pattern("C:\\\\temp"))
print(find("say \"hi\"", p"\"[let]+\""))
```

```output
true true
"hi"
```

Which symbols have to be escaped to mean themselves:

| Symbol | Needs `\` when you mean the character itself? |
| :--- | :--- |
| `[`, `(`, `<` | Yes. They open a token or set, a group, or a named capture. |
| `]`, `)`, `>` | Yes. An unescaped one with nothing to close is `E3-010`. |
| `~` | Yes, unless it is part of a repeat range (`3~5`, `3~`, `~5`); any other `~` is `E3-009`. |
| `+`, `*`, `?` | Only after something they could repeat. At the start of a pattern or group they are plain characters. |
| `\` | Yes. |
| `\|` | No. Outside `[one:...]` it is an ordinary character: there is no alternation operator, so `"(a\|b)"` matches the text `a\|b`, not `a` or `b`. Use `[one:a\|b]`. |
| `.`, `:`, `!`, `{`, `}` | No. They are ordinary characters; escaping them is allowed. |

> `.` is an ordinary literal character in CuffScript patterns (it does **not** mean
> "any character"; that is `[any]`), so `\.` is accepted but not required. See item 2
> in `docs/internals/design-notes.md`.

---

## 21. Strict colon-spacing rule

Following the language's overriding principle, **a space before the colon is never
allowed**, even in the regex-internal tokens (`[one:...]`, `<name:...>`).

Violating it raises a regex syntax error (`Regex Syntax Error`) immediately, at
parse time, before the script runs.

- **Correct:** `"[one:apple|banana]"`, `"<price:[num]+>"`
- **Wrong:** `"[one :apple|banana]"`, `"<price :[num]+>"`

---

## 22. Extracting 1-based indexed captures (`match`)

To extract the actual value of a part wrapped in parentheses `()`, use the `match`
function.

`match` searches the text for the first place the pattern matches, the way `find` does; it does not
require the whole text to match (pin the pattern with `[start]` and `[end]` for that). Following the
language philosophy, captures are numbered from **1**, in the order of their opening parentheses,
and a negative number counts from the end: `result[-1]` is the last capture.

If nothing matches, `empty` is the result. That is not an error, so an `or_else` block does not
run; test for it with `is empty`. A variable that may hold that result is declared `any`, because `empty` is not a `match` value.

```cuff
str serial = "SN-2026-998"
any result = match(serial, p"SN-([num]4)-([num]+)")

if result is not empty do:
    print(f"Year made: {result[1]}") note: "2026"
    print(f"Serial number: {result[2]}") note: "998"
end
```

A `match` value prints as `<match "text">`, where `text` is the whole text that matched, and `type_of` names it `match`. Asking for `result[0]`, for a position past the last capture, or for a name that was not captured is an error (`E4-007`, `E4-008`, `E4-009`). `length()` and `to_json()` do not accept a `match`; copy the captures you need into a list or map first.

Details of numbering:

- Groups are numbered by where they open: in `(a(b)(c))(d)` capture 1 is `abc`, 2 is `b`, 3 is `c` and 4 is `d`.
- A group that did not take part in the match, such as the `(b)?` of `(a)(b)?` matched against `a`, is captured as an empty string, not as `empty`.
- A group that repeats, such as `(ab)+`, holds its last repetition.

---

## 23. Named capture (`<name:...>`)

Instead of parenthesis numbers, you can give a capture an intuitive name and extract
data by it. The extracted result is looked up by key, map-style.

```cuff
str date_text = "2026-12-25"
any res = match(date_text, p"<year:[num]4>-<month:[num]2>-<day:[num]2>")

if res is not empty do:
    print(f"Year: {res['year']}")  note: "2026"
    print(f"Month: {res['month']}")   note: "12"
    print(f"Day: {res['day']}")     note: "25"
end
```

A name is whatever stands between `<` and `:`; letters, digits and underscores all work (`<my_name:...>`). Named and numbered captures are counted separately: in `<key:[let]>=(v)` the named capture is `key` and the only numbered capture is `1`, so a pattern with only named captures has no `result[1]`. Reusing a name keeps the last value. A key that is not in the result is `E4-009`. A `<` that does not begin a `<name:...>` capture must be escaped as `\<`.

---

## 24. Searching the body and collecting everything (`find` and `find_all`)

To find the pieces of a long body of text that fit a pattern, rather than matching
the whole string, use the `find` and `find_all` functions.

- `find(text, pattern)`: **returns the first text found, as a string**.
- `find_all(text, pattern)`: **returns every matching text as a 1-based list**.
- No match: **`find` returns `empty`**, and `find_all` returns an empty list `[]`.

Matches are found from left to right and never overlap. Because `find` returns the
matched text itself (a `str`), declare its result as `str`; a `match` value comes only from the
`match` function.

```cuff
str article = "Ticket number: T-101, next ticket: T-205, spare ticket: T-309"

note: extract only the first match (returns a string)
str first_ticket = find(article, p"T-[num]3")
print(first_ticket) note: "T-101"

note: collect every match (returns a 1-based list)
list tickets = find_all(article, p"T-[num]3")

print(tickets[1]) note: "T-101"
print(tickets[2]) note: "T-205"
print(tickets[3]) note: "T-309"

note: returns empty when nothing matches
any result = find(article, p"T-[num]5")
if result is empty do:
    print("No matching pattern.")
end
```

---

## 25. Pattern-based string replacement (`replace` and `replace_all`)

To swap the part that matches a pattern for another string, use `replace(text, pattern, replacement)`.

```cuff
str raw_log = "Phone: 010-1234-5678 (personal data)"

note: mask the 4 middle digits (****)
str masked_log = replace(raw_log, p"010-[num]4-", "010-****-")

print(masked_log) note: "Phone: 010-****-5678 (personal data)"
```

`replace` changes only the first match, and a text with no match comes back unchanged. The
replacement is plain text: `[num]`, `\1` or `$1` in it stand for themselves, because there are no
back-references.

To replace every occurrence, use `replace_all`.

```cuff
str clean_text = replace_all("Hello   Cuff    World", p"[sp]+", " ")
print(clean_text) note: "Hello Cuff World"
```

---

## 26. Pattern-based string splitting (`split`)

To cut a string into several pieces using a particular symbol or pattern as the
dividing line, use `split(text, pattern)`.

```cuff
str tag_data = "apple, pear; grape: tangerine"

note: split on a composite separator: a comma, semicolon or colon followed by optional whitespace
list fruit_list = split(tag_data, p"[,;:][sp]*")

loop repeat number i = 1 ~ 4 do:
    print(f"Fruit {i}: {fruit_list[i]}")
end
```

Empty pieces are kept: splitting `",a,,b,"` by `p","` gives `["", "a", "", "b", ""]`. A text with no match comes back as a list of one piece, and so does a split by the empty pattern.

---

## 27. Counting pattern occurrences (`count`)

You can instantly count, as an integer, how many times a particular pattern appears
in a body of text.

When there is no match: **returns `0`**

```cuff
str document = "apple, banana, Apple, orange, APPLE"

note: count the occurrences with case ignored (i)
number apple_count = count(document, p"apple"i)

print(f"Occurrences of the word apple: {apple_count}") note: 3

note: returns 0 when there is no match
number grape_count = count(document, p"grape")
print(f"Occurrences of the word grape: {grape_count}") note: 0
```

Matches do not overlap (`count("aaaa", p"aa")` is `2`). `count` always counts every match. An empty pattern matches between every pair of characters, so `count("abc", p"")` is `4`.

---

## 28. The flags system

A pattern literal can end with flags that tune how it matches, written directly after the closing quote with no space: `p"error:[num]+"i`.

| Flag | Name        | What it does                                                          |
| :--- | :---------- | :-------------------------------------------------------------------- |
| `i`  | Ignore Case | Matches without distinguishing upper and lower case English letters.  |
| `m`  | Multiline   | Applies start and end to each line, using line-break characters.      |

The two can be combined as `im`, and each may appear once. A flag belongs to the pattern, so it applies wherever the pattern is used: with `matches`, `find`, `replace`, and the rest. For a pattern built with `pattern(text, flags)`, `flags` is a string such as `"i"` or `"im"`.

There is no flag for "every match": the functions `find_all` and `replace_all` do that. A letter that is not a flag is a syntax error (`E2-001`) with a hint, and so is a repeated flag.

`i` folds only the English letters `A`-`Z` and `a`-`z`. `m` changes only `[start]` and `[end]`: they then match at the start and the end of every line (a line feed ends a line), not just of the whole text. Two patterns are equal (`is`) only when their text and flags are both the same.

```cuff
str server_logs = "Error:12 ok ERROR:7"
list matches_ = find_all(server_logs, p"error:[num]+"i)
print(matches_)
```

```output
["Error:12", "ERROR:7"]
```

---

## 29. Anchor tokens for partial matching (`[start]`, `[end]`)

When you use the in-text partial search functions such as `find` and `replace`, use
the anchor tokens to pin a position to the very start or very end of the text. (They
correspond to `^` and `$` in traditional regex.)

- `[start]` : the very beginning of the text
- `[end]` : the very end of the text

```cuff
str message = "NOTICE: maintenance is starting."

note: search only for the notice header at the very start
str header = find(message, p"[start]NOTICE:")
print(header)
```

```output
NOTICE:
```

Both anchors are zero-width: they match a position and consume nothing. With the `m` flag they also match at every line break.

---

## 30. Empty patterns and blank checks

- With `matches`, `p""` (the empty pattern) fits only an empty string of length 0. In `find`, `count` and `replace` it matches the empty string between characters (`replace("abc", p"", "-")` is `-abc`).
- Forms with a missing space or empty syntax, such as `[one:]`, are blocked at
  compile time.

```cuff
str empty_box = ""

if empty_box matches p"" do:
    print("Confirmed empty text")
end
```

---

## 31. Combining with the `or_else` safety net

A pattern that does not match is a normal outcome, not an error: `match` and `find` give `empty`, `find_all` gives `[]` and `count` gives `0`, so the way to handle it is an `is empty` check.

```cuff
str text = "no digits here"
any found = match(text, p"([num]+)")

if found is empty do:
    print("no number in the text")
end
```

```output
no number in the text
```

`or_else` is for the errors a pattern can raise at run time: matching that runs past its step, time or recursion budget (`E3-101`, `E3-102`, `E3-103`), a text that is not a `str` (`E4-005`) or a statement that is too large (`E4-026`). The matching budget is the usual way to meet one, with a pattern that backtracks badly:

```cuff
str text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"

boolean ok = text matches p"([any]*)*x" or_else do:
    print("the pattern is too complex for this text")
    ok = false
end
print(ok)
```

```output
the pattern is too complex for this text
false
```

A pattern with a syntax error is different: it is never caught (see the next section).

---

## 32. Regex syntax errors (Regex Syntax Error)

If the syntax inside a pattern is wrong, the engine does not quietly return
`false`; it raises a `Regex Syntax Error`. A pattern literal (`p"..."`) is checked
when the script is parsed, before anything runs. A pattern made with `pattern(text)` can only be checked when the call runs,
so its syntax error appears at run time. Either way `or_else` cannot catch it: a broken pattern is a bug to fix, not a condition to recover from.

**Cases that raise a syntax error:**

- An unclosed parenthesis: `p"(abc"`
- An unclosed bracket: `p"[num"`
- A wrong space before a colon: `p"[one :a|b]"`
- A reversed repeat range: `p"[num]5~2"` (the start is greater than the end)
- Pointless stacked quantifiers: `p"[num]++"`
- A `<` that does not begin a named capture, such as the `<b>` of `p"<b>"`: write `p"\<b\>"`
- A `~` that is not part of a range, such as `p"a~b"`: write `p"a\~b"`
- A closing `)`, `]` or `>` with nothing to close, or an unknown escape such as `p"\d"`

The [error catalogue](errors.md) lists every code with an example.

---

## 33. Execution limits and safety nets (Regex ReDoS defense)

To prevent endless loops (catastrophic backtracking) caused by a beginner's careless
quantifier combinations (such as `([any]+)+`), the engine ships with a maximum match
step count (step limit) and a time limit (timeout).

When the threshold is exceeded, it raises a `Regex Runtime Error` (`E3-101` for steps, `E3-102` for time, `E3-103` for recursion depth) and
stops matching safely, to keep the system from running away. These errors can be caught with `or_else`. The other
limits that apply to patterns (their size, nesting and repeat counts) are on the [limits](limits.md) page.

Each attempt to match from one position may take 200,000 steps of backtracking and half a second, and one call (a `find_all`
over a whole text, say) five seconds plus two for each MiB of the text, up to ten minutes. A run of one repeated
character, such as `[any]*` or `[num]+`, is scanned and spends no steps however long it is; trying the rest of the pattern
from each place the run could end does. The recursion depth is spent by a repeated group, a few levels each time round, which
is why `(ab)+` stops at a few hundred rounds where `[ab]+` goes on to the end of the text
([phenomena](../guide/phenomena.md#a-repeated-character-runs-as-long-as-the-text-a-repeated-group-does-not)).

---

## 34. Practical example 1 — validating sign-up form data

```cuff
str user_id = "cuff_master"
str user_pw = "pass1234!"
str user_name = "Alice"

note: ID: 6 to 12 characters made of English letters/digits/_
if !(user_id matches p"[word]6~12") do:
    print("Invalid ID format")
end

note: password: at least 8 characters with no whitespace (special characters allowed)
if user_pw matches p"[!sp]8~" do:
    print("Valid password format")
end
```

---

## 35. Practical example 2 — validating Korean data (phone and business registration numbers)

```cuff
str mobile = "010-7777-8888"
str biz_no = "123-45-67890"

note: mobile phone number check
if mobile matches p"010-[num]4-[num]4" do:
    print("Valid mobile number")
end

note: business registration number check: 3 digits-2 digits-5 digits
if biz_no matches p"[num]3-[num]2-[num]5" do:
    print("Valid business registration number structure")
end
```

---

## 36. Practical example 3 — parsing server log data

```cuff
str log_line = "2026-03-31 [ERROR] 192.168.0.1 - DB Connection Lost"

any parsed = match(log_line, p"<date:[num]4-[num]2-[num]2> \[[one:INFO|WARN|ERROR]\] <ip:[num]1~3\.[num]1~3\.[num]1~3\.[num]1~3> - <msg:[any]+>")

if parsed is not empty do:
    print(f"Date: {parsed['date']}")
    print(f"Client IP: {parsed['ip']}")
    print(f"Error message: {parsed['msg']}")
end
```

---

## 37. Practical example 4 — masking personal data in a document (replacement)

```cuff
str doc = "The caller's phone number is 010-1111-2222."

note: find the whole mobile number and replace it with a privacy label in one go
str secured_doc = replace(doc, p"010-[num]4-[num]4", "[phone number withheld]")

print(secured_doc) note: "The caller's phone number is [phone number withheld]."
```

---

## 38. Practical example 5 — splitting natural-language text and extracting words

```cuff
str raw_tags = "python, cuff; javascript / kotlin"

note: use commas, semicolons, slashes and the surrounding whitespace together as the split rule
list tags = split(raw_tags, p"[sp]*[,;/][sp]*")

loop repeat number idx = 1 ~ 4 do:
    print(f"Registered tag #{idx}: {tags[idx]}")
end
```

---

## 39. Token quick reference

```text
[Character tokens]
  [num]       : digit (0-9)
  [let]       : English letter (a-z, A-Z)
  [low]       : lowercase English letter (a-z)
  [up]        : uppercase English letter (A-Z)
  [str]       : English letter + digit (a-zA-Z0-9)
  [word]      : English letter + digit + underscore (identifier)
  [sp]        : whitespace character (space, tab)
  [nl]        : line-break character (\n or \r)
  [any]       : any one character (except \n)

[Preset tokens]
  [int]       : integer with sign
  [float]     : decimal number with sign
  [hex]       : hexadecimal character
  [email]     : standard email format
  [phone]     : standard phone number format
  [url]       : web address format
  [edge]      : word boundary
  [start]     : start-of-text anchor
  [end]       : end-of-text anchor

[Quantifiers and optionals]
  N           : exactly N
  +           : one or more
  *           : zero or more
  ?           : zero or one
  N~M         : N or more, M or fewer
  N~          : N or more
  ~M          : M or fewer
  ? (suffix)  : lazy matching mode

[Choice and sets]
  [one:a|b]   : choose one of several words or symbols
  [abc]       : character set
  [!abc]      : negated character set

[Groups and extraction]
  (...)       : capture group (1-based index access)
  <name:...>  : named capture (looked up by key)

[Escapes]
  \ + one of  [ ] ( ) { } < > + * ? ~ | . \ : !   : that character itself
  elsewhere    : < > ] ) and ~ outside a range must be escaped to mean themselves

[Using a pattern]
  p"..."      : pattern literal (checked when the program is read; raw text)
  pattern(t)  : a pattern made from text while the program runs
  matches     : whole-text test (text matches pattern)
  match       : extract pattern captures (first match, empty on failure)
  find        : first part that fits (empty on failure)
  find_all    : every part that fits, as a list
  replace     : replace the first fit
  replace_all : replace every fit
  split       : split text on a pattern (keeps empty pieces)
  count       : count pattern occurrences (0 when none)

[Flags, directly after the closing quote of a literal]
  i           : ignore English letter case
  m           : [start] and [end] match at every line
```
