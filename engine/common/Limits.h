#pragma once

#include <cstddef>
#include <cstdint>

namespace cuff::limits
{

    constexpr size_t kMaxSourceBytes = 16u * 1024 * 1024;
    constexpr int kMaxParseDepth = 256;
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

    constexpr int kMaxRegexNesting = 64;
    constexpr size_t kMaxRegexPatternBytes = 64u * 1024;
    constexpr int kMaxRegexQuantifier = 100000;
    constexpr size_t kMaxRegexCacheEntries = 512;

} // namespace cuff::limits
