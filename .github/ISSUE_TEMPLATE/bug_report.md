---
name: "Bug report"
about: "Something behaves differently from what the documentation says"
title: "[Bug] "
labels: ["bug"]
assignees: []

---

## Summary
One line describing the problem.

## The smallest program that shows it
Cut lines until the problem disappears, then put the last one back. Save it as a `.cuff` file and run it with `cuffc`.

```cuff
note: paste the CuffScript program here
```

If it needs input, options or other files, say which: for example `cuffc --max-steps 1000 program.cuff`.

## What you expected

## What happened
Paste the complete output, including the `ERROR: [E...]` report with its source line and caret.

```text
paste the output here
```

## Environment
- Operating system and version:
- Architecture (x86_64, arm64, ...):
- Compiler and version:
- CuffScript version (`cuffsh --version`, or the commit you built):
- How you run it (`cuffc`, `cuffsh`, embedded in a C++ program, the WebAssembly package):

## Additional information
- Does it still happen on the latest release or on `main`? (yes / no / not tried)
- Anything you tried that changes the result:
