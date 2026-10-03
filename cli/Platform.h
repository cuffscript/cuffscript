#pragma once

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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
#include <shellapi.h>
#undef TRUE
#undef FALSE
#undef IN
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cuff::cli::platform
{

#if defined(_WIN32)

    inline bool widen(const std::string &utf8, std::wstring &out)
    {
        out.clear();
        if (utf8.empty())
            return true;
        if (utf8.size() > static_cast<size_t>(INT_MAX))
            return false;
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0)
            return false;
        out.assign(static_cast<size_t>(n), L'\0');
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &out[0], n) == n;
    }

    inline std::string narrow(const wchar_t *w, size_t len)
    {
        if (len == 0)
            return std::string();
        if (len > static_cast<size_t>(INT_MAX))
            return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return std::string();
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &out[0], n, nullptr, nullptr);
        return out;
    }

    inline std::vector<std::string> commandLineArgs(int argc, char **argv)
    {
        std::vector<std::string> args;
        int wargc = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv)
        {
            for (int i = 0; i < wargc; ++i)
                args.push_back(narrow(wargv[i], wcslen(wargv[i])));
            LocalFree(wargv);
            return args;
        }
        for (int i = 0; i < argc; ++i)
            args.emplace_back(argv[i]);
        return args;
    }

    inline std::filesystem::path pathFromUtf8(const std::string &utf8)
    {
        std::wstring w;
        if (widen(utf8, w))
            return std::filesystem::path(w);
        return std::filesystem::path(utf8);
    }

    inline bool isTerminal(int fd)
    {
        const HANDLE h = GetStdHandle(fd == 0 ? STD_INPUT_HANDLE : (fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
        DWORD mode = 0;
        return h != INVALID_HANDLE_VALUE && h != nullptr && GetConsoleMode(h, &mode) != 0;
    }

    // Git Bash / MSYS2 / Cygwin terminals (mintty) don't give native Windows
    // programs a console: stdin is a named pipe whose name contains
    // "msys-"/"cygwin-" and "-pty". Such a pipe can't deliver keystrokes
    // interactively, so a bare `cuffsh.exe` there would just wait for
    // end-of-input and look hung (`winpty cuffsh.exe` provides a real console).
    inline bool isMsysPty(int fd)
    {
        const HANDLE h = GetStdHandle(fd == 0 ? STD_INPUT_HANDLE : (fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
        if (h == INVALID_HANDLE_VALUE || h == nullptr || GetFileType(h) != FILE_TYPE_PIPE)
            return false;
        alignas(FILE_NAME_INFO) char buf[sizeof(FILE_NAME_INFO) + MAX_PATH * sizeof(WCHAR)];
        if (!GetFileInformationByHandleEx(h, FileNameInfo, buf, sizeof buf))
            return false;
        const FILE_NAME_INFO *info = reinterpret_cast<const FILE_NAME_INFO *>(buf);
        const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
        return (name.find(L"msys-") != std::wstring::npos || name.find(L"cygwin-") != std::wstring::npos) &&
               name.find(L"-pty") != std::wstring::npos;
    }

    // Reads at most maxBytes + 1 bytes so that an oversized file is visible to
    // the caller without ever being held in memory in full.
    inline bool readFile(const std::string &pathUtf8, size_t maxBytes, std::string &out, std::string &error)
    {
        out.clear();
        std::wstring w;
        if (pathUtf8.empty() || !widen(pathUtf8, w))
        {
            error = "invalid file name";
            return false;
        }
        const HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            error = "cannot open file";
            return false;
        }
        if (GetFileType(h) != FILE_TYPE_DISK)
        {
            CloseHandle(h);
            error = "not a regular file";
            return false;
        }
        const size_t cap = maxBytes + 1;
        char buf[1 << 16];
        while (out.size() < cap)
        {
            const DWORD want = static_cast<DWORD>(std::min(sizeof buf, cap - out.size()));
            DWORD got = 0;
            if (!ReadFile(h, buf, want, &got, nullptr))
            {
                CloseHandle(h);
                error = "read error";
                return false;
            }
            if (got == 0)
                break;
            out.append(buf, got);
        }
        CloseHandle(h);
        return true;
    }

#else

    inline std::vector<std::string> commandLineArgs(int argc, char **argv)
    {
        return std::vector<std::string>(argv, argv + argc);
    }

    inline std::filesystem::path pathFromUtf8(const std::string &utf8)
    {
        return std::filesystem::path(utf8);
    }

    inline bool isTerminal(int fd) { return isatty(fd) != 0; }

    inline bool isMsysPty(int) { return false; }

    inline bool readFile(const std::string &pathUtf8, size_t maxBytes, std::string &out, std::string &error)
    {
        out.clear();
        if (pathUtf8.empty())
        {
            error = "invalid file name";
            return false;
        }
        const int fd = open(pathUtf8.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
        {
            error = "cannot open file";
            return false;
        }
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        {
            close(fd);
            error = "not a regular file";
            return false;
        }
        const size_t cap = maxBytes + 1;
        char buf[1 << 16];
        while (out.size() < cap)
        {
            const ssize_t got = read(fd, buf, std::min(sizeof buf, cap - out.size()));
            if (got < 0)
            {
                close(fd);
                error = "read error";
                return false;
            }
            if (got == 0)
                break;
            out.append(buf, static_cast<size_t>(got));
        }
        close(fd);
        return true;
    }

#endif

    // Reads all of stdin (bounded the same way readFile bounds a script
    // file: up to maxBytes + 1, so an oversized stream is visible to the
    // caller rather than silently truncated or fully buffered in memory).
    // Used only when stdin isn't a terminal — see Repl::run().
    inline bool readAllStdin(size_t maxBytes, std::string &out)
    {
        out.clear();
        const size_t cap = maxBytes + 1;
        char buf[1 << 16];
#if defined(_WIN32)
        const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        while (out.size() < cap)
        {
            DWORD got = 0;
            if (!ReadFile(h, buf, static_cast<DWORD>(std::min(sizeof buf, cap - out.size())), &got, nullptr))
            {
                // A pipe whose writer has finished reports end-of-input as a
                // failed read (ERROR_BROKEN_PIPE) — that is the normal end of
                // `something | cuffsh`, not an error.
                const DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
                    break;
                return false;
            }
            if (got == 0)
                break;
            out.append(buf, got);
        }
#else
        while (out.size() < cap)
        {
            const ssize_t got = read(0, buf, std::min(sizeof buf, cap - out.size()));
            if (got < 0)
                return false;
            if (got == 0)
                break;
            out.append(buf, static_cast<size_t>(got));
        }
#endif
        return true;
    }

    inline void stripBom(std::string &s)
    {
        if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
            static_cast<unsigned char>(s[2]) == 0xBF)
            s.erase(0, 3);
    }

    // The engine turns its narrow path strings back into paths with the
    // platform's native narrow encoding, so hand it exactly that.
    inline std::string enginePath(const std::string &utf8)
    {
        try
        {
            return pathFromUtf8(utf8).string();
        }
        catch (const std::exception &)
        {
            return utf8;
        }
    }

    inline std::string parentDirectory(const std::string &pathUtf8)
    {
        try
        {
            const std::filesystem::path parent = pathFromUtf8(pathUtf8).parent_path();
            return parent.empty() ? std::string(".") : parent.string();
        }
        catch (const std::exception &)
        {
            return ".";
        }
    }

} // namespace cuff::cli::platform
