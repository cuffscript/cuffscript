#include "engine/CuffEngine.h"
#include "engine/EngineOptionsCli.h"
#include "engine/debug/BytecodePrinter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#include <atomic>
#include <climits>
#include <memory>
#include <streambuf>
#include <fcntl.h>
#include <io.h>
#include <cwchar>
#include <shellapi.h> // CommandLineToArgvW; <windows.h> itself comes with the engine (engine/common/Attributes.h)
#endif

namespace
{
    void printUsage(std::ostream &out, const char *prog)
    {
        out << "Usage: " << prog << " [options] [script.cuff [arguments...]]\n"
            << "  (no file, or -)        read the script from stdin\n"
            << "  arguments...           given to the script as args(); everything after the script is one\n"
            << "  --ast                  print tokens + AST instead of running the script\n"
            << "  --bytecode             run the script, printing each piece of bytecode as it is compiled\n"
            << "  -h, --help             show this help and exit\n"
            << cuff::engineOptionsHelp()
            << "By default modules may be loaded from the script's own directory only.\n";
    }

    // Reads at most limit+1 bytes so an oversized input is detected (and
    // rejected by the engine) without ever being held in memory in full.
    std::string readLimited(std::istream &in)
    {
        const size_t cap = cuff::limits::kMaxSourceBytes + 1;
        std::string data;
        char buf[1 << 16];
        while (data.size() < cap && in.good())
        {
            in.read(buf, static_cast<std::streamsize>(std::min(sizeof buf, cap - data.size())));
            data.append(buf, static_cast<size_t>(in.gcount()));
        }
        return data;
    }

#ifdef _WIN32
    // The engine's text is UTF-8. A Windows console writes and reads text in a code page of its own (949 for Korean, for
    // example), so while cuffc runs, the console shows UTF-8, and a script read from the keyboard, and what input()
    // reads, arrive as UTF-8. A pipe or a file is passed through untouched. Everything for that is in this file.
    std::atomic<UINT> g_savedOutputCp{0};

    BOOL WINAPI restoreCodePage(DWORD)
    {
        const UINT cp = g_savedOutputCp.exchange(0);
        if (cp != 0)
            SetConsoleOutputCP(cp);
        return 0; // not handled: the default handler ends the process, as it did before
    }

    bool isConsole(DWORD which)
    {
        const HANDLE h = GetStdHandle(which);
        DWORD mode = 0;
        return h != INVALID_HANDLE_VALUE && h != nullptr && GetConsoleMode(h, &mode) != 0;
    }

    std::string narrow(const wchar_t *w)
    {
        const size_t len = wcslen(w);
        if (len == 0 || len > static_cast<size_t>(INT_MAX))
            return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return std::string();
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &out[0], n, nullptr, nullptr);
        return out;
    }

    // The command line as UTF-8: argv is in the ANSI code page, which cannot hold every file name.
    std::vector<std::string> commandLineArgs(int argc, char **argv)
    {
        std::vector<std::string> args;
        int wargc = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv)
        {
            for (int i = 0; i < wargc; ++i)
                args.push_back(narrow(wargv[i]));
            LocalFree(wargv);
            return args;
        }
        for (int i = 0; i < argc; ++i)
            args.emplace_back(argv[i]);
        return args;
    }

    // Adds UTF-16 text to `out` as UTF-8, without '\r'. A lone surrogate becomes U+FFFD. A high surrogate at the end of
    // a chunk waits in `carry` for the next one.
    void appendUtf16AsUtf8(const wchar_t *w, size_t n, std::string &out, wchar_t &carry)
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
            const unsigned lo = w[0];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                emit(0x10000u + ((static_cast<unsigned>(carry) - 0xD800u) << 10) + (lo - 0xDC00u));
                i = 1;
            }
            else
                emit(0xFFFD);
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
                    carry = static_cast<wchar_t>(cp);
                    break;
                }
                else
                    cp = 0xFFFD;
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
                cp = 0xFFFD;
            emit(cp);
        }
    }

    // std::cin on a real console: reads through ReadConsoleW and hands the text out as UTF-8 (Ctrl+Z ends the input).
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
                if (wide[0] == 0x1A)
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

    class Utf8Console
    {
    public:
        Utf8Console()
        {
            if (isConsole(STD_OUTPUT_HANDLE) || isConsole(STD_ERROR_HANDLE))
            {
                const UINT cp = GetConsoleOutputCP();
                if (cp != 0 && cp != CP_UTF8 && SetConsoleOutputCP(CP_UTF8))
                {
                    g_savedOutputCp.store(cp);
                    SetConsoleCtrlHandler(restoreCodePage, 1);
                }
            }
            if (isConsole(STD_INPUT_HANDLE))
            {
                in_ = std::make_unique<ConsoleInBuf>(GetStdHandle(STD_INPUT_HANDLE));
                oldIn_ = std::cin.rdbuf(in_.get());
            }
        }
        ~Utf8Console()
        {
            if (oldIn_)
                std::cin.rdbuf(oldIn_);
            std::cout.flush();
            if (g_savedOutputCp.load() != 0)
            {
                SetConsoleCtrlHandler(restoreCodePage, 0);
                restoreCodePage(0);
            }
        }
        Utf8Console(const Utf8Console &) = delete;
        Utf8Console &operator=(const Utf8Console &) = delete;

    private:
        std::unique_ptr<ConsoleInBuf> in_;
        std::streambuf *oldIn_ = nullptr;
    };
#endif
}

static int runCli(int argc, char *argv[])
{
    std::ios::sync_with_stdio(false);
#ifdef _WIN32
    const Utf8Console console;
    // The arguments as UTF-8: argv is in the ANSI code page, which cannot hold every file name.
    const std::vector<std::string> all = commandLineArgs(argc, argv);
    const std::vector<std::string> args(all.begin() + (all.empty() ? 0 : 1), all.end());
    const std::string prog = all.empty() ? std::string("cuffc") : all[0];
#else
    const std::vector<std::string> args(argv + 1, argv + argc);
    const std::string prog = argc > 0 ? argv[0] : "cuffc";
#endif

    bool debugMode = false;
    bool onlyPaths = false; // after "--" every argument is a file name, even one that starts with '-'
    std::string path;
    cuff::Engine::Options options;

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &arg = args[i];

        if (!onlyPaths && arg.size() > 1 && arg[0] == '-')
        {
            if (arg == "--")
            {
                onlyPaths = true;
                continue;
            }
            if (arg == "--ast" || arg == "--tokens" || arg == "--debug")
            {
                debugMode = true;
                continue;
            }
            if (arg == "--bytecode")
            {
                options.onCompiled = [](const cuff::bc::Proto &proto) { std::cout << cuff::BytecodePrinter::print(proto); };
                continue;
            }
            if (arg == "-h" || arg == "--help")
            {
                printUsage(std::cout, prog.c_str());
                return 0;
            }
            std::string error;
            switch (cuff::parseEngineOption(args, i, options, error))
            {
            case cuff::OptionStatus::Consumed:
                continue;
            case cuff::OptionStatus::Invalid:
                std::cerr << "Error: " << error << "\n";
                return 2;
            case cuff::OptionStatus::NotEngineOption:
                std::cerr << "Error: unknown option '" << arg << "' (see --help)\n";
                return 2;
            }
        }

        // The first argument that is not an option is the script; every one after it is the script's own (args()),
        // even one that looks like an option.
        if (arg != "-")
            path = arg;
        options.arguments.assign(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
        break;
    }

    std::string source;
    std::string scriptDir = ".";

    if (!path.empty())
    {
        // As the engine reads a path it is given: UTF-8 (on Windows through the wide form, so a name outside the
        // ANSI code page opens too).
        const std::filesystem::path p = cuff::hostPath(path);
        // Binary: the engine understands CRLF itself. A folder opens as a stream on some systems and reads as
        // nothing, which would run as an empty program; a pipe or a device is read as it comes.
        std::error_code isDirError;
        std::ifstream file;
        if (!std::filesystem::is_directory(p, isDirError))
            file.open(p, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Error: Cannot open file '" << path << "'\n";
            return 1;
        }
        source = readLimited(file);
        // UTF-8, as the engine reads every path it is given (utf8OfPath: string() throws on Windows for a name
        // outside the ANSI code page).
        scriptDir = p.has_parent_path() ? cuff::utf8OfPath(p.parent_path()) : std::string(".");
    }
    else
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        source = readLimited(std::cin);
    }

    if (debugMode)
    {
        const cuff::Engine::Parsed parsed = cuff::Engine::parse(source);
        cuff::Engine::dump(parsed);
        return parsed.success ? 0 : 1;
    }

    cuff::Engine::Outcome result = cuff::Engine::execute(std::move(source), scriptDir, options);
    if (!result.success)
    {
        // A report that is empty could not be written: there was no memory left for it.
        if (result.error.empty())
            throw std::bad_alloc();
        std::cerr << "ERROR: " << result.error << "\n";
        return 1;
    }
    return result.exited ? result.exitStatus : 0;
}

int main(int argc, char *argv[])
{
    // Nothing leaves main as an exception: one that did would end the process with an abort and no report. What can
    // still arrive here is a failure outside a run, such as no memory while the arguments are read.
    try
    {
        return runCli(argc, argv);
    }
    catch (const std::bad_alloc &)
    {
        std::fputs("ERROR: [E6-003] Resource Limit Error: the program ran out of memory\n", stderr);
    }
    catch (const std::exception &e)
    {
        std::fputs("ERROR: Internal error: ", stderr);
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    }
    catch (...)
    {
        std::fputs("ERROR: Internal error: unknown exception\n", stderr);
    }
    return 1;
}
