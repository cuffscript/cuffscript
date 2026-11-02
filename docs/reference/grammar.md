# Grammar

The formal grammar of CuffScript, as the parser reads it. It is a companion to the [language reference](language.md), which says what each construct *means*, and to the [classes reference](classes.md), which
covers the object model; this page says what is *accepted*. Where the pages seem to differ, the language reference describes the behaviour and this page the shape.

## Notation

```text
a b         a, then b
a | b       a or b
[ a ]       a, or nothing
{ a }       zero or more times a
( a b )     grouping
"word"      the word exactly, as a token
NAME        an identifier
CLASSNAME   an identifier that starts with a capital letter
NEWLINE     the end of a line
```

Spaces between tokens are free except where the language reference says otherwise (a space is not allowed before a `:`, and the `use` path is read to the end of its line). Keywords are
case sensitive and lower case; the list is in [reserved words](language.md#15-names-and-reserved-words).

## Program

```text
program        = { statement } ;

statement      = ( simple_statement | block_statement ) [ or_else ] NEWLINE ;

or_else        = "or_else" [ "as" NAME ] "do" ":" NEWLINE { statement } "end" ;
```

An `or_else` follows the statement it protects, on the same line for a simple statement and after the closing `end` for a block. The handler is a block of its own and has its own
`end`. A statement may be followed by only one `or_else`. `as NAME` gives the handler the [error object](classes.md#errors-are-objects) in a variable of that name. A statement ends at the end of its line: two statements on one line are a syntax error.

## Statements

```text
simple_statement = declaration
                 | change | add | remove | global
                 | "stop" | "skip"
                 | return | fail
                 | await_statement
                 | use
                 | expression ;

declaration    = "set" [ "constant" ] type NAME "to" expression ;
type           = "number" | "str" | "list" | "map" | "boolean" | "match" | "pattern" | "object" | "empty"
               | CLASSNAME ;

place          = ( NAME | "self" | "." NAME ) { "[" expression "]" | "." NAME } ;

change         = "change" place "to" expression ;
add            = "add" expression "to" place ;
remove         = "remove" expression "from" place ;
global         = "global" NAME { "," NAME } ;

return         = "return" [ expression ] ;
fail           = "fail" expression ;
await_statement = "await" postfix ;
```

- `declaration`: the type is required, and a constant's name must be written in capitals (checked when the statement runs). A type name is read as a type only where a type may stand (here, in a parameter list and in a field); everywhere else the same word is an ordinary name.
- `place` is a spot that holds a value: a variable, a list item, a map entry or a field, to any depth. `change`, `add` and `remove` all take one, so `add 5 to grid[1]` adds to the inner list and `change self.items[2] to 0` changes an item of a field. A slice or the result of a call is not a place. `.NAME` is a member of the object of the innermost [`with` block](classes.md#the-with-block).
- `global` links variables of the program to the function it is written in; it must come before the names are used. At the top level it is an error, because there every variable is already global.
- `fail` raises an error; its value is an `Error` object or a string (see [errors are objects](classes.md#errors-are-objects)).
- `await postfix` is the statement form; `await` can also be used inside an expression (`set number x to await f()`).
- An `expression` on its own is a statement; its value is discarded. This is how a call such as `print(1)` is a statement.
- `skip` and `stop` act on the nearest enclosing loop and are an error outside one.

```text
block_statement = if | loop | with | function | class ;

if             = "if" expression "do" ":" body
                 { "else" "if" expression "do" ":" body }
                 [ "else" "do" ":" body ]
                 "end" ;

loop           = "loop" "repeat" NAME "to" expression "~" expression "do" ":" body "end"
               | "loop" "while" expression "do" ":" body "end"
               | "loop" "each" NAME "in" expression "do" ":" body "end" ;

with           = "with" expression "do" ":" body "end" ;

function       = { "async" | "pure" } "func" NAME
                 "(" [ parameter { "," parameter } ] ")" "do" ":" body "end" ;

parameter      = [ type ] NAME [ "to" expression ] ;

class          = "class" CLASSNAME [ "extends" CLASSNAME ] "do" ":" { member } "end" ;
member         = [ "private" ] ( field | function ) ;
field          = "set" [ "constant" ] type NAME "to" expression ;

body           = { statement } ;
```

- The body of an `if`, a loop, a `with`, a function or a class may be written on the same line as its header (`if x is 1 do: print("one") end`). An empty body may be written on one line only for a class (`class Oops extends Error do: end`).
- The words `async` and `pure` can appear in any order before `func`, each at most once. `private` may only appear in front of a member of a class.
- A parameter's type is optional and is a type name followed by the name (`list items`); `empty` is not a parameter type. A single word is always a name, so `f(list)` has a parameter called `list`. A parameter with a default is written `NAME to expression`; the parameters without one come first.
- A class contains fields and methods only, and appears only at the top level of a program or a module. A function is not declared inside another function or a method.
- A body closes at the first `end` (or `else` for an `if` branch) that is not inside a deeper block.

```text
use            = "use" "DLC" ":" NAME { "," "DLC" ":" NAME }
               | "use" NAME "from" path ;

path           = { WORD | NUMBER | "." | "/" | "-" } ;
```

A `path` runs to the end of the line. It is written with forward slashes and has no quotes. The library names after `DLC:` are listed in [libraries](dlc/README.md); each name in a list needs its own `DLC:` prefix.

## Expressions

From the loosest binding to the tightest:

| Level | Operators | Associativity |
| ---: | :--- | :--- |
| 1 | `or` | left |
| 2 | `and` | left |
| 3 | `!` (prefix) | right |
| 4 | `is`, `is not`, `matches`, `isa`, `<`, `<=`, `>`, `>=` | left |
| 5 | `+`, `-` | left |
| 6 | `*`, `/`, `%` | left |
| 7 | `-` (prefix) | right |
| 8 | `**` | right |
| 9 | indexing `x[i]`, slicing `x[i~j]`, member `x.name`, calling `f(...)` | left |

```text
expression     = or_expr ;
or_expr        = and_expr { "or" and_expr } ;
and_expr       = not_expr { "and" not_expr } ;
not_expr       = "!" not_expr | comparison ;
comparison     = additive { ( "is" [ "not" ] | "matches" | "<" | "<=" | ">" | ">=" ) additive
                          | "isa" CLASSNAME } ;
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary          = "-" unary | power ;
power          = postfix [ "**" unary ] ;
postfix        = primary { "[" expression "]" | "[" expression "~" expression "]" | "." WORD | call_args } ;
call_args      = "(" [ expression { "," expression } ] ")" ;
```

Some consequences:

- `!` binds looser than comparison, so `!x is 3` means `!(x is 3)` and `!text matches p"a"` means `!(text matches p"a")`. `and` binds tighter than `or`.
- Comparison is left-associative, so `1 < 2 < 3` is `(1 < 2) < 3`, which is a type error at run time and not a chain.
- `**` binds tighter than a minus on its left and takes a unary operand on its right: `-2 ** 2` is `-4`, `2 ** -1` is `0.5`, and `2 ** 3 ** 2` is `2 ** 9`.
- `is` compares values for every type; it never reads its right side as a pattern. `matches` needs a `str` on the left and a `pattern` on the right and tells whether the whole text fits ([patterns](patterns.md)).
- A call is written directly after a name (`f(...)`), after a member (`x.name(...)`) or after `super.name`: `f(1)(2)` is a syntax error. Indexing can follow any primary or call: `f()[1]`. A member name after a `.` may be any word, including one that is otherwise reserved (`x.count`, `m.end`).
- A method is only reached by calling it: `x.name` without the parentheses is allowed for a field only.
- A line may break inside `(...)`, `[...]` and `{...}`, but not after an operator.

```text
primary        = NUMBER
               | STRING
               | FSTRING
               | PATTERN
               | "true" | "false" | "empty"
               | NAME
               | "self"
               | "." NAME
               | super_call
               | list_literal
               | map_literal
               | "(" expression ")"
               | "await" postfix ;

super_call     = "super" "." NAME call_args ;

list_literal   = "[" [ expression { "," expression } [ "," ] ] "]" ;
map_literal    = "{" [ map_entry { "," map_entry } [ "," ] ] "}" ;
map_entry      = expression ":" expression ;
```

A list or map literal may end with a trailing comma; the arguments of a call and the parameters of a function may not. Map keys must be strings when the program runs. `self` is only available inside a method and `super` only in a method of a class that has a parent; `.NAME` only inside a `with` block.

## Lexical elements

```text
NAME           = ( letter | "_" ) { letter | digit | "_" } ;       letter = "A".."Z" | "a".."z"
NUMBER         = digit { digit } [ "." digit { digit } ] ;
STRING         = '"' { character | escape } '"'  |  "'" { character | escape } "'" ;
FSTRING        = "f" '"' { character | escape | "{" expression "}" | "{{" | "}}" } '"' ;
PATTERN        = "p" '"' { pattern_char } '"' [ "i" ] [ "m" ] ;
comment        = "note" ":" { any character up to the end of the line }
               | "note" ":" NEWLINE { any text } "endnote" ;
```

- A name is not a [reserved word](language.md#15-names-and-reserved-words), apart from the few that may be used as names (the language reference lists them). The names of the types are ordinary names outside the places where a type may stand.
- A number has digits on both sides of its point, and no sign or exponent: a minus is the prefix operator, and `.5`, `5.` and `1e5` are not numbers.
- A string is written with double quotes or single quotes; an f-string only with double quotes. Inside a string a backslash starts an escape (`\"`, `\'`, `\\`, `\n`, `\t`, `\r`); any other backslash and the character after it are kept as written. A string may span lines.
- Inside an f-string, `{` begins an expression that runs to the matching `}`, and `{{` and `}}` are literal braces. The expression cannot contain a double quote, and there is no format specifier: `{x:2}` is a syntax error.
- A pattern literal is raw: the text between the quotes goes to the [pattern parser](patterns.md) as written, except that `\"` stands for a quote. Any other backslash and the character after it are both kept, and a backslash always takes the character after it, so `p"a\\"` is the pattern `a\\` and the literal ends at the next quote. A literal may span lines. The flags `i` (ignore case) and `m` (lines) follow the closing quote with no space, each at most once. `p` and the quote must touch: `p "a"` is a variable `p` followed by a string.
- A comment's colon follows `note` with no space.
- Indentation (spaces or tabs, a tab counting as four columns) must be consistent within a block, but blocks are closed by `end`, not by indentation.

## Patterns

The syntax between the quotes of a pattern literal. The full meaning of every token is in the [pattern reference](patterns.md).

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
