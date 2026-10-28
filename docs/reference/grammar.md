# Grammar

The formal grammar of CuffScript, as the parser reads it. It is a companion to the [language reference](language.md), which says what each construct *means*; this page says
what is *accepted*. Where the two seem to differ, the language reference describes the behaviour and this page the shape.

## Notation

```text
a b         a, then b
a | b       a or b
[ a ]       a, or nothing
{ a }       zero or more times a
( a b )     grouping
"word"      the word exactly, as a token
NAME        an identifier
NEWLINE     the end of a line
```

Spaces between tokens are free except where the language reference says otherwise (a space is not allowed before a `:`, and the `use` path is read to the end of its line). Keywords are
case sensitive and lower case, except `IS`; the list is in [reserved words](language.md#15-names-and-reserved-words).

## Program

```text
program        = { statement } ;

statement      = simple_statement [ or_else ] NEWLINE
               | block_statement [ or_else ] ;

or_else        = "or_else" "do" ":" NEWLINE { statement } "end" ;
```

An `or_else` follows the statement it protects, on the same line for a simple statement and after the closing `end` for a block. The handler is a block of its own and has its own
`end`. A statement may be followed by only one `or_else`.

## Statements

```text
simple_statement = declaration
                 | change
                 | add | remove | replace
                 | "stop"
                 | return
                 | await_statement
                 | use
                 | expression ;

declaration    = "set" [ "constant" ] type NAME "to" expression ;
type           = "number" | "str" | "list" | "map" | "boolean" | "match" | "empty" ;

change         = "change" NAME { "[" expression "]" } "to" expression
               | "change" NAME "to" "global" ;

add            = "add" expression "to" NAME ;
remove         = "remove" expression "from" NAME ;
replace        = "replace" NAME "[" expression "]" "to" expression ;

return         = "return" [ expression ] ;
await_statement = "await" call ;
```

- `declaration`: the type is required, and a constant's name must be written in capitals (checked when the statement runs).
- `change ... to global` is the bridge to a global variable; it has no index and needs a function body around it to mean anything.
- `add` and `remove` take a variable name and nothing else: what follows the name starts the next statement ([collections](language.md#51-collections-add-change-remove)).
- `replace NAME [ index ] to value` is a second spelling of `change NAME[index] to value`. It takes exactly one index and fails in the same ways. It is not the `replace ... in ... to` expression (see below).
- `await call` is the statement form; `await` can also be used inside an expression (`set number x to await f()`).
- An `expression` on its own is a statement; its value is discarded. This is how a call such as `print(1)` is a statement.

```text
block_statement = if | loop | function ;

if             = "if" expression "do" ":" body
                 { "else" "if" expression "do" ":" body }
                 [ "else" "do" ":" body ]
                 "end" ;

loop           = "loop" "repeat" NAME "to" expression "~" expression "do" ":" body "end"
               | "loop" "while" expression "do" ":" body "end" ;

function       = "set" { "returnable" | "async" | "pure" } "func" NAME
                 "(" [ parameter { "," parameter } ] ")" "do" ":" NEWLINE body "end" ;

parameter      = [ param_type ] NAME ;
param_type     = "number" | "str" | "boolean" | "list" | "map" | "match" ;

body           = { statement } ;
```

- A body of an `if` or a loop may be written on the same line as its header (`if x is 1 do: print("one") end`). A function body may not: it has to start on a new line.
- The words `returnable`, `async` and `pure` can appear in any order between `set` and `func`.
- A parameter's type is optional; `empty` is not a parameter type.
- A body closes at the first `end` (or `else` for an `if` branch) that is not inside a deeper block.

```text
use            = "use" "DLC" ":" NAME { "," "DLC" ":" NAME }
               | "use" NAME "from" path ;

path           = { WORD | NUMBER | "." | "/" | "-" } ;
```

A `path` runs to the end of the line. It is written with forward slashes and has no quotes. The library names after `DLC:` are listed in [libraries](dlc/README.md).

## Expressions

From the loosest binding to the tightest:

| Level | Operators | Associativity |
| ---: | :--- | :--- |
| 1 | `or`, `\|\|` | left |
| 2 | `and`, `&&` | left |
| 3 | `!` (prefix) | right |
| 4 | `is`, `is not`, `IS`, `IS not`, `<`, `<=`, `>`, `>=` | left |
| 5 | `+`, `-` | left |
| 6 | `*`, `/`, `%` | left |
| 7 | `-` (prefix) | right |
| 8 | `**` | right |
| 9 | indexing `x[i]`, slicing `x[i~j]`, calling `f(...)` | left |

```text
expression     = or_expr ;
or_expr        = and_expr { ( "or" | "||" ) and_expr } ;
and_expr       = not_expr { ( "and" | "&&" ) not_expr } ;
not_expr       = "!" not_expr | comparison ;
comparison     = additive { ( "is" [ "not" ] | "IS" [ "not" ] | "<" | "<=" | ">" | ">=" ) additive } ;
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary          = "-" unary | power ;
power          = postfix [ "**" unary ] ;
postfix        = primary { "[" expression "]" | "[" expression "~" expression "]" | "(" [ arguments ] ")" } ;
arguments      = expression { "," expression } ;
```

Some consequences:

- `!` binds looser than comparison, so `!x is 3` means `!(x is 3)`. `and` binds tighter than `or`.
- Comparison is left-associative, so `1 < 2 < 3` is `(1 < 2) < 3`, which is a type error at run time and not a chain.
- `**` binds tighter than a minus on its left and takes a unary operand on its right: `-2 ** 2` is `-4`, `2 ** -1` is `0.5`, and `2 ** 3 ** 2` is `2 ** 9`.
- When the right operand of `is` or `IS` is a string literal, it is read as a [pattern](patterns.md) and the whole expression is a pattern test (checked for syntax when the program is parsed); `is not` negates the test.
- A call is written `NAME(...)`: only an identifier can be called. `f(1)(2)` is a syntax error. Indexing can follow any primary or call: `f()[1]`.
- A line may break inside `(...)`, `[...]` and `{...}`, but not after an operator.

```text
primary        = NUMBER
               | STRING
               | FSTRING
               | "true" | "false" | "empty"
               | NAME
               | call
               | list_literal
               | map_literal
               | "(" expression ")"
               | pattern_expression
               | "await" call ;

call           = NAME "(" [ arguments ] ")" ;

list_literal   = "[" [ expression { "," expression } [ "," ] ] "]" ;
map_literal    = "{" [ map_entry { "," map_entry } [ "," ] ] "}" ;
map_entry      = expression ":" expression ;
```

A list or map literal may end with a trailing comma; the arguments of a call and the parameters of a function may not. Map keys must be strings when the program runs.

### Pattern expressions

The five text expressions take their pattern and their text as additive expressions, so `find p from a + b` searches the joined text. `flags` is any combination of the letters `i`, `g` and `m`, written
directly after the last operand.

```text
pattern_expression = "find"  additive "from" additive [ flags ]
                   | "match" additive "from" additive [ flags ]
                   | "replace" additive "in" additive "to" additive [ flags ]
                   | "split" additive "by" additive
                   | "count" additive "in" additive [ flags ] ;
```

For `match` and `find` the first operand is the pattern and the second the text; for `split` it is the other way round (text, then pattern), as the words read.

## Lexical elements

```text
NAME           = ( letter | "_" ) { letter | digit | "_" } ;       letter = "A".."Z" | "a".."z"
NUMBER         = digit { digit } [ "." digit { digit } ] ;
STRING         = '"' { character | escape } '"'  |  "'" { character | escape } "'" ;
FSTRING        = "f" '"' { character | escape | "{" expression "}" | "{{" | "}}" } '"' ;
comment        = "note" ":" { any character up to the end of the line }
               | "note" ":" NEWLINE { any text } "endnote" ;
```

- A name is not a [reserved word](language.md#15-names-and-reserved-words), apart from the few that may be used as names (the language reference lists them).
- A number has digits on both sides of its point, and no sign or exponent: a minus is the prefix operator, and `.5`, `5.` and `1e5` are not numbers.
- A string is written with double quotes or single quotes; an f-string only with double quotes. Inside a string a backslash starts an escape (`\"`, `\'`, `\\`, `\n`, `\t`, `\r`); any other backslash and the character after it are kept as written. A string may span lines.
- Inside an f-string, `{` begins an expression that runs to the matching `}`, and `{{` and `}}` are literal braces. There is no format specifier: `{x:2}` is a syntax error.
- A comment's colon follows `note` with no space.
- Indentation (spaces or tabs, a tab counting as four columns) must be consistent within a block, but blocks are closed by `end`, not by indentation.

## Patterns

The syntax inside a string that is read as a pattern. The full meaning of every token is in the [pattern reference](patterns.md).

```text
pattern        = { element } ;
element        = atom [ quantifier [ "?" ] ] ;

atom           = LITERAL_CHAR
               | "\" ESCAPABLE
               | token
               | set
               | one_of
               | group
               | named_group ;

token          = "[" WORD "]" ;                                  num let low up str word sp nl any
                                                                 hex int float email phone url
                                                                 edge start end
set            = "[" [ "!" ] { SET_CHAR | SET_CHAR "-" SET_CHAR } "]" ;
one_of         = "[" "one" ":" ALTERNATIVE { "|" ALTERNATIVE } "]" ;
group          = "(" pattern ")" ;
named_group    = "<" NAME ":" pattern ">" ;

quantifier     = DIGITS | DIGITS "~" [ DIGITS ] | "~" DIGITS | "+" | "*" | "?" ;
ESCAPABLE      = one of the symbols listed under "Escaping special symbols" in the pattern reference ;
```

- A `token` whose word is not one of the named tokens is read as a `set` of its letters, not rejected.
- A quantifier applies to the atom before it. `+`, `*` and `?` at the very start of a pattern or group are ordinary characters.
- Everything else is a literal character, `.`, `|`, `:`, `-`, `{` and `}` included.
- A set ends at the first `]`; inside it nothing is escaped.
- `[one:...]` has no escapes either: its alternatives are literal text separated by bars.
