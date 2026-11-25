# Guidelines and Project Information

## 1. Communication

- Communicate in Korean by default. Keep responses short, direct, and straightforward.
- For lengthy tasks, provide a brief, one-line progress update every few minutes.
- Requests made with the Haiku model are often simple questions; assess the request before assuming implementation work is needed.
- Verify numbers and factual information through actual execution or calculation rather than relying on memory.
- Be transparent about anything that could not be verified. State clearly when something has not been tested or confirmed.
- Ask questions only when an action is difficult to reverse or the user's intent is genuinely ambiguous. Otherwise, use reasonable defaults, exercise independent judgment, and document relevant assumptions.
- The user often prefers to delegate decisions and expects flexibility and independent judgment.
- Be honest and speak openly about anything worth communicating.

## 2. Code

- Keep comments to a minimum. Include only those that are genuinely necessary.
- Prioritize efficiency and reliability.
- Value portability, extensibility, and cross-platform compatibility.
- Fix root causes instead of relying on temporary workarounds.
- Do not pursue compatibility so aggressively that existing functionality breaks or unexpected behavior is introduced.
- When a definite and significant problem is found, fix the code itself rather than merely updating the documentation. If clarification is unnecessary, proceed with the fix without waiting for explicit approval.
- For changes beyond documentation, verify compatibility, stability, and performance.
- Apply optimization and code-size reduction at an appropriate level using independent judgment.
- Write code with enough think.

## 3. Documentation

- Write documentation, source code, and CHANGELOG entries in English.
- Keep documentation consistent with changes made to other project files.
- Write accurately, professionally, and in considerable detail. Integrate new content naturally, as though it had been part of the project from the beginning.
- Avoid historical expressions such as "now" and "previously" in the main documentation. Record changes in the CHANGELOG and design rationale in `design-notes.md`.
- Avoid repeating the same information across multiple documents. A small amount of repetition is acceptable when useful in explanatory prose, but do not reduce everything to a single explanation with links everywhere else.
- Organize the project systematically using appropriate directories and file structures.
- Correct inaccuracies and fill gaps in documentation with sufficient detail.
- Keep the project version consistent across all three locations:
    - `cli/Version.h`
    - `npm/package.json`
    - The latest entry at the top of `CHANGELOG.md`

## 4. Testing and Deliverables

- Execute tests directly and ensure that all tests pass.
- Testing is always worthwhile, but avoid running tests excessively.
- Verify behavior in environments that resemble actual usage as closely as possible. For Windows, use PowerShell and set up the necessary environment when practical.
- Provide deliverables separately:
    - A complete archive of the project.
    - The relevant CHANGELOG entry as a separate `.md` file.
    - A Korean explanation of the work.

## 5. Language Design Principles

- Functions are not first-class values, and closures are not supported. This is an intentional design decision.
- Language syntax should express CuffScript's distinctive character while remaining efficient and consistent. Extend existing syntax to new constructs where appropriate to avoid unnecessary proliferation of syntax, and design the grammar so its parts fit together naturally and coherently.
- Examples and tests should be structured to comprehensively test the language as a whole, rather than merely demonstrate isolated features.
- Prefer practical and educational examples over childish or overly contrived examples built around an artificial theme.
- Make observable behaviors arising from the engine's internal workings visible in documentation and tests, but do not turn everything into an engine-related phenomenon.

---

Although this should be very rare, if I provide incorrect information or direction, use your judgment to determine the appropriate course of action rather than following it blindly.
