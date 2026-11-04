# Phenomena

Not everything a language does was designed one rule at a time. A lot of it is a *phenomenon*: something that follows from how the engine happens to work, and that people then learn and rely on. The best-known one is JavaScript's hoisting, which was never a feature. It is what you see when an engine collects declarations before it runs the first line.

CuffScript has these too. Some are kept because they are useful, some because the rule that causes them is simple and the alternative would cost more than it gives. This page lists the ones worth knowing, each with a program, what it prints, and the rule behind it. The rules themselves are written once, in [How it works](../internals/how-it-works.md); the language reference says what each statement *means*, and this page says what *follows*.

Every program here is also a test (`tests/cases/phenomena/`, or the unit test named under the example), so a phenomenon cannot quietly change: if it did, a test would fail and the change would have to be written down.

## Before the program runs

### Functions and classes are known before the first line runs

<!-- test: phenomena/hoisting -->
```cuff
note: a function or a class can be used above the place where it is written
print(double(4))
set Point corner to Point(2, 3)
print(corner.x + corner.y)

func double(number n) do:
    return n * 2
end

class Point do:
    set number x to 0
    set number y to 0

    func init(number x, number y) do:
        change self.x to x
        change self.y to y
    end
end

note: a variable is only there from the line that makes it
print(total) or_else as problem do:
    print(problem.code, "the variable does not exist yet")
end
set number total to 5
print(total)
```

```output
8
5
E4-001 the variable does not exist yet
5
```

**Why.** A run begins by reading the whole text, then walking the top level once to register every function and every class (see [a run in four steps](../internals/how-it-works.md#a-run-in-four-steps)). Only then does the first statement run. A variable has no such step: it comes into being when its `set` line runs, so asking for it earlier is [`E4-001`](../reference/errors.md#e4-001).

**Use it.** Put the steps of a program first and the helpers under them. Classes may be written in any order too, even a child before its parent.

### The last function of a name wins, everywhere

<!-- test: phenomena/last_declaration_wins -->
```cuff
note: both calls print the same word, because every function was collected before the first line ran
func pick() do:
    return "first"
end

print(pick())

func pick() do:
    return "second"
end

print(pick())

note: functions are looked up before the library, so one of your own replaces a library function everywhere, above its line too
use DLC:string
print(str_upper("hello"))

func str_upper(text) do:
    return "no thanks"
end
```

```output
second
second
no thanks
```

**Why.** Registering is a table with one entry per name, so a second `func pick` replaces the first before anything runs, and both calls reach it. Calls look in that table before they look in the libraries, which is why a function of your own can stand in for a library function, above its line as well as below.

**Watch out.** Nothing warns about a name used twice. If a function seems to do the wrong thing, search the file for another one of the same name.

### A syntax error stops a program before its first line; a runtime error does not

```cuff error E2-001
print("first line")
print(1 +
```

```cuff error E4-006
print("first line")
print(1 / 0)
```

The first program prints no `first line`: only the error report. The second prints `first line`, and then the report.

<!-- test: tests/unit/phenomena_test.cpp -->

**Why.** The whole text is parsed before any of it runs, so a mistake in the last line, or in a function that is never called, is found first. A runtime error is found when its line is reached, and by then the earlier lines have run and printed. An error also drops any `async` call that was still waiting, so it does not run later.

**Use it.** A script either starts or does not. A program that has printed something has parsed completely.

### `use` runs when execution reaches it

<!-- test: phenomena/use_runs_when_reached -->
`fixtures/helpers.cuff`:

```cuff
func shout(word) do:
    return word + "!"
end
```

The program:

```cuff
note: use is a statement. Until execution reaches it, the module's functions do not exist.
print("calling before the use line")
print(shout("x")) or_else as problem do:
    print(problem.code, "the module is not loaded yet")
end

use helpers from ./fixtures
print(shout("now"))

note: a function written in this very file needs no such order
print(local_shout("anytime"))
func local_shout(word) do:
    return word + "!"
end
```

```text
calling before the use line
E4-002 the module is not loaded yet
now!
anytime!
```

**Why.** `use name from ./folder` is a statement, not a declaration. The module is read and run at that point, and only then do its functions exist. The functions written in the program itself were registered before the run, so they have no such order.

**Use it.** Put the `use` lines at the top, as the examples in this guide do.

## What a name stands for

### Lists, maps and objects are shared; numbers and text are copied

<!-- test: phenomena/shared_values -->
```cuff
note: a list, a map or an object is one thing with several names; a number or a text is copied
set list first to [1, 2]
set list second to first
add 3 to second
print(first, second)

func grow(list items) do:
    add 99 to items
end
grow(first)
print(first)

set number count to 5
func bump(number x) do:
    change x to x + 1
    return x
end
print(bump(count), count)

note: copy() makes a new outer list, but the lists inside are still shared
set list grid to [[1], [2]]
set list twin to grid.copy()
add 5 to twin[1]
add [9] to twin
print(grid, twin)

note: is compares the items of a list or a map, but an object only with itself
class Dot do:
    set number x to 1
end
set Dot a to Dot()
set Dot b to Dot()
set Dot c to a
print([1, 2] is [1, 2], {"k": 1} is {"k": 1})
print(a is b, a is c, a is a.copy())
```

```output
[1, 2, 3] [1, 2, 3]
[1, 2, 3, 99]
6 5
[[1, 5], [2]] [[1, 5], [2], [9]]
true true
false true false
```

**Why.** A number or a text is handed on by value, so the function's `x` is its own. A list, a map or an object is made once and a name only points at it, so `second` and `first`, and the `items` a function receives, are the one list ([how values are held](../internals/how-it-works.md#values-and-sharing)). `copy()` makes a new outer list and the lists inside it are still shared. `is` asks whether two lists or maps hold the same items, and whether two objects are the same object.

**Watch out.** To change a list inside a function without touching the caller's, `copy()` it first, and copy the inner lists too if they will change.

### Only a function opens a scope

<!-- test: phenomena/scopes -->
```cuff
note: a loop and an if do not open a scope; a function does
loop repeat i to 1 ~ 3 do:
    set number last to i * 10
end
print(i, last)

if true do:
    set number inside to 7
end
print(inside)

func work() do:
    set number hidden to 1
    return hidden
end
print(work())
print(hidden) or_else as problem do:
    print(problem.code, "a function's variables stay inside it")
end
```

```output
3 30
7
1
E4-001 a function's variables stay inside it
```

**Why.** A loop or an `if` runs in the scope it is written in, so `i` and `last` are still there after the loop, and `inside` after the `if`. A function call starts a scope of its own that sees nothing of its caller's variables ([scopes](../internals/how-it-works.md#scopes)). A function reaches a program variable only through [`global`](../reference/language.md#71-reaching-a-global-from-a-function).

### Inside `with`, a bare name looks in the object first

<!-- test: phenomena/with_finds_members_first -->
```cuff
class Box do:
    set number size to 1
    set str label to "box"
end

set number size to 100
set number label to 5
set Box b to Box()

with b do:
    note: inside with, a bare name is looked for in the object first
    print(size, label)
    print(.size)
end
print(size, label)
```

```output
1 box
1
100 5
```

**Why.** `with` adds one step to a name lookup: before the scopes outward, the object's members. The variable `size` is still there, but the member is found first, and after the block the plain variable is seen again. `.size` always means the member.

**Use it.** Inside `with`, write `.size` when you mean the member. It reads the same whatever variables happen to exist.

### Private belongs to the class that wrote it

<!-- test: phenomena/private_belongs_to_the_class -->
```cuff
class Parent do:
    private set number secret to 7

    func reveal() do:
        return self.secret
    end
end

class Child extends Parent do:
    func peek() do:
        return self.secret
    end
end

set Child kid to Child()
print(kid.reveal())
print(kid.peek()) or_else as problem do:
    print(problem.code, "a child inherits the field but not the right to read it")
end
```

```output
7
E4-034 a child inherits the field but not the right to read it
```

**Why.** A private member is checked against the class whose method is running, not against the class of the object. `reveal` was written in `Parent`, so it may read `secret`. `peek` was written in `Child`, so it may not, although every `Child` object has the field.

### A field's type is checked when the object is made

<!-- test: phenomena/field_types_at_creation -->
```cuff
class Counter do:
    set number count to 0
end

note: a field's type is checked when the object is made...
class Bad do:
    set number n to "x"
end
set Bad b to Bad() or_else as problem do:
    print(problem.code)
end

note: ...and not afterwards
set Counter c to Counter()
change c.count to "many"
print(c.count, type_of(c.count))
```

```output
E4-020
many str
```

**Why.** The type written in front of a field is checked once, against the field's default, as the object is made. After that a `change` is an ordinary assignment, whether it is written in `init`, in another method or outside the class, and it does not look at the type again.

**Watch out.** Do not count on a `number` field staying a number. If it matters, check the value in the method that changes it.

## The order things happen in

### A loop works from the items it started with

<!-- test: phenomena/loop_snapshot -->
```cuff
note: loop each walks the items it started with
set list xs to [1, 2, 3]
loop each x in xs do:
    if x < 3 do:
        add x + 10 to xs
    end
    print(x)
end
print(xs)

note: a key removed during the loop is still visited, and is gone afterwards
set map m to {"a": 1, "b": 2, "c": 3}
loop each key in m do:
    remove "c" from m
    print(key)
end
print(m)
```

```output
1
2
3
[1, 2, 3, 11, 12]
a
b
c
{"a": 1, "b": 2}
```

**Why.** `loop each` takes the items of the list, or the keys of the map, when the loop begins, and walks that. Adding to the list does not lengthen the loop, and a key removed from the map is still visited. This is what makes it safe to `add` and `remove` while looping.

### An async call that is not awaited waits for the end

<!-- test: phenomena/async_waits -->
```cuff
note: an async call that is not awaited waits until the program's own lines are done
async func hello(str who) do:
    print("hello " + who)
end

print("1")
hello("a")
print("2")
await hello("b")
print("3")
hello("c")
print("4")
```

```output
1
2
hello b
3
4
hello a
hello c
```

**Why.** CuffScript runs one thing at a time. A call to an `async` function that is not awaited is put in a queue, and the queue is emptied, in order, after the last top-level line. `await` runs the call there and then. So the text of a program is not always the order of its output ([the queue](../internals/how-it-works.md#the-async-queue)).

### A default is worked out at every call

<!-- test: phenomena/fresh_defaults -->
```cuff
note: a default is worked out again at every call, so a new list is made each time
func collect(item, list bag to []) do:
    add item to bag
    return bag
end

print(collect(1))
print(collect(2))
print(collect(3, [0]))
print(collect(4))
```

```output
[1]
[2]
[0, 3]
[4]
```

**Why.** The default `[]` is an expression, and it is evaluated again each time a call needs it, so every call gets a new list. In languages that work out a default once, the second call would print `[1, 2]`. A default also cannot see the variables of the program, only the earlier parameters.

## Numbers and operators

### A number prints rounded and compares exactly

<!-- test: phenomena/numbers_print_rounded -->
```cuff
note: a number prints with 15 significant digits, but is compared exactly
print(0.1 + 0.2)
print(0.1 + 0.2 is 0.3)
print(1 / 3)
print(0.1 * 3, 0.1 * 3 is 0.3)

note: so compare after rounding
use DLC:math
print(math_round(0.1 + 0.2, 10) is math_round(0.3, 10))
```

```output
0.3
false
0.333333333333333
0.3 false
true
```

**Why.** A number is a double, as in most languages. It is written out with 15 significant digits, so `0.1 + 0.2` shows as `0.3`, but the stored value is a hair above it, and `is` compares the stored values. This is the same phenomenon as in every language that uses doubles; only the printing hides it more.

**Use it.** Round both sides with `math_round` before comparing numbers that came out of a division or a decimal sum.

### A few operators are not what a calculator suggests

<!-- test: phenomena/operator_surprises -->
```cuff
note: a range counts both ends and runs backwards when it starts higher
loop repeat i to 3 ~ 1 do: print(i) end
loop repeat i to 2 ~ 2 do: print("once", i) end

note: ** binds tighter than a minus on its left, and groups to the right
print(-2 ** 2, (-2) ** 2, 2 ** 3 ** 2)

note: and, or and ! always give true or false, never one of their operands
print("a" or "b", 0 or "x", "a" and "b", 0 and 1)

note: ! is looser than is
print(!1 is 2)
```

```output
3
2
1
once 2
-4 4 512
true true true false
true
```

**Why.** Each follows from the [grammar](../reference/grammar.md): a range includes both ends and counts down when it starts higher, so `2 ~ 2` is one round and `3 ~ 1` is three; a minus in front belongs to the whole power (`-(2 ** 2)`); `and`, `or` and `!` turn their operands into `true` or `false`, so `or` cannot be used to pick a default; and `!` is looser than `is`, so `!1 is 2` means `!(1 is 2)`.

## What is not here

A phenomenon is something that follows from a rule. The rules themselves are in the [language reference](../reference/language.md): that `is` is the only equality, that a map key must be a text, that places count from 1. When a behaviour is listed there as a decision, it is not a phenomenon, however surprising it is at first. [Known limitations](../reference/known-limitations.md) lists what the language does not do.
