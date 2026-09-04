#pragma once

#include <stdexcept>
#include <string>
#include "SourceLocation.h"

namespace cuff {

class CuffError : public std::runtime_error {
public:
    SourceLocation location;

    CuffError(const std::string& kind, const std::string& msg, const SourceLocation& loc)
        : std::runtime_error(loc.toString() + ": " + kind + ": " + msg)
        , location(loc) {}
};

class SyntaxError : public CuffError {
public:
    SyntaxError(const std::string& msg, const SourceLocation& loc)
        : CuffError("Syntax Error", msg, loc) {}
};

class CuffRuntimeError : public CuffError {
public:
    CuffRuntimeError(const std::string& msg, const SourceLocation& loc)
        : CuffError("Runtime Error", msg, loc) {}
};

} // namespace cuff
