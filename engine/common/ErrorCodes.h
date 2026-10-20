#pragma once

#include <algorithm>
#include <string>

namespace cuff
{

    // Error code ranges: 1000 Lexical, 2000 Syntax, 3000 Regex,
    // 4000 Runtime, 5000 Module, 6000 Resource, 9000 Internal.
    //
    // Each enumerator is followed by a one-line description. docs/reference/errors.md is generated from this
    // enum, those descriptions and errorCodeRecoverable() below (tools/docs.py), so the catalogue cannot drift
    // from the code. Keep the description to one line and end it with a full stop.
    enum class ErrorCode
    {
        UnexpectedCharacter = 1001, // A character that cannot begin any token.
        UnterminatedString = 1002, // A string literal that is never closed.
        InvalidNumberLiteral = 1003, // A malformed number literal.
        InconsistentIndentation = 1004, // Indentation that does not line up with the enclosing block.
        ColonSpaceBeforeNotAllowed = 1005, // A space before the colon that opens a block.
        UnterminatedComment = 1006, // A block comment that is never closed.
        InvalidAssignmentSymbol = 1007, // `=` written where `to` is required.
        UnexpectedToken = 2001, // The token found cannot appear here. Every syntax problem found while tokenizing or parsing is reported with this code.
        ExpectedToken = 2002, // A specific token was required.
        MalformedFunctionDecl = 2003, // A malformed function declaration.
        MalformedControlFlow = 2004, // A malformed `if` or loop.
        MalformedImport = 2005, // A malformed `use` statement.
        UnbalancedBlock = 2006, // A block opened without a matching `end`.
        InvalidAssignmentTarget = 2007, // Something that cannot be assigned to.
        NestingTooDeep = 2008, // Code nested deeper than the nesting limit, or a single expression longer than the expression limit.
        SourceTooLarge = 2009, // A program, module file or shell entry larger than the source size limit.
        RegexUnclosedGroup = 3001, // A `(` or named capture that is never closed.
        RegexUnclosedBracket = 3002, // A `[` that is never closed.
        RegexInvalidColonSpacing = 3003, // Wrong spacing around the colon of a token such as `[one:a|b]`.
        RegexInvalidQuantifierRange = 3004, // A repeat range that cannot match anything, such as `5~2`.
        RegexStackedQuantifier = 3005, // Two quantifiers applied to the same item, such as `[num]++`.
        RegexEmptyToken = 3006, // An empty token or an empty alternative.
        RegexUnknownToken = 3007, // A `[name]` that is not a known token.
        RegexInvalidEscape = 3008, // A backslash that escapes nothing, or something that cannot be escaped.
        RegexDanglingQuantifier = 3009, // A quantifier with nothing before it.
        RegexUnexpectedCharacter = 3010, // A character that cannot appear at this point of a pattern.
        RegexPatternTooComplex = 3011, // A pattern larger than the pattern size limit, or nested deeper than the pattern nesting limit.
        RegexStepLimitExceeded = 3101, // Matching used more steps than its budget allows.
        RegexTimeout = 3102, // Matching ran longer than its time budget allows.
        RegexRecursionLimitExceeded = 3103, // Matching recursed deeper than its limit.
        UndefinedVariable = 4001, // A variable that has not been declared, or is not visible here.
        UndefinedFunction = 4002, // A function that has not been declared.
        ConstantReassignment = 4003, // A constant that is changed after it was created.
        InvalidConstantName = 4004, // A constant whose name is not all uppercase letters, digits and underscores.
        TypeMismatch = 4005, // A value of the wrong type for an operation.
        DivisionByZero = 4006, // Division or remainder by zero, or zero to a negative power.
        ZeroIndexAccess = 4007, // Position 0, which does not exist (positions start at 1).
        IndexOutOfRange = 4008, // A position outside a list, string or slice.
        KeyNotFound = 4009, // A map key that is not present.
        ArgumentCountMismatch = 4010, // A call with the wrong number of arguments.
        NotCallable = 4011, // Calling something that is not a function.
        InvalidOperand = 4012, // An operand an operator cannot work with.
        LocalVariableOutOfScope = 4013, // A local variable used outside the function that declared it.
        InvalidCollectionOperation = 4014, // An operation a list or map does not support.
        ElementNotFound = 4015, // A value to remove that is not in the list.
        UnsupportedOperation = 4016, // An operation not supported here, such as `return value` in a function that is not `returnable`.
        StackOverflow = 4017, // Recursion deeper than the call depth limit, or deeper than the native stack budget allows.
        NestedFunctionNotSupported = 4018, // A function declared inside another function.
        AwaitOnNonAsync = 4019, // `await` applied to a function that is not `async`.
        DeclarationTypeMismatch = 4020, // A declared type that does not match the value.
        InvalidGlobalDeclaration = 4021, // `change ... to global` outside a function body.
        ReturnOutsideFunction = 4022, // `return` outside any function.
        StopOutsideLoop = 4023, // `stop` outside any loop.
        FractionalIndex = 4024, // A position or range bound that is not a whole number.
        InvalidArgumentValue = 4025, // An argument of the right type but a value the function or operator does not accept.
        SizeLimitExceeded = 4026, // A string, list, map or `async` queue larger than its limit.
        PureFunctionGlobalAccess = 4027, // A `pure` function that reaches a global variable.
        NetworkRequestFailed = 4028, // A network request that failed, timed out, or targeted a blocked address.
        PureFunctionImpureCall = 4029, // A `pure` function that calls a function that is not `pure`.
        ParameterTypeMismatch = 4030, // An argument that does not match the parameter's declared type.
        ModuleNotFound = 5001, // A module file that does not exist or cannot be read.
        ModuleParseFailed = 5002, // A module file that does not parse.
        CircularImport = 5003, // Modules that import each other in a cycle.
        UnknownDLC = 5004, // A library name that does not exist.
        DLCFeatureUnavailable = 5005, // A library switched off for this run (`--no-network`, `--no-filesystem`).
        ModuleAccessDenied = 5006, // A module path that is absolute, or resolves outside the allowed directory.
        ModuleLimitExceeded = 5007, // Modules imported too deeply, or a module file larger than the source size limit.
        FilesystemAccessDenied = 5008, // A file path that resolves outside the allowed directory.
        ExecutionStepLimit = 6001, // The run exceeded `--max-steps`.
        ExecutionTimeout = 6002, // The run exceeded `--timeout`.
        OutOfMemory = 6003, // The host ran out of memory.
        InternalError = 9001, // A bug in the engine: please report it.
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
