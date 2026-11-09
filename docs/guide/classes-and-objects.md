# Classes and objects

A program often has several of the same kind of thing: players in a game, accounts in a bank, items in a shop. A **class** describes one kind of thing, and an **object** is one such thing. This
page builds up classes step by step. Every example is run by the documentation checks. The [classes reference](../reference/classes.md) has the exact rules.

## Your first class

A class lists the values an object holds (its *fields*) and what it can do (its *methods*):

```cuff
class Counter do:
    number count = 0

    func bump() do:
        self.count = self.count + 1
    end
end

Counter clicks = Counter()
clicks.bump()
clicks.bump()
print(clicks.count)
```

```output
2
```

- `number count = 0` is a field with a starting value, written like a variable.
- `func bump()` is a method. Inside it, `self` is the object the method was called on, and a field is always written `self.count`.
- `Counter()` makes a new object. Each object has its own fields, so two counters count separately.
- `clicks.bump()` calls a method and `clicks.count` reads a field, both with a dot.

## Giving an object its starting values

A method called `init` runs when an object is made, with the arguments you give. It is where an object gets values that differ from one object to the next. A parameter can have a default:

```cuff
class Player do:
    str name = "unknown"
    number score = 0

    func init(str name, number score = 0) do:
        self.name = name
        self.score = score
    end

    func describe() do:
        return f"{self.name} has {self.score} points"
    end
end

Player ann = Player("Ann", 30)
Player bob = Player("Bob")
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
    str name = ""
    number score = 0

    func init(str name, number score = 0) do:
        self.name = name
        self.score = score
    end
end

list team = [Player("Ann", 30), Player("Bob", 12), Player("Cho", 21)]
number total = 0
loop each Player p in team do:
    total = total + p.score
end
print(total)
team[2].score = team[2].score + 5
print(team[2].score)
```

```output
63
17
```

An assignment, `add` and `remove` accept the same kind of target, however deep: `team[2].score`, `self.items`, `grid[1]`.

## Objects are shared, not copied

Giving an object a second name does not copy it. Both names lead to the same object, as with lists and maps:

```cuff
class Box do:
    list things = []
end

Box a = Box()
Box b = a
add "pen" to b.things
print(a.things)

Box c = a.copy()
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
    str label = "shape"

    func area() do:
        return 0
    end

    func describe() do:
        return f"{self.label} with area {self.area()}"
    end
end

class Square extends Shape do:
    number side = 1

    func init(number side) do:
        self.label = "square"
        self.side = side
    end

    func area() do:
        return self.side * self.side
    end
end

class Circle extends Shape do:
    number radius = 1

    func init(number radius) do:
        self.label = "circle"
        self.radius = radius
    end

    func area() do:
        return 3 * self.radius * self.radius
    end

    func describe() do:
        return "round " + super.describe()
    end
end

list shapes = [Square(2), Circle(1)]
loop each Shape s in shapes do:
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

Shape s = Square()
print(s isa Square, s isa Shape, type_of(s))
```

```output
true true Square
```

## Keeping things private

Put `private` in front of a field or a method that only the class should touch. Code outside the class can then not read or change it, so the class can promise that its values stay consistent:

```cuff
class Wallet do:
    private number cents = 0

    func add_cents(number amount) do:
        if amount < 0 do:
            fail "cannot add a negative amount"
        end
        self.cents = self.cents + amount
    end

    func total() do:
        return self.cents
    end
end

Wallet w = Wallet()
w.add_cents(250)
print(w.total())
print(w.cents) or_else as Error e do:
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
    number x = 0
    number y = 0

    func init(number x, number y) do:
        self.x = x
        self.y = y
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
    number x = 0
    number y = 0

    func right(number n) do:
        self.x = self.x + n
    end

    func up(number n) do:
        self.y = self.y + n
    end
end

Turtle t = Turtle()
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
    str item = ""
end

func take(str item, number wanted, number have) do:
    if wanted > have do:
        OutOfStock problem = OutOfStock(f"not enough {item}")
        problem.item = item
        fail problem
    end
    return have - wanted
end

take("tea", 5, 2) or_else as Error err do:
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
