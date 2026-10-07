# CuffScript (Cuff) Official Specification

### 1\. Declaring and changing (set & change)

- **Description:** Creating data for the first time, or registering a memory slot, always uses the `set [type]` structure. The symbol that stores (assigns) a value here is the natural-language keyword `to`.

- **Syntax:**
    - Variable declaration: `set [type] [variable name] to [value]`
    - Variable change: `change [variable name] to [value]`
- **Example:**

```cuff
set number age to 25
set str name to "Alice"
set empty data to empty  note: use empty to declare that the value is blank

change age to 26
change name to "Bob"
```

    The name position of a variable, function, parameter or `loop repeat` loop
    variable may hold a word that doubles as a syntax keyword elsewhere, such as
    `add`, `count`, `find`, `split`, `replace`, `match`, `in`, `by`, `not` and
    `global` — `set number count to 10` and `set returnable func add(x, y) do: ... end`
    both work as written. However, `match`/`find`/`replace`/`split`/`count` also
    have their own expression syntax (`match [target] from [pattern]`, etc.), so
    when that syntax follows right after, the word is read as that syntax rather
    than as a name.

---

### 2\. Constant declaration rules (set constant)

- **Description:** To declare a constant that cannot be changed once set, put `constant` right after the `set` keyword. A constant's assignment symbol does not allow `=`; only `to` is used.

    To keep constants readable and safe to identify, a constant's name must always be written entirely in uppercase (UPPER_CASE), regardless of how many letters or joined words it has.

    If this naming rule is violated, or a `change` statement that modifies a constant is executed, the interpreter engine blocks the run immediately and raises a runtime error.

- **Syntax:** `set constant [type] [constant name] to [value]`

- **Example:**

```cuff
set constant number X to 10
set constant number PI to 3.14
set constant str API_URL to "https://cufflang.dev"
```

- **List constants (tuples):** `constant` can also be attached to `list` (`set constant list [constant name] to [...]`). A list declared this way is, like a Python tuple, a completely read-only value: every later attempt to change its contents with `add`/`remove`/`change [i]` is blocked immediately by a runtime error (`ConstantReassignment`). This immutability is attached to the list value itself, so it follows the value even when it is assigned to another variable or passed as a function argument (that is, through an alias) — it is not just one variable name that is constant. However, as with a Python tuple, the immutability is **shallow**: the list or map elements inside the tuple can still be changed freely unless they are declared `constant` separately.

```cuff
set constant list PRIMES to [2, 3, 5, 7]
add 11 to PRIMES         note: immediate error -> ConstantReassignment

set list mutable to [1, 2, 3]
set constant list SNAPSHOT to mutable  note: copies the contents of mutable and freezes the copy
add 99 to mutable                      note: the original mutable can still change freely
print(SNAPSHOT)                        note: [1, 2, 3] -- unaffected

set constant list NESTED to [1, [2, 3]]
change NESTED[2][1] to 999             note: allowed -- shallow immutability (the list inside the tuple is separate)

set list copy to PRIMES + []           note: escape hatch: concatenating produces a new mutable list
```

---

### 3\. Colon (:) spacing rule and comments (note:, endnote)

- **Description:** To enforce tidy source files at the language level, every colon (`:`) used outside strings, in control-flow branches, comments and so on, follows the rule **no space before the colon, a space after the colon recommended**.

    If this is violated, the lexer declares a syntax error (Syntax Error) immediately, at the stage where it splits tokens.

    Comments come in a single-line `note:` and a multi-line `note: ~ endnote` structure. Line breaks and indentation inside a multi-line comment block are left entirely to the developer.

- **Syntax:**
    - Single-line comment: `note: [content]`
    - Multi-line comment: `note: [line break] [free multi-line content] [line break] endnote`
    - Statement separator: `[statement] do: [body]` (when the body after `do:` is complete on one line, the statement is treated as a one-line shorthand and does not require indentation.)
- **Example:**

```cuff
note: this is the correct single-line comment form.
if X is 10 do: print("Passed") note: the do: symbol also forbids a space before it and recommends one after.

note:
This is a multi-line comment area.
It is not constrained at all by the indentation or the number of line breaks before it, and can be written freely.
endnote
```

---

### 4\. Comparison and negation operators (is, IS, !)

- **Description:** The equality comparison operators used for condition checks have the same letters, but the internal judging engine is split in two depending on the case form (`is` / `IS`).

    Lowercase `is` is the ordinary equality comparison operator. How it judges, including English case and the data type, follows CuffScript's type rules.

    Uppercase `IS` is a **case-insensitive comparison operator for English strings**. When checking English strings it removes the distinction between upper and lower case entirely and verifies only the alphabetical order of the letters.

    The exclamation mark (`!`) is the globally standard negation operator (NOT) that turns true into false and false into true.

    When parentheses are used, the expression inside them is evaluated first. The evaluation order of compound expressions without parentheses follows CuffScript's operator precedence rules.

- **Syntax:**
    - `[value] is [value]`
    - `[English string] IS [English string]`
    - `![boolean value]`
- **Example:**

```cuff
set str input_text to "Apple"

if input_text is "apple" do: print("The case differs, so this statement does not run.") end
if input_text IS "apple" do: print("Case is ignored, so this statement runs normally.") end

set boolean is_active to false

if !is_active do: print("false is flipped to true, so this statement runs.") end
```

---

### 5\. 1-based indexing and tilde range slicing (~)

- **Description:** Reflecting natural-language intuition, the start index of the sequential data structures in CuffScript (arrays, lists, strings, etc.) is **1, not 0, as the first address**.

    Address 0 is not defined in the CuffScript runtime; if you try to access 0, the virtual machine immediately emits a warning error and stops.

    An index with a minus (`-`) sign counts backward from the end of the data and accesses it in reverse; the first slot from the end is given the address `-1`.

    Slicing, which cuts out part of the data, is done by placing the tilde `~` inside the brackets, and extracts the data between the start and end index numbers **inclusive on both boundaries**.

- **Syntax:**
    - `[collection name][index]`
    - `[collection name][start index~end index]`
- **Example:**

```cuff
set list colors to ["red", "green", "blue", "yellow"]

print(colors[1])  note: the first element, "red", is printed. 0 does not exist.
print(colors[-1]) note: the last address in reverse order, "yellow", is printed.

set list sub_colors to colors[2~3]
print(sub_colors) note: items 2 (green) and 3 (blue) are both included, so ["green", "blue"] is extracted.
```

---

### 6\. Regex and pattern matching (is, IS)

- **Description:** It supports JavaScript-level regex features while eliminating the complicated special symbols (`^`, `$`, `\d`, `\w`, etc.) entirely.

    Patterns are built inside a string literal by combining intuitive bracket tokens (`[num]`, `[str]`, `[let]`, etc.) with quantifiers (`+`, `*`, `?`) and the range symbol (`~`). A condition that contains a pattern verifies a **full match** of the string by default.

    The detailed token list, quantifier rules and advanced features (capture, search, replace, split) of regex are defined in the separate regex specification (`REGEX.md`).

- **Basic token specification:**
    - `[num]` : one digit (0~9)
    - `[let]` : one English letter (a~z, A~Z)
    - `[str]` : one English letter or digit (a~zA~Z0~9)
    - `[up]` : one uppercase English letter (A~Z)
    - `[low]` : one lowercase English letter (a~z)
    - `[sp]` : one whitespace character (space, tab)
    - `[any]` : any one character
- **Quantifiers:**
    - `N` : exactly N
    - `+` : one or more
    - `*` : zero or more
    - `?` : zero or one
    - `N~M` : N or more and M or fewer
- **Choice token:**
    - `[one:apple|banana|orange]` : matches exactly one of the listed words/symbols
- **Example:**

```cuff
note: mobile phone number check (010-4 digits-4 digits pattern)
if phone is "[one:010]-[num]4-[num]4" do: print("Valid number") end

note: starts with English letters or digits, then an @ sign and a domain pattern
if email is "[str]+@[str]2~10" do: print("Looks like an email format") end

note: matches one of the file extensions
if filename is "[str]+[one:.jpg|.png|.gif]" do: print("Supported image format") end
```

Note that a run of digits right after an atom is always read as a repeat count
(`"v2"` means `v` twice), which is why the literal digits `010` above are wrapped in
`[one:010]`. See `REGEX.md`.

---

### 7\. Collection (List, Map) manipulation syntax (add, change, remove)

- **Description:** When working with lists (list), which are ordered bundles of data, and maps (map) of key-value pairs, data input and output is controlled with natural-language syntax instead of method calls.

    To append a single new element to the end of a list use the `add to` command; to modify the value at a particular index slot inside a list, or a key-value in a map, use the `change` command. To remove an element from a collection use the `remove from` command.

    Inside the brackets (`[]`) of a list, use a 1-based index. Inside the brackets (`[]`) of a map, use a string key. Assigning a value to a key that does not exist in the map creates a new key-value pair, initialized from an empty value as in C.

- **Syntax:**
    - Append an element to the end of a list: `add [value to add] to [list name]`
    - Modify a particular list index: `change [list name][index] to [new value]`
    - Add or modify a particular map key: `change [map name]["key"] to [new value]`
    - Destroy and remove a collection element: `remove [index or key or actual value] from [collection name]`
- **Example:**

```cuff
set list inventory to ["sword", "shield"]

add "potion" to inventory
change inventory[1] to "magic_staff"
remove 2 from inventory

set map user_profile to {"name": "Bob"}

change user_profile["level"] to 50
remove "level" from user_profile
```

---

### 8\. Conditionals and the two immediate-execution loops (if, loop, end, stop)

- **Description:** Conditional branching uses an `if`, `else if`, `else` chain, with the `do:` keyword placed right before switching to the execution body.

    A loop (loop) statement is about immediate command execution rather than work that stays resident in a memory variable, so the variable constructor `set` must never be put at the start of it.

    Two forms are provided: `loop repeat`, which iterates over a specified range, and `loop while`, which runs while a condition is true.

    The terminus that closes a control-flow block is marked by the `end` keyword, and for short bodies `end` may be placed on the same line, at the developer's visual preference.

    Block statements such as loops, functions and conditionals require indentation, as in Python. The exception is a one-line shorthand statement whose body and ending are complete on one line, which does not require indentation.

    `end` cannot be omitted, shorted or added unnecessarily. The number of open blocks, including nested ones, and the number of closing `end`s must match exactly.

    To leave a loop immediately, use the `stop` keyword; `stop` ends **only the single nearest loop**.

- **Syntax:**
    - Multi-condition line: `if [condition] do: [code] else if [condition] do: [code] else do: [code] end`
    - Range repeat control: `loop repeat [loop variable] to [start value] ~ [end value] do: [code] end`
    - Logical-condition repeat: `loop while [logical condition] do: [code] end`
- **Example:**

```cuff
if score >= 90 do: print("Excellent") else if score >= 80 do: print("Encouraging") else do: print("Keep trying") end

loop repeat i to 1 ~ 10 do:
    if i is 4 do:
        stop
    end
    print(f"Round: {i}")
end
```

---

### 9\. Advanced function definition and control (async, returnable, await)

- **Description:** To preserve readability, a function definition area **syntactically forbids being written compressed onto one line**, and must always use physical line breaks.

    For consistency with variable declarations, a function definition always starts with the `set` keyword. A parameter may omit its type (`set func add(a, b)`) or specify it in the form `type name` (`set func add_num(number n1, number n2) do:`). Typed and untyped parameters may be mixed within one function.

    The types usable for parameters are `number`, `str`, `boolean`, `list`, `map` and `match`. Passing a value of a different type to a typed parameter raises a runtime error (`ParameterTypeMismatch`, `E4-030`) immediately **on the calling line** — an `async` function is also checked at the call, before it is queued. `empty` is accepted by a parameter of any type, by the same rule as `set`. If a name does not follow the type keyword right away (`(number)`, `(number, x)`), it is treated as an ordinary parameter with that name, not as a type.

    Declare an asynchronous function with `async` and a function that has a return value with `returnable`; use `return` to actually return a value.

    To start an `async` function and wait for it to finish, place the `await` keyword in front of the call.

    A function call does not use the `do:` symbol, only the parentheses `()`.

    A function definition must always be written as a multi-line block statement, and the function body must be indented. One-line shorthand is not allowed for a function definition.

    The actual execution model and detailed behavior of asynchronous functions are defined in a separate implementation specification.

- **Syntax:**
    - Pure void function definition: `set func [function name]([parameters]) do: [line break] [code] end`
    - Value-returning function definition: `set returnable func [function name](...) do: [line break] return [output value] end`
    - Asynchronous function definition: `set async func [function name](...) do: [line break] [code] end`
    - Global-access-blocking function definition: `set pure func [function name](...) do: [line break] [code] end`
    - Waiting for an asynchronous function call: `await [async function name]()`

    `async`, `returnable` and `pure` are independent modifiers, so they can be combined freely in any order (`set returnable pure func`, `set async pure func`, and so on, all work).

    In the body of a `pure` function, parameters, local variables and the built-in functions loaded with `use DLC:...` can be used freely as usual, but trying to read (including bridging with `change ... to global`) or write a top-level global variable outside the function raises a runtime error (`PureFunctionGlobalAccess`) at once. This restriction propagates through the whole call graph: a `pure` function can call only itself, other `pure` functions, and built-in/DLC functions, and **calling a user-defined function that is not pure** raises a runtime error (`PureFunctionImpureCall`) at once — otherwise you could get around `pure`'s global-access block just by wrapping a general function that touches globals in one more call. To lift the restriction, simply delete the `pure` keyword.
- **Example:**

```cuff
set returnable func calculate_bonus(base_pay) do:
    set constant number MULTIPLIER to 2
    return base_pay * MULTIPLIER
end

set async func download_graphics() do:
    print("Loading the graphics data asynchronously.")
end

set number final_reward to calculate_bonus(5000)
await download_graphics()

use DLC:math
set returnable pure func hypotenuse(a, b) do:
    return math_sqrt(math_pow(a, 2) + math_pow(b, 2))
end
print(hypotenuse(3, 4))
```

---

### 10\. Global/local variables and scope (global)

- **Description:** CuffScript uses the same function-level scope as Python. A variable declared inside a function is valid only within that function and disappears automatically when the function ends.

    A function does not see global variables on its own. To read or modify a global variable inside a function (constants included), first declare that variable as global with `change [variable name] to global`; the lines after that can use it, and `change [variable name] to [new value]` modifies the real global. Without the bridge, a global name is simply undefined inside the function (`UndefinedVariable`), and trying to change it does not touch the global. A local variable declared with `set` always takes precedence over a bridge of the same name.

    Nested functions (defining a function inside a function) and closures (treating a function as a value) are not supported. A function can see only its own local scope and the globals it has bridged.

- **Syntax:**
    - Global variable declaration: `set [type] [variable name] to [value]` (outside a function)
    - Local variable declaration: `set [type] [variable name] to [value]` (inside a function)
    - Using a global variable inside a function: `change [variable name] to global` → then read it or `change [variable name] to [new value]`
- **Example:**

```cuff
set number global_count to 0

set func increment() do:
    change global_count to global  note: declare it as the global variable
    change global_count to global_count + 1
end

set func peek() do:
    print(global_count)  note: Undefined Variable Error (a global is not visible without the bridge)
end

set func test_local() do:
    set number local_var to 100  note: local variable (valid only inside the function)
    print(local_var)             note: 100
end

increment()
print(global_count)  note: 1 (the global variable was modified)

test_local()
print(local_var)     note: Undefined Variable Error (a local variable cannot be accessed outside the function)
```

---

### 11\. Safety-net error handling syntax (or_else do:)

- **Description:** Instead of the heavy, readability-damaging `try-catch` block of older languages, an error situation is handled by placing an `or_else do:` clause, one space after, right behind the function or command line that risks an error.

    To reassign an existing variable inside an `or_else` block, use the `change` keyword. You can also declare a new variable, in which case it has a block-local scope.

    The concrete execution method and return-value handling rules of `or_else` are defined in a separate implementation specification.

- **Syntax:** `[risky statement] or_else do: [code to run on error] end`

- **Example:**

```cuff
set str config to read_file("config.txt") or_else do:
    print("Failed to read the file! Loading the default environment options instead.")
    change config to "default_mode"
end
```

---

### 12\. Basic screen output and keyboard input functions (print, input)

- **Description:** Printing text to the screen uses the standard `print()` function, and receiving the user's text input from the keyboard uses the standard `input()` function.

    Embedding variables follows the f-string style, with the prefix `f` in front of the string, and an f-string may contain simple expressions. To print a brace literally, use double braces `{{` `}}`.

- **Syntax:**
    - `print([value])`
    - `input([prompt message])`
    - f-string: `f"text {variable} text"`
- **Example:**

```cuff
set str user_name to input("Please enter your name: ")
print(f"Welcome, {user_name}!")

set number x to 5
print(f"x + 1 = {x + 1}")

note: printing literal braces
print(f"JSON: {{\"name\": \"Alice\"}}")
```

---

### 13\. Module and library (DLC) loading system (use & from)

- **Description:** The set of official built-in library packages of CuffScript is called DLC, reflecting the humor of this language.

    To combine a local file component that the developer created outside the main source file, use the `use` keyword with the `from` keyword that specifies a relative file path.

    To completely prevent the fragmentation of package-loading code, every module-loading command must be written on a single line (Single Line). Built-in libraries can be loaded several at a time in one `use` statement separated by commas, or each can have its own `use` statement on its own line — the result is the same.

- **Syntax:**
    - Absorb a built-in official library: `use DLC:[core library name]`
    - Several built-in official libraries on one line: `use DLC:[name1], DLC:[name2], DLC:[name3]` (every entry in the list needs the exact name and the `DLC:` prefix)
    - Absorb a custom local module part: `use [module file name] from [relative folder path]`
- **Example:**

```cuff
use DLC:network
use DLC:math, DLC:string, DLC:convert
use dlc_graphic_pack from ./assets/plugins
```

- **`DLC:network`:** An ultra-light client that sends plain HTTP (not HTTPS) GET/POST requests.
    - `network_get(url)` / `network_post(url, body[, content_type])` — both return a `map` of the form `{"status": status code, "ok": whether it is 200~299, "body": response body}`.
    - `url` must start with `http://` (`https://` is rejected with a clear error, because this client does not implement TLS).
    - Connection failures, timeouts, blocked addresses and the like are all runtime errors that `or_else` can catch.
    - By default it cannot connect to localhost/private network addresses (SSRF protection). Host run options can turn the network off altogether or allow private network access — see `SECURITY.md` for details and the risks.

- **`DLC:filesystem`:** A library that reads and writes files inside the folder the script is in (or the `--root` the host specified).

    | Function | Return type | Description |
    | :--- | :--- | :--- |
    | `file_exist(path)` | boolean | Checks whether the file exists. |
    | `file_size(path)` | number | Returns the file's size as a number of bytes. (`empty` on failure) |
    | `file_read(path)` | str | Reads all the text of the file. (`empty` on failure) |
    | `file_readlines(path)` | list | Splits the file on line breaks and reads it as a list. (`empty` on failure) |
    | `file_write(path, text)` | boolean | Overwrites the file if it exists, otherwise creates it and writes. |
    | `file_add(path, text)` | boolean | Appends text to the end of the file's contents. Creates it if missing. |
    | `file_remove(path)` | boolean | Deletes a file permanently and returns whether it succeeded. Only regular files are deleted; a directory (even an empty one) is left alone and `false` is returned. |

    - Every path is resolved relative to the folder the script is in (or `--root`), and a path that escapes it (an absolute path, a `../` escape, etc.) is rejected immediately with a `FilesystemAccessDenied` error. It shares exactly the same sandbox root as loading modules with `use ... from`.
    - Ordinary OS-level failures, such as a missing file or no permission, are not errors; they quietly return `empty`/`false` as the table says — check existence first with `file_exist()`.
    - The host run option (`--no-filesystem`) can turn this library off altogether — see `SECURITY.md` for details and the risks.

- **Function naming rule:** DLC functions are named in the `library_verb` form (`math_sqrt`, `str_upper`, `list_sort`, `map_keys`, `time_now`, `random_int`, `network_get`, `file_read`, etc.), so the name alone tells you which library a function belongs to. The exceptions are `length`/`contains`/`index_of` (polymorphic functions shared by strings, lists and maps) and `to_json`/`from_json`/`to_number`/`to_str`/`to_boolean` (whose names already show their domain).

---

# CuffScript Comprehensive Verification Code

```cuff
note: Step 1: load the official libraries (DLC) and a custom module (one statement per line)
use DLC:network
use stage_data from ./maps/core_engine

note:
This block is a multi-line comment.
It is the final end-to-end check covering 1-based indexes, or_else, pattern matching, and global/local variables.
endnote

note: Step 2: core variables and fixed constants (constants use only `to`)
set constant number MAX_LEVEL to 99
set constant str ENGINE_SIGNATURE to "CUFF_LANG_V1"

set number current_lvl to 1
set str user_email to "Player_One@CuffLang.com"
set list reward_tier_list to ["Gold", "Silver", "Bronze"]

note: Step 3: declare the returnable functions that handle logic checks and text pattern matching
set returnable func audit_and_assess_user(email, lvl) do:

    change MAX_LEVEL to global

    note: validate the email's structure with pattern matching (the `is` operator)
    if email is "[str]+@[str]2~10" do:
        print("Pattern matching engine: email structure verified")
    end

    note: the `!` negation operator combined with a strict, lowercase `is` comparison
    if !lvl is MAX_LEVEL do:
        print(f"Current level {lvl} is not the highest level.")
    end

    if lvl is MAX_LEVEL do:
        return "epic_rank"
    else if lvl >= 50 do:
        return "rare_rank"
    else do:
        return "common_rank"
    end
end

note: Step 4: define a standalone async function for background data processing
set async func backup_user_cloud_data() do:
    print("Sending a snapshot of the virtual machine's internal data to the remote cloud infrastructure.")
end

note: Step 5: run the main program logic and exercise 1-based collection operations
set str evaluation_result to audit_and_assess_user(user_email, current_lvl)

note: call a function with the or_else safety net
await backup_user_cloud_data() or_else do:
    print("Working around the cloud sync failure caused by the network environment.")
end

note: with 1-based indexing, index 1 is the first element, "Gold".
print(f"Data identifying the top-tier reward emblem: {reward_tier_list[1]}")

note: modify list contents with the standalone `add ... to` and `change` statements
add "None_Tier" to reward_tier_list
change reward_tier_list[1] to "Platinum_Tier" note: replace the existing word at position 1 with Platinum

note: Step 6: fast repetition using the range operator (~) and the `loop repeat` structure
loop repeat step to 1 ~ 5 do:
    if step is 4 do:
        print("Forcing the loop to stop.")
        stop
    end
    print(f"CuffScript fast VM sync engine running... current loop step: {step}")
end
```

---

### 14\. Error code notation

- **Description:** The code in an error message has the form `E<category>-<number>` (for example `E4-005`, `E2-001`). The category number means the kind of error, and the three digits after the hyphen are the number within that category. Thanks to the hyphen it stays unambiguous even if a category number reaches two digits (`E10-001`).

    | Category | Kind |
    | :--- | :--- |
    | `E1-xxx` | Tokenization (Lexical) error |
    | `E2-xxx` | Syntax error |
    | `E3-0xx` / `E3-1xx` | Regex/pattern syntax error / regex runtime error |
    | `E4-xxx` | Runtime error |
    | `E5-xxx` | Module/library error |
    | `E6-xxx` | Resource limit error (not caught by `or_else`) |
    | `E9-xxx` | Internal error |

    The line where the error occurred is printed as is below the message, with a `^` marker under the exact position. An error inside an f-string's `{...}` points at the line that holds the f-string.
