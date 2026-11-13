# Resource limits

A script can be written by anyone, so the engine promises the host something stronger than "usually works": **no script
can crash the process.** Everything a script can make unboundedly large or deep is bounded, and exceeding a bound is an
ordinary error. This page explains how that is arranged. The values themselves are in [limits](../reference/limits.md).

## One home for every number

All limits are constants in `engine/common/Limits.h`, each with a comment block that says what it protects and which
error it raises. The reference page is generated from those comments, and no other document writes a limit's value (a
check fails if one does). To change a limit, change it there and run `python3 tools/docs.py generate`.

Limits that depend on each other are tied together by `static_assert`s at the end of the file, so that changing one
number cannot silently break a guarantee another one makes. For example: the smallest stack budget the engine can end up
with must still hold as many nested `to_str` calls as the call depth limit allows, and a pattern at the nesting limit must
fit in the pattern size limit.

## Calls and the native stack

A program's own calls do not use the native stack. A call makes a frame in the bytecode machine, on memory the machine
manages, and the machine is a loop that does not call itself for it ([the bytecode machine](bytecode.md)). Two limits
follow, and they guard different things:

- **Call depth** counts the calls that are active and raises `E4-017` at a fixed number. It is the limit that ends runaway
  recursion, and it is reached at the same depth by every function: a body with twenty nested blocks costs a deeper frame,
  not more native stack.
- **Stack budget** bounds the native stack that the engine itself uses, and raises `E4-017` when the stack in use passes
  it. What draws on it is work that nests *inside* the engine: the compiler following nested blocks; an object's `to_str`
  that the engine runs because a value is being shown, which may show another object; the default value of a field, which
  may make another object; a module that loads a module. Measured on a release build, one level of nested `to_str` costs
  about 1.1 KiB and one level of nested default values about 1.6 KiB. A default value that makes an object of its own
  class nests without end, and the budget is what turns that into an error.

`Interpreter::planStack(configured, available)` decides the budget:

| Situation | Result |
| :--- | :--- |
| Nothing configured, the real stack size is known | Everything but a safety margin, up to a ceiling. |
| Nothing configured, the size is unknown | The default budget. |
| A budget is configured and the thread can back it | The configured budget. |
| A budget is configured that the thread **cannot** back | Reduced to what exists. Asking for more than there is must not turn runaway nesting into a crash. |

The real stack is measured by `availableStackBytes()` (`engine/common/Attributes.h`): the distance from the current frame
to the lowest address of the thread's stack, as the platform reports it (`pthread_getattr_np` on Linux,
`pthread_get_stackaddr_np` and `pthread_get_stacksize_np` on macOS, `GetCurrentThreadStackLimits` on Windows, and
`emscripten_stack_get_free` in WebAssembly). The parser asks at the start of every parse and the interpreter at the start
of every run, so `cuffc hello.cuff` asks three times and `cuffsh` at least twice for every entry. For the first thread of
a process, glibc and Bionic find the answer by reading `/proc/self/maps` and taking the stack limit (`RLIMIT_STACK`) into
account, which with glibc costs about 125,000 instructions. The lowest address is therefore kept for each thread and
asked for again only when the stack limit is no longer the one it was found with. Other C libraries are asked every time:
musl measures the first thread's stack by how far it has grown so far, an answer that changes as it grows.

The plan has two levels. `soft` is where the engine gives up with a catchable error; `hard` sits a few MiB below it
and is the floor under which even leaf work, such as a regular-expression match, refuses to start. So a program that
is near `soft` when it reaches something heavy still fails with an error. The parser has the same kind of floor, derived
from the same measurement, so a deeply nested source is refused with `E2-008` when the stack would run out before the
nesting limit.

The tests run the interpreter on a real thread with a small stack, with a far larger budget requested, and check that
endless nesting ends in `E4-017`, not a crash, and that recursion to the call depth limit succeeds on a thread whose
stack is far too small to recurse on natively (`tests/unit/limits_test.cpp`).

The memory a frame takes is bounded with the calls: registers are handed out from blocks that grow as the program goes
deeper and are reused when it comes back, and the call depth limit bounds how many frames there can be.

## Long chains use no stack

`a + b + c + ...`, `x[1][2][3]...` and `s[1~5][2~4]...` lean to the left: each node's first operand is the next node of the
chain. Compiling one by recursion would use stack in proportion to its length, so a chain the parser accepted could still
fail to compile, and which length failed would depend on the platform. `Compiler::gen` therefore walks the chain instead:
it collects the nodes, compiles the innermost operand, and emits each node's instruction from the inside out, which is the
left-to-right order the operations run in. The chain's intermediate results reuse one register, so its length costs
instructions and no registers.

A deep tree also has to be **freed**, and the default destructor recurses once per node. `Expr::~Expr` therefore takes a
tall tree apart iteratively, moving each node's children onto a work list. Without it, a chain at the expression limit
overflowed a 256 KiB stack when it was merely destroyed. (`ChildTaker` in `ASTNodes.h` lists the children of every kind of
node, next to `ChildHeight`; a new kind of node needs a line in both.)

With both in place, the expression limit no longer depends on the stack. It bounds the memory and time one expression can
cost, and the recursion in `--ast`, which is a debugging aid.

## Memory

Strings, lists, maps, JSON text, pattern sizes and the `async` queue each have a size limit, and so do the shapes of a class: the depth of inheritance, the members of a class and the number of classes a program declares. A method call counts against the call depth like any call, and so does making an object of a class that has an `init`, so recursion through methods and constructors is bounded the same way. The engine does not count
total memory: a host that needs to must set a limit on the process. A `std::bad_alloc` that reaches the front end is
reported as `E6-003`, not a crash.

Two shapes are never freed: a list, map or object that refers to itself, directly or through others (reference counts cannot
reach zero), and the process-wide table of identifier names. [Design notes](design-notes.md) records the first; `NameInterner.h` documents the
second.

## Adding a limit

1. Add the constant to `Limits.h` with an `@limit` block (title, error code, one or two sentences).
2. Raise the error where the limit is crossed, with the code you named. A limit that cannot be crossed by a test is a
   limit that does not work.
3. If it relates to another limit, say how with a `static_assert`.
4. Add a case to `tests/unit/limits_test.cpp` for the value just under the limit and the value just over it.
5. Run `python3 tools/docs.py generate`.
