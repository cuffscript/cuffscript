#pragma once

#include "Value.h"
#include "../common/CuffError.h"

namespace cuff
{

    // An error the program raised with `fail`. It carries the Error object that was raised, so a handler
    // (`or_else as e`) receives that very object, whatever class it is an instance of.
    class ProgramError : public CuffError
    {
    public:
        Value payload;

        ProgramError(const std::string &msg, const SourceLocation &loc, Value object, const std::string &hintText = "")
            : CuffError(ErrorCode::ProgramError, msg, loc, hintText), payload(std::move(object))
        {
        }
    };

}
