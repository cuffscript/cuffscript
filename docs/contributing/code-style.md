# Code style

The conventions the engine is written in. Where a rule is checked by a tool, the tool is named.

## Language and layout

- C++17, header-only under `engine/` (and `cli/`). A header is `#pragma once`, with everything `inline` or in a class.
- Four spaces for indentation, no tabs. `.editorconfig` has the rest; CHANGELOG.md is exempt.
- Everything is in `namespace cuff` (limits in `cuff::limits`, the shell in `cuff::cli`).
- No `using namespace std;` in a header.
- `-Wall -Wextra -Werror` must be clean on GCC and Clang, including with libc++.

## Names

Types and classes are `PascalCase`, functions and variables `camelCase`, constants `kCamelCase`, members of a class end with
`_` when private. Library functions exposed to scripts are `library_verb` in lower case.

## Errors

- A script's mistake is a `CuffError` thrown with the most specific code and subclass, a message that says what is wrong in one
  sentence, and a hint when one helps. Never `assert`, `abort` or `exit` for something a script can cause.
- A new kind of error gets its code, description and test; see [the error model](../internals/error-model.md#adding-an-error-code).
- Take the `SourceLocation` from the token that starts the construct.
- Anything a script can make large or deep has a limit; see [Resource limits](../internals/resource-limits.md#adding-a-limit).

## Platform code

Code that touches the platform (sockets, the terminal, file names) lives in one place each (`engine/net/HttpClient.h`, `cli/`) and
behind `#ifdef`s that are compiled and run on at least one platform in CI. The Windows build targets Vista (`_WIN32_WINNT` in `engine/common/Attributes.h`), and a header declares an API newer than that only for a newer target, which the MinGW-w64 headers of MSYS2 enforce and older ones did not. So an API from Windows 8 or later is not called directly: look it up with `GetProcAddress` and cope with its absence, as `availableStackBytes` does. A path built from script text uses
`std::filesystem::u8path`, not `path(str)`, which reads the string in the ANSI code page on Windows. Engine code that can sit between a `throw` and its `catch` does not force a frame pointer (`__builtin_frame_address`,
`alloca`, variable-length arrays): on Windows, GCC then describes the saved `xmm` registers of such a frame wrongly to the
unwinder, and an exception passing through it corrupts them (design notes, item 45).

## Comments

A comment records what the code cannot say: an invariant, a platform quirk, the reason for a check that looks unnecessary. Names
say what code does. Long explanations belong in the [design notes](../internals/design-notes.md) or on a page under
`docs/internals/`, not in a header. When you change behaviour, fix the page that explains it in the same change.

## Tests with the change

A behaviour change comes with a test that fails without it: a `tests/cases` or `tests/errors` file when it is visible from a
script, a unit test when it is not. See [Testing](testing.md).
