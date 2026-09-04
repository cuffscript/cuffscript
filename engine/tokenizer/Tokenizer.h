#pragma once

#include "../common/Token.h"
#include "../common/CuffError.h"
#include "ScanState.h"
#include "CharUtils.h"
#include "CommentScanner.h"
#include "StringScanner.h"
#include "NumberScanner.h"
#include "IdentifierScanner.h"
#include "OperatorScanner.h"
#include <vector>
#include <string>
#include <stack>

namespace cuff {

// Main tokenizer: converts raw source text into a token stream.
//
// Key features:
//   - Tracks indentation and emits INDENT/DEDENT tokens (Python-style)
//   - Emits NEWLINE tokens for statement separation
//   - Skips comments (note: and note: ~ endnote)
//   - Records hasSpaceBefore on each token for colon validation
//   - Records indentLevel on each token for block-structure enforcement
//
// Indentation rules (from spec):
//   - Block bodies require indentation (except one-line shorthand forms)
//   - Blank lines are allowed and ignored
//   - One-line shorthand (do: ... end on same line) doesn't require indentation
class Tokenizer {
public:
    explicit Tokenizer(const std::string& src) : state_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        std::stack<int> indentStack;
        indentStack.push(0); // base indent level

        bool atLineStart = true;
        bool pendingNewline = false;

        while (!state_.atEnd()) {
            // ---- Line start: handle indentation ----
            if (atLineStart) {
                // Skip blank lines (whitespace + optional comment + newline)
                int lineStartOffset = state_.offset;

                // Count leading whitespace
                int indent = 0;
                while (!state_.atEnd() && (state_.peek() == ' ' || state_.peek() == '\t')) {
                    // Treat tab as 4 spaces for simplicity
                    indent += (state_.peek() == '\t') ? 4 : 1;
                    state_.advance();
                }

                // If line is blank (newline or comment-only or EOF), skip indent processing
                if (state_.atEnd() || isNewline(state_.peek())) {
                    if (!state_.atEnd()) state_.advance(); // consume newline
                    continue;
                }

                // Check for comment-only line
                if (state_.peek() == 'n' && isCommentAhead()) {
                    scanComment(state_);
                    // After comment, skip to end of line
                    while (!state_.atEnd() && !isNewline(state_.peek())) state_.advance();
                    if (!state_.atEnd()) state_.advance(); // consume newline
                    continue;
                }

                // Emit pending NEWLINE if needed (before indent tokens)
                if (pendingNewline) {
                    tokens.emplace_back(TokenType::NEWLINE, "\\n", state_.here(), false);
                    pendingNewline = false;
                }

                // Emit INDENT / DEDENT tokens
                int currentIndent = indentStack.top();
                if (indent > currentIndent) {
                    indentStack.push(indent);
                    tokens.emplace_back(TokenType::INDENT, "", state_.here(), false, indent);
                } else {
                    while (indent < indentStack.top()) {
                        indentStack.pop();
                        tokens.emplace_back(TokenType::DEDENT, "", state_.here(), false, indent);
                    }
                    if (indent != indentStack.top()) {
                        throw SyntaxError("Inconsistent indentation", state_.here());
                    }
                }

                atLineStart = false;
                state_.pendingSpaceBefore = false;
                continue;
            }

            // ---- Mid-line: skip whitespace, scan tokens ----
            state_.skipInlineWhitespace();
            if (state_.atEnd()) break;

            int c = state_.peek();

            // Newline → set pendingNewline, go to line start
            if (isNewline(c)) {
                state_.advance();
                atLineStart = true;
                pendingNewline = true;
                continue;
            }

            // Comments (consumed entirely)
            if (c == 'n' && isCommentAhead()) {
                scanComment(state_);
                continue;
            }

            // Strings and f-strings
            if (c == '"' || (c == 'f' && state_.peek(1) == '"')) {
                if (c == 'f' && state_.peek(1) != '"') {
                    tokens.push_back(scanIdentifier(state_));
                } else {
                    tokens.push_back(scanString(state_));
                }
                state_.pendingSpaceBefore = false;
                continue;
            }

            // Numbers
            if (isDigit(c)) {
                tokens.push_back(scanNumber(state_));
                state_.pendingSpaceBefore = false;
                continue;
            }

            // Identifiers
            if (isAlpha(c)) {
                tokens.push_back(scanIdentifier(state_));
                state_.pendingSpaceBefore = false;
                continue;
            }

            // Operators and delimiters
            tokens.push_back(scanOperator(state_));
            state_.pendingSpaceBefore = false;
        }

        // Flush final newline
        if (pendingNewline || (!tokens.empty() && !tokens.back().is(TokenType::NEWLINE))) {
            tokens.emplace_back(TokenType::NEWLINE, "\\n", state_.here(), false);
        }

        // Emit remaining DEDENTs
        while (indentStack.top() > 0) {
            indentStack.pop();
            tokens.emplace_back(TokenType::DEDENT, "", state_.here(), false, 0);
        }

        // EOF sentinel
        tokens.emplace_back(TokenType::EOF_TOKEN, "", state_.here(), false);

        return tokens;
    }

private:
    ScanState state_;

    // Check if the upcoming text is a "note" comment
    bool isCommentAhead() const {
        return state_.peek(0) == 'n' && state_.peek(1) == 'o'
            && state_.peek(2) == 't' && state_.peek(3) == 'e'
            && !isAlphaNum(state_.peek(4));
    }
};

} // namespace cuff
