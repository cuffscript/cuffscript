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
with must still let plain recursion reach the call depth limit, and a pattern at the nesting limit must fit in the pattern
size limit.

## The native stack

The evaluator is recursive, so deep recursion in a script is deep recursion on the native stack. Two limits guard it, and
they are independent:

- **Call depth** counts active calls and raises `E4-017` at a fixed number. Plain recursion reaches it.
- **Stack budget** counts the bytes of native stack in use and raises `E4-017` when they pass the budget. A function with a
  large body uses more stack per call, so it meets the budget first. Measured on a release build, a plain call costs about
  1.3 KiB and a function whose body has twenty nested `if`s about 8.6 KiB; the budget is what keeps the second kind of
  program from overflowing.

`Interpreter::planStack(configured, available)` decides the budget:

| Situation | Result |
| :--- | :--- |
| Nothing configured, the real stack size is known | Everything but a safety margin, up to a ceiling. |
| Nothing configured, the size is unknown | The default budget. |
| A budget is configured and the thread can back it | The configured budget. |
| A budget is configured that the thread **cannot** back | Reduced to what exists. Asking for more than there is must not turn a runaway recursion into a crash. |

The plan has two levels. `soft` is where the evaluator gives up with a catchable error; `hard` sits a few MiB below it
and is the floor under which even leaf work, such as a regular-expression match, refuses to start. So a program that
is near `soft` when it reaches something heavy still fails with an error. The parser has the same kind of floor, derived
from the same measurement, so a deeply nested source is refused with `E2-008` when the stack would run out before the
nesting limit.

The tests run the interpreter on a real thread with a small stack, with a far larger budget requested, and check that the
outcome is `E4-017`, not a crash (`tests/unit/limits_test.cpp`).

## Long chains use no stack

`a + b + c + ...`, `x[1][2][3]...` and `s[1~5][2~4]...` lean to the left: each node's first operand is the next node of the
chain. Evaluating one by recursion would use stack in proportion to its length, so a chain the parser accepted could still
fail to run, and which length failed would depend on the platform. Above a small height, `Interpreter::evalSpine` walks the
chain instead: it collects the nodes, evaluates the innermost operand, and applies each node's operation from the inside
out, in the same left-to-right order the recursive form uses. Results, errors and error locations are identical.

A deep tree also has to be **freed**, and the default destructor recurses once per node. `Expr::~Expr` therefore takes a
tall tree apart iteratively, moving each node's children onto a work list. Without it, a chain at the expression limit
overflowed a 256 KiB stack when it was merely destroyed. (`ChildTaker` in `ASTNodes.h` lists the children of every kind of
node, next to `ChildHeight`; a new kind of node needs a line in both.)

With both in place, the expression limit no longer depends on the stack. It bounds the memory and time one expression can
cost, and the recursion in `--ast`, which is a debugging aid.

## Memory

Strings, lists, maps, JSON text, pattern sizes and the `async` queue each have a size limit. The engine does not count
total memory: a host that needs to must set a limit on the process. A `std::bad_alloc` that reaches the front end is
reported as `E6-003`, not a crash.

Two shapes are never freed: a list or map that contains itself (reference counts cannot reach zero), and the
process-wide table of identifier names. [Design notes](design-notes.md) records the first; `NameInterner.h` documents the
second.

## Adding a limit

1. Add the constant to `Limits.h` with an `@limit` block (title, error code, one or two sentences).
2. Raise the error where the limit is crossed, with the code you named. A limit that cannot be crossed by a test is a
   limit that does not work.
3. If it relates to another limit, say how with a `static_assert`.
4. Add a case to `tests/unit/limits_test.cpp` for the value just under the limit and the value just over it.
5. Run `python3 tools/docs.py generate`.
