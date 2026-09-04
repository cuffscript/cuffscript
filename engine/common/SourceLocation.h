#pragma once

#include <string>

namespace cuff
{

    struct SourceLocation
    {
        int line = 1;
        int column = 1;
        int offset = 0;

        SourceLocation() = default;

        SourceLocation(int line, int column, int offset)
            : line(line), column(column), offset(offset) {}

        std::string toString() const
        {
            return "line " + std::to_string(line) + ", column " + std::to_string(column);
        }
    };

} // namespace cuff
