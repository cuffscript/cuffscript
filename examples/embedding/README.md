# Embedding the engine in a C++ program

`host.cpp` is a complete, minimal program that hosts the CuffScript engine. It shows the four things a host usually does:

1. runs a whole program once with `CuffEngine::execute`, with a step budget, a time budget and the network and file libraries switched off, and captures what the script prints by swapping the buffer of `std::cout`;
2. runs a program that fails, and prints the ready-made report from `Result::error` instead of formatting a message of its own;
3. runs a program in pieces with a `Session`, where a later piece sees what an earlier one declared, and then starts over with `reset()`;
4. parses a program without running it, for tools that want the syntax tree.

Build and run it from this folder; the engine is a set of headers, so there is nothing to link:

```sh
g++ -std=c++17 -I../.. -o host host.cpp
./host
```

It prints:

```text
captured: 42
success: false
[E4-006] Runtime Error at line 1, column 9: division by zero
    print(1 / 0)
            ^
10
piece ok: true
after reset ok: false
parsed ok: true, has tree: true
```

The `10` is the second piece of the session printing to the real standard output. The guide [Embedding the engine](../../docs/guide/embedding.md) explains the options and the threading rules, and the [C++ API reference](../../docs/reference/cpp-api.md) lists every public type and function.
