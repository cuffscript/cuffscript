# Classes and objects

A class describes a kind of thing: the values it holds (its *fields*) and what it can do (its *methods*). An *object* is one thing of that kind. This page is the reference for classes, objects, the
`with` block and error objects; the [guide](../guide/classes-and-objects.md) teaches them step by step, and the [grammar](grammar.md) lists the exact shapes.

## Declaring a class

```cuff
class Player do:
    set str name to "unknown"
    set number x to 0
    private set number steps to 0

    func init(str name, number x to 0) do:
        change self.name to name
        change self.x to x
    end

    func sayhello() do:
        print(f"Hello, I am {self.name}")
    end

    func turnleft() do:
        change self.steps to self.steps + 1
    end
end

set Player player1 to Player("Ann")
player1.sayhello()
```

```output
Hello, I am Ann
```

- A class is declared with `class NAME do: ... end`. Its body holds field declarations and method declarations, and nothing else.
- A class name starts with a capital letter (`E4-039` otherwise).
- A class is declared at the top level of a file, never inside a block or a function (`E4-036`). A class declared in a module is visible to the whole program once the module is loaded with `use`.
- Classes and functions share one namespace: a class cannot have the name of a function, of another class or of the built-in `Error` (`E4-038`).
- Like functions, classes exist from the start of the program: a class may be used above the place where it is written, and `extends` may name a class that is declared further down.

## Fields

A field is declared with the same words as a variable: `set [constant] TYPE NAME to value`.

- The default value is worked out **again for every object**, so each object gets its own list or map. The default cannot use `self`.
- The set of fields is fixed. Reading or writing a field the class does not have is an error that names the fields it does have (`E4-033`), which catches a misspelt name.
- The declared type is checked on the default value only. `change` does not check the type of the new value, in a field or in a variable.
- A `constant` field is written in capitals like any constant (`E4-004` otherwise). It can be set while `init` runs and cannot be changed afterwards (`E4-003`).
- A field is read and written with a dot: `player1.name`, `change player1.x to 5`. Inside a method it is always reached through `self`.
- Fields are places, so `add`, `change` and `remove` work on them and on what they hold: `add item to self.items`, `change self.grid[2][3] to 0`.

## Methods

A method is a function declared in a class body. It follows the rules of functions: it sees only its own variables and the ones linked with `global`, and `return` works anywhere. In addition:

- The object the method was called on is `self`. A field is always written `self.name`; a bare `name` in a method is a variable of the method.
- A method is called with a dot and parentheses: `player1.sayhello()`. A method is not a value, so `player1.sayhello` without the parentheses is an error (`E4-040`), and so is calling a field.
- A method can be `async` (see [async methods](#async-methods)) or `pure`. A `pure` method may read fields and call other `pure` functions and methods; it cannot change a field (`E4-041`) or call a function that is not `pure` (`E4-029`).
- Calls to a method count against the step budget and the recursion limit like calls to any function.

## Making an object

A class is used like a function: `Player("Ann")`. There is no `new`.

1. The object is allocated.
2. The default of every field is worked out, from the oldest ancestor down to the class itself.
3. The nearest `init` runs with the arguments of the call.

A class without an `init` takes no arguments (`E4-010` otherwise). If `init` raises an error, no object is made. `init` is a method like any other; its parameters may have defaults, and its return value is not used.

## Inheritance

`class Hero extends Player do: ... end` makes `Hero` a *child* of `Player`. A child has every field and method of its parent and of the parent's parent, and so on.

```cuff
class Animal do:
    set str name to "animal"

    func init(str name) do:
        change self.name to name
    end

    func speak() do:
        print(f"{self.name} makes a sound")
    end
end

class Dog extends Animal do:
    set number tricks to 0

    func init(str name, number tricks to 0) do:
        super.init(name)
        change self.tricks to tricks
    end

    func speak() do:
        super.speak()
        print(f"{self.name} barks")
    end
end

set Dog rex to Dog("Rex", 3)
rex.speak()
print(rex.tricks)
```

```output
Rex makes a sound
Rex barks
3
```

- A method of the same name in the child **replaces** the parent's for objects of the child. There is no `override` word.
- `super.name(arguments)` calls the parent's version on the same object. It is only available in a class that has a parent, and only in a method.
- A child that does not define `init` uses its parent's. A child that defines `init` does not get the parent's automatically: call it with `super.init(...)` when it is needed.
- A child cannot declare a field or method with the name of one of its fields.
- A class cannot extend itself, directly or through its ancestors, and can only extend a class (`E4-032`). The depth of inheritance and the number of members are limited (see [limits](limits.md)).

## Private members

`private` in front of a field or a method hides it outside the class that declares it:

```cuff
class Account do:
    private set number balance to 0

    func deposit(number amount) do:
        change self.balance to self.balance + amount
    end

    func report() do:
        return f"balance: {self.balance}"
    end
end

set Account a to Account()
a.deposit(40)
print(a.report())
```

```output
balance: 40
```

Writing `a.balance` outside the class is an error (`E4-034`).

- A private member can be used only in the methods of the class that declares it. A child class cannot see the private members of its parent, but a method the child inherits runs as part of the parent and can.
- Two objects of one class can use each other's private members.
- A private field is left out of the JSON text of an object and of the way `print` shows one.
- There is no `protected`.

## Types and `isa`

A class name can be written wherever a type can: in `set Player p to ...`, in a parameter (`func f(Player p)`) and in a field. A value of a child class is accepted where its parent is wanted. A mismatch is `E4-020` for a variable and `E4-030` for a parameter. The type `object` accepts any object.

`value isa ClassName` tells whether an object is an instance of the class or of one of its descendants. It compares like `is` does, so `!` negates it: `!animal isa Dog`. `type_of(object)` is the name of its class, as text.

```cuff
class Animal do:
end

class Dog extends Animal do:
end

set Animal pet to Dog()
print(pet isa Animal, pet isa Dog, type_of(pet))
print(1 isa Animal, Animal() isa Dog)
```

```output
true true Dog
false false
```

A name after `isa` that is not a class is `E4-031`.

The names of the types (`number`, `str`, `list`, `map`, `boolean`, `match`, `pattern`, `object`, `empty`) and of the classes are read as types only where a type may stand. Anywhere else they are ordinary names, so a variable may be called `list` or `pattern`.

## Objects are values

An object is held by reference, like a list or a map: giving it a second name does not copy it, and a change through either name is seen through both.

- `is` on two objects asks whether they are the *same* object, never whether their fields are equal.
- Every object has a `copy()` method that makes a new object of the same class with the same field values. The copy is shallow: a field that holds a list or an object is shared between the two.
- A list or a map that holds objects behaves the same way. `copy()` also works on lists and maps.
- Objects are freed when nothing refers to them any more. Two objects that refer to each other are not freed before the program ends (see [known limitations](known-limitations.md)).

```cuff
class Point do:
    set number x to 0
    set number y to 0
end

set Point a to Point()
set Point b to a
set Point c to a.copy()
change b.x to 5
print(a.x, c.x, a is b, a is c)
```

```output
5 0 true false
```

## Showing an object

`print`, an f-string and `to_str(object)` use the object's `to_str()` method when its class has one, and `<ClassName>` when it does not. The method must return a string. The fields are not shown by default: write a `to_str` for that.

```cuff
class Point do:
    set number x to 0
    set number y to 0

    func to_str() do:
        return f"({self.x}, {self.y})"
    end
end

class Blank do:
end

print(Point(), f"at {Point()}", Blank())
```

```output
(0, 0) at (0, 0) <Blank>
```

`init` and `to_str` are the two method names the language gives a meaning to. `copy` is built in for every object.

## JSON

`to_json(object)` writes the **public** fields in the order they were declared, the parent's first, with nested objects, lists and maps written the same way. Methods are not written. `from_json` always gives back a map: build an object from it in your own code.

```cuff
use DLC: json

class Item do:
    set str name to "pen"
    private set number cost to 3
end

class Box extends Item do:
    set list tags to ["new"]
end

print(to_json(Box()))
```

```output
{"name":"pen","tags":["new"]}
```

## Async methods

An `async` method is queued like an `async` function: the call returns at once and the body runs later, with the object it was called on. `await object.method()` runs it now and gives its result.

```cuff
class Job do:
    set str label to "job"

    async func run(number n) do:
        print(f"{self.label} {n}")
        return n * 2
    end
end

set Job j to Job()
j.run(1)
print("queued")
print(await j.run(2))
```

```output
queued
job 2
4
job 1
```

The calls that were queued and not awaited run when the main program ends.

## The `with` block

`with object do: ... end` lets a block use the members of one object without writing its name each time.

```cuff
class Robot do:
    set str name to "R2"
    set number y to 0

    func move(number steps) do:
        change self.y to self.y + steps
    end

    func status() do:
        return f"{self.name} at {self.y}"
    end
end

set Robot rover to Robot()

with rover do:
    move(3)
    move(2)
    print(status())
    change y to y + 10
    print(.y)
end
```

```output
R2 at 5
15
```

- The object is worked out once, when the block starts, and must be an object (`E4-035`).
- A name is looked up in this order: a variable made inside the block, then a member of the object (a method when it is called, a field otherwise), then the rest of the program as usual. Members come before the outer names on purpose: what a name means is decided by the class, and does not change when a variable of that name appears elsewhere.
- The same order applies to the first name of a place, so `change y to y + 10` changes the field `y` here. `add` and `remove` follow it as well.
- `.name` always means a member of the object of the innermost `with`: `.y` above. Use it when a plain name could mean something else. It is a syntax error outside a `with` block.
- With blocks nest. A name is looked for in the inner object first, then in the next one out.
- A variable made with `set` inside the block is gone after the `end`. `return`, `stop`, `skip`, `await` and `or_else` work through a block as if it were not there.
- Inside a method, `self` is still the object the method was called on, not the object of a `with` inside it.
- A private member stays private: `with` does not open it (`E4-034`).

`player1.sayhello()` and `with player1 do: sayhello() end` do the same thing.

## Errors are objects

The built-in class `Error` describes what went wrong. Its fields are all public:

| Field | Type | Holds |
| :--- | :--- | :--- |
| `message` | `str` | what went wrong |
| `code` | `str` | the error code, such as `E4-006`, or `E7-001` for an error raised with `fail` |
| `line`, `column` | `number` | where it happened (`0` for an error that was never raised) |
| `hint` | `str` | the suggestion the engine gave, or an empty string |

`Error("text")` makes one, and its `to_str()` is `[code] message`. A class can extend it to carry more.

### Raising an error

`fail value` raises an error. The value is an `Error` object, or a string, which is wrapped as `Error(string)`. Any other value is an error (`E4-005`). The error is of the category `E7` ("Program errors"); it can be caught like the errors the engine raises, and when nothing catches it the program stops with:

```text
ERROR: [E7-001] Program Error at line 3, column 1
  |
3 | fail "negative amount"
  | ^^^^ negative amount
```

An error object of a class of your own shows the name of its class before the message.

### Catching an error

`or_else as NAME do: ... end` runs its block when the statement before it fails, with the error in the variable `NAME`. Every error the engine can raise and that a program may catch (the `E3-1xx`, `E4`, `E5` and `E7` codes) arrives as an `Error`, so a handler can look at `code`, `message`, `line` and `column`. The errors that cannot be caught (the syntax errors, the budget errors and the internal errors) are not affected. `or_else do:` without a name works as before.

```cuff
class ValidationError extends Error do:
    set str field to ""
end

func check_age(number age) do:
    if age < 0 do:
        set ValidationError e to ValidationError("age is negative")
        change e.field to "age"
        fail e
    end
    return age
end

check_age(-1) or_else as err do:
    if err isa ValidationError do:
        print(f"bad {err.field}: {err.message}")
    end
end

print(1 / 0) or_else as err do:
    print(err.code, err.line)
end
```

```output
bad age: age is negative
E4-006 20
```

There is no separate branch for each kind of error: test with `isa` or compare `code`. `fail err` inside a handler raises the error again, and an error object keeps its class on the way. A handler is one block for the one statement before it; for more than one statement, put them in a function and protect the call.

## Errors and limits

The error codes for the object model are listed in [errors](errors.md) (`E4-031` to `E4-041` and `E7-001`). The limits on inheritance depth, members per class and the number of classes are in [limits](limits.md).
