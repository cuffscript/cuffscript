# `cli/`

`cuffsh`, the interactive shell: a line editor and a read-evaluate-print loop over the engine's `Session`.

```sh
./cli/build.sh          # Linux, macOS  ->  cli/cuffsh
cli\build.bat           # Windows       ->  cli\cuffsh.exe
EXCLUDE_DLC="network hardware" ./cli/build.sh   # without those libraries (set EXCLUDE_DLC=... on Windows)
```

Using it is described in the [`cuffsh` reference](../docs/reference/cuffsh.md); how it is built is in
[the shell's architecture](../docs/internals/shell-architecture.md); its tests are in `tests/` here and described in
[Testing](../docs/contributing/testing.md#the-shell-suite).
