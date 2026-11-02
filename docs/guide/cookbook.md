# Cookbook

Short, complete recipes for common jobs. Each one runs as shown, and the output under it is what the documentation checks see. Read the [language tour](language-tour.md) first if a word here is new.

## Numbers and lists

**Sum, average, smallest and largest of a list**

```cuff
use DLC:list
use DLC:math
set list scores to [72, 95, 88, 61]
print(list_sum(scores), list_average(scores))
print(math_min(scores), math_max(scores))
```

```output
316 79
61 95
```

**The same sum with a loop**, when you need to do something to each element on the way:

```cuff
set list prices to [3.5, 2, 10]
set number total to 0
loop each price in prices do:
    change total to total + price
end
print(total)
```

```output
15.5
```

**FizzBuzz**

```cuff
loop repeat n to 1 ~ 15 do:
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
set list squares to []
loop repeat n to 1 ~ 5 do:
    add n * n to squares
end
print(squares)
```

```output
[1, 4, 9, 16, 25]
```

**Keep only the elements that match a test**

```cuff
set list numbers to [4, 9, 16, 25, 36]
set list evens to []
loop each n in numbers do:
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
set list scores to [72, 95, 88, 61, 79]
set list ordered to list_reverse(list_sort(scores))
print(ordered[1~3])
```

```output
[95, 88, 79]
```

**A countdown**, written with a range that counts down:

```cuff
loop repeat i to 3 ~ 1 do:
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
set str word to "stressed"
set str reversed to ""
loop repeat i to length(word) ~ 1 do:
    change reversed to reversed + word[i]
end
print(reversed)
```

```output
desserts
```

**Is it a palindrome?** Compare the text with its reverse, ignoring case:

```cuff
use DLC:string
func is_palindrome(text) do:
    set str clean to str_lower(text)
    set str back to ""
    loop repeat i to length(clean) ~ 1 do:
        change back to back + clean[i]
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
set str sentence to "the quick  brown fox"
set list words to split(sentence, p"[sp]+")
print(length(words), words)
```

```output
4 ["the", "quick", "brown", "fox"]
```

**Word frequencies**, with a map as the counter:

```cuff
use DLC:map
set list words to split("to be or not to be", p"[sp]+")
set map seen to {}
loop each w in words do:
    if map_has_key(seen, w) do:
        change seen[w] to seen[w] + 1
    else do:
        change seen[w] to 1
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
set list names to ["tea", "milk", "bread"]
set list prices to [3, 2.5, 4]
loop repeat i to 1 ~ length(names) do:
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
set list inputs to ["ann@example.com", "not an email", "010-1234-5678"]
loop each text in inputs do:
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
set list found to find_all("3 apples, -2 pears and 10 plums", p"[int]")
print(found)
```

```output
["3", "-2", "10"]
```

`find` gives text, so convert before adding up:

```cuff
set list found to find_all("3 apples, 2 pears and 10 plums", p"[num]+")
set number total to 0
loop each digits in found do:
    change total to total + to_number(digits)
end
print(total)
```

```output
15
```

**Parse `key=value` lines into a map**

```cuff
set str config to "host=localhost\nport=8080\nmode=fast"
set list lines to split(config, p"[nl]")
set map settings to {}
loop each line in lines do:
    set match pair to match(line, p"<key:[word]+>=<value:[any]+>")
    if pair is not empty do:
        change settings[pair["key"]] to pair["value"]
    end
end
print(settings)
```

```output
{"host": "localhost", "port": "8080", "mode": "fast"}
```

**Mask part of a text**

```cuff
set str card to "card 1234-5678-9012-3456"
print(replace(card, p"[num]4-[num]4-[num]4", "****-****-****"))
```

```output
card ****-****-****-3456
```

## Functions

**Return two values** as a list:

```cuff
use DLC:list
func min_max(numbers) do:
    set number low to numbers[1]
    set number high to numbers[1]
    loop repeat i to 2 ~ length(numbers) do:
        if numbers[i] < low do:
            change low to numbers[i]
        end
        if numbers[i] > high do:
            change high to numbers[i]
        end
    end
    return [low, high]
end
set list result to min_max([4, 9, 2, 7])
print(result[1], result[2])
```

```output
2 9
```

**Recursion**

```cuff
func factorial(n) do:
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
set map memo to {}
func fib(n) do:
    global memo
    if n <= 2 do:
        return 1
    end
    set str key to to_str(n)
    if map_has_key(memo, key) do:
        return memo[key]
    end
    set number value to fib(n - 1) + fib(n - 2)
    change memo[key] to value
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
set number first to to_number("12") or_else do:
    change first to 0
end
set number second to to_number("twelve") or_else do:
    change second to 0
end
print(first, second)
```

```output
12 0
```

**Try again a few times**

```cuff
set number attempts to 0
set boolean done to false
loop while !done and attempts < 3 do:
    change attempts to attempts + 1
    set number answer to 10 / (attempts - 2) or_else do:
        print("attempt", attempts, "failed")
        change answer to empty
    end
    if answer is not empty do:
        change done to true
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
set map user to {"name": "Ann"}
set str city to user["city"] or_else do:
    change city to "unknown"
end
print(user["name"], city)
```

```output
Ann unknown
```

**Raise an error of your own**, and look at it in the handler:

```cuff
func parse_age(text) do:
    set number age to to_number(text)
    if age < 0 do:
        fail f"age cannot be negative: {age}"
    end
    return age
end

print(parse_age("-4")) or_else as err do:
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
    private set number balance to 0

    func deposit(number amount) do:
        if amount <= 0 do:
            fail "deposit must be positive"
        end
        change self.balance to self.balance + amount
    end

    func report() do:
        return f"balance: {self.balance}"
    end
end

set Account savings to Account()
savings.deposit(40)
savings.deposit(2.5)
print(savings.report())
savings.deposit(-1) or_else as err do:
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

set list path to [Point(0, 0), Point(2, 1)]
loop each p in path do:
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
    set list items to []
end

set Bag a to Bag()
set Bag b to a.copy()
add "pen" to a.items
add "cup" to b.items
print(a.items, b.items)
```

```output
["pen", "cup"] ["pen", "cup"]
```

(The copy is shallow: both bags still share the one list in `items`, as the output shows.) Copy the list as well when the two should be independent: `change b.items to copy(a.items)`.

**Call several methods on one object** without repeating its name, with `with`:

```cuff
class Robot do:
    set number x to 0

    func right(number n) do:
        change self.x to self.x + n
    end
end

set Robot r to Robot()
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
set list lines to file_readlines("scores.txt")
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
set map data to {"name": "Ann", "scores": [90, 85]}
file_write("data.json", to_json(data, 2))
set map loaded to from_json(file_read("data.json"))
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
set number started to time_now()
set number total to 0
loop repeat i to 1 ~ 100000 do:
    change total to total + i
end
set number elapsed to time_now() - started
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
set number roll to random_int(1, 6)
set list deck to random_shuffle([1, 2, 3, 4, 5])
print(roll >= 1 and roll <= 6, length(deck), list_sum(deck))
```

```output
true 5 15
```
