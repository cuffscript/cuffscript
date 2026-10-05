#pragma once

#include <algorithm>
#include <string>

namespace cuff
{

    // Error code ranges: 1000 Lexical, 2000 Syntax, 3000 Regex,
    // 4000 Runtime, 5000 Module, 6000 Resource, 9000 Internal.
    enum class ErrorCode
    {
        UnexpectedCharacter = 1001,
        UnterminatedString = 1002,
        InvalidNumberLiteral = 1003,
        InconsistentIndentation = 1004,
        ColonSpaceBeforeNotAllowed = 1005,
        UnterminatedComment = 1006,
        InvalidAssignmentSymbol = 1007,

        UnexpectedToken = 2001,
        ExpectedToken = 2002,
        MalformedFunctionDecl = 2003,
        MalformedControlFlow = 2004,
        MalformedImport = 2005,
        UnbalancedBlock = 2006,
        InvalidAssignmentTarget = 2007,
        NestingTooDeep = 2008,
        SourceTooLarge = 2009,

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
        RegexPatternTooComplex = 3011,
        RegexStepLimitExceeded = 3101,
        RegexTimeout = 3102,
        RegexRecursionLimitExceeded = 3103,

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
        ReturnOutsideFunction = 4022,
        StopOutsideLoop = 4023,
        FractionalIndex = 4024,
        InvalidArgumentValue = 4025,
        SizeLimitExceeded = 4026,
        PureFunctionGlobalAccess = 4027,
        NetworkRequestFailed = 4028,
        PureFunctionImpureCall = 4029,
        ParameterTypeMismatch = 4030,

        ModuleNotFound = 5001,
        ModuleParseFailed = 5002,
        CircularImport = 5003,
        UnknownDLC = 5004,
        DLCFeatureUnavailable = 5005,
        ModuleAccessDenied = 5006,
        ModuleLimitExceeded = 5007,
        FilesystemAccessDenied = 5008,

        ExecutionStepLimit = 6001,
        ExecutionTimeout = 6002,
        OutOfMemory = 6003,

        InternalError = 9001,
    };

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
        if (n >= 6000 && n < 7000)
            return "Resource Limit Error";
        return "Internal Error";
    }

    // Short machine-readable error tag, e.g. "E4-008".
    inline std::string errorCodeTag(ErrorCode code)
    {
        const int n = static_cast<int>(code);
        std::string num = std::to_string(n % 1000);
        num.insert(0, 3 - std::min<size_t>(3, num.size()), '0');
        return "E" + std::to_string(n / 1000) + "-" + num;
    }

    // Whether an `or_else` block may catch errors of this category.
    inline bool errorCodeRecoverable(ErrorCode code)
    {
        int n = static_cast<int>(code);
        return (n >= 3100 && n < 4000) || (n >= 4000 && n < 6000);
    }

}
