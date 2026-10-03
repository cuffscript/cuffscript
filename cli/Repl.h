#pragma once

// This REPL only ever calls into the engine through the same public surface
// main.cpp/cuffc uses (cuff::CuffEngine::run for parsing, cuff::Interpreter's
// ordinary constructor + run() for execution). Nothing under engine/ is
// modified: one long-lived Interpreter is simply kept alive and reused across
// entries instead of being constructed fresh per run, which the class already
// supports (globalEnv_/userById_ are ordinary members that run() does not
// reset).
//
// What is typed is parsed and run exactly once. Errors are printed exactly the
// way the engine's own front end (cuffc) prints them — "ERROR: " followed by
// the engine's message — rather than anything this shell invents.

#include "../engine/CuffEngine.h"
#include "../engine/common/Limits.h"
#include "Banner.h"
#include "LineEditor.h"
#include "Platform.h"
#include "Terminal.h"
#include "Text.h"
#include "Version.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace cuff::cli
{

    // Marks "a chunk is actively executing" for the Ctrl+C handler in
    // Terminal.h: the unmodified engine has no hook to ask a running
    // computation to stop, so a Ctrl+C received while this guard is alive
    // exits the whole process instead of pretending to cancel gracefully.
    // Always cleared on the way out, including when run() throws.
    struct RunningGuard
    {
        RunningGuard() { g_programRunning.store(true, std::memory_order_relaxed); }
        ~RunningGuard() { g_programRunning.store(false, std::memory_order_relaxed); }
        RunningGuard(const RunningGuard &) = delete;
        RunningGuard &operator=(const RunningGuard &) = delete;
    };

    class Repl
    {
    public:
        struct Options
        {
            cuff::Interpreter::Config engineConfig;
            bool showBanner = true;
            bool useColor = true;
            std::string startupFile; // run before the prompt appears (empty = none)
        };

        Repl(Terminal &term, Options opts)
            : term_(term), opts_(std::move(opts))
        {
            interp_ = std::make_unique<cuff::Interpreter>(opts_.engineConfig);
            if (!opts_.useColor)
                term_.disableColor();
        }

        // Runs a .cuff file in the current session, exactly the way cuffc runs
        // a script. Used for the startup file and for ':load'. Never throws.
        void loadFile(const std::string &pathUtf8)
        {
            std::string source, ioError;
            if (!platform::readFile(pathUtf8, cuff::limits::kMaxSourceBytes, source, ioError))
            {
                printShellError("Cannot open file '" + text::sanitize(pathUtf8) + "' (" + ioError + ")");
                return;
            }
            if (source.size() > cuff::limits::kMaxSourceBytes)
            {
                printShellError("File is too large (limit " + std::to_string(cuff::limits::kMaxSourceBytes / 1024) + " KiB)");
                return;
            }
            platform::stripBom(source);
            executeSource(source, platform::parentDirectory(pathUtf8));
        }

        void run()
        {
            const bool interactive = term_.inputIsTerminal();
            // The logo goes first, before anything the startup file prints.
            if (interactive && opts_.showBanner)
                printBanner();

            if (!opts_.startupFile.empty())
                loadFile(opts_.startupFile);

            if (!interactive)
            {
                runNonInteractive();
                return;
            }

            LineEditor editor(term_);
            for (;;)
            {
                std::string entry;
                const EntryStatus st = editor.readEntry(entry);
                if (st == EntryStatus::Eof)
                    break;
                if (st == EntryStatus::Cancelled)
                    continue;

                const std::string trimmed = text::trim(entry);
                if (trimmed.empty())
                    continue;

                if (trimmed[0] == ':' && trimmed.find('\n') == std::string::npos)
                {
                    if (!dispatchMeta(trimmed))
                        break; // ':exit' / ':quit'
                    continue;
                }

                executeSource(entry, scriptDir_);
            }
        }

    private:
        Terminal &term_;
        Options opts_;
        std::unique_ptr<cuff::Interpreter> interp_;
        std::vector<std::unique_ptr<cuff::Program>> sessionPrograms_;
        std::string scriptDir_ = ".";

        // stdin isn't a terminal (piped or redirected): there is no prompt and
        // no Shift+Enter to press, so the whole input is one script, exactly
        // like `cuffc` reading a script from stdin.
        void runNonInteractive()
        {
            std::string source;
            if (!platform::readAllStdin(cuff::limits::kMaxSourceBytes, source))
            {
                printShellError("Failed to read stdin");
                return;
            }
            if (source.size() > cuff::limits::kMaxSourceBytes)
            {
                printShellError("Input is too large (limit " + std::to_string(cuff::limits::kMaxSourceBytes / 1024) + " KiB)");
                return;
            }
            platform::stripBom(source);
            executeSource(source, scriptDir_);
        }

        // ---- execution -------------------------------------------------------

        void executeSource(const std::string &source, const std::string &dir)
        {
            cuff::CuffEngine::Result parsed = cuff::CuffEngine::run(source, false);
            if (!parsed.success)
            {
                printEngineError(parsed.error);
                return;
            }

            cuff::Program *program = parsed.ast.get();
            try
            {
                RunningGuard running;
                interp_->run(*program, dir);
            }
            catch (const cuff::CuffError &e)
            {
                printEngineError(e.what());
            }
            catch (const std::bad_alloc &)
            {
                // Same text CuffEngine::execute() produces for this case.
                printEngineError("[" + cuff::errorCodeTag(cuff::ErrorCode::OutOfMemory) + "] " +
                                 cuff::errorCategoryName(cuff::ErrorCode::OutOfMemory) + ": the program ran out of memory");
            }
            catch (const std::exception &e)
            {
                printEngineError(std::string("Internal error: ") + e.what());
            }
            catch (...)
            {
                printEngineError("Internal error: unknown exception");
            }

            // Keep the AST alive for the rest of the session: any function
            // declaration inside it is now referenced by raw pointer from the
            // interpreter's function table, at any nesting depth the engine
            // allows (including inside top-level if/loop bodies), so freeing
            // this Program would leave that table dangling the next time such
            // a function is called from a later entry.
            sessionPrograms_.push_back(std::move(parsed.ast));
        }

        // Matches cuffc's own error output ("ERROR: " + engine message). Only
        // control characters are neutralised, so a message can't drive the
        // terminal.
        void printEngineError(const std::string &engineMessage)
        {
            std::cout << std::flush;
            std::cerr << "ERROR: " << text::sanitize(engineMessage) << "\n";
        }

        // For problems that are the shell's own (bad :load path, unknown
        // command...), in the style cuffc uses for its own CLI errors.
        void printShellError(const std::string &msg)
        {
            std::cout << std::flush;
            std::cerr << "Error: " << text::sanitize(msg) << "\n";
        }

        // ---- meta-commands ---------------------------------------------------

        static std::string unquote(const std::string &s)
        {
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
                return s.substr(1, s.size() - 2);
            return s;
        }

        // Returns false when the session should end.
        bool dispatchMeta(const std::string &cmdLine)
        {
            std::string cmd = cmdLine;
            std::string arg;
            if (const size_t sp = cmdLine.find_first_of(" \t"); sp != std::string::npos)
            {
                cmd = cmdLine.substr(0, sp);
                arg = unquote(text::trim(cmdLine.substr(sp + 1)));
            }

            if (cmd == ":exit" || cmd == ":quit" || cmd == ":q")
                return false;

            if (cmd == ":help" || cmd == ":h" || cmd == ":?")
            {
                printHelp();
                return true;
            }

            if (cmd == ":version")
            {
                std::cout << "CuffScript " << kVersion << " (cuffsh)\n";
                return true;
            }

            if (cmd == ":clear" || cmd == ":cls")
            {
                term_.clearScreen();
                return true;
            }

            if (cmd == ":reset")
            {
                interp_ = std::make_unique<cuff::Interpreter>(opts_.engineConfig);
                sessionPrograms_.clear();
                std::cout << "Session reset.\n";
                return true;
            }

            if (cmd == ":load")
            {
                if (arg.empty())
                {
                    printShellError("usage: :load <path>");
                    return true;
                }
                loadFile(arg);
                return true;
            }

            printShellError("Unknown command '" + text::sanitize(cmd) + "' (try :help)");
            return true;
        }

        void printHelp()
        {
            std::cout <<
                "  :help            show this help\n"
                "  :load <path>     run a .cuff file in the current session\n"
                "  :reset           clear all session state (variables/functions)\n"
                "  :clear           clear the screen\n"
                "  :version         show version info\n"
                "  :exit, :quit     leave the shell\n"
                "\n"
                "Enter runs what you've typed. Shift+Enter (or Alt+Enter) adds a new line\n"
                "instead, for if/loop/func and other multi-line code. Arrow keys, Home/End,\n"
                "Delete, Ctrl+A/E/K/U/W edit the entry; Up/Down also step through history.\n"
                "Pasted multi-line text arrives as one entry.\n"
                "\n"
                "Only what your code print()s is shown, as when running a script.\n"
                "Ctrl+C at the prompt cancels the entry. While code is RUNNING it exits the\n"
                "shell, because the engine has no way to stop a computation once started;\n"
                "--timeout guards against runaway loops.\n";
        }

        void printBanner()
        {
            if (!term_.outputIsTerminal())
                return;
            std::cout << term_.paint("36", logo());
            std::cout << "CuffScript Interactive Shell " << kVersion << "  (:help for help)\n\n";
        }
    };

} // namespace cuff::cli
