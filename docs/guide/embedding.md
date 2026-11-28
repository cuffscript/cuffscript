# Embedding the engine

The language engine is a set of C++17 headers under `engine/`. There is nothing to link: include the header you need and
compile with `-I<repository root>`, as C++17 or later (C++20 and C++23 too). A complete, buildable host program lives in
[`examples/embedding`](../../examples/embedding/README.md); this page explains what it does and what to decide before you
ship one. Every type and function is listed in the [C++ API reference](../reference/cpp-api.md).

```cpp
#include "engine/CuffEngine.h"
#include <iostream>

int main()
{
    cuff::Engine::Options options;
    options.timeoutMs = 2000;          // stop a runaway script after two seconds
    options.networkEnabled = false;    // this host does not want scripts reaching the network

    auto result = cuff::Engine::execute("print(1 + 1)\n", ".", options);
    if (!result.success)
        std::cerr << "ERROR: " << result.error << "\n";
    return result.success ? 0 : 1;
}
```

```sh
g++ -std=c++17 -I/path/to/cuffscript -o host host.cpp
```

The headers compile without warnings under `-Wall -Wextra`. The engine uses exceptions internally, so build with exceptions enabled
(the default); it does not use RTTI-dependent features that need special flags.

## Input and output

`print` writes to `std::cout` and `input` reads from `std::cin`; the engine has no stream parameter of its own. To capture a script's
output or to supply its input, replace the stream buffers around the call, and put them back afterwards:

```cpp
std::ostringstream captured;
auto* previous = std::cout.rdbuf(captured.rdbuf());
auto result = cuff::Engine::execute("print(6 * 7)\n", ".", options);
std::cout.rdbuf(previous);
// captured.str() is "42\n"
```

Error reports are never written by the engine: they come back in `result.error` for the host to print where it wants.

The locale of the process does not reach a script. A host may call `setlocale` (many toolkits do it at start-up): numbers are still read
and written with `.` as the decimal point, in source text, `to_number`, JSON and output, and letter case is still that of the
[string library](../reference/dlc/string.md), whatever the C library would do under that locale.

## One class: `Engine`

A host uses one class, `cuff::Engine`, in one of two ways:

| Use | When |
| :--- | :--- |
| `Engine::execute(source, scriptDir, options)` | You have a whole program and want it run once. |
| an `Engine` that lives on | The program arrives in pieces (a shell, a notebook, a game console), or the host calls into it, reads its variables or gives it functions. |

Every member reports failure the same way: `success` is false and `error` holds the finished report, in the form `cuffc` prints, with the
source line and caret. A host prints `error` and does not format its own message; that is what keeps every front end in step when the report
changes. Nothing throws for a failing script, and no engine exception escapes.

```cpp
cuff::Engine engine(options);
auto a = engine.run("number n = 5\n");
auto b = engine.run("print(n * 2)\n");   // sees n
engine.reset();                          // forget everything, keep the options
```

`scriptDir` is the folder that `use ... from` and `DLC:filesystem` resolve against (unless `Options::rootDir` is set). Pass `"."` when
the program has no file of its own.

The pieces one engine runs are one program: a later piece uses what the earlier ones declared, and may not declare one of their
functions, classes or top-level variables again (`E2-010`, or `E4-038` for a class), just as it could not further down one file; a
piece whose declarations fail to be made leaves none of them behind. To start a different program, `reset()` the engine or make another.
A function declared in one piece that fails while a later piece runs is reported against the piece that declared it. A run returns when
its piece and every task that piece started have finished, and an error ends the run without leaving tasks behind. An `Engine` keeps every
program it has run until it is reset, because the interpreter holds pointers into them, so an engine that runs unbounded amounts of code
should be reset now and then.

## Talking to the program

The other way round from `run`: the host calls the program, looks at it, and lends it functions of its own. This is all the traffic there
is between the two, and it carries the language's own values.

```cpp
cuff::Engine engine;
engine.addFunction("beep", [](cuff::Args& args) -> cuff::Value {
    args.expectCount(1);
    double hz = args.number(0);           // E4-005 for a non-number, at the script's call
    if (hz <= 0)
        args.fail("the pitch must be above 0");   // E4-042, which or_else can catch
    std::cout << "\a";
    return cuff::Value();                 // empty
});

engine.run("number volume = 3\n"
           "func louder(number by) do:\n    global volume\n    volume = volume + by\n    return volume\nend\n");

auto r = engine.call("louder", {cuff::Value::makeNumber(2)});   // r.value is 5
engine.set("volume", cuff::Value::makeNumber(0));                // keeps the declared type
std::optional<cuff::Value> v = engine.get("volume");             // 0
```

- `call` checks the arguments as a script's call is checked, fills in default values, and gives the function's value. An `async` function
  runs to its end. A failure is reported like a failed run, without a source line when it is the call itself that is wrong.
- A program that calls [`exit(status)`](../reference/builtins.md#arguments-exit-and-assert) ends the `run` or `call` there: the outcome is a success
  with no report, `exited` set and the status in `exitStatus`. The engine can run again afterwards. What the status means is the host's to
  decide; `cuffc` ends with it.
- `get` gives the value of a top-level variable, or nothing. A list, map or object is shared with the program.
- `set` assigns as a script would, so a `number` variable stays a number and a `constant` cannot be changed; a new name is declared as `any`.
- `addFunction` lends the program a function. Its arguments come checked through `Args`, its value goes back as a `Value`, and a failure it
  reports, or a `std::exception` it lets out, is the script's `E4-042`. A function of the program with the same name is the one a call
  reaches. Host functions survive `reset()`.

A host function runs inside the engine's run. It must not block for long (the program's [tasks](../reference/language.md#65-asynchronous-functions-and-tasks)
wait while it runs) and it cannot use the engine that called it. The full list is in the [C++ API reference](../reference/cpp-api.md#host-functions).

## Options

`Engine::Options` is the interpreter's configuration (`Interpreter::Config`):

| Field | Meaning | Default |
| :--- | :--- | :--- |
| `rootDir` | Where `use ... from` and `DLC:filesystem` may reach. | The script's folder |
| `maxSteps` | Loop iterations plus function calls before `E6-001`; `0` is unlimited. | `0` |
| `timeoutMs` | Milliseconds before `E6-002`; `0` is unlimited. | `0` |
| `stackBudgetBytes` | Native stack the engine may use for work that nests inside it (a program's own calls use none); `0` derives it from the real stack. A larger value than the thread has is reduced to what exists. | `0` |
| `networkEnabled` | Whether `DLC:network` calls are allowed. | `true` |
| `allowPrivateNetworkTargets` | Whether `DLC:network` may reach loopback and private addresses. | `false` |
| `filesystemEnabled` | Whether `DLC:filesystem` calls are allowed. | `true` |
| `collectCycles` | Whether the engine frees, as a program runs, lists, maps and objects that refer only to each other (see [memory](../reference/cpp-api.md#memory)). Off, they wait for `collectCycles()` or the engine's end. | `true` |
| `arguments` | What the program's [`args()`](../reference/builtins.md#arguments-exit-and-assert) gives, in order (`std::vector<std::string>`, UTF-8). `cuffc` puts there every argument after the script. | empty |
| `hardware` | The parts of the device that [`DLC:hardware`](../reference/dlc/hardware.md) reaches, each a function of the host; see [below](#giving-a-program-hardware). | none |
| `onCompiled` | A function the engine calls with each piece of bytecode as it compiles it (`const bc::Proto&`); `cuffc --bytecode` prints them with `BytecodePrinter::print` from `engine/debug/BytecodePrinter.h`. | none |

A library that is switched off raises `E5-005` from every call; see [the rules for libraries](../reference/dlc/README.md#libraries-that-reach-outside-the-program).

Paths in the options and in `scriptDir` are UTF-8, like every other string of the engine, on every platform. On Windows, a string that is
not UTF-8 is read in the ANSI code page instead, which is how every path was read before 6.2.0, so a host that passes ANSI paths keeps
working. A host on Windows that has a path as `std::filesystem::path` hands over `path.u8string()`, not `path.string()`, which throws for
a name outside the ANSI code page.

`cuffc` and `cuffsh` build their `Options` from the command line with `parseEngineOption` in `engine/EngineOptionsCli.h`, which a
host can call too, to accept the same flags with the same meaning.

A host that runs scripts it did not write should set `maxSteps` and `timeoutMs`, switch off the libraries it does not need, and apply
process-level memory and CPU limits as well. [Security](../../SECURITY.md) says why, and what the engine does and does not protect.

## Giving a program hardware

The engine reaches no hardware by itself. A host gives [`DLC:hardware`](../reference/dlc/hardware.md) the parts of its device as
functions in `Options::hardware` (a `cuff::Hardware`, from `engine/dlc/HardwareDLC.h`), and a script calls them with `hw_` functions.
A part left empty is one the device does not have: its function is `E4-044` for the script, and `hw_supports` says `false`. So nothing
is reachable until the host gives it.

```cpp
cuff::Engine::Options options;
options.hardware.battery = [] { return cuff::BatteryState{read_battery_percent(), charger_connected()}; };
options.hardware.led = [](int led, bool on) { gpio_write(LED_PINS[led], on); };
options.hardware.sensor = [](const std::string& name) -> double {
    if (name == "temperature")
        return read_thermistor_celsius();
    throw cuff::HardwareUnsupported("there is no sensor \"" + name + "\"");
};
cuff::Engine engine(options);
engine.run("use DLC:hardware\nhw_led(0, hw_battery()[\"level\"] < 20)\n");
```

| Field | Called for | Gives back |
| :--- | :--- | :--- |
| `battery` | `hw_battery()` | `BatteryState{level, charging}`, `level` from 0 to 100 |
| `button` | `hw_button(button)` | `bool`, true while held down |
| `led` | `hw_led(light, on)` | |
| `beep` | `hw_beep([hertz [, seconds]])`: 20 to 20000 Hz, up to 10 s | |
| `screenText` | `hw_screen_text(line, text)` | |
| `serialRead`, `serialWrite` | `hw_serial()`, `hw_serial(text)` | `std::string`: what arrived since the last read, or `""` |
| `sensor` | `hw_sensor(name)` | `double` |
| `pinRead`, `pinWrite` | `hw_pin(pin)`, `hw_pin(pin, high)` | `bool`, true for high |
| `pwm` | `hw_pwm(pin, duty)`, `duty` from 0 to 1 | |
| `analog` | `hw_analog(pin)` | `double` from 0 to 1 of the input's range |
| `supports` | `hw_supports(part)`, for a part whose function is given | `bool`: whether it is there after all (optional) |

The engine checks every argument before it calls the host, so a function of the host sees only values in the ranges above, and whole
numbers from 0 to 65535 for a button, a light, a line or a pin. It checks what comes back as well: a battery level outside 0 to 100,
an analog reading outside 0 to 1, or a sensor reading that is not a number is `E4-042`. A function of the host that throws
`cuff::HardwareUnsupported` gives the script `E4-044` with its text (a part that is missing now, a sensor of another name); one that
throws anything else gives `E4-042`, as a function added with `addFunction` does. The functions run on the thread that runs the
script, and while one runs, nothing else of that engine does.

`cuffc --hardware` and `cuffsh --hardware` give what a desktop or laptop computer has without a library, with `cuff::desktopHardware()`
from `engine/DesktopHardware.h`; [the library's page](../reference/dlc/hardware.md#what---hardware-gives) lists it. A host may start
from it and add the parts of its own device.

## Threads and stacks

An `Engine` is not shared between threads; give each thread its own, and let go of an engine's values on that thread. Three pieces of
state are shared by every `Engine` in the process, and all are locked, so engines on different threads need no further care:

- the table of identifier names, which grows by every distinct name the process ever sees, so count it among the things a host limits if it
  runs unbounded numbers of unrelated untrusted scripts;
- the random generator behind [`DLC:random`](../reference/dlc/random.md). It is one generator for the process, so `random_seed` in one engine
  changes the sequence the others see;
- the table that numbers the engines for their records of lists, maps and objects, which has room for 4,095 engines alive at once. An engine
  made beyond that works, but frees no cycles.

A program's own calls take frames in the engine's bytecode machine and no native stack, so a script can recurse to the call depth limit on
a thread with a small stack (the tests do it on 256 KiB). The engine measures the stack it is really running on for the work that does
nest inside it, and ends that with the catchable `E4-017` instead of overflowing (the tests run it on threads of 512 KiB and 1 MiB). See
[Resource limits](../internals/resource-limits.md).

## WebAssembly and npm

`make wasm` builds `wasm/bindings.cpp` with Emscripten into `npm/dist/`, and the [`cuffscript-wasm`](../../npm/README.md) package
ships the result. The binding exposes two calls to JavaScript:

| Call | What it does |
| :--- | :--- |
| `mod.cuffRun(source, scriptDir)` | Parses and runs the program, like `Engine::execute`, and returns `{ success, error }`. |
| `mod.cuffDump(source)` | Parses without running and prints the token and syntax-tree dump through `print`, like `cuffc --ast`. It returns `{ success, error }`. |
| `mod.cuffBytecode(source, scriptDir)` | Runs the script and writes each piece of bytecode through `print` at the moment it is compiled, in order with what the program prints, like `cuffc --bytecode`. Returns `{ success, error }`. |

The binding does not expose the engine options, so a script runs with the defaults: no step or time budget, and every library enabled. A browser cannot open
ordinary network connections, so do not expect `DLC:network` to work there, and a `time_sleep` keeps the processor busy for its length, because
the module has nothing to wait on. The memory of the module is capped by the build flags in the `Makefile`, which
also bounds what a script can allocate. A page that runs code it did not write should run the module in a Web Worker that it can terminate. Output reaches the page through the `print` callback given when the module is created, and `input` reads
through the `stdin` callback. Files that the program loads with `use ... from` are written into the module's virtual file system (`mod.FS`) before the call.
