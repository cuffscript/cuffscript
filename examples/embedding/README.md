# Embedding the engine in a C++ program

`host.cpp` is a complete, minimal program that hosts the CuffScript engine. It shows what a host usually does:

1. runs a whole program once with `Engine::execute`, with a step budget, a time budget and the network and file libraries switched off, and captures what the script prints by swapping the buffer of `std::cout`;
2. runs a program that fails, and prints the ready-made report from `Outcome::error` instead of formatting a message of its own;
3. runs a program in pieces with one `Engine`, where a later piece sees what an earlier one declared;
4. gives the script a function of its own with `addFunction`, which the script calls like a library function and whose failure it catches with `or_else`;
5. goes the other way: sets and reads a variable of the script with `set` and `get`, and calls one of its functions with `call`;
6. starts over with `reset()`, and parses a program without running it, for tools that want the syntax tree.

Build and run it from this folder; the engine is a set of headers, so there is nothing to link:

```sh
g++ -std=c++17 -I../.. -o host host.cpp
./host
```

It prints:

```text
captured: 42
success: false
[E4-006] Runtime Error at line 1, column 9
  |
1 | print(1 / 0)
  |         ^ division by zero
10
piece ok: true
15
host_scale() failed: the factor cannot be 0
call: Ann has 8, n: 8
[E4-030] Runtime Error: describe(): parameter 'who' expects str, got number

  hint: declared as 'str who' in the parameter list
after reset ok: false
parsed ok: true, has tree: true
```

The `10` and the `15` are the script printing to the real standard output. The last report has no line: the wrong call was made by the host, not written in a script. The guide [Embedding the engine](../../docs/guide/embedding.md) explains the options and the threading rules, and the [C++ API reference](../../docs/reference/cpp-api.md) lists every public type and function.
