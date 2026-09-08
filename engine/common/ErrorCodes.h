#pragma once

#include <string>

namespace cuff
{

    // =========================================================================
    // Systematic error classification.
    //
    // Every error the engine can raise has a stable numeric code and belongs to
    // exactly one category. Categories map to numeric ranges so that a bare code
    // number alone tells you roughly where in the pipeline it came from:
    //
    //   1000-1999  Lexical    (tokenizer stage)
    //   2000-2999  Syntax     (parser stage)
    //   3000-3999  Regex      (CuffScript pattern compiler / matcher)
    //   4000-4999  Runtime    (interpreter — recoverable via or_else)
    //   5000-5999  Module     (use/from — recoverable via or_else)
    //   9000-9999  Internal   (engine bugs — should never surface to users)
    //
    // Adding a new error kind is a two-step, additive process:
    //   1. Add an entry to ErrorCode (pick the next free number in the range).
    //   2. Add a matching case in errorCodeName() below.
    // Nothing else needs to change — CuffError (see CuffError.h) formats any
    // code generically, and new leaf exception types can be added independently.
    // =========================================================================

    enum class ErrorCode
    {
        // ---- Lexical (1000s) ----
        UnexpectedCharacter = 1001,
        UnterminatedString = 1002,
        InvalidNumberLiteral = 1003,
        InconsistentIndentation = 1004,
        ColonSpaceBeforeNotAllowed = 1005,
        UnterminatedComment = 1006,
        InvalidAssignmentSymbol = 1007,

        // ---- Syntax (2000s) ----
        UnexpectedToken = 2001,
        ExpectedToken = 2002,
        MalformedFunctionDecl = 2003,
        MalformedControlFlow = 2004,
        MalformedImport = 2005,
        UnbalancedBlock = 2006,
        InvalidAssignmentTarget = 2007,

        // ---- Regex (3000s: syntax subset compiled at parse time) ----
        RegexUnclosedGroup = 3001,
        RegexUnclosedBracket = 3002,
        RegexInvalidColonSpacing = 3003,
        RegexInvalidQuantifierRange = 3004,
        RegexStackedQuantifier = 3005,
        RegexEmptyToken = 3006,
        RegexUnknownToken = 3007,
        RegexInvalidEscape = 3008,
        RegexDanglingQuantifier = 3009,
        RegexUnexpectedCharacter = 3010,
        // ---- Regex runtime (3100s: safety limits during matching) ----
        RegexStepLimitExceeded = 3101,
        RegexTimeout = 3102,
        RegexRecursionLimitExceeded = 3103,

        // ---- Runtime (4000s) ----
        UndefinedVariable = 4001,
        UndefinedFunction = 4002,
        ConstantReassignment = 4003,
        InvalidConstantName = 4004,
        TypeMismatch = 4005,
        DivisionByZero = 4006,
        ZeroIndexAccess = 4007,
        IndexOutOfRange = 4008,
        KeyNotFound = 4009,
        ArgumentCountMismatch = 4010,
        NotCallable = 4011,
        InvalidOperand = 4012,
        LocalVariableOutOfScope = 4013,
        InvalidCollectionOperation = 4014,
        ElementNotFound = 4015,
        UnsupportedOperation = 4016,
        StackOverflow = 4017,
        NestedFunctionNotSupported = 4018,
        AwaitOnNonAsync = 4019,
        DeclarationTypeMismatch = 4020,
        InvalidGlobalDeclaration = 4021,

        // ---- Module (5000s) ----
        ModuleNotFound = 5001,
        ModuleParseFailed = 5002,
        CircularImport = 5003,
        UnknownDLC = 5004,
        DLCFeatureUnavailable = 5005,

        // ---- Internal (9000s) ----
        InternalError = 9001,
    };

    // Category name — used as the human-readable prefix in formatted messages
    // ("Syntax Error", "Runtime Error", etc.) and to bucket codes for tooling.
    inline std::string errorCategoryName(ErrorCode code)
    {
        int n = static_cast<int>(code);
        if (n >= 1000 && n < 2000)
            return "Lexical Error";
        if (n >= 2000 && n < 3000)
            return "Syntax Error";
        if (n >= 3000 && n < 3100)
            return "Regex Syntax Error";
        if (n >= 3100 && n < 4000)
            return "Regex Runtime Error";
        if (n >= 4000 && n < 5000)
            return "Runtime Error";
        if (n >= 5000 && n < 6000)
            return "Module Error";
        return "Internal Error";
    }

    // Short machine-friendly tag, e.g. "E4005". Handy for grepping docs/tests
    // and for issue reports (see CONTRIBUTING.md).
    inline std::string errorCodeTag(ErrorCode code)
    {
        return "E" + std::to_string(static_cast<int>(code));
    }

    // Whether this category is, in principle, something an `or_else` block is
    // allowed to catch. Lexical/Syntax/RegexSyntax/Internal errors mean the
    // *program itself* is malformed and cannot be meaningfully recovered from
    // at runtime — they should never be reached inside an already-parsed,
    // already-running program, so or_else does not catch them. Runtime,
    // RegexRuntime, and Module errors represent a well-formed program
    // encountering a bad *situation* (missing file, bad index, timeout, ...),
    // which is exactly what or_else exists to handle.
    inline bool errorCodeRecoverable(ErrorCode code)
    {
        int n = static_cast<int>(code);
        return (n >= 3100 && n < 4000) || (n >= 4000 && n < 6000);
    }

} // namespace cuff
