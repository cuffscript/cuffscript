# Security Policy

## Supported versions

CuffScript is currently in early development. Security patches are provided based on the latest version of the `main` branch. See the
[changelog](docs/CHANGELOG.md) for what each release contains.

---

## Reporting a vulnerability

If you discover a security vulnerability, please report it privately via GitHub's
[Private Security Advisory](https://github.com/cuffscript/cuffscript/security/advisories/new) feature, or by opening a public issue.

To help us process your report quickly, please include:

- a minimal input or code snippet that reproduces the issue;
- a description of the symptoms and the potential impact;
- environment details such as the operating system, the compiler and its version, and the CuffScript version (`cuffsh --version`).

---

## Response process

Once a report is received, we will review it and respond as soon as possible. After a patch is released, the details are disclosed
through Security Advisories, the release notes or the changelog. If you wish, we will credit you as the reporter.

---

## Scope

This policy applies to the engine code (`engine/`) and the other core components of this repository (`cuffc`, `cuffsh`, the WebAssembly
build and the npm package). Logic issues in user-written CuffScript code and vulnerabilities in external build tools are outside the
scope of this policy.

---

## Running untrusted scripts

The engine is designed so that a script can fail but not crash the host: nesting, recursion, value depth, string and collection size, and
pattern matching work are all bounded and reported as ordinary error codes, and `use ... from` cannot read outside the script's directory
(or the `--root` you choose). The engine has two features that touch the real filesystem, module loading and `DLC:filesystem` (see below), and
they share the same sandboxed root; nothing else performs file access.

Two things are deliberately left to the embedder:

- **An execution budget.** `--max-steps`, `--timeout` and the matching `CuffEngine::Options` are off unless set.
- **Process-level limits.** The engine does not count total memory. Apply memory and CPU limits to the process that hosts untrusted code.

The numbers are on the [limits page](docs/reference/limits.md), and the design is described in [Resource limits](docs/internals/resource-limits.md) and in
`docs/internals/design-notes.md`, sections 22 and 23.

Two pieces of state are shared by every program a process runs, and a long-lived host should keep that in mind:

- The table of identifier names grows by every distinct name it ever sees, so a host that runs many unrelated untrusted programs should account for that growth
  with limits of its own.
- The random generator behind `DLC:random` is a single one per process. Its sequence is not private to one program, so one program can reseed it for
  another. Do not use it for anything where unpredictability matters.

---

## DLC:network

`use DLC:network` gives a script real outbound HTTP access (`network_get` and `network_post`, plain HTTP only: there is no TLS, so `https://` is rejected rather than silently
downgraded). This is a meaningfully different trust boundary from everything else in the engine, worth calling out on its own:

- **It is on by default.** Unlike the module sandbox, which is safe by default and widened explicitly, network access is available unless the host switches it off, because
  that is what makes `use DLC:network` useful out of the box for a script you run yourself. If you embed this engine to run scripts you did not write (a web IDE, a
  multi-tenant service, anything processing untrusted input), set `CuffEngine::Options::networkEnabled = false` (or run `cuffc --no-network`) before the script runs, not after.
- **SSRF is blocked by default, not eliminated.** Every resolved address is checked against loopback, private and link-local ranges (including the common cloud-metadata
  address `169.254.169.254` and the IPv6 unspecified address `::`, which reaches the local host on Linux) right before connecting, so a script cannot reach `localhost`, your
  internal network or instance metadata through it. The check is on the resolved IP, not the hostname text, and it applies to every redirect target as well, but it is a single
  point-in-time check, not a general DNS-rebinding defence. `allowPrivateNetworkTargets` / `--allow-private-network` turns it off entirely: only do this for scripts you trust, or
  from a host that is itself isolated from anything sensitive.
- **There is no allowlist or denylist of hosts.** A script with network enabled can reach any public address. Restricting *which* hosts a script may reach has to be enforced
  outside the engine (a filtering proxy, a network namespace or a firewall in front of the process); it is not a configuration option here.
- **Response size and time are capped** (the HTTP entries in [limits](docs/reference/limits.md)), so a slow or oversized response cannot hang the host or exhaust its memory. A
  script can still make many requests in a loop; the execution budget above is what bounds that.

---

## DLC:filesystem

`use DLC:filesystem` gives a script real local file access: `file_exist`, `file_size`, `file_read`, `file_readlines`, `file_write`, `file_add` and `file_remove`. Unlike `DLC:network`,
it follows the same sandbox-by-default model that module loading uses, not a separate, more permissive one:

- **Confined to the same root as `use ... from`.** Every path is resolved relative to, and checked against, the script's own directory (or `--root` / `Options::rootDir`, whichever the
  host configured). An absolute path or a `../`-style escape is rejected with `E5-008` (`FilesystemAccessDenied`) before anything is touched. There is no separate filesystem-specific
  root to configure; widening `--root` widens module loading and `DLC:filesystem` together, deliberately.
- **It is on by default, like `DLC:network`, for the same reason.** A script you run yourself should be able to read and write files next to it without extra configuration. If you
  embed this engine to run scripts you did not write, set `CuffEngine::Options::filesystemEnabled = false` (or run `cuffc --no-filesystem`) before the script runs, not after.
- **Nothing inside the root is treated as sensitive.** Anything the sandbox root contains, including a `.env` file the script was never given the name of, is readable and writable if
  the script can guess or list the path. Do not point `--root` at a directory that holds anything you would not want an untrusted script to read, write or delete.
- **Symlinks that point outside the root are rejected; deleting is permanent.** Every path is resolved with `std::filesystem::weakly_canonical` before the containment check, so a symlink
  inside the root whose target lies outside it (a file link or a directory link you would write through) is rejected with `E5-008`, for reading and writing alike. Symlinks that stay inside
  the root are followed normally. The one gap is inherent to check-then-use: something *other than the script* (another process) creating or swapping a symlink between the check and the
  open could still redirect an access. Scripts have no way to create symlinks themselves, but do not expose a root that untrusted processes can write to. `file_remove` deletes regular files
  only (a folder is never removed) and goes straight to `std::filesystem::remove`: there is no trash and no undo.
- **There is no size cap dedicated to `DLC:filesystem`** beyond the language's own limits. Reading a file larger than the string limit, or a file with more lines than the collection limit,
  fails cleanly with `E4-026` (`SizeLimitExceeded`) instead of exhausting memory. Writing has no cap of its own: set one at the host or operating-system level (disk quotas, a size-limited mount)
  if the language limits are not enough for your deployment.
