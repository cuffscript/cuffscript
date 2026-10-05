# Security Policy

## Supported Versions

CuffScript is currently in early development. Security patches are provided based on the latest version of the `main` branch.

---

## Reporting a Vulnerability

If you discover a security vulnerability, please report it privately via GitHub's [Private Security Advisory](https://github.com/cuffscript/cuffscript/security/advisories/new) feature, or by opening a public issue.

To help us process your report quickly, please include the following:

- A minimal input or code snippet that reproduces the issue
- A description of the symptoms and potential impact
- Environment details such as OS and compiler version

---

## Response Process

Once a report is received, we will review it and respond as soon as possible.
After a patch is released, the details will be disclosed through Security Advisories, releases, or the changelog. If you wish, we will credit you as the reporter.

---

## Scope

This policy applies to the engine code (`engine/`) and other core components of this repository.
Logic issues in user-written CuffScript code and vulnerabilities in external build tools are outside the scope of this policy.

---

## Running untrusted scripts

The engine is designed so that a script can fail but not crash the host: nesting, recursion,
value depth, string/collection size and regex work are all bounded and reported as ordinary
error codes, and `use ... from` cannot read outside the script's directory (or the `--root`
you choose). The engine's only two features that touch the real filesystem — module loading
and, as of this release, `DLC:filesystem` (see below) — share that same sandboxed root; nothing
else performs file access. Two things are deliberately left
to the embedder: an execution budget (`--max-steps`, `--timeout`, or `CuffEngine::Options`) is
off by default, and process-level memory and CPU limits should still be applied when hosting
untrusted code. Details and numbers are in `docs/IMPLEMENTATION_NOTES.md`, sections 22-23.

## DLC:network

As of v2.0.0, `use DLC:network` gives a script real outbound HTTP access (`get`/`post`, plain
HTTP only — no TLS, so `https://` is rejected rather than silently downgraded). This is a
meaningfully different trust boundary from everything else in the engine, worth calling out on
its own:

- **Off by default is not the default.** Unlike the module sandbox (safe by default, widened
  explicitly), network access is _on_ by default, because that's what makes `use DLC:network`
  useful out of the box for a script you run yourself. If you embed this engine to run scripts
  you did not write — a web IDE, a multi-tenant service, anything processing untrusted
  input — set `CuffEngine::Options::networkEnabled = false` (or run `cuffc --no-network`)
  before you need it, not after.
- **SSRF is blocked by default, not eliminated.** Every resolved address is checked against
  loopback/private/link-local ranges (including the common cloud-metadata address,
  `169.254.169.254`) right before connecting, so a script cannot reach `localhost`, your
  internal network, or instance metadata through it. This check is deliberately on the
  resolved IP, not the hostname text, but it is a single point-in-time check, not a general
  DNS-rebinding defense. `allowPrivateNetworkTargets` / `--allow-private-network` turns it off
  entirely — only do this for scripts you trust, or from a host that is itself isolated from
  anything sensitive.
- **No allowlist/denylist of hosts.** A script with network enabled can reach any public
  address. If you need to restrict _which_ hosts a script may reach, that has to be enforced
  outside the engine today (a filtering proxy, network namespace, or firewall in front of the
  process) — it is not a configuration option here.
- **Response size and time are capped** (`engine/common/Limits.h`:
  `kHttpMaxResponseBytes`/`kHttpConnectTimeoutMs`/`kHttpTotalTimeoutMs`/`kHttpMaxRedirects`), so
  a slow or oversized response can't hang or exhaust memory, but a script can still make many
  requests in a loop — the execution budget above (`--max-steps`/`--timeout`) is what bounds
  that, and it's off by default too.

## DLC:filesystem

`use DLC:filesystem` gives a script real local file access — `file_exist`, `file_size`,
`file_read`, `file_readlines`, `file_write`, `file_add`, `file_remove`. Unlike `DLC:network`,
this one follows the *same* sandbox-by-default model module loading already uses, not a
separate, more permissive one:

- **Confined to the same root as `use ... from`.** Every path is resolved relative to, and
  checked against, the script's own directory (or `--root` / `Options::rootDir`, whichever
  the host configured) — an absolute path or a `../`-style escape is rejected with
  `FilesystemAccessDenied` (`E5-008`) before anything is touched. There is no separate
  filesystem-specific root to configure; widening `--root` widens both module loading and
  `DLC:filesystem` together, deliberately.
- **On by default, like `DLC:network` — for the same reason.** A script you run yourself
  should be able to read and write files next to it without extra configuration. If you embed
  this engine to run scripts you did not write, set `CuffEngine::Options::filesystemEnabled =
  false` (or run `cuffc --no-filesystem`) before that script runs, not after — exactly the same
  caution as `networkEnabled` above.
- **No special-casing of "sensitive" files inside the root.** Anything the sandbox root
  contains — including, say, a `.env` file a script itself was never given the name of — is
  readable and writable if a script can guess or enumerate the path. Don't point `--root` at a
  directory containing anything you wouldn't want an untrusted script to read, write, or
  delete.
- **Symlinks that point outside the root are rejected; deleting is permanent.** Every path
  is resolved with `std::filesystem::weakly_canonical` before the containment check, so a
  symlink inside the root whose target lies outside it (a file link *or* a directory link
  you'd write through) is rejected with `E5-008` — verified by hand for both read and write.
  Symlinks that stay inside the root are followed normally. The one gap is inherent to
  check-then-use: something *other than the script* (another process) creating or swapping a
  symlink between the check and the open could still redirect an access — scripts have no
  way to create symlinks themselves, but don't expose a root that untrusted processes can
  write to. `file_remove` calls `std::filesystem::remove` directly — there is no trash/undo.
- **No size cap dedicated to `DLC:filesystem` beyond the language's own string limit** — a
  `file_read`/`file_readlines` on a file larger than the engine's normal string size ceiling
  (`engine/common/Limits.h`'s `kMaxStringBytes`) fails cleanly with `SizeLimitExceeded` (`E4-026`)
  rather than exhausting memory, but there's no separate, smaller default for files specifically;
  set one at the host/OS level (disk quotas, a size-limited mount) if that's not enough for your
  deployment.
