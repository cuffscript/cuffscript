#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "SourceText.h"
#include "ErrorCodes.h"

namespace cuff
{

// CuffError is the base class for all engine errors.
//
// Each error carries an ErrorCode, category, source location,
// message, optional hint, and recoverability flag.
//
// what() includes the error code, category, location, message,
// and optional hint, but not the original source line. Use
// renderErrorWithSnippet() (common/ErrorFormat.h) for the full report:
// it draws the source line and caret from the text the location points into.
//
// `source` names that text when it is not the program being run (see SourceText.h);
// `related` is an optional extra block shown under the snippet; `trace` is the backtrace.

    // One frame of a backtrace: the function that was running (empty at the top level of a program or module) and
    // where in it the error was, which is where it was raised for the innermost frame and the call it came out of for
    // every other. `repeated` counts the frames right after it that were the same (a function calling itself).
    struct TraceEntry
    {
        std::string function;
        SourceLocation location;
        SourceRef source;
        uint32_t repeated = 0;
    };

    class CuffError : public std::runtime_error
    {
    public:
        ErrorCode code;
        std::string category;
        SourceLocation location;
        std::string message;
        std::string hint;
        bool recoverable;
        SourceRef source;
        std::string related;

        // The frames the error passed through, innermost first (Interpreter::handleError adds them): the first
        // kTraceHead, then the last kTraceTail in a ring, with the number left out between them. A run of the same
        // frame is one entry, so even a recursion that hit the depth limit costs a few entries.
        static constexpr size_t kTraceHead = 30;
        static constexpr size_t kTraceTail = 10;
        std::vector<TraceEntry> trace;
        std::vector<TraceEntry> traceTail;
        size_t traceTailNext = 0;
        size_t traceOmitted = 0;
        // Where the error is in the next frame out (the call it is coming out of), once a frame has been left.
        SourceLocation traceAt;
        bool traceHasAt = false;
        bool traceStarted = false;

        void addTrace(TraceEntry entry)
        {
            if (TraceEntry *last = lastTrace())
            {
                if (last->source == entry.source && last->location.offset == entry.location.offset &&
                    last->location.line == entry.location.line && last->function == entry.function)
                {
                    ++last->repeated;
                    return;
                }
            }
            if (trace.size() < kTraceHead)
                trace.push_back(std::move(entry));
            else if (traceTail.size() < kTraceTail)
                traceTail.push_back(std::move(entry));
            else
            {
                traceTail[traceTailNext] = std::move(entry); // the oldest of the tail goes
                traceTailNext = (traceTailNext + 1) % kTraceTail;
                ++traceOmitted;
            }
        }

        // Every entry kept, innermost first. The traceOmitted frames left out come right after the first kTraceHead.
        std::vector<const TraceEntry *> traceEntries() const
        {
            std::vector<const TraceEntry *> out;
            out.reserve(trace.size() + traceTail.size());
            for (const TraceEntry &t : trace)
                out.push_back(&t);
            for (size_t k = 0; k < traceTail.size(); ++k)
                out.push_back(&traceTail[(traceTailNext + k) % traceTail.size()]);
            return out;
        }

        // Records which text `location` points into, unless something closer to the error already did.
        void attachSource(const SourceRef &s)
        {
            if (!source)
                source = s;
        }

        CuffError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                  const std::string &hintText = "")
            : std::runtime_error(render(c, msg, loc, hintText)),
              code(c),
              category(errorCategoryName(c)),
              location(loc),
              message(msg),
              hint(hintText),
              recoverable(errorCodeRecoverable(c))
        {
        }

        CuffError(const std::string &kind, const std::string &msg, const SourceLocation &loc)
            : std::runtime_error(loc.toString() + ": " + kind + ": " + msg),
              code(ErrorCode::InternalError),
              category(kind),
              location(loc),
              message(msg),
              hint(""),
              recoverable(false)
        {
        }

    private:
        TraceEntry *lastTrace()
        {
            if (!traceTail.empty())
                return &traceTail[(traceTailNext + traceTail.size() - 1) % traceTail.size()];
            return trace.empty() ? nullptr : &trace.back();
        }

        static std::string render(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                                   const std::string &hintText)
        {
            std::string out = "[" + errorCodeTag(c) + "] " + errorCategoryName(c) +
                               " at " + loc.toString() + ": " + msg;
            if (!hintText.empty())
            {
                out += "\n    hint: " + hintText;
            }
            return out;
        }
    };

    class SyntaxError : public CuffError
    {
    public:
        SyntaxError(const std::string &msg, const SourceLocation &loc)
            : CuffError(ErrorCode::UnexpectedToken, msg, loc) {}

        SyntaxError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                    const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    class RegexSyntaxError : public CuffError
    {
    public:
        RegexSyntaxError(const std::string &msg, const SourceLocation &loc,
                          const std::string &hintText = "")
            : CuffError(ErrorCode::RegexUnknownToken, msg, loc, hintText) {}

        RegexSyntaxError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                          const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    class CuffRuntimeError : public CuffError
    {
    public:
        CuffRuntimeError(const std::string &msg, const SourceLocation &loc)
            : CuffError(ErrorCode::UnsupportedOperation, msg, loc) {}

        CuffRuntimeError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                          const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    class RegexRuntimeError : public CuffError
    {
    public:
        RegexRuntimeError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                           const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    class TypeError : public CuffRuntimeError
    {
    public:
        TypeError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::TypeMismatch, msg, loc, hint) {}
    };

    class UndefinedVariableError : public CuffRuntimeError
    {
    public:
        UndefinedVariableError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::UndefinedVariable, msg, loc, hint) {}
    };

    class UndefinedFunctionError : public CuffRuntimeError
    {
    public:
        UndefinedFunctionError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::UndefinedFunction, msg, loc, hint) {}
    };

    class ConstantError : public CuffRuntimeError
    {
    public:
        ConstantError(ErrorCode c, const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(c, msg, loc, hint) {}
    };

    class IndexError : public CuffRuntimeError
    {
    public:
        IndexError(ErrorCode c, const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(c, msg, loc, hint) {}
    };

    class KeyError : public CuffRuntimeError
    {
    public:
        KeyError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::KeyNotFound, msg, loc, hint) {}
    };

    class DivisionByZeroError : public CuffRuntimeError
    {
    public:
        DivisionByZeroError(const std::string &msg, const SourceLocation &loc)
            : CuffRuntimeError(ErrorCode::DivisionByZero, msg, loc) {}
    };

    class ArgumentError : public CuffRuntimeError
    {
    public:
        ArgumentError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::ArgumentCountMismatch, msg, loc, hint) {}
    };

    class ValueError : public CuffRuntimeError
    {
    public:
        ValueError(const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(ErrorCode::InvalidArgumentValue, msg, loc, hint) {}
    };

    class ElementNotFoundError : public CuffRuntimeError
    {
    public:
        ElementNotFoundError(const std::string &msg, const SourceLocation &loc)
            : CuffRuntimeError(ErrorCode::ElementNotFound, msg, loc) {}
    };

    class ScopeError : public CuffRuntimeError
    {
    public:
        ScopeError(ErrorCode c, const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffRuntimeError(c, msg, loc, hint) {}
    };

    class StackOverflowError : public CuffRuntimeError
    {
    public:
        StackOverflowError(const std::string &msg, const SourceLocation &loc)
            : CuffRuntimeError(ErrorCode::StackOverflow, msg, loc) {}
    };

    class ModuleError : public CuffError
    {
    public:
        ModuleError(ErrorCode c, const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffError(c, msg, loc, hint) {}
    };

    class InternalEngineError : public CuffError
    {
    public:
        explicit InternalEngineError(const std::string &msg, const SourceLocation &loc = SourceLocation())
            : CuffError(ErrorCode::InternalError, msg, loc) {}
    };

}
