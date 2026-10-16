# Limits

CuffScript is built to run code you did not write, so everything a program can make unboundedly large or
deep has a limit. Exceeding one is never a crash: it is an ordinary [error](errors.md) with a code, which
a script can handle with `or_else` where the error is catchable.

The values below are the engine's defaults. They are defined in `engine/common/Limits.h`, and this page is
generated from that file, so it is always the current truth.

<!-- BEGIN GENERATED: limits-table -->
_This table is generated from `engine/common/Limits.h` by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._

| Limit | Value | Raises | What it is |
| :--- | ---: | :--- | :--- |
| Source size | 16 MiB | [`E2-009`](errors.md#e2-009) | The largest program, module file or shell entry. Each file is measured on its own. |
| Nesting depth | 512 | [`E2-008`](errors.md#e2-008) | How deeply code may nest: blocks, parentheses, brackets, call arguments. The parser recurses on the native stack, so this also stops hostile input from exhausting it. It is checked against the stack that is really available as well, so on a very small stack it can trigger sooner. |
| Expression length | 10,000 | [`E2-008`](errors.md#e2-008) | How tall one expression's tree may be: the operators, indexes and slices in one chain such as `a + b + c`, or the levels of nested expressions. Long chains are evaluated and freed without recursion, so this does not depend on the stack; it bounds the memory and time one expression can cost. |
| Call depth | 1,000 | [`E4-017`](errors.md#e4-017) | How many function calls may be active at once. Independently, the stack budget below stops recursion whose frames are large; whichever is reached first raises the same error. |
| Stack budget ceiling | 64 MiB |  | The most native stack the evaluator may use, however much the host offers or an embedder asks for. |
| Default stack budget | 4 MiB | [`E4-017`](errors.md#e4-017) | The evaluator's stack budget when the real stack size cannot be determined (8 MiB in WebAssembly). When it can, the budget is derived from it instead: everything but a safety margin, up to the ceiling above, and never more than the thread really has even if a larger budget is configured. |
| String size | 128 MiB | [`E4-026`](errors.md#e4-026) | The longest string a program can build. |
| List and map size | 32 Mi (33,554,432) | [`E4-026`](errors.md#e4-026) | The most elements one list or map can hold. |
| Value nesting in output | 1,000 |  | How deeply nested lists and maps are written out when printed or converted to text; anything deeper is shown as `[...]` or `{...}`. |
| JSON nesting | 200 | [`E4-025`](errors.md#e4-025) | How deeply nested the text `from_json` reads, or the value `to_json` writes, may be. |
| Queued async calls | 1,000,000 | [`E4-026`](errors.md#e4-026) | How many `async` calls may wait to run at once. |
| Module nesting | 64 | [`E5-007`](errors.md#e5-007) | How many levels of modules may import other modules. |
| HTTP connect timeout (ms) | 5,000 ms | [`E4-028`](errors.md#e4-028) | How long `DLC:network` waits to connect. |
| HTTP total timeout (ms) | 15,000 ms | [`E4-028`](errors.md#e4-028) | How long one `DLC:network` request may take in all. |
| HTTP response size | 8 MiB | [`E4-028`](errors.md#e4-028) | The largest response `DLC:network` accepts. |
| HTTP redirects | 5 | [`E4-028`](errors.md#e4-028) | How many redirects `DLC:network` follows. |
| Pattern group nesting | 64 | [`E3-011`](errors.md#e3-011) | How deeply the groups of one pattern may nest. |
| Pattern size | 64 KiB | [`E3-011`](errors.md#e3-011) | The longest pattern. |
| Pattern repeat count | 100,000 | [`E3-004`](errors.md#e3-004) | The largest count in a repeat such as `[num]100000`. |
<!-- END GENERATED: limits-table -->

## Budgets you choose

Two limits are off unless the host or the person running the script sets them:

| Option | `cuffc` / `cuffsh` flag | `CuffEngine::Options` | Raises |
| :--- | :--- | :--- | :--- |
| Steps | `--max-steps <n>` | `maxSteps` | [`E6-001`](errors.md#e6-001) |
| Time | `--timeout <ms>` | `timeoutMs` | [`E6-002`](errors.md#e6-002) |

A step is one loop iteration or one function call. The clock is checked every so often rather than at every
step, so a run can overshoot its timeout slightly. See [Security](../../SECURITY.md) for why a host that runs
untrusted scripts should always set both.

## Stack and recursion

Two limits stop runaway recursion, and they are independent: the **call depth** counts active calls, and the
**stack budget** counts the native stack the evaluator has used. Plain recursion reaches the call depth
limit first. A function with a large body uses more stack per call, so it can meet the budget first. Either
way the error is the same, `E4-017`, and it can be caught.

The stack budget is derived from the stack the thread really has, so the engine is safe on a thread with a
small stack. A budget that is configured larger than the thread can back is reduced to what exists. How
this is done, and why a long chain such as `a + b + c + ...` does not use stack in proportion to its length,
is explained in [Resource limits](../internals/resource-limits.md).

## Internal values

These are not limits a script meets directly; they are the numbers the limits above are built from. They are
listed so that the relationships between the limits can be checked, and they are checked at compile time (see
the end of `Limits.h`).

<!-- BEGIN GENERATED: internal-limits -->
_This table is generated from `engine/common/Limits.h` by `tools/docs.py`. Change the source, then run `python3 tools/docs.py generate`._

| Value | Setting | What it is |
| :--- | ---: | :--- |
| Parser stack per nesting level (`kParseStackBytesPerLevel`) | 4 KiB | Native stack the parser is assumed to need for one level of nesting. |
| Parser stack margin (`kParseStackMargin`) | 262,144 | Stack left untouched when the parser sizes its own budget from the stack that is really available. |
| Parser stack budget (`kParseStackBudget`) | 2304 KiB | Native stack the parser may use at most: a full nesting depth plus the margin. |
| Stack per plain call (`kPlainCallStackBytes`) | 2 KiB | Native stack one interpreted call of an ordinary function is assumed to need at most. Measured at about 1.3 KiB; this leaves headroom for other compilers. |
| Pattern cache size (`kMaxRegexCacheEntries`) | 512 | Compiled patterns kept for reuse; the cache is emptied when it fills. |
<!-- END GENERATED: internal-limits -->
