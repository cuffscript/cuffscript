# Classes and objects

A program often has several of the same kind of thing: players in a game, accounts in a bank, items in a shop. A **class** describes one kind of thing, and an **object** is one such thing. This
page builds up classes step by step. Every example is run by the documentation checks. The [classes reference](../reference/classes.md) has the exact rules.

## Your first class

A class lists the values an object holds (its *fields*) and what it can do (its *methods*):

```cuff
class Counter do:
    set number count to 0

    func bump() do:
        change self.count to self.count + 1
    end
end

set Counter clicks to Counter()
clicks.bump()
clicks.bump()
print(clicks.count)
```

```output
2
```

- `set number count to 0` is a field with a starting value, written like a variable.
- `func bump()` is a method. Inside it, `self` is the object the method was called on, and a field is always written `self.count`.
- `Counter()` makes a new object. Each object has its own fields, so two counters count separately.
- `clicks.bump()` calls a method and `clicks.count` reads a field, both with a dot.

## Giving an object its starting values

A method called `init` runs when an object is made, with the arguments you give. It is where an object gets values that differ from one object to the next. A parameter can have a default:

```cuff
class Player do:
    set str name to "unknown"
    set number score to 0

    func init(str name, number score to 0) do:
        change self.name to name
        change self.score to score
    end

    func describe() do:
        return f"{self.name} has {self.score} points"
    end
end

set Player ann to Player("Ann", 30)
set Player bob to Player("Bob")
print(ann.describe())
print(bob.describe())
```

```output
Ann has 30 points
Bob has 0 points
```

A field you spell wrongly is an error, not a new field: `ann.scroe` fails with a message that lists the fields the class has. That is the reason a class declares its fields up front.

## Several objects, one list

An object can go anywhere a value can: in a list, in a map, in a field, as an argument.

```cuff
class Player do:
    set str name to ""
    set number score to 0

    func init(str name, number score to 0) do:
        change self.name to name
        change self.score to score
    end
end

set list team to [Player("Ann", 30), Player("Bob", 12), Player("Cho", 21)]
set number total to 0
loop each p in team do:
    change total to total + p.score
end
print(total)
change team[2].score to team[2].score + 5
print(team[2].score)
```

```output
63
17
```

`add`, `change` and `remove` accept the same kind of target, however deep: `team[2].score`, `self.items`, `grid[1]`.

## Objects are shared, not copied

Giving an object a second name does not copy it. Both names lead to the same object, as with lists and maps:

```cuff
class Box do:
    set list things to []
end

set Box a to Box()
set Box b to a
add "pen" to b.things
print(a.things)

set Box c to a.copy()
add "cup" to c.things
print(a.things, c.things)
```

```output
["pen"]
["pen", "cup"] ["pen", "cup"]
```

`copy()` makes a new object with the same field values, but a list inside it is still the same list: the copy is shallow. `is` asks whether two names lead to the same object; it never compares fields.

## Building on a class

A class can `extend` another to reuse it. The child has everything its parent has, and can add more or replace a method. `super` calls the parent's version:

```cuff
class Shape do:
    set str label to "shape"

    func area() do:
        return 0
    end

    func describe() do:
        return f"{self.label} with area {self.area()}"
    end
end

class Square extends Shape do:
    set number side to 1

    func init(number side) do:
        change self.label to "square"
        change self.side to side
    end

    func area() do:
        return self.side * self.side
    end
end

class Circle extends Shape do:
    set number radius to 1

    func init(number radius) do:
        change self.label to "circle"
        change self.radius to radius
    end

    func area() do:
        return 3 * self.radius * self.radius
    end

    func describe() do:
        return "round " + super.describe()
    end
end

set list shapes to [Square(2), Circle(1)]
loop each s in shapes do:
    print(s.describe())
end
```

```output
square with area 4
round circle with area 3
```

`describe` is written once, in `Shape`, and calls `self.area()`; each child supplies its own `area`. When a child writes its own `init`, the parent's `init` is not called for it: call `super.init(...)` if you want it.

A variable declared with a class type accepts that class and its children, and `isa` tells which one an object is:

```cuff
class Shape do:
end

class Square extends Shape do:
end

set Shape s to Square()
print(s isa Square, s isa Shape, type_of(s))
```

```output
true true Square
```

## Keeping things private

Put `private` in front of a field or a method that only the class should touch. Code outside the class can then not read or change it, so the class can promise that its values stay consistent:

```cuff
class Wallet do:
    private set number cents to 0

    func add_cents(number amount) do:
        if amount < 0 do:
            fail "cannot add a negative amount"
        end
        change self.cents to self.cents + amount
    end

    func total() do:
        return self.cents
    end
end

set Wallet w to Wallet()
w.add_cents(250)
print(w.total())
print(w.cents) or_else as e do:
    print(e.code)
end
```

```output
250
E4-034
```

## Showing an object

`print` shows an object as `<Wallet>`. To show something more useful, give the class a `to_str` method; `print`, f-strings and `to_str(object)` all use it:

```cuff
class Point do:
    set number x to 0
    set number y to 0

    func init(number x, number y) do:
        change self.x to x
        change self.y to y
    end

    func to_str() do:
        return f"({self.x}, {self.y})"
    end
end

print(Point(3, 4))
print(f"the corner is {Point(0, 0)}")
```

```output
(3, 4)
the corner is (0, 0)
```

## Using one object for a while

When a block works on one object, `with` saves repeating its name. Inside the block, a method call or a field name is looked up in the object first:

```cuff
class Turtle do:
    set number x to 0
    set number y to 0

    func right(number n) do:
        change self.x to self.x + n
    end

    func up(number n) do:
        change self.y to self.y + n
    end
end

set Turtle t to Turtle()
with t do:
    right(3)
    up(2)
    right(1)
    print(x, y)
end
```

```output
4 2
```

Because the object's names win, `.x` is there for the cases where you want to say so out loud: `.x` always means the object's `x`.

## Raising and catching errors

`fail` raises an error, and `or_else as` catches it as an object with a `message`, a `code` and the line where it happened. A class that extends `Error` can carry more:

```cuff
class OutOfStock extends Error do:
    set str item to ""
end

func take(str item, number wanted, number have) do:
    if wanted > have do:
        set OutOfStock problem to OutOfStock(f"not enough {item}")
        change problem.item to item
        fail problem
    end
    return have - wanted
end

take("tea", 5, 2) or_else as err do:
    if err isa OutOfStock do:
        print("restock", err.item)
    else do:
        print("something else:", err.message)
    end
end
```

```output
restock tea
```

The same handler catches the errors the engine raises, such as a division by zero (`err.code` is `E4-006`). See [Errors and debugging](errors-and-debugging.md) for the whole story.

## Where to go next

- The [classes reference](../reference/classes.md) has every rule, including how objects turn into JSON and how `async` methods work.
- [`examples/10_classes`](../../examples/README.md) to `12_with_block`, `08_errors_as_objects` and the practical programs `17_inventory` and `18_bank_account` are complete programs that use everything on this page.
