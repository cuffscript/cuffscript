# `cuffc`: the script runner

`cuffc` runs a CuffScript program from a file or from standard input. Build it with `make` (see [Building](../contributing/building.md)).

```text
cuffc [options] [script.cuff]
```

```sh
cuffc hello.cuff                      # run a file
echo 'print(1 + 1)' | cuffc           # read the script from standard input
cuffc --max-steps 1000000 --timeout 2000 job.cuff
cuffc --no-network --no-filesystem untrusted.cuff
cuffc --ast hello.cuff                # show how the program was understood, without running it
```

With no file, or with `-`, the script is read from standard input. After `--`, every argument is a file name, so a file called `--odd.cuff` can be run with
`cuffc -- --odd.cuff`. Exactly one script can be given.

## Options

<!-- BEGIN GENERATED: help-cuffc -->
```text
Usage: cuffc [options] [script.cuff]
  (no file, or -)        read the script from stdin
  --ast                  print tokens + AST instead of running the script
  -h, --help             show this help and exit
  --root <dir>           allow 'use ... from' to load modules anywhere under <dir>
  --max-steps <n>        stop after <n> loop iterations + function calls (0 = unlimited)
  --timeout <ms>         stop after <ms> milliseconds of run time (0 = unlimited)
  --no-network           switch DLC:network off (calls into it raise E5-005)
  --allow-private-network
                         let DLC:network reach loopback/private/link-local addresses
  --no-filesystem        switch DLC:filesystem off (calls into it raise E5-005)
By default modules may be loaded from the script's own directory only.
```
<!-- END GENERATED: help-cuffc -->

| Option | What it does |
| :--- | :--- |
| `--ast` | Prints what the program was made of and does not run it: the raw tokens, the tokens after the lexer, the syntax tree, then `PARSE SUCCESS`. A program with a syntax error prints the error instead and exits with status 1. `--tokens` and `--debug` are older spellings of the same option. |
| `--root <dir>` | The folder that `use ... from` and [`DLC:filesystem`](dlc/filesystem.md) may reach. Without it they are confined to the folder of the script. See [Modules](../guide/modules.md). |
| `--max-steps <n>` | Stops the program with `E6-001` when the number of loop iterations plus calls of the program's own functions reaches `n`. Calls of library functions, `print` included, are not counted. `0` means unlimited, which is also the default. |
| `--timeout <ms>` | Stops the program with `E6-002` when it has been running for `<ms>` milliseconds of real time, measured with a steady clock that the system clock cannot move. `0` means unlimited, which is also the default. |
| `--no-network` | Switches [`DLC:network`](dlc/network.md) off: every call into it raises `E5-005`. |
| `--allow-private-network` | Lets `DLC:network` reach loopback, private and link-local addresses, which it refuses by default. |
| `--no-filesystem` | Switches [`DLC:filesystem`](dlc/filesystem.md) off in the same way. |
| `-h`, `--help` | Prints the usage text and exits with status 0. |

A number given to `--max-steps` or `--timeout` must be a non-negative decimal integer: a sign, a space, trailing text or a value that is too large is a command-line error.

An option that `cuffc` does not know is an error (exit status 2), and so is giving more than one script, an option that needs a value and has none,
or a malformed value. The engine options (`--root`, `--max-steps`, `--timeout`, `--no-network`, `--allow-private-network`, `--no-filesystem`) are shared with
[`cuffsh`](cuffsh.md): both programs read them with the same code, so they mean the same thing in both, and a host that embeds the engine can read them with
`parseEngineOption` ([C++ API](cpp-api.md#command-line-options)).

A host that runs scripts it did not write should always give `--max-steps` and `--timeout`, because the engine has no budget unless one is set.
[Security](../../SECURITY.md) explains the rest.

## Output streams

What the script prints with `print` goes to standard output. Everything `cuffc` itself reports goes to standard error, so `cuffc script.cuff > out.txt` keeps the report on the screen:

```text
$ cuffc fail.cuff > out.txt
ERROR: [E4-006] Runtime Error at line 2, column 9: division by zero
    print(1 / 0)
            ^
```

Output that the script produced before the error has already been written. `input` reads standard input, so a script that reads input cannot also be read from standard input;
give it as a file.

## Exit status

| Status | Meaning |
| ---: | :--- |
| 0 | The script ran to the end (or `--help` was asked for, or `--ast` parsed the program). |
| 1 | The script failed with an error (any error code, including a budget running out), or the file could not be read. |
| 2 | The command line was wrong. |

An error is printed to the standard error stream, starting with `ERROR: `, in the form described in
[reading an error report](../guide/errors-and-debugging.md#reading-an-error-report). A file that cannot be opened is reported as `Error: Cannot open file '<name>'` and also exits with status 1.

## Where modules are looked up

`use name from ./dir` is resolved against the directory of the script, and may not leave it. With
`--root <dir>` it may reach anywhere under `<dir>` instead. A script read from standard input counts as being in
the current directory. See [Modules](../guide/modules.md).

## Reading a program that is not UTF-8

Source is UTF-8. A leading byte order mark is ignored, and lines may end in LF or CRLF. The file is read as
bytes, so it is not changed on the way in on any platform. The engine does not check the text inside a string literal for valid UTF-8, so a stray byte there is passed through to the output as it is; a
program in another encoding (Latin-1, EUC-KR, Shift-JIS) should be converted to UTF-8 first.
