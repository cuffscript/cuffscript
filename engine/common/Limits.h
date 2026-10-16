#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Every resource limit the engine enforces, with what it protects and what happens when it is exceeded.
//
// docs/reference/limits.md is generated from this file (tools/docs.py), so the two cannot drift apart. A constant
// is documented by a comment block written directly above it:
//
//     // @limit <title> | <error code, optional>        a limit users can run into
//     // @internal <title>                              a derived or tuning value
//     // <description, one or more lines>
//     constexpr ... kName = value;
//
// After changing a value or a description, run `python3 tools/docs.py generate`; CI fails if the page is stale.
//
// Limits that depend on one another are tied together by the static_asserts at the bottom of this file, so
// changing one number cannot silently break a guarantee another one makes.

namespace cuff::limits
{

    // @limit Source size | E2-009
    // The largest program, module file or shell entry. Each file is measured on its own.
    constexpr size_t kMaxSourceBytes = 16u * 1024 * 1024;

    // @limit Nesting depth | E2-008
    // How deeply code may nest: blocks, parentheses, brackets, call arguments. The parser recurses on the native
    // stack, so this also stops hostile input from exhausting it. It is checked against the stack that is really
    // available as well, so on a very small stack it can trigger sooner.
    constexpr int kMaxParseDepth = 512;

    // @internal Parser stack per nesting level
    // Native stack the parser is assumed to need for one level of nesting.
    constexpr size_t kParseStackBytesPerLevel = 4u * 1024;

    // @internal Parser stack margin
    // Stack left untouched when the parser sizes its own budget from the stack that is really available.
    constexpr size_t kParseStackMargin = 256u * 1024;

    // @internal Parser stack budget
    // Native stack the parser may use at most: a full nesting depth plus the margin.
    constexpr size_t kParseStackBudget =
        static_cast<size_t>(kMaxParseDepth) * kParseStackBytesPerLevel + kParseStackMargin;

    // @limit Expression length | E2-008
    // How tall one expression's tree may be: the operators, indexes and slices in one chain such as `a + b + c`,
    // or the levels of nested expressions. Long chains are evaluated and freed without recursion, so this does
    // not depend on the stack; it bounds the memory and time one expression can cost.
    constexpr uint32_t kMaxExprHeight = 10000;

    // @limit Call depth | E4-017
    // How many function calls may be active at once. Independently, the stack budget below stops recursion whose
    // frames are large; whichever is reached first raises the same error.
    constexpr int kMaxCallDepth = 1000;

    // @limit Stack budget ceiling
    // The most native stack the evaluator may use, however much the host offers or an embedder asks for.
    constexpr size_t kMaxStackBudget = 64u * 1024 * 1024;

    // @limit Default stack budget | E4-017
    // The evaluator's stack budget when the real stack size cannot be determined (8 MiB in WebAssembly). When it
    // can, the budget is derived from it instead: everything but a safety margin, up to the ceiling above, and
    // never more than the thread really has even if a larger budget is configured.
#if defined(__EMSCRIPTEN__)
    constexpr size_t kDefaultStackBudget = 8u * 1024 * 1024;
#else
    constexpr size_t kDefaultStackBudget = 4u * 1024 * 1024;
#endif

    // @internal Stack per plain call
    // Native stack one interpreted call of an ordinary function is assumed to need at most. Measured at about
    // 1.3 KiB; this leaves headroom for other compilers.
    constexpr size_t kPlainCallStackBytes = 2u * 1024;

    // @limit String size | E4-026
    // The longest string a program can build.
    constexpr size_t kMaxStringBytes = 128u * 1024 * 1024;

    // @limit List and map size | E4-026
    // The most elements one list or map can hold.
    constexpr size_t kMaxCollectionItems = 32u * 1024 * 1024;

    // @limit Value nesting in output
    // How deeply nested lists and maps are written out when printed or converted to text; anything deeper is
    // shown as `[...]` or `{...}`.
    constexpr int kMaxValueDepth = 1000;

    // @limit JSON nesting | E4-025
    // How deeply nested the text `from_json` reads, or the value `to_json` writes, may be.
    constexpr int kMaxJsonDepth = 200;

    // @limit Queued async calls | E4-026
    // How many `async` calls may wait to run at once.
    constexpr size_t kMaxQueuedTasks = 1000000;

    // @limit Module nesting | E5-007
    // How many levels of modules may import other modules.
    constexpr int kMaxImportDepth = 64;

    // @limit HTTP connect timeout (ms) | E4-028
    // How long `DLC:network` waits to connect.
    constexpr int kHttpConnectTimeoutMs = 5000;

    // @limit HTTP total timeout (ms) | E4-028
    // How long one `DLC:network` request may take in all.
    constexpr int kHttpTotalTimeoutMs = 15000;

    // @limit HTTP response size | E4-028
    // The largest response `DLC:network` accepts.
    constexpr size_t kHttpMaxResponseBytes = 8u * 1024 * 1024;

    // @limit HTTP redirects | E4-028
    // How many redirects `DLC:network` follows.
    constexpr int kHttpMaxRedirects = 5;

    // @limit Pattern group nesting | E3-011
    // How deeply the groups of one pattern may nest.
    constexpr int kMaxRegexNesting = 64;

    // @limit Pattern size | E3-011
    // The longest pattern.
    constexpr size_t kMaxRegexPatternBytes = 64u * 1024;

    // @limit Pattern repeat count | E3-004
    // The largest count in a repeat such as `[num]100000`.
    constexpr int kMaxRegexQuantifier = 100000;

    // @internal Pattern cache size
    // Compiled patterns kept for reuse; the cache is emptied when it fills.
    constexpr size_t kMaxRegexCacheEntries = 512;

    // "16 MiB", "64 KiB", "500 bytes": a byte count as people write it, for messages that name a limit.
    inline std::string formatBytes(size_t n)
    {
        constexpr size_t kKiB = 1024, kMiB = 1024 * kKiB;
        if (n >= kMiB && n % kMiB == 0)
            return std::to_string(n / kMiB) + " MiB";
        if (n >= kKiB && n % kKiB == 0)
            return std::to_string(n / kKiB) + " KiB";
        return std::to_string(n) + " bytes";
    }

    // ---- how the limits depend on one another ----------------------------------------------------------------

    // The smallest stack budget the evaluator can end up with must still let plain recursion reach the call depth
    // limit. If it could not, "maximum call depth (1000)" would be a number the language promises but cannot
    // deliver on a platform that cannot report its stack size. (A function with a large body uses more stack per
    // call and meets the stack budget first; that is what the budget is for.)
    static_assert(kDefaultStackBudget >= static_cast<size_t>(kMaxCallDepth) * kPlainCallStackBytes,
                  "kDefaultStackBudget cannot hold kMaxCallDepth plain calls: raise it, or lower kMaxCallDepth");

    // A budget that was configured or derived is clamped to the ceiling, and the default must not exceed it.
    static_assert(kDefaultStackBudget <= kMaxStackBudget, "kDefaultStackBudget exceeds kMaxStackBudget");

    // Program text is held in a string, so it can be no larger than the longest string.
    static_assert(kMaxSourceBytes <= kMaxStringBytes, "a source file could not be held in a string");

    // A pattern is a string, and the shortest pattern with a group at every nesting level is two bytes per level.
    static_assert(kMaxRegexPatternBytes <= kMaxStringBytes, "kMaxRegexPatternBytes exceeds kMaxStringBytes");
    static_assert(static_cast<size_t>(kMaxRegexNesting) * 2 <= kMaxRegexPatternBytes,
                  "a pattern at kMaxRegexNesting would not fit in kMaxRegexPatternBytes");

    // A response is read into a string.
    static_assert(kHttpMaxResponseBytes <= kMaxStringBytes, "a network response could not be held in a string");

    // Queued calls are counted against the same family of limits as collections: the queue is never allowed to
    // outgrow what a list of the same calls could be.
    static_assert(kMaxQueuedTasks <= kMaxCollectionItems, "kMaxQueuedTasks exceeds kMaxCollectionItems");

    // The connect timeout is part of the total one.
    static_assert(kHttpConnectTimeoutMs <= kHttpTotalTimeoutMs, "kHttpConnectTimeoutMs exceeds kHttpTotalTimeoutMs");

}
