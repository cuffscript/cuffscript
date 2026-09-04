#pragma once

#include <cctype>

namespace cuff {

inline bool isAlpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool isDigit(int c) {
    return c >= '0' && c <= '9';
}

inline bool isAlphaNum(int c) {
    return isAlpha(c) || isDigit(c);
}

inline bool isSpace(int c) {
    return c == ' ' || c == '\t' || c == '\r';
}

inline bool isNewline(int c) {
    return c == '\n';
}

} // namespace cuff
