# Cookbook

Short, complete recipes for common jobs. Each one runs as shown, and the output under it is what the documentation checks see. Read the [language tour](language-tour.md) first if a word here is new.

## Numbers and lists

**Sum, average, smallest and largest of a list**

```cuff
use DLC:list
use DLC:math
list scores = [72, 95, 88, 61]
print(list_sum(scores), list_average(scores))
print(math_min(scores), math_max(scores))
```

```output
316 79
61 95
```

**The same sum with a loop**, when you need to do something to each element on the way:

```cuff
list prices = [3.5, 2, 10]
number total = 0
loop each number price in prices do:
    total = total + price
end
print(total)
```

```output
15.5
```

**FizzBuzz**

```cuff
loop repeat number n = 1 ~ 15 do:
    if n % 15 is 0 do:
        print("FizzBuzz")
    else if n % 3 is 0 do:
        print("Fizz")
    else if n % 5 is 0 do:
        print("Buzz")
    else do:
        print(n)
    end
end
```

```output
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
```

**Build a list of squares**

```cuff
list squares = []
loop repeat number n = 1 ~ 5 do:
    add n * n to squares
end
print(squares)
```

```output
[1, 4, 9, 16, 25]
```

**Keep only the elements that match a test**

```cuff
list numbers = [4, 9, 16, 25, 36]
list evens = []
loop each number n in numbers do:
    if n % 2 is 0 do:
        add n to evens
    end
end
print(evens)
```

```output
[4, 16, 36]
```

**Sort, then show the top three**

```cuff
use DLC:list
list scores = [72, 95, 88, 61, 79]
list ordered = list_reverse(list_sort(scores))
print(ordered[1~3])
```

```output
[95, 88, 79]
```

**A countdown**, written with a range that counts down:

```cuff
loop repeat number i = 3 ~ 1 do:
    print(i)
end
print("go")
```

```output
3
2
1
go
```

## Text

**Reverse a string** (positions are characters, so Hangul and emoji work too):

```cuff
use DLC:string
str word = "stressed"
str reversed = ""
loop repeat number i = length(word) ~ 1 do:
    reversed = reversed + word[i]
end
print(reversed)
```

```output
desserts
```

**Is it a palindrome?** Compare the text with its reverse, ignoring case:

```cuff
use DLC:string
func is_palindrome(str text) do:
    str clean = str_lower(text)
    str back = ""
    loop repeat number i = length(clean) ~ 1 do:
        back = back + clean[i]
    end
    return clean is back
end
print(is_palindrome("Level"), is_palindrome("Cuff"))
```

```output
true false
```

**Count the words in a sentence**

```cuff
use DLC:list
str sentence = "the quick  brown fox"
list words = split(sentence, p"[sp]+")
print(length(words), words)
```

```output
4 ["the", "quick", "brown", "fox"]
```

**Word frequencies**, with a map as the counter:

```cuff
use DLC:map
list words = split("to be or not to be", p"[sp]+")
map seen = {}
loop each str w in words do:
    if map_has_key(seen, w) do:
        seen[w] = seen[w] + 1
    else do:
        seen[w] = 1
    end
end
print(seen)
```

```output
{"to": 2, "be": 2, "or": 1, "not": 1}
```

**Pad columns into a table**

```cuff
use DLC:string
list names = ["tea", "milk", "bread"]
list prices = [3, 2.5, 4]
loop repeat number i = 1 ~ length(names) do:
    print(str_pad_right(names[i], 8, ".") + str_pad_left(to_str(prices[i]), 5))
end
```

```output
tea.....    3
milk....  2.5
bread...    4
```

## Validating and extracting with patterns

**Validate input**

```cuff
list inputs = ["ann@example.com", "not an email", "010-1234-5678"]
loop each str text in inputs do:
    if text matches p"[email]" do:
        print(text, "is an email")
    else if text matches p"[phone]" do:
        print(text, "is a phone number")
    else do:
        print(text, "is neither")
    end
end
```

```output
ann@example.com is an email
not an email is neither
010-1234-5678 is a phone number
```

**Pull every number out of a text**

```cuff
list found = find_all("3 apples, -2 pears and 10 plums", p"[int]")
print(found)
```

```output
["3", "-2", "10"]
```

`find` gives text, so convert before adding up:

```cuff
list found = find_all("3 apples, 2 pears and 10 plums", p"[num]+")
number total = 0
loop each str digits in found do:
    total = total + to_number(digits)
end
print(total)
```

```output
15
```

**Parse `key=value` lines into a map**

```cuff
str config = "host=localhost\nport=8080\nmode=fast"
list lines = split(config, p"[nl]")
map settings = {}
loop each str line in lines do:
    any pair = match(line, p"<key:[word]+>=<value:[any]+>")
    if pair is not empty do:
        settings[pair["key"]] = pair["value"]
    end
end
print(settings)
```

```output
{"host": "localhost", "port": "8080", "mode": "fast"}
```

**Mask part of a text**

```cuff
str card = "card 1234-5678-9012-3456"
print(replace(card, p"[num]4-[num]4-[num]4", "****-****-****"))
```

```output
card ****-****-****-3456
```

## Functions

**Return two values** as a list:

```cuff
use DLC:list
func min_max(list numbers) do:
    number low = numbers[1]
    number high = numbers[1]
    loop repeat number i = 2 ~ length(numbers) do:
        if numbers[i] < low do:
            low = numbers[i]
        end
        if numbers[i] > high do:
            high = numbers[i]
        end
    end
    return [low, high]
end
list result = min_max([4, 9, 2, 7])
print(result[1], result[2])
```

```output
2 9
```

**Recursion**

```cuff
func factorial(number n) do:
    if n <= 1 do:
        return 1
    end
    return n * factorial(n - 1)
end
print(factorial(10))
```

```output
3628800
```

**Remember earlier answers** with a map kept in a global variable:

```cuff
use DLC:map
map memo = {}
func fib(number n) do:
    global memo
    if n <= 2 do:
        return 1
    end
    str key = to_str(n)
    if map_has_key(memo, key) do:
        return memo[key]
    end
    number value = fib(n - 1) + fib(n - 2)
    memo[key] = value
    return value
end
print(fib(50))
```

```output
12586269025
```

## Handling failure

**Use a default when something fails**

```cuff
number first = to_number("12") or_else do:
    first = 0
end
number second = to_number("twelve") or_else do:
    second = 0
end
print(first, second)
```

```output
12 0
```

**Try again a few times**

```cuff
number attempts = 0
boolean done = false
loop while !done and attempts < 3 do:
    attempts = attempts + 1
    number answer = 10 / (attempts - 2) or_else do:
        print("attempt", attempts, "failed")
        answer = empty
    end
    if answer is not empty do:
        done = true
        print("worked on attempt", attempts, "with", answer)
    end
end
```

```output
worked on attempt 1 with -10
```

**Read a key that may be missing**

```cuff
use DLC:map
map user = {"name": "Ann"}
str city = user["city"] or_else do:
    city = "unknown"
end
print(user["name"], city)
```

```output
Ann unknown
```

**Raise an error of your own**, and look at it in the handler:

```cuff
func parse_age(str text) do:
    number age = to_number(text)
    if age < 0 do:
        fail f"age cannot be negative: {age}"
    end
    return age
end

print(parse_age("-4")) or_else as Error err do:
    print(err.message)
end
```

```output
age cannot be negative: -4
```

## Objects

**Keep values and the functions that use them together**, with a class:

```cuff
class Account do:
    private number balance = 0

    func deposit(number amount) do:
        if amount <= 0 do:
            fail "deposit must be positive"
        end
        self.balance = self.balance + amount
    end

    func report() do:
        return f"balance: {self.balance}"
    end
end

Account savings = Account()
savings.deposit(40)
savings.deposit(2.5)
print(savings.report())
savings.deposit(-1) or_else as Error err do:
    print(err.message)
end
```

```output
balance: 42.5
deposit must be positive
```

**Give an object a readable form** with `to_str`; `print` and f-strings use it:

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

list path = [Point(0, 0), Point(2, 1)]
loop each Point p in path do:
    print(p)
end
```

```output
(0, 0)
(2, 1)
```

**Make a separate copy of an object or a list** with `copy`; assigning shares it:

```cuff
class Bag do:
    list items = []
end

Bag a = Bag()
Bag b = a.copy()
add "pen" to a.items
add "cup" to b.items
print(a.items, b.items)
```

```output
["pen", "cup"] ["pen", "cup"]
```

(The copy is shallow: both bags still share the one list in `items`, as the output shows.) Copy the list as well when the two should be independent: `b.items = copy(a.items)`.

**Call several methods on one object** without repeating its name, with `with`:

```cuff
class Robot do:
    number x = 0

    func right(number n) do:
        self.x = self.x + n
    end
end

Robot r = Robot()
with r do:
    right(2)
    right(5)
    print(x)
end
```

```output
7
```

## Files, JSON and time

**Write, read back and remove a file**

```cuff
use DLC:filesystem
use DLC:list
file_write("scores.txt", "ana 31\nben 27\n")
list lines = file_readlines("scores.txt")
print(length(lines), lines[1])
file_remove("scores.txt")
```

```output
2 ana 31
```

**Save data as JSON and load it again**

```cuff
use DLC:json
use DLC:filesystem
map data = {"name": "Ann", "scores": [90, 85]}
file_write("data.json", to_json(data, 2))
map loaded = from_json(file_read("data.json"))
print(loaded["scores"][2], loaded is data)
file_remove("data.json")
```

```output
85 true
```

**Time a piece of work**

```cuff
use DLC:time
use DLC:math
number started = time_now()
number total = 0
loop repeat number i = 1 ~ 100000 do:
    total = total + i
end
number elapsed = time_now() - started
print(total, elapsed >= 0)
```

```output
5000050000 true
```

## Random numbers

**Pick and shuffle** (the numbers differ from run to run; what stays the same is the shape of the result):

```cuff
use DLC:random
use DLC:list
number roll = random_int(1, 6)
list deck = random_shuffle([1, 2, 3, 4, 5])
print(roll >= 1 and roll <= 6, length(deck), list_sum(deck))
```

```output
true 5 15
```
