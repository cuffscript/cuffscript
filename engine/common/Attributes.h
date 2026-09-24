#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
// Vista+ API; needs _WIN32_WINNT >= 0x0600 (set below if the includer hasn't
// already asked for a newer one).
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
// windef.h (pulled in by windows.h even with WIN32_LEAN_AND_MEAN) #defines
// TRUE/FALSE/IN as plain macros, which silently mangles cuff::TokenType's
// own TRUE/FALSE/IN enumerators wherever this header is included before
// TokenTypes.h — every use of TokenType::TRUE etc. would macro-expand into
// nonsense instead of a compile error, since TRUE/FALSE are just `1`/`0`.
// Undefining them here, right after windows.h, is the standard fix and
// affects nothing else: nothing downstream in this engine uses the Win32
// macros by those names.
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
    // platform offers no way to ask (callers then fall back to fixed
    // budgets — see limits::kDefaultStackBudget / kParseStackBudget).
    // Implemented for every shipped target: Linux, macOS, Windows and
    // WebAssembly/Emscripten, so both the parser's and the interpreter's
    // stack guards are genuinely adaptive everywhere, not just on the
    // platform this was first written on.
    inline size_t availableStackBytes()
    {
#if defined(__EMSCRIPTEN__)
        // Emscripten's shadow stack is a fixed size set at link time (this
        // project links with -s STACK_SIZE, see the `wasm` Makefile target),
        // but it still reports real, current headroom rather than a guess.
        return emscripten_stack_get_free();
#elif defined(_WIN32)
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        if (low == 0)
            return 0;
        const uintptr_t sp = stackPointer();
        return sp > low ? static_cast<size_t>(sp - low) : 0;
#elif defined(__APPLE__)
        // pthread_get_stackaddr_np returns the *base* (high address, since the
        // stack grows down) of the calling thread's stack, for the main
        // thread as well as any pthread — no attr object needed.
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

} // namespace cuff
