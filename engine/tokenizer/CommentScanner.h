#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"

namespace cuff {

// Scans `note:` single-line comments and `note: ~ endnote` multi-line comments.
// Comments are fully consumed — no tokens emitted.
// Returns true if a comment was consumed at the current position.
inline bool scanComment(ScanState& s) {
    // Check for "note" keyword
    if (s.peek(0) != 'n' || s.peek(1) != 'o' || s.peek(2) != 't' || s.peek(3) != 'e')
        return false;
    // Must be followed by a word boundary (not part of a longer identifier)
    if (isAlphaNum(s.peek(4))) return false;

    // Consume "note"
    for (int i = 0; i < 4; ++i) s.advance();

    // Expect ':' immediately after "note" (colon rule: no space before)
    if (s.peek() != ':') {
        throw SyntaxError("Expected ':' after 'note'", s.here());
    }
    s.advance(); // consume ':'

    // Skip whitespace after ':'
    while (!s.atEnd() && isSpace(s.peek())) s.advance();

    // Multi-line comment: note: followed by newline, content, then endnote
    if (isNewline(s.peek()) || s.atEnd()) {
        // Multi-line comment — consume newline
        if (!s.atEnd()) s.advance();

        // Read until "endnote"
        while (!s.atEnd()) {
            // Check for "endnote" at current position
            if (s.peek(0) == 'e' && s.peek(1) == 'n' && s.peek(2) == 'd'
                && s.peek(3) == 'n' && s.peek(4) == 'o' && s.peek(5) == 't'
                && s.peek(6) == 'e' && !isAlphaNum(s.peek(7))) {
                for (int i = 0; i < 7; ++i) s.advance();
                s.pendingSpaceBefore = true;
                return true;
            }
            s.advance();
        }
        throw SyntaxError("Unterminated multi-line comment — missing 'endnote'", s.here());
    }

    // Single-line comment: read until newline (or EOF)
    while (!s.atEnd() && !isNewline(s.peek())) {
        s.advance();
    }
    s.pendingSpaceBefore = true;
    return true;
}

} // namespace cuff
