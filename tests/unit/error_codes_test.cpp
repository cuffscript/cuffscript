#include "engine/common/ErrorCodes.h"
#include <iostream>

int main()
{
    if (cuff::errorCodeTag(static_cast<cuff::ErrorCode>(4001)) != "E4-001" ||
        cuff::errorCodeTag(static_cast<cuff::ErrorCode>(9001)) != "E9-001" ||
        cuff::errorCodeTag(static_cast<cuff::ErrorCode>(10001)) != "E10-001")
    {
        std::cerr << "error code format mismatch\n";
        return 1;
    }
    std::cout << "error code formats passed\n";
    return 0;
}
