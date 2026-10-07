# CuffScript Regex Specification

> Specification for CuffScript regular expressions and pattern matching
> Version: 2.0

---

## 1. Purpose

CuffScript regular expressions (regex) keep the text validation and processing power
of JavaScript, but drop the complicated, cryptic special symbols (`\d`, `\w`, `\s`,
`(?<=...)`, `\1`, ...) in favor of intuitive word-style tokens (`[num]`, `[str]`,
...) and natural-language syntax.

The goal is to let programming beginners understand a pattern at a glance and apply
it right away when they write scripts for everyday task automation, web data
collection and data cleanup.

---

## 2. Basic usage and combining with conditions (`is`, `IS`)

In CuffScript, pattern validation needs no separate function call: it is done
directly with the comparison operator `is` or `IS`.

```cuff
set str user_code to "AB-1234"

if user_code is "[up]2-[num]4" do:
    print("The code matches the format.")
end
```

When a string literal contains a bracket token (`[num]`, `[str]`, ...) or a
quantifier symbol, the engine recognizes it as a regex pattern automatically.

---

## 3. Case-sensitivity engine (`is` vs `IS`)

- **`is`**: compares strictly, distinguishing upper and lower case English letters.
- **`IS`**: removes the distinction entirely and performs a case-insensitive pattern
  match.

```cuff
set str answer to "Yes"

note: only lowercase yes is expected, so this is false
if answer is "yes" do:
    print("lowercase match")
end

note: IS ignores case, so this is true
if answer IS "yes" do:
    print("passes regardless of case")
end
```

---

## 4. Full match is the default

A condition in CuffScript checks, by default, whether the **entire target string
matches from start to end**.

So, unlike regex in other languages, you do not need a start anchor (`^`) or an end
anchor (`$`).

```cuff
set str pin to "1234"

note: it is true only when exactly 4 digits match the whole string.
if pin is "[num]4" do:
    print("Valid PIN")
end
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
> defined, so `"안녕" is "[let]+"` is false — use `[any]` to accept every character,
> Hangul included. For the exact boundaries see item 17 in
> `docs/IMPLEMENTATION_NOTES.md`.

---

## 6. The `[str]` token (English letters and digits)

`[str]` stands for one character of the most commonly checked kind in programming:
an **English letter or a digit**.

- Matches: `A-Z`, `a-z`, `0-9`
- Special characters (`_`, `-`, `@`, `.`, etc.) are not included.

```cuff
set str id to "user2026"

if id is "[str]8" do:
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
set str account to "admin_cuff_01"

if account is "[word]+" do:
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

> **Digits in a pattern are always repeat counts.** A run of digits right after any
> atom (a token, a group, or even a plain literal character) is read as its repeat
> count, so `"v2"` means `v` twice, and a pattern that starts with literal digits
> such as `"010-[num]4"` means `0` repeated 10 times. To match literal digits
> next to other atoms, wrap them in a one-choice token: `[one:010]-[num]4`,
> `[one:v2]`.

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
if protocol is "https?" do:
    print("Web protocol confirmed")
end

note: matches both color (American) and colour (British)
if text is "colou?r" do:
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
if password is "[str]8~16" do:
    print("Password of a suitable length")
end

note: 2 or more English letters, no upper limit
if code is "[let]2~" do:
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
note: Greedy: swallows everything from the first <tag> to the very last </tag>
"<tag>[any]*</tag>"

note: Lazy: the match completes as soon as the nearest </tag> is found
"<tag>[any]*?</tag>"
```

---

## 13. Parenthesis groups (`(...)`)

Use parentheses `()` to bundle several pattern tokens into one, to apply a
quantifier to the bundle or to make a unit of operation.

```cuff
note: the word set "AB" repeated 3 times ("ABABAB")
if code is "(AB)3" do:
    print("Repeated code passed")
end

note: one or more linked "2 digits-2 letters" bundles
if token is "([num]2-[let]2)+" do:
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
if ext is "[one:jpg|png|gif|webp]" do:
    print("A supported image format.")
end

note: payment method check
if payment is "[one:card|cash|point]" do:
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
if gender_code is "[1234]" do:
    print("Valid gender identification digit")
end
```

---

## 16. Negated character sets (`[!...]`)

If you put an exclamation mark (`!`) as the first character of a bracket character
set, it matches **every character except those**.

- `[!0-9]` : any one character that is not a digit
- `[!a-z]` : any one character that is not a lowercase English letter
- `[!sp]` : any one character that is not whitespace

```cuff
note: one or more characters with no whitespace at all
if input_data is "[!sp]+" do:
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
set str sentence to "The category of catalog is books"

note: search only for the standalone word 'cat' (category and catalog are excluded)
set list result to find "[edge]cat[edge]" from sentence g

print(result) note: an empty result (there is no standalone word cat)
```

---

## 18. Numeric preset tokens (`[int]`, `[float]`, `[hex]`)

Advanced shortcut tokens are provided so that common number formats can be used
right away, without assembling them from regex symbols each time.

| Preset token | Description                              | Example matches         |
| :----------- | :--------------------------------------- | :---------------------- |
| `[int]`      | An integer that may carry a sign         | `100`, `-25`, `+7`      |
| `[float]`    | A decimal number that may carry a sign   | `3.14`, `-0.05`, `10.0` |
| `[hex]`      | One hexadecimal character (`0-9`, `a-f`, `A-F`) | `A`, `f`, `9`    |

```cuff
set str coordinate to "-12.54"

if coordinate is "[float]" do:
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

```cuff
set str user_contact to "010-8888-9999"

if user_contact is "[phone]" do:
    print("A valid South Korean phone number format.")
end
```

---

## 20. Escaping special symbols (`\`)

To treat a character that has syntactic meaning inside a pattern as just the plain
character itself, put a backslash (`\`) in front of it.

- Escapable symbols: `[`, `]`, `(`, `)`, `<`, `>`, `+`, `*`, `?`, `~`, `|`, `.`, `\`, `:`

```cuff
note: escaping is recommended when checking for a literal dot (.) character.
if filename is "[str]+\.png" do:
    print("A PNG file extension.")
end

note: when checking for the parenthesis characters themselves
if math_exp is "\([num]+\)" do:
    print("A number wrapped in parentheses.")
end
```

> `.` is an ordinary literal character in CuffScript patterns (it does **not** mean
> "any character"; that is `[any]`), so `\.` is accepted but not required. See item 2
> in `docs/IMPLEMENTATION_NOTES.md`.

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
statement.

Following the language philosophy, the result's access index **starts at 1**.

If the match fails, `empty` is assigned to the result variable.

```cuff
set str serial to "SN-2026-998"
set match result to match serial from "SN-([num]4)-([num]+)"

if result is not empty do:
    print(f"Year made: {result[1]}") note: "2026"
    print(f"Serial number: {result[2]}") note: "998"
end
```

---

## 23. Named capture (`<name:...>`)

Instead of parenthesis numbers, you can give a capture an intuitive name and extract
data by it. The extracted result is looked up by key, map-style.

```cuff
set str date_text to "2026-12-25"
set match res to match date_text from "<year:[num]4>-<month:[num]2>-<day:[num]2>"

if res is not empty do:
    print(f"Year: {res['year']}")  note: "2026"
    print(f"Month: {res['month']}")   note: "12"
    print(f"Day: {res['day']}")     note: "25"
end
```

---

## 24. Searching the body and collecting everything (`find` & `g`)

To find the pieces of a long body of text that fit a pattern, rather than matching
the whole string, use the `find` command.

- On its own: **returns the first text found, as a string**.
- Combined with the `g` flag: **returns every matching text as a 1-based list**.
- No match: **returns `empty`** (with the `g` flag, an empty list `[]`)

```cuff
set str article to "Ticket number: T-101, next ticket: T-205, spare ticket: T-309"

note: extract only the first match (returns a string)
set str first_ticket to find "T-[num]3" from article
print(first_ticket) note: "T-101"

note: collect every match (g flag, returns a 1-based list)
set list tickets to find "T-[num]3" from article g

print(tickets[1]) note: "T-101"
print(tickets[2]) note: "T-205"
print(tickets[3]) note: "T-309"

note: returns empty when nothing matches
set str result to find "T-[num]5" from article
if result is empty do:
    print("No matching pattern.")
end
```

---

## 25. Pattern-based string replacement (`replace pattern in text to`)

To swap the part that matches a regex pattern for another string, use the
natural-language syntax `replace in to`.

```cuff
set str raw_log to "Phone: 010-1234-5678 (personal data)"

note: mask the 4 middle digits (****)
set str masked_log to replace "[one:010]-[num]4-" in raw_log to "010-****-"

print(masked_log) note: "Phone: 010-****-5678 (personal data)"
```

To replace every occurrence, add the `g` flag at the end of the statement.

```cuff
set str clean_text to replace "[sp]+" in "Hello   Cuff    World" to " " g
print(clean_text) note: "Hello Cuff World"
```

---

## 26. Pattern-based string splitting (`split text by`)

To cut a string into several pieces using a particular symbol or pattern as the
dividing line, use the `split by` syntax.

```cuff
set str tag_data to "apple, pear; grape: tangerine"

note: split on a composite separator: a comma, semicolon or colon followed by optional whitespace
set list fruit_list to split tag_data by "[,;:][sp]*"

loop repeat i to 1 ~ 4 do:
    print(f"Fruit {i}: {fruit_list[i]}")
end
```

---

## 27. Counting pattern occurrences (`count pattern in text`)

You can instantly count, as an integer, how many times a particular pattern appears
in a body of text.

When there is no match: **returns `0`**

```cuff
set str document to "apple, banana, Apple, orange, APPLE"

note: count the occurrences with case ignored (i)
set number apple_count to count "apple" in document i

print(f"Occurrences of the word apple: {apple_count}") note: 3

note: returns 0 when there is no match
set number grape_count to count "grape" in document
print(f"Occurrences of the word grape: {grape_count}") note: 0
```

---

## 28. The flags system

Flags that tune how the search works can be added at the very end of the search
statements (`find`, `match`, `count`, `replace`).

| Flag | Name        | What it does                                                          |
| :--- | :---------- | :-------------------------------------------------------------------- |
| `i`  | Ignore Case | Searches without distinguishing upper and lower case English letters. |
| `g`  | Global      | Walks the whole text instead of stopping at the first match.          |
| `m`  | Multiline   | Applies start and end to each line, using line-break characters.      |

Several flags can be written back to back with no spaces: `gim`

```cuff
set list matches to find "error:[num]+" from server_logs gi
```

---

## 29. Anchor tokens for partial matching (`[start]`, `[end]`)

When you use the in-text partial search statements such as `find` and `replace`, use
the anchor tokens to pin a position to the very start or very end of the text. (They
correspond to `^` and `$` in traditional regex.)

- `[start]` : the very beginning of the text
- `[end]` : the very end of the text

```cuff
set str message to "NOTICE: maintenance is starting."

note: search only for the notice header at the very start
set match alert to find "[start]NOTICE:" from message
```

---

## 30. Empty patterns and blank checks

- `""` (the empty pattern) matches only an empty string of length 0.
- Forms with a missing space or empty syntax, such as `[one:]`, are blocked at
  compile time.

```cuff
set str empty_box to ""

if empty_box is "" do:
    print("Confirmed empty text")
end
```

---

## 31. Combining with the `or_else` safety net

When matching or parsing with a regex fails and produces `empty`, or a runtime error
is expected, you can attach CuffScript's own `or_else do:` block right away.

```cuff
set match user_info to match input_str from "<name:[str]+>-(<id:[str]+>)" or_else do:
    print("Could not parse the format! Falling back to default info.")
    change user_info to {"name": "guest", "id": "0000"}
end
```

---

## 32. Regex syntax errors (Regex Syntax Error)

If the syntax inside a pattern string is wrong, the engine does not quietly return
`false`; it raises a `Regex Syntax Error` immediately, when the script is parsed
(before it runs), to isolate the bug quickly.

**Cases that raise a syntax error:**

- An unclosed parenthesis: `"(abc"`
- An unclosed bracket: `"[num"`
- A wrong space before a colon: `"[one :a|b]"`
- A reversed repeat range: `"[num]5~2"` (the start is greater than the end)
- Pointless stacked quantifiers: `"[num]++"`

---

## 33. Execution limits and safety nets (Regex ReDoS defense)

To prevent endless loops (catastrophic backtracking) caused by a beginner's careless
quantifier combinations (such as `([any]+)+`), the engine ships with a maximum match
step count (step limit) and a time limit (timeout).

When the configured threshold is exceeded, it raises a `Regex Runtime Error` and
stops the engine safely, to keep the system from running away.

---

## 34. Practical example 1 — validating sign-up form data

```cuff
set str user_id to "cuff_master"
set str user_pw to "pass1234!"
set str user_name to "Alice"

note: ID: 6 to 12 characters made of English letters/digits/_
if !(user_id is "[word]6~12") do:
    print("Invalid ID format")
end

note: password: at least 8 characters with no whitespace (special characters allowed)
if user_pw is "[!sp]8~" do:
    print("Valid password format")
end
```

---

## 35. Practical example 2 — validating Korean data (phone and business registration numbers)

```cuff
set str mobile to "010-7777-8888"
set str biz_no to "123-45-67890"

note: mobile phone number check
if mobile is "[one:010]-[num]4-[num]4" do:
    print("Valid mobile number")
end

note: business registration number check: 3 digits-2 digits-5 digits
if biz_no is "[num]3-[num]2-[num]5" do:
    print("Valid business registration number structure")
end
```

---

## 36. Practical example 3 — parsing server log data

```cuff
set str log_line to "2026-03-31 [ERROR] 192.168.0.1 - DB Connection Lost"

set match parsed to match log_line from "<date:[num]4-[num]2-[num]2> \[[one:INFO|WARN|ERROR]\] <ip:[num]1~3\.[num]1~3\.[num]1~3\.[num]1~3> - <msg:[any]+>"

if parsed is not empty do:
    print(f"Date: {parsed['date']}")
    print(f"Client IP: {parsed['ip']}")
    print(f"Error message: {parsed['msg']}")
end
```

---

## 37. Practical example 4 — masking personal data in a document (replacement)

```cuff
set str doc to "The caller's phone number is 010-1111-2222."

note: find the whole mobile number and replace it with a privacy label in one go
set str secured_doc to replace "[one:010]-[num]4-[num]4" in doc to "[phone number withheld]"

print(secured_doc) note: "The caller's phone number is [phone number withheld]."
```

---

## 38. Practical example 5 — splitting natural-language text and extracting words

```cuff
set str raw_tags to "python, cuff; javascript / kotlin"

note: use commas, semicolons, slashes and the surrounding whitespace together as the split rule
set list tags to split raw_tags by "[sp]*[,;/][sp]*"

loop repeat idx to 1 ~ 4 do:
    print(f"Registered tag #{idx}: {tags[idx]}")
end
```

---

## 39. CuffScript regex token quick reference

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

[Command API]
  is / IS     : whole-string match test (IS ignores case)
  match       : extract pattern captures (empty on failure)
  find        : search the body for a part (supports the g flag, empty on failure)
  replace     : pattern-based text replacement
  split       : split text on a pattern
  count       : count pattern occurrences (0 when none)
```
