#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CUFF_NOINLINE __attribute__((noinline))
#define CUFF_COLD __attribute__((noinline, cold))
#define CUFF_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define CUFF_NOINLINE __declspec(noinline)
#define CUFF_COLD __declspec(noinline)
#define CUFF_ALWAYS_INLINE __forceinline
#else
#define CUFF_NOINLINE
#define CUFF_COLD
#define CUFF_ALWAYS_INLINE inline
#endif

namespace cuff
{

    // Address of the current stack frame; the stack grows downward on every
    // supported target (x86, ARM, WebAssembly's shadow stack).
    CUFF_ALWAYS_INLINE uintptr_t stackPointer()
    {
#if defined(__GNUC__) || defined(__clang__)
        return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#else
        volatile char probe = 0;
        return reinterpret_cast<uintptr_t>(&probe);
#endif
    }

    // Lowest address that leaf recursion (regex matching) may reach; 0 = no
    // floor. Set by Interpreter::run from the real stack size.
    inline uintptr_t &stackFloor()
    {
        static thread_local uintptr_t floor = 0;
        return floor;
    }

    // Bytes of stack still available below the current frame, or 0 when the
    // platform offers no way to ask (callers then fall back to fixed budgets).
    inline size_t availableStackBytes()
    {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) != 0)
            return 0;
        void *low = nullptr;
        size_t size = 0;
        int rc = pthread_attr_getstack(&attr, &low, &size);
        pthread_attr_destroy(&attr);
        if (rc != 0 || !low)
            return 0;
        const uintptr_t sp = stackPointer();
        const uintptr_t lo = reinterpret_cast<uintptr_t>(low);
        return sp > lo ? static_cast<size_t>(sp - lo) : 0;
#else
        return 0;
#endif
    }

} // namespace cuff
