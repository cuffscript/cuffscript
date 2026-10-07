#pragma once

#include <stdexcept>
#include <string>
#include "SourceLocation.h"
#include "ErrorCodes.h"

namespace cuff
{

// CuffError is the base class for all engine errors.
//
// Each error carries an ErrorCode, category, source location,
// message, optional hint, and recoverability flag.
//
// what() includes the error code, category, location, message,
// and optional hint, but not the original source line.
// CuffEngine::execute()/run() can use renderErrorWithSnippet()
// to include the source line and caret while the source is available.

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
