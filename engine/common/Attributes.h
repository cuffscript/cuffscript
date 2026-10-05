#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windef.h #defines TRUE/FALSE/IN as macros, which would mangle TokenType's enumerators.
#undef TRUE
#undef FALSE
#undef IN
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten/stack.h>
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

    CUFF_ALWAYS_INLINE uintptr_t stackPointer()
    {
#if defined(__GNUC__) || defined(__clang__)
        return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#else
        volatile char probe = 0;
        return reinterpret_cast<uintptr_t>(&probe);
#endif
    }

    inline uintptr_t &stackFloor()
    {
        static thread_local uintptr_t floor = 0;
        return floor;
    }

    // Bytes of stack left below the current frame; 0 when the platform can't tell.
    inline size_t availableStackBytes()
    {
#if defined(__EMSCRIPTEN__)
        return emscripten_stack_get_free();
#elif defined(_WIN32)
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        if (low == 0)
            return 0;
        const uintptr_t sp = stackPointer();
        return sp > low ? static_cast<size_t>(sp - low) : 0;
#elif defined(__APPLE__)
        void *base = pthread_get_stackaddr_np(pthread_self());
        size_t size = pthread_get_stacksize_np(pthread_self());
        if (!base || size == 0)
            return 0;
        const uintptr_t hi = reinterpret_cast<uintptr_t>(base);
        const uintptr_t lo = hi - size;
        const uintptr_t sp = stackPointer();
        return sp > lo ? static_cast<size_t>(sp - lo) : 0;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
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

}
