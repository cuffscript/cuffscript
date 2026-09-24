#pragma once

#include <cstddef>
#include <cstdint>

namespace cuff::limits
{

    constexpr size_t kMaxSourceBytes = 16u * 1024 * 1024;
    constexpr int kMaxParseDepth = 512;

    // The parser recurses on the real C++ stack (there's no bytecode to
    // unwind into), and unlike the interpreter's per-node stack check, a
    // single deeply-nested construct — f-strings, parenthesized expressions —
    // costs several kilobytes of native stack per level. kMaxParseDepth alone
    // doesn't protect a small stack (a 1 MiB thread, the Windows default,
    // could crash well before the counter ever fires), so the parser also
    // checks real remaining stack; this budget scales with kMaxParseDepth so
    // raising one keeps the other honest automatically.
    constexpr size_t kParseStackBytesPerLevel = 4u * 1024;
    constexpr size_t kParseStackMargin = 256u * 1024;
    constexpr size_t kParseStackBudget =
        static_cast<size_t>(kMaxParseDepth) * kParseStackBytesPerLevel + kParseStackMargin;
    constexpr uint32_t kMaxExprHeight = 10000;

    constexpr int kMaxCallDepth = 1000;
    constexpr size_t kMaxStackBudget = 64u * 1024 * 1024;
#if defined(__EMSCRIPTEN__)
    constexpr size_t kDefaultStackBudget = 8u * 1024 * 1024;
#else
    constexpr size_t kDefaultStackBudget = 4u * 1024 * 1024;
#endif

    constexpr size_t kMaxStringBytes = 128u * 1024 * 1024;
    constexpr size_t kMaxCollectionItems = 32u * 1024 * 1024;
    constexpr int kMaxValueDepth = 1000;
    constexpr int kMaxJsonDepth = 200;
    constexpr size_t kMaxQueuedTasks = 1000000;

    constexpr int kMaxImportDepth = 64;

    // DLC:network (engine/net/HttpClient.h) — plain HTTP only, safe-by-default
    // SSRF guard (see net::isPrivateOrLoopback). Independent of the string/
    // collection caps above: a response is capped far below kMaxStringBytes
    // because a network peer's declared Content-Length can't be trusted the
    // way an in-script value can.
    constexpr int kHttpConnectTimeoutMs = 5000;
    constexpr int kHttpTotalTimeoutMs = 15000;
    constexpr size_t kHttpMaxResponseBytes = 8u * 1024 * 1024;
    constexpr int kHttpMaxRedirects = 5;

    constexpr int kMaxRegexNesting = 64;
    constexpr size_t kMaxRegexPatternBytes = 64u * 1024;
    constexpr int kMaxRegexQuantifier = 100000;
    constexpr size_t kMaxRegexCacheEntries = 512;

} // namespace cuff::limits
