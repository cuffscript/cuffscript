# `cuffc`: the script runner

`cuffc` runs a CuffScript program. Build it with `make` (see [Building](../contributing/building.md)).

```text
cuffc [options] [script.cuff]
```

With no file, or with `-`, the script is read from standard input. After `--`, every argument is a file name.

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
  --no-network           disable 'use DLC:network' entirely
  --allow-private-network
                         let DLC:network reach loopback/private/link-local addresses
  --no-filesystem        disable 'use DLC:filesystem' entirely
By default modules may be loaded from the script's own directory only.
```
<!-- END GENERATED: help-cuffc -->

`--ast` also accepts the older spellings `--tokens` and `--debug`.

An option that `cuffc` does not know is an error (exit status 2), and so is giving more than one script. The
engine options (`--root`, `--max-steps`, `--timeout`, `--no-network`, `--allow-private-network`,
`--no-filesystem`) are shared with [`cuffsh`](cuffsh.md): both programs read them with the same code, so they mean
the same thing in both.

## Exit status

| Status | Meaning |
| ---: | :--- |
| 0 | The script ran to the end (or `--help` was asked for). |
| 1 | The script failed with an error, or could not be read. |
| 2 | The command line was wrong. |

An error is printed to the standard error stream in the form described in
[reading an error report](../guide/errors-and-debugging.md#reading-an-error-report).

## Where modules are looked up

`use name from ./dir` is resolved against the directory of the script, and may not leave it. With
`--root <dir>` it may reach anywhere under `<dir>` instead. A script read from standard input counts as being in
the current directory. See [Modules](../guide/modules.md).

## Reading a program that is not UTF-8

Source is UTF-8. A leading byte order mark is ignored, and lines may end in LF or CRLF. The file is read as
bytes, so it is not changed on the way in on any platform.
