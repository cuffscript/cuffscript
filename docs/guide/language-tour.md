# A tour of the language

Every example on this page is run by the documentation checks, so the output shown is what you get. The
[language reference](../reference/language.md) is the complete definition; this page is the short way in.

## Values and variables

```cuff
set number age to 25
set str name to "Ann"
set boolean active to true
set list scores to [90, 85, 77]
set map profile to {"name": "Ann", "city": "Seoul"}
change age to 26
print(name, age, active)
print(scores, profile)
```

```output
Ann 26 true
[90, 85, 77] {"name": "Ann", "city": "Seoul"}
```

`set` declares and checks the type once; `change` gives an existing variable a new value. A constant is written
`set constant number MAX to 10`, with a name in capitals, and cannot be changed.

## Text

Strings use double quotes, `+` joins them, and an f-string puts values into text. Positions start at 1, and
a negative position counts from the end.

```cuff
set str city to "Seoul"
print("Hello, " + city + "!")
print(f"{city} has {1 + 1} syllables")
print(city[1], city[-1], city[2~4])
```

```output
Hello, Seoul!
Seoul has 2 syllables
S l eou
```

`a~b` takes positions `a` through `b`, both included.

## Deciding

Equality is written `is` and `is not`, and a comparison works on two numbers or two strings.

```cuff
set number temp to 18
if temp > 25 do:
    print("hot")
else if temp > 15 do:
    print("mild")
else do:
    print("cold")
end
```

```output
mild
```

`and` and `or` combine conditions and `!` reverses one. `a and b` stops as soon as `a` is false, and `a or b` as soon
as `a` is true. `%` gives the remainder of a division and `**` raises to a power.

```cuff
set number hour to 14
if hour >= 9 and hour < 18 do:
    print("open")
end
print(hour % 12, 2 ** 5)
```

```output
open
2 32
```

## Repeating

```cuff
loop repeat i to 1 ~ 3 do:
    print(i * i)
end

set number n to 1
loop while n < 20 do:
    change n to n * 3
end
print(n)
```

```output
1
4
9
27
```

`stop` ends the nearest loop. A loop does not open a new scope: `i` and `n` are still there afterwards.

## Lists and maps

```cuff
set list items to [10, 20]
add 30 to items
change items[1] to 11
remove 2 from items
print(items)

set map stock to {"apples": 3}
change stock["pears"] to 5
print(stock["pears"], stock)
```

```output
[11, 30]
5 {"apples": 3, "pears": 5}
```

`remove 2 from items` takes out the element at position 2; removing a value instead of a position is done by naming a
value that is not a number, such as a string. Lists and maps are shared, not copied: if two names refer to the same list, a change through one shows through the
other. The [list](../reference/dlc/list.md) and [map](../reference/dlc/map.md) libraries add sorting, joining, keys
and more.

## Functions

```cuff
set func announce(text) do:
    print(">> " + text)
end

set returnable func add(number a, number b) do:
    return a + b
end

announce("start")
print(add(2, 3))
```

```output
>> start
5
```

A function that is not `returnable` gives nothing back. Parameters may carry a type (`number a`), which is checked at
the call. Functions can call themselves, and a function may be called above the line that declares it. There are no
closures: a function does not see your global variables unless it asks, with `change x to global`.

## Handling errors

An `or_else` block runs if the statement before it fails with an error that can be caught:

```cuff
set number n to to_number("abc") or_else do:
    print("not a number, using 1")
    change n to 1
end
print(n)
```

```output
not a number, using 1
1
```

Which errors can be caught is in the [error catalogue](../reference/errors.md), and
[errors and debugging](errors-and-debugging.md) shows how to read and handle them.

## Using libraries

```cuff
use DLC:math
use DLC:string
print(math_sqrt(81), str_upper("done"), length("한글"))
```

```output
9 DONE 2
```

## Searching text

Pattern expressions find and test text by shape instead of by exact characters:

```cuff
set str code to "order SN-2024-0042 shipped"
set match found to match code from "SN-([num]4)-([num]+)"
if found do:
    print("matched")
end
print(find "[num]4" from code)
```

```output
matched
2024
```

[Text and patterns](text-and-patterns.md) builds this up step by step.

## Splitting a program into files

`use name from ./folder` loads `./folder/name.cuff`. See [Modules](modules.md).

## What is not here

No classes, no closures, no `==` or `!=` (equality is `is`), and no one-line function declarations. The
language is small on purpose, and the [reference](../reference/language.md) says exactly what is and is not
allowed.
