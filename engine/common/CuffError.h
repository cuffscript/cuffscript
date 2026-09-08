#pragma once

#include <stdexcept>
#include <string>
#include "SourceLocation.h"
#include "ErrorCodes.h"

namespace cuff
{

    // =========================================================================
    // CuffError — the single base class for every error the engine can raise.
    //
    // Every error carries:
    //   - an ErrorCode (see ErrorCodes.h)         -> stable, greppable identity
    //   - a category name derived from the code   -> "Syntax Error", etc.
    //   - a SourceLocation                         -> where it happened
    //   - a human message                          -> what happened
    //   - an optional hint                         -> how to fix it (may be empty)
    //   - `recoverable`                             -> can `or_else` catch this?
    //
    // what() renders all of this into one line, e.g.:
    //   [E4008] Runtime Error at line 12, column 5: index out of range (got 5, length 3)
    //           hint: CuffScript lists are 1-based; the last valid index here is 3.
    //
    // Adding a brand new *kind* of error is just a new small subclass at the
    // bottom of this file (or in whatever module owns the concept) that calls
    // the CuffError constructor with a fixed ErrorCode -- no changes required
    // anywhere else in the hierarchy.
    // =========================================================================
    class CuffError : public std::runtime_error
    {
    public:
        ErrorCode code;
        std::string category;
        SourceLocation location;
        std::string message;
        std::string hint;
        bool recoverable;

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

        // Legacy constructor kept for older call sites that pass a free-form
        // category string directly (e.g. "Syntax Error"). New code should
        // prefer the ErrorCode-based constructor above.
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

    // -------------------------------------------------------------------------
    // Compile-time error families (never recoverable -- a malformed program
    // cannot sensibly be "handled" by the very program that failed to parse).
    // -------------------------------------------------------------------------

    // Generic syntax error -- tokenizer or parser stage. Most existing call
    // sites use this 2-arg form; it defaults to ErrorCode::UnexpectedToken.
    class SyntaxError : public CuffError
    {
    public:
        SyntaxError(const std::string &msg, const SourceLocation &loc)
            : CuffError(ErrorCode::UnexpectedToken, msg, loc) {}

        SyntaxError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                    const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    // Raised by the CuffScript pattern compiler (engine/regex) when a pattern
    // literal is malformed. Per the language spec this must fire "immediately"
    // -- for literal string patterns we compile eagerly at parse time, so most
    // of these are in fact caught before the program ever runs.
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

    // -------------------------------------------------------------------------
    // Runtime error families (recoverable -- these are exactly the situations
    // `or_else` exists to handle: bad data, missing resources, exhausted
    // safety limits).
    // -------------------------------------------------------------------------

    // Generic runtime error. Kept 2-arg-compatible with the original engine
    // skeleton; defaults to a generic "unsupported operation" code.
    class CuffRuntimeError : public CuffError
    {
    public:
        CuffRuntimeError(const std::string &msg, const SourceLocation &loc)
            : CuffError(ErrorCode::UnsupportedOperation, msg, loc) {}

        CuffRuntimeError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                          const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    // Pattern matched syntactically fine but blew a safety limit while running
    // (catastrophic-backtracking guard). See engine/regex/RegexMatcher.h.
    class RegexRuntimeError : public CuffError
    {
    public:
        RegexRuntimeError(ErrorCode c, const std::string &msg, const SourceLocation &loc,
                           const std::string &hintText = "")
            : CuffError(c, msg, loc, hintText) {}
    };

    // ---- Specific runtime error kinds -------------------------------------
    // Each of these is a thin, self-documenting subclass so call sites read
    // naturally (`throw TypeError(...)`) and `catch` clauses can target a
    // precise kind when useful, while still being catchable as a generic
    // CuffRuntimeError / CuffError by the interpreter's or_else handler.

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

    // Module loading (use / from / DLC). Its own small category namespace
    // (5000s) but behaves exactly like a runtime error for or_else purposes.
    class ModuleError : public CuffError
    {
    public:
        ModuleError(ErrorCode c, const std::string &msg, const SourceLocation &loc, const std::string &hint = "")
            : CuffError(c, msg, loc, hint) {}
    };

    // -------------------------------------------------------------------------
    // Internal errors -- represent a bug in the engine itself (a broken
    // invariant), never a user-fixable mistake. Not recoverable via or_else.
    // -------------------------------------------------------------------------
    class InternalEngineError : public CuffError
    {
    public:
        explicit InternalEngineError(const std::string &msg, const SourceLocation &loc = SourceLocation())
            : CuffError(ErrorCode::InternalError, msg, loc) {}
    };

} // namespace cuff
