#pragma once

// Owns: TTY detection, VT/color setup for output, terminal width, and the
// process-level signal / Ctrl+C handling that governs execution-time
// behaviour. It does not read the shell's prompt input — LineEditor.h does,
// because it needs raw per-keystroke access (to see Shift+Enter).
// ConsoleInBuf below exists only so the engine's own input() builtin gets
// correct UTF-8 text on Windows; LineEditor never touches std::cin except in
// its plain-line fallback.

#include "Interrupt.h"
#include "Platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>

#if !defined(_WIN32)
#include <csignal>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace cuff::cli
{

    // Terminal modes LineEditor switches on while it reads a line (see
    // LineEditor.h): bracketed paste, and — only on terminals that confirmed
    // support — the Kitty keyboard protocol. The "off" sequences live here so
    // the termination path below can send them too.
    inline constexpr char kPasteOn[] = "\x1b[?2004h";
    inline constexpr char kPasteOff[] = "\x1b[?2004l";
    inline constexpr char kKittyPush[] = "\x1b[>1u";
    inline constexpr char kKittyPop[] = "\x1b[<1u";

#if defined(_WIN32)

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

    // Console state to put back if the process has to exit from the Ctrl+C
    // handler (which skips destructors).
    inline std::atomic<UINT> g_savedOutputCp{0};
    inline std::atomic<DWORD> g_savedOutMode{0};
    inline std::atomic<DWORD> g_savedErrMode{0};
    inline std::atomic<bool> g_outModeSaved{false};
    inline std::atomic<bool> g_errModeSaved{false};

    inline void restoreConsoleForExit()
    {
        if (g_outModeSaved.load())
            SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_savedOutMode.load());
        if (g_errModeSaved.load())
            SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), g_savedErrMode.load());
        if (g_savedOutputCp.load() != 0)
            SetConsoleOutputCP(g_savedOutputCp.load());
    }

    // Appends `n` UTF-16 code units as UTF-8 to `out`, stripping '\r' and
    // replacing lone/invalid surrogates with U+FFFD. A high surrogate at the
    // very end of this chunk is held in `carry` (owned by the caller, who
    // must pass the same variable across chunks of one logical read) instead
    // of being emitted, so a surrogate pair split across two ReadConsoleW
    // calls is still decoded correctly.
    inline void appendUtf16AsUtf8(const wchar_t *w, size_t n, std::string &out, wchar_t &carry)
    {
        auto emit = [&out](unsigned cp)
        {
            if (cp < 0x80)
                out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        };

        size_t i = 0;
        if (carry != 0 && n > 0)
        {
            const wchar_t lo = w[0];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                emit(0x10000u + ((static_cast<unsigned>(carry) - 0xD800u) << 10) + (static_cast<unsigned>(lo) - 0xDC00u));
                i = 1;
            }
            else
            {
                emit(0xFFFD); // unpaired high surrogate from the previous chunk
            }
            carry = 0;
        }

        for (; i < n; ++i)
        {
            unsigned cp = w[i];
            if (cp == L'\r')
                continue;
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                if (i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF)
                {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (static_cast<unsigned>(w[i + 1]) - 0xDC00u);
                    ++i;
                }
                else if (i + 1 == n)
                {
                    carry = static_cast<wchar_t>(cp); // resolved once the next chunk arrives
                    break;
                }
                else
                {
                    cp = 0xFFFD;
                }
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
                cp = 0xFFFD;
            }
            emit(cp);
        }
    }

    // Reads a real console through ReadConsoleW, so non-ASCII input arrives
    // intact, and hands it out as UTF-8. Installed as std::cin's buffer
    // purely so the language's own input() builtin (which calls
    // std::getline(std::cin, ...) directly, unmodified) gets correct UTF-8
    // text on Windows.
    class ConsoleInBuf : public std::streambuf
    {
    public:
        explicit ConsoleInBuf(HANDLE handle) : handle_(handle) {}

    protected:
        int_type underflow() override
        {
            if (gptr() < egptr())
                return traits_type::to_int_type(*gptr());

            while (true)
            {
                wchar_t wide[4096];
                DWORD got = 0;
                if (!ReadConsoleW(handle_, wide, static_cast<DWORD>(sizeof(wide) / sizeof(wide[0])), &got, nullptr) || got == 0)
                    return traits_type::eof();
                if (wide[0] == 0x1A) // Ctrl+Z
                    return traits_type::eof();

                utf8_.clear();
                appendUtf16AsUtf8(wide, got, utf8_, pendingHigh_);
                if (utf8_.empty())
                    continue;
                setg(&utf8_[0], &utf8_[0], &utf8_[0] + utf8_.size());
                return traits_type::to_int_type(*gptr());
            }
        }

    private:
        HANDLE handle_;
        wchar_t pendingHigh_ = 0;
        std::string utf8_;
    };

    // Runs on a system-created thread: only touches atomics and raw handle
    // calls, so it is safe regardless of what the main thread is doing. The
    // engine, unmodified, has no hook to ask a running computation to stop,
    // so if execution is in progress the only safe option is to exit.
    // While idle this does nothing: LineEditor reads Ctrl+C itself as a raw
    // key (it switches the console's processed-input handling off for the
    // duration of a read), so this only ever matters during execution.
    inline BOOL WINAPI consoleCtrlHandler(DWORD type)
    {
        if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT)
            return 0;
        if (g_programRunning.load(std::memory_order_relaxed))
        {
            static const char msg[] = "\n[cuffsh] Can't safely interrupt while running, so the shell is exiting.\n";
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, nullptr);
            restoreConsoleForExit();
            ExitProcess(130);
        }
        return 1;
    }

#else

    // State the signal handlers need to put the terminal back the way it was.
    inline struct termios g_savedTermios;
    inline std::atomic<bool> g_rawActive{false};
    inline std::atomic<bool> g_kittyActive{false};
    inline std::atomic<int> g_uiFd{-1};

    // Async-signal-safe: only write(2) and tcsetattr(3).
    inline void restoreTerminalFromSignal()
    {
        if (!g_rawActive.exchange(false))
            return;
        const int fd = g_uiFd.load();
        if (fd >= 0)
        {
            ssize_t ignored;
            if (g_kittyActive.exchange(false))
                ignored = write(fd, kKittyPop, sizeof(kKittyPop) - 1);
            ignored = write(fd, kPasteOff, sizeof(kPasteOff) - 1);
            (void)ignored;
        }
        tcsetattr(0, TCSANOW, &g_savedTermios);
    }

    // Same idea as the Windows handler: while running there is no safe way to
    // stop just the computation, so exit. Only reachable during execution —
    // while idle LineEditor has ISIG off and reads Ctrl+C as a byte itself.
    inline void sigintHandler(int)
    {
        if (g_programRunning.load(std::memory_order_relaxed))
        {
            static const char msg[] = "\n[cuffsh] Can't safely interrupt while running, so the shell is exiting.\n";
            ssize_t ignored = write(2, msg, sizeof(msg) - 1);
            (void)ignored;
            _exit(130);
        }
    }

    // Being killed (SIGTERM, terminal closed -> SIGHUP) while a line is being
    // read must not leave the user's terminal in raw mode with echo off.
    inline void fatalSignalHandler(int sig)
    {
        restoreTerminalFromSignal();
        signal(sig, SIG_DFL);
        raise(sig);
    }

#endif

    class Terminal
    {
    public:
        // Where the interactive prompt and line echo go: stdout if that is a
        // terminal, otherwise stderr if that is (so `cuffsh > log.txt` still
        // shows what you are typing), otherwise nowhere.
        enum class Ui
        {
            None,
            Stdout,
            Stderr
        };

        Terminal()
        {
            std::ios::sync_with_stdio(false);
            stdinTty_ = platform::isTerminal(0);
            stdoutTty_ = platform::isTerminal(1);
            stderrTty_ = platform::isTerminal(2);
            const char *noColor = std::getenv("NO_COLOR");
            allowColor_ = !(noColor && *noColor);
            setup();
            if (stdoutTty_)
                std::cout.setf(std::ios::unitbuf);
        }

        Terminal(const Terminal &) = delete;
        Terminal &operator=(const Terminal &) = delete;

        ~Terminal()
        {
#if defined(_WIN32)
            if (oldCinBuf_)
                std::cin.rdbuf(oldCinBuf_);
            if (ctrlInstalled_)
                SetConsoleCtrlHandler(consoleCtrlHandler, 0);
            if (outModeChanged_)
                SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), oldOutMode_);
            if (errModeChanged_)
                SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), oldErrMode_);
            if (cpChanged_)
                SetConsoleOutputCP(oldOutputCp_);
#endif
        }

        bool inputIsTerminal() const { return stdinTty_; }
        bool outputIsTerminal() const { return stdoutTty_; }
        bool errorIsTerminal() const { return stderrTty_; }

        Ui ui() const { return stdoutTty_ ? Ui::Stdout : (stderrTty_ ? Ui::Stderr : Ui::None); }
        std::ostream &uiStream() { return ui() == Ui::Stderr ? std::cerr : std::cout; }

        // True if the UI terminal understands the cursor-movement escape
        // sequences the line editor needs (not TERM=dumb, and on Windows only
        // if VT processing could be switched on).
        bool uiSupportsVt() const
        {
            switch (ui())
            {
            case Ui::Stdout:
                return outVt_;
            case Ui::Stderr:
                return errVt_;
            default:
                return false;
            }
        }

        void disableColor() { allowColor_ = false; }
        bool colorOut() const { return allowColor_ && stdoutTty_ && outVt_; }
        bool colorErr() const { return allowColor_ && stderrTty_ && errVt_; }

        std::string paint(const char *sgr, const std::string &text, bool toStderr = false) const
        {
            if (!(toStderr ? colorErr() : colorOut()))
                return text;
            return std::string("\x1b[") + sgr + "m" + text + "\x1b[0m";
        }

        std::string paintUi(const char *sgr, const std::string &text) const
        {
            return paint(sgr, text, ui() == Ui::Stderr);
        }

        void clearScreen()
        {
            if (uiSupportsVt())
            {
                std::ostream &o = uiStream();
                o << "\x1b[2J\x1b[3J\x1b[H" << std::flush;
            }
        }

        // Width in columns of the UI terminal; 80 if it can't be determined.
        int columns() const
        {
            int w = 0;
#if defined(_WIN32)
            const HANDLE h = GetStdHandle(ui() == Ui::Stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (GetConsoleScreenBufferInfo(h, &info))
                w = info.srWindow.Right - info.srWindow.Left + 1;
#else
            struct winsize ws;
            if (ioctl(ui() == Ui::Stderr ? 2 : 1, TIOCGWINSZ, &ws) == 0)
                w = ws.ws_col;
            if (w <= 0)
            {
                if (const char *c = std::getenv("COLUMNS"))
                    w = std::atoi(c);
            }
#endif
            return w > 0 ? w : 80;
        }

        // Height in rows of the UI terminal; 24 if it can't be determined.
        int rows() const
        {
            int h = 0;
#if defined(_WIN32)
            const HANDLE hd = GetStdHandle(ui() == Ui::Stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (GetConsoleScreenBufferInfo(hd, &info))
                h = info.srWindow.Bottom - info.srWindow.Top + 1;
#else
            struct winsize ws;
            if (ioctl(ui() == Ui::Stderr ? 2 : 1, TIOCGWINSZ, &ws) == 0)
                h = ws.ws_row;
            if (h <= 0)
            {
                if (const char *l = std::getenv("LINES"))
                    h = std::atoi(l);
            }
#endif
            return h > 0 ? h : 24;
        }

    private:
        bool stdinTty_ = false;
        bool stdoutTty_ = false;
        bool stderrTty_ = false;
        bool allowColor_ = true;
        bool outVt_ = false;
        bool errVt_ = false;

#if defined(_WIN32)
        UINT oldOutputCp_ = 0;
        bool cpChanged_ = false;
        DWORD oldOutMode_ = 0;
        DWORD oldErrMode_ = 0;
        bool outModeChanged_ = false;
        bool errModeChanged_ = false;
        bool ctrlInstalled_ = false;
        std::unique_ptr<ConsoleInBuf> inBuf_;
        std::streambuf *oldCinBuf_ = nullptr;

        static bool enableVt(HANDLE handle, DWORD &savedMode, bool &changed)
        {
            DWORD mode = 0;
            if (handle == INVALID_HANDLE_VALUE || handle == nullptr || !GetConsoleMode(handle, &mode))
                return false;
            savedMode = mode;
            if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
                return true;
            if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                return false;
            changed = true;
            return true;
        }

        void setup()
        {
            if (stdoutTty_ || stderrTty_)
            {
                oldOutputCp_ = GetConsoleOutputCP();
                if (oldOutputCp_ != 0 && SetConsoleOutputCP(CP_UTF8))
                {
                    cpChanged_ = true;
                    g_savedOutputCp.store(oldOutputCp_);
                }
            }
            if (stdoutTty_)
            {
                outVt_ = enableVt(GetStdHandle(STD_OUTPUT_HANDLE), oldOutMode_, outModeChanged_);
                if (outModeChanged_)
                {
                    g_savedOutMode.store(oldOutMode_);
                    g_outModeSaved.store(true);
                }
            }
            if (stderrTty_)
            {
                errVt_ = enableVt(GetStdHandle(STD_ERROR_HANDLE), oldErrMode_, errModeChanged_);
                if (errModeChanged_)
                {
                    g_savedErrMode.store(oldErrMode_);
                    g_errModeSaved.store(true);
                }
            }
            if (SetConsoleCtrlHandler(consoleCtrlHandler, 1))
                ctrlInstalled_ = true;
            if (stdinTty_)
            {
                // Only used for the engine's own input() builtin (via
                // std::cin) — see the ConsoleInBuf class comment.
                inBuf_ = std::make_unique<ConsoleInBuf>(GetStdHandle(STD_INPUT_HANDLE));
                oldCinBuf_ = std::cin.rdbuf(inBuf_.get());
            }
        }
#else
        void setup()
        {
            const char *term = std::getenv("TERM");
            const bool capable = term && *term && std::strcmp(term, "dumb") != 0;
            outVt_ = stdoutTty_ && capable;
            errVt_ = stderrTty_ && capable;
            g_uiFd.store(stdoutTty_ ? 1 : (stderrTty_ ? 2 : -1));

            struct sigaction sa;
            std::memset(&sa, 0, sizeof sa);
            sigemptyset(&sa.sa_mask);
            // SA_RESTART: a Ctrl+C at the plain-line fallback prompt must not
            // make std::cin's read fail and look like end-of-input.
            sa.sa_flags = SA_RESTART;
            sa.sa_handler = sigintHandler;
            sigaction(SIGINT, &sa, nullptr);

            sa.sa_flags = 0;
            sa.sa_handler = fatalSignalHandler;
            sigaction(SIGTERM, &sa, nullptr);
            sigaction(SIGHUP, &sa, nullptr);
        }
#endif
    };

} // namespace cuff::cli
