# How it works

This page is about what the engine does while a program runs: the few rules that decide when things happen and what a name stands for. They are small, and a good deal of what people notice about CuffScript follows from them; [Phenomena](../guide/phenomena.md) lists the behaviours that do, each with a program and a test. The [architecture](architecture.md) covers the files and the pipeline from text to tree. This page starts where the tree has been built.

## A run in four steps

A program goes through the same four steps every time (`Interpreter::run` and `execProgram`, in `engine/interpreter/Interpreter.h`):

```text
 1. Read     the whole text becomes a tree; nothing has run yet
 2. Collect  every top-level function, then every class, is put in its table
 3. Run      the other top-level statements, one after another, in the order written
 4. Drain    the async calls that nobody awaited run, first come first served
```

- **Step 1** is why a syntax error anywhere, even in a function nobody calls, stops the program before its first line.
- **Step 2** is why a function or a class can be used above the place it is written. It is a pass over the top level of the tree, not a feature of calls. Functions are recorded under their name, one entry per name, so a later function of the same name replaces an earlier one. Classes are built after their parents, whatever the order in the file. A statement such as `set number total to 5` is not a declaration in this sense: it is run in step 3, when its line is reached.
- **Step 3** runs `use` as the statement it is, so a module's functions are collected when execution reaches its line, not before. A module is itself a program, run through the same steps, once.
- **Step 4** is the end of the program text, not the end of the process. If an error ends the run at any step, the queue is emptied without running anything in it.

In the middle of step 3 the engine is simply walking the tree: it evaluates the expression of a statement, does what the statement says, and goes to the next one. There is no compiling and no bytecode.

## Names

There are five tables. A call looks for a user function first, then for a built-in or library function; classes and methods of built-in values have their own lookups.

| Table | Holds | Filled |
| :--- | :--- | :--- |
| User functions | the program's `func`s, and those of the modules it has loaded | step 2, and at each `use` |
| Library functions | what `use DLC:name` makes available | when the `use` line runs |
| Built-in functions | the always-available ones, such as `print` | before the run |
| Classes | every `class`, the built-in `Error` among them | step 2 |
| Methods of built-in values | `"text".upper()` and its kin, each naming the library function it runs | fixed |

A function and a class share one namespace, so one cannot have the name of the other. A user function is looked up before a library function, so a program can replace a library function by writing one of the same name, in the whole program at once. A name is turned into a small number once, when it is read, and the tables are indexed by that number.

## Scopes

A scope is a set of variables. The engine has two kinds:

- A **function scope** is made for each call and starts empty. It is not linked to the caller's scope, and it cannot see the program's variables. The only way in is `global`, which links a name in the function to the program's variable of that name.
- A **block scope** is chained to the scope around it and falls back to it. `with` makes one; the bodies of `if` and the loops do not, they run in the scope they are written in. A variable made in a loop, or in an `if`, therefore stays after it.

A name is looked up in the scope, then outward through block scopes, and stops at the function boundary. Inside `with` there is one step before that: the innermost object's members are tried first, which is why a member hides a variable of the same name inside the block, and `.name` goes straight to the member.

## Calls

Calling a function does these things, in this order: the arguments are evaluated in the caller's scope; a new function scope is made; the parameters are bound to the arguments, and a parameter that was given no argument gets its default, **evaluated now, in the new scope**; the body runs; a `return` ends it and hands back its value, or `empty` if there is none.

A method call is the same, with `self` bound to the object and the class that *wrote* the method remembered next to it. `private` and `super` are checked against that class and not against the class of the object, which is why a child's method cannot read its parent's private field even though the object has it.

An object is made by building its fields, from the oldest ancestor's down, each default evaluated and its type checked, and then running the nearest `init`. The types are not looked at again.

## Values and sharing

A value is a small tagged union: a number (a double), a boolean, `empty`, or a pointer to something bigger. Numbers, booleans and `empty` are copied when they are assigned or passed. A text is shared and never changes, so sharing it is invisible. A list, a map and an object are made once and live behind a counted pointer: assigning one to a second name, passing it to a function, or putting it in another list hands over the pointer, and every holder sees a change.

`copy()` makes one new outer list, map or object and copies the pointers inside it, so anything nested is still shared; `init` is not run again. `is` compares numbers, booleans and texts by value, lists and maps item by item, and objects by whether they are the same one.

The count is how memory is given back. A list that holds itself keeps its own count above zero, so such a structure is never freed while the program runs (see [known limitations](../reference/known-limitations.md)).

## Loops

`loop repeat` computes its two ends once and counts from one to the other, upward or downward, both ends included. `loop while` evaluates its condition before every round.

`loop each` takes what it walks when it begins: a copy of the list's items, a copy of the map's keys, the text's characters. The body then works on that copy, which is why adding to a list inside the loop does not lengthen it, and why a map key that was removed is still visited. Each round binds the loop variable in the scope the loop is in.

## Errors

An error is a C++ exception, a `CuffError`, thrown at the place it is found and caught in one of two places: by an `or_else` that is guarding the statement it passed through, or by the front end, which prints the report and ends the run. Nothing is checked in between, so a function that fails halfway leaves whatever it already changed, changed. Which errors an `or_else` may catch is decided by their code alone ([the error model](error-model.md)): the ones found while the text is read, and the ones that mean a budget ran out, end the program whatever is guarding it.

`or_else` guards one statement, or one block statement with everything inside it. Statements before it have run and stay run.

While it runs, the engine also counts: a step budget and a time budget, if configured, and how much native stack is left. They end a run the same way an error does ([resource limits](resource-limits.md)).

## The async queue

CuffScript runs one thing at a time, and `async` does not change that. Calling an `async` function that is not awaited puts the call, with its arguments already evaluated, at the back of a queue and returns at once. `await` on a call runs it at that point and gives its value. The queue is emptied in step 4, from the front, and a call that queues more calls puts them at the back. So the order of the output is the order of the calls that were awaited or run directly, then the order of the calls that were queued.

## Where to look in the code

| Mechanism | Where |
| :--- | :--- |
| The four steps | `Interpreter::run`, `execProgram`, `drainTaskQueue` |
| Collecting functions and classes | `registerFunction`, `registerClasses` |
| Scopes | `engine/interpreter/Environment.h` |
| Calls and `self` | `callUserFunction`, `FrameGuard` |
| Loops | `execLoop`, `execLoopEach` |
| `with` | `execWith`, `Environment::setWithObject` |
| Values | `engine/interpreter/Value.h` |
| Errors | `engine/common/CuffError.h`, `ErrorCodes.h`, `ErrorFormat.h` |

The decisions behind these, one at a time and with the alternatives that were rejected, are in the [design notes](design-notes.md).
