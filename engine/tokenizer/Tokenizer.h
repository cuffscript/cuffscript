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
#include <algorithm>
#include <vector>
#include <string>
#include <stack>

namespace cuff
{

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
    class Tokenizer
    {
    public:
        explicit Tokenizer(const std::string &src) : state_(src) {}

        std::vector<Token> tokenize()
        {
            std::vector<Token> tokens;
            // Dense code comes to about one token per four bytes. Growing the vector one doubling at a time moves
            // every token several times over (a token owns a string), which was the largest single cost of
            // reading a big program. Capped, so a large source does not reserve memory for tokens it may not have.
            tokens.reserve(std::min<size_t>(state_.source.size() / 4 + 16, kReserveTokensMax));
            std::stack<int> indentStack;
            indentStack.push(0);

            bool atLineStart = true;
            bool pendingNewline = false;
            // Where the statement being finished ended. NEWLINE and the closing DEDENT/EOF tokens sit here, not
            // at the start of whatever follows (blank lines, comments, the next statement): an error such as
            // "expected an expression" belongs on the line that is incomplete, and an unclosed block belongs
            // at the end of the code, not on an empty line past it.
            SourceLocation endOfStatement;

            while (!state_.atEnd())
            {
                if (atLineStart)
                {
                    int indent = 0;
                    while (!state_.atEnd() && (state_.peek() == ' ' || state_.peek() == '\t'))
                    {
                        indent += (state_.peek() == '\t') ? 4 : 1;
                        state_.advance();
                    }

                    if (state_.atEnd() || isNewline(state_.peek()))
                    {
                        if (!state_.atEnd())
                            state_.advance();
                        continue;
                    }

                    if (state_.peek() == 'n' && isCommentAhead())
                    {
                        scanComment(state_);
                        while (!state_.atEnd() && !isNewline(state_.peek()))
                            state_.advance();
                        if (!state_.atEnd())
                            state_.advance();
                        continue;
                    }

                    if (pendingNewline)
                    {
                        tokens.emplace_back(TokenType::NEWLINE, "\\n", endOfStatement, false);
                        pendingNewline = false;
                    }

                    int currentIndent = indentStack.top();
                    if (indent > currentIndent)
                    {
                        indentStack.push(indent);
                        tokens.emplace_back(TokenType::INDENT, "", state_.here(), false, indent);
                    }
                    else
                    {
                        while (indent < indentStack.top())
                        {
                            indentStack.pop();
                            tokens.emplace_back(TokenType::DEDENT, "", state_.here(), false, indent);
                        }
                        if (indent != indentStack.top())
                        {
                            throw SyntaxError("inconsistent indentation", state_.here());
                        }
                    }

                    atLineStart = false;
                    state_.pendingSpaceBefore = false;
                    continue;
                }

                state_.skipInlineWhitespace();
                if (state_.atEnd())
                    break;

                int c = state_.peek();

                if (isNewline(c))
                {
                    endOfStatement = state_.here();
                    state_.advance();
                    atLineStart = true;
                    pendingNewline = true;
                    continue;
                }

                if (c == 'n' && isCommentAhead())
                {
                    scanComment(state_);
                    continue;
                }

                if (c == '"' || c == '\'' || (c == 'f' && state_.peek(1) == '"'))
                {
                    if (c == 'f' && state_.peek(1) != '"')
                    {
                        tokens.push_back(scanIdentifier(state_));
                    }
                    else
                    {
                        tokens.push_back(scanString(state_));
                    }
                    state_.pendingSpaceBefore = false;
                    continue;
                }

                if (isDigit(c))
                {
                    tokens.push_back(scanNumber(state_));
                    state_.pendingSpaceBefore = false;
                    continue;
                }

                if (isAlpha(c))
                {
                    tokens.push_back(scanIdentifier(state_));
                    state_.pendingSpaceBefore = false;
                    continue;
                }

                tokens.push_back(scanOperator(state_));
                state_.pendingSpaceBefore = false;
            }

            // The end of the program: the end of the last statement if it was followed by a newline, else the
            // end of the text itself.
            const SourceLocation eof = pendingNewline ? endOfStatement : state_.here();

            if (pendingNewline || (!tokens.empty() && !tokens.back().is(TokenType::NEWLINE)))
            {
                tokens.emplace_back(TokenType::NEWLINE, "\\n", eof, false);
            }

            while (indentStack.top() > 0)
            {
                indentStack.pop();
                tokens.emplace_back(TokenType::DEDENT, "", eof, false, 0);
            }

            tokens.emplace_back(TokenType::EOF_TOKEN, "", eof, false);

            return tokens;
        }

    private:
        static constexpr size_t kReserveTokensMax = size_t(1) << 20;

        ScanState state_;

        bool isCommentAhead() const
        {
            return state_.peek(0) == 'n' && state_.peek(1) == 'o' && state_.peek(2) == 't' && state_.peek(3) == 'e' && !isAlphaNum(state_.peek(4));
        }
    };

}
