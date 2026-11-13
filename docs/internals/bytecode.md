# The bytecode machine

A program is not run from its syntax tree. Each function, and each statement of the top level, is compiled into
instructions for a small register machine, and the machine runs them. This page describes what compiled code looks like,
how the compiler decides what a name stands for, and how the machine calls, loops and recovers from errors.
[How it works](how-it-works.md) says what the language rules are; this page says how the engine carries them out.

The code is in three files: `engine/bytecode/Bytecode.h` (the instruction set and what compiled code is),
`engine/bytecode/Compiler.h` (syntax tree to bytecode) and `engine/interpreter/Interpreter.h` (the machine).

## Seeing it

`cuffc --bytecode script.cuff` runs the script and prints every piece of code at the moment the compiler makes it, so
the listing and the program's own output appear in the order things happened:

```cuff
func total(list prices, number tax = 10) do:
    number sum = 0
    loop each number price in prices do:
        if price < 0 do:
            skip
        end
        sum = sum + price
    end
    return sum + sum * tax / 100
end

number shown = total([120, -5, 80])
print(f"total: {shown}")
```

```text
== statement, line 12: 5 registers ==
     0  LoadK       r1 120
     1  LoadK       r2 -5
     2  LoadK       r3 80
     3  NewList     r0 r1 3
     4  Call        r0 1  ; total
     5  ChkDecl     r0  must be number
     6  DeclG       g0 r0 [number]  takes
     7  RetNil
== function total, line 1: 8 registers, 2 parameters ==
     0  ArgJmp      1 ->3
     1  LoadK       r1 10
     2  DefChk      r1 1  must be number
     3  LoadK       r2 0
     4  EachPrep    r4 r0
     5  EachNext    r4 r3 ->10  must be number
     6  JLtK        r3 0 ->8  if false
     7  Jmp         ->5
     8  Add         r2 r2 r3
     9  Jmp         ->5
    10  Mul         r4 r2 r1
    11  DivK        r4 r4 100
    12  Add         r4 r2 r4
    13  Ret         r4
    14  RetNil
== statement, line 13: 3 registers ==
     0  GetG        r1 g0  ; shown
     1  ToStr       r1
     2  Concat      r0 r1
     3  Call        r0 1  ; print
     4  RetNil
total: 220
```

The function is listed after the statement that calls it, because that call is what compiled it. `r` is a register of
the running code, `g` a variable of the top level, `->` the instruction a jump goes to, a bare number or text a constant.
`engine/debug/BytecodePrinter.h` writes the listing from the same table of instructions the machine is built from.

## Pieces of code

The compiler works on one piece at a time, and a compiled piece is a `Proto`: its instructions, its constants, and the
tables the instructions refer to.

| Piece | Compiled | Kept |
| :--- | :--- | :--- |
| A function or method | The first time it is called. A function that is never called is never compiled. | For as long as the interpreter lasts. |
| A statement of the top level | Just before it runs, after every statement above it has run. | Until the statement has finished. |
| The default value of a field | The first time an object of the class is made, and only for a default that is not a literal, `[]` or `{}`. | For as long as the class lasts. |

A top-level statement is compiled late for a reason: by then the compiler can see which variables the statements before
it declared, with which types, and compile a use of one as a direct reference. A loop at the top level is one statement,
so its body is compiled once however often it runs.

## Registers and frames

A running piece of code has a **frame**: a row of **registers**. A register holds a value and, beside it, a 32-bit
**meta** word that says whether the register is a declared variable, whether it is a constant, and which type it was
declared with. The compiler gives a frame its registers in this order:

```text
 r0                                                        r(n-1)
 | self | parameters | variables | block variables | global flags | temporaries |
   ^ methods only      ^ of the function's own scope  ^ one for each `global name`
                                   ^ of `with` bodies and `or_else` handlers
```

Every variable a function declares has its own register for the whole call, so reading or assigning a variable is a
move between registers and involves no table. Temporaries hold the operands of an expression that is being worked out,
the arguments of a call that is being prepared, and a loop's own bookkeeping.

Registers live in large blocks that never move (`Segment`), and frames in blocks of `Frame` records that never move
either, so a pointer to a register or to a frame stays valid while other code runs. A program's variables of the top
level are not registers: they live in a `VarTable`, one slot for each name, in chunks that never move, and compiled code
refers to a slot by its address.

## Instructions

An instruction is sixteen bytes: an operation, sixteen bits of flags `x`, and three operands `a`, `b`, `c`.

```cpp
struct Instr { uint16_t op; uint16_t x; int32_t a, b, c; };
```

A register operand is the register's offset in bytes from the start of the frame, and a constant operand its offset in
the constant table, so the machine reaches either with one addition. A jump operand counts instructions from the one
after the jump. The instruction set is one table, `CUFF_OPCODES` in `Bytecode.h`, which names each instruction and what
its three operands are; the machine's jump table, the printer and the count of instructions are all made from it.

The syntax tree node an instruction came from is kept in a list beside the instructions (`Proto::nodes`). The machine
looks at it only when an instruction fails, to find the position for the error report, and for the few instructions
whose work is described by the node itself.

### Values and variables

| Instruction | Does |
| :--- | :--- |
| `LoadK r k`, `LoadNil r`, `LoadBool r` | Puts a constant, `empty`, or `true`/`false` in a register. |
| `Move r r`, `Take r r` | Copies a register, or moves it and leaves the source empty. |
| `GetG r g`, `GetGC r g` | Reads a variable of the top level; `GetGC` first checks that it has been declared. |
| `SetG g r`, `SetGU g r` | Assigns to one; `SetG` checks the value against the declared type, `SetGU` is used when the compiler has already proved the type. |
| `CheckG g` | The checks an assignment makes before its right-hand side is worked out: the variable exists and is not a constant. |
| `DeclG g r m`, `Decl r r m` | Declares a variable of the top level, or one in a register, with the meta `m`. |
| `ChkDecl r`, `ChkLoop r` | Checks a value against the type of the declaration or loop variable it is about to be stored in. |
| `Store r r t` | Assigns to a variable in a register, checking the type `t` when the compiler could not prove it. |
| `GetName r n` | Finds a variable by name at run time (see [Names](#names)). |
| `SetMeta r m`, `Undecl r i`, `Freeze r` | Marks a register as a declared variable; marks a block's registers as not declared when the block is entered; makes a list read-only for a constant. |
| `PlaceholdG g m`, `PlaceholdL r m` | After a declaration failed and its `or_else` recovered: declares the name `empty`, unless it was already declared. |
| `BindG g r m`, `BindL r r m`, `Bridge r` | Gives a loop variable its value; records that `global name` has run in this call. |

### Operators and jumps

| Instruction | Does |
| :--- | :--- |
| `Add` `Sub` `Mul` `Div` `Mod` `Pow` | `a = b op c` on registers. Two numbers are handled in place; anything else (text joined with `+`, a type error) goes to the general routine. |
| `AddK` ... `PowK` | The same with a number constant on the right, as in `n - 1`. |
| `Neg` `Not` `ToBool` | `-x`, `!x`, and the truth of a value as a boolean. |
| `Lt` `Le` `Gt` `Ge` `Eq` `Ne` `Matches` `Isa` | Comparisons that leave a boolean in a register. |
| `Jmp`, `JmpIf r` | Jumps always, or on the truth of a register. |
| `JLt` `JLe` `JGt` `JGe` `JEq`, and the same with `K` | Compare and jump in one instruction: the condition of an `if` or `loop while` needs no boolean in between. A flag gives the sense (jump if true, or if false); `is not` is `JEq` with the sense turned. |
| `JAnd`, `JOr` | The short circuit of `and` and `or` when the result is wanted as a value. |

A jump that goes back to the start of a loop carries a flag that counts one pass against the step budget.

### Lists, maps, text, objects

| Instruction | Does |
| :--- | :--- |
| `NewList r r i`, `ListAdd r r` | Makes a list from consecutive registers; a long literal is built by adding to it instead. |
| `NewMap r i`, `MapSet r r r`, `ChkKey r` | Makes a map and fills it; checks that a key is a str. |
| `Index r r r`, `Slice r r r` | `x[i]` and `x[a ~ b]`. |
| `ToStr r`, `Concat r r` | An f-string: each embedded value becomes its text, then the pieces are joined once. |
| `GetField r r c`, `SetField r r c` | Reads or assigns a field. `c` is an inline cache (see [Caches](#caches)). |
| `With r r` | Checks that the value of `with` is an object and keeps it for the block. |
| `PlaceChk o`, `PlaceSet r r o`, `CollOp r r o` | The general form of an assignment, and of `add`/`remove`, to a place such as `a.items[2].name`: the checks made before the value is worked out, then the walk down the place and the store. |
| `ListPush r r`, `SetIndex r r r` | The short forms for `add x to xs` and `xs[i] = x` when `xs` is a variable in a register. |

### Calls, loops, statements

| Instruction | Does |
| :--- | :--- |
| `Call r i f` | Calls by name: a user function, else a class (which makes an object), else a native function. The arguments are the `i` registers from `r`, and the result replaces the first of them. |
| `CallM r i c`, `CallS r i`, `CallW r i i` | A method call on the object in `r`, a `super` call, and a call by name inside `with`, where the name may be a method of the object. |
| `AwaitF`, `AwaitM r`, `AwaitS` | What `await` checks before the arguments are worked out: that the function is `async`. |
| `Ret r`, `RetNil` | Returns a value, or `empty`. |
| `ArgJmp i j`, `DefChk r i m` | The start of a function with default values: skips the code of a default whose argument was given; checks a default's value against the parameter's type. |
| `ForPrep r r`, `ForNext r r j` | `loop repeat`: works out both ends once, then steps. |
| `EachPrep r r`, `EachNext r r j` | `loop each`: takes what the loop walks, then hands out one element each pass. |
| `GlobalChk`, `DefFunc`, `Use i`, `Fail r`, `RetChk`, `ErrChk r`, `Raise`, `DynReset i i` | `global` outside a function, a nested `func`, `use`, `fail`, `return value` outside a function, the type of an `or_else as` name, an error the compiler could already see, and the bookkeeping of a `use` inside a block. |

## Names

What a name stands for is the compiler's main decision, and it makes it once, when the code is compiled. For every use
of a name it knows the function's whole text: which scopes there are, which variables each declares and where, and where
`global` and `use` are written. It also follows, statement by statement, which variables are **certainly declared** at
the point it has reached: a declaration counts from the next statement on, a declaration inside an `if` counts inside
that branch but not after it, and what was certain before a loop is what is certain after it.

- **A variable that is certainly declared** is used through its register directly. This is the common case, and it is
  why a loop body costs one instruction for each operation in it.
- **A variable that might not be declared yet**, such as one that an earlier `if` declares, or that has two declarations
  with different types in two blocks, is found at run time by `GetName` or by the general place instructions. The
  compiler has already worked out the chain of places to try, in the language's order: the innermost scope outward,
  the fields of a `with` object where the block has one, then a top-level variable that `global` has linked. The
  machine walks that chain and asks each register's meta whether it is declared. The error for a name that is found
  nowhere is raised by the same walk.
- **A variable of the top level**, read by a top-level statement or by a function after `global name`, is used through
  the address of its slot.

Meta is only kept up to date for the variables that some instruction will ask about. The compiler finds out which those
are while it compiles: it begins by assuming none, and when a use turns out to need a variable's meta it starts the piece
again with that known. Ordinary functions never need a second pass.

The compiler also follows types. When it can prove that a value has the declared type (a number literal, the result of
arithmetic on two `number` variables, a variable assigned to another of the same type), it leaves the check out and lets
the instruction write its result straight into the variable's register. When it cannot, the store carries the type and
the machine checks it. Either way a variable never holds a value of another type than its own.

### What a module adds

`use name from path` copies the variables that a module's top level declared into the scope the statement is in, and
the compiler cannot know their names. A piece of code that contains such a statement is therefore compiled with every
variable's meta kept and every name looked up, and a name the code itself never declares is kept in a list beside the
frame (`dynVars_`). Only that piece of code pays for it.

### Functions that use the program's variables

A function is compiled once, but the variable that `global count` links to may be declared again with another type
later: by a module, or by a later entry in the shell. So when a function is compiled against a top-level variable, the
variable's slot is marked as watched, and declaring it differently marks every such function's code as out of date. The
next call compiles the function again. If the function is running at that moment its old code is kept until it returns.

## Calls

A call puts its arguments in consecutive registers and names the first. For a user function, the machine makes a new
frame **on top of those registers**: the callee's first parameter is the register the caller put the first argument in.
Nothing is copied, and when the callee returns, its result is in the register the caller named. A method call has the
object in the register before the arguments, where the callee finds it as `self`.

The call itself checks, in order: that a `pure` function is not calling one that is not, the types of the arguments
against the parameters, the number of arguments, the call depth, and the step budget. A parameter that was given no argument gets its default from the first instructions of the
function itself, which is why a default is worked out anew in every call, after the parameters before it are bound.

A function call does not use the native stack. The machine is one C++ function with a loop (`Interpreter::runLoop`);
calling makes the callee's frame the current one and continues the loop, and returning goes back to the caller's frame.
How deep a program may recurse is therefore a count of frames ([limits](../reference/limits.md)), and does not depend on
what the function's body looks like.

The loop is entered again from inside itself in four cases, each of which is the engine calling back into a program
while it is in the middle of something: an object's `to_str` run because a value is being shown, the default value of a
field while an object is made, the top level of a module, and a queued `async` call. These do use native stack, and the
stack budget is what bounds them ([resource limits](resource-limits.md)).

## Loops

`loop repeat` keeps three hidden registers: the current number, the last one, and the direction. `ForPrep` fills them
from the two ends, and `ForNext` steps, writes the loop variable and jumps back. `loop each` keeps what it walks (the
list's items, the map's keys or the text) in hidden registers too, taken once by `EachPrep`, which is why changing the
list inside the loop does not change what the loop visits.

`loop while` is compiled with its condition at the bottom, so one pass is the body and one compare-and-jump. `stop` and
`skip` are jumps; the compiler knows the loop they belong to, and one that is in no loop compiles to the instruction that
raises the error.

## Errors and `or_else`

An error is a C++ exception thrown from inside an instruction. The loop catches it and looks at the current frame's
**handler table**: for each `or_else` in the piece of code, the range of instructions it guards, where its handler
begins, and the register for the `Error` object. If the failing instruction is inside a range, the frame continues at
the handler. If not, the frame is left (its registers are released) and the caller's table is asked, and so on down to
the frame the loop was entered for; from there the exception goes on to whoever entered the loop.

Leaving a frame, or catching in it, stamps the error with the source text of that frame's code. An error raised in a
function from another file, or from an earlier entry in the shell, is therefore reported against the text it was written
in, whatever it was doing when it failed.

`stop`, `skip` and `return` in the wrong place are errors that the `or_else` of the same piece of code must not catch
(the statement they would leave is the one being guarded). The machine notes the frame such an error came from and skips
that frame's handlers.

## Caches

Three kinds of instruction remember what they found last time, in a slot of their own beside the code:

- `GetField` and `SetField` remember the class of the object and the position of the field. Every class has a serial
  number, so the test is one comparison; an object of another class, or a private field read from outside, takes the
  general route.
- `CallM` remembers the class and the method it resolved to, with the method's compiled code.
- A call or a name inside `with` remembers whether the name was a member of the object's class.

A class that is declared again in the shell gets a new serial number, so nothing has to be invalidated.

## Rules for changing the machine

- An instruction is added in three places: a line in `CUFF_OPCODES` with its operands, the code that emits it in
  `Compiler.h`, and a `CUFF_VM_CASE` block in `runLoop`. The printer and this page's listing follow from the table. Add
  the instruction to the table above.
- The loop jumps from one instruction to the next through a table of labels on GCC and Clang, and through a `switch`
  elsewhere or with `-DCUFF_VM_SWITCH`. A jump through the table runs no destructors, so no `Value`, `shared_ptr` or
  string may be in scope where `CUFF_VM_NEXT()` is written; such a local goes in an inner block that ends first. Clang
  refuses code that breaks this rule.
- The `catch` blocks of the loop read none of the loop's local variables. Each frame's position is written to the frame
  at every instruction, and the current frame is a member. This is what lets the compiler keep the instruction pointer
  and the frame's base in machine registers.
- A function called from the loop takes at most six arguments, counting `this` and a returned structure. One more is
  passed on the stack, and GCC on Linux then gives the whole loop a frame pointer, which costs it a register.
- Whatever a slow path needs (the node, the position, the call's name) is worked out in the slow path. The fast path of
  an instruction should touch its operands and nothing else.
- A change to what the compiler proves is checked against a build that proves nothing: `-DCUFF_DYNAMIC_NAMES` looks
  every name up at run time, and the whole suite must pass on it ([testing](../contributing/testing.md)).
