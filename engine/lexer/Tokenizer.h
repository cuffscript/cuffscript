#pragma once

#include "../common/SourceText.h"
#include "../common/Token.h"
#include "../common/CuffError.h"
#include "../common/Text.h"
#include <string>
#include <algorithm>
#include <vector>
#include <stack>

namespace cuff
{

    // ---- characters -----------------------------------------------------------------------------------------------

    inline bool isAlpha(int c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    inline bool isDigit(int c)
    {
        return c >= '0' && c <= '9';
    }

    inline bool isAlphaNum(int c)
    {
        return isAlpha(c) || isDigit(c);
    }

    inline bool isSpace(int c)
    {
        return c == ' ' || c == '\t' || c == '\r';
    }

    inline bool isNewline(int c)
    {
        return c == '\n';
    }

    // ---- the cursor over the source text --------------------------------------------------------------------------

    class ScanState
    {
    public:
        const std::string &source;
        int offset;
        int line;
        int column;
        bool pendingSpaceBefore;

        explicit ScanState(const std::string &src)
            : source(src), offset(0), line(1), column(1), pendingSpaceBefore(false)
        {
            // A UTF-8 byte order mark (Windows editors add one) is not part of the program.
            if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
                static_cast<unsigned char>(src[1]) == 0xBB && static_cast<unsigned char>(src[2]) == 0xBF)
                offset = 3;
        }

        bool atEnd() const { return offset >= static_cast<int>(source.size()); }

        int peek(int ahead = 0) const
        {
            int idx = offset + ahead;
            if (idx >= static_cast<int>(source.size()))
                return -1;
            return static_cast<unsigned char>(source[idx]);
        }

        int advance()
        {
            if (atEnd())
                return -1;
            int c = source[offset++];
            if (c == '\n')
            {
                ++line;
                column = 1;
            }
            else if ((c & 0xC0) != 0x80)
            {
                ++column; // columns count characters: the continuation bytes of a multi-byte one do not
            }
            return c;
        }

        SourceLocation here() const
        {
            return SourceLocation(line, column, offset);
        }

        // Skips inline whitespace but leaves newlines for the tokenizer.
        void skipInlineWhitespace()
        {
            while (!atEnd())
            {
                int c = peek();
                if (isSpace(c))
                {
                    advance();
                    pendingSpaceBefore = true;
                }
                else
                {
                    break;
                }
            }
        }
    };

    // ---- escapes --------------------------------------------------------------------------------------------------

    // What a backslash escape inside a string literal stands for, appended to `out`. `esc` is the character after
    // the backslash. An escape that is not defined keeps its backslash, so "\q" is the two characters \q.
    //
    // One definition, used wherever escapes are read: the tokenizer decodes literals with it, and the parser decodes
    // them again to find where each character of an f-string came from (LiteralParser, in parser/ExpressionParser.h),
    // so the two cannot disagree about what an escape means.
    inline void appendEscaped(std::string &out, int esc)
    {
        switch (esc)
        {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case '"':
            out += '"';
            break;
        case '\'':
            out += '\'';
            break;
        case '\\':
            out += '\\';
            break;
        default:
            out += '\\';
            out += static_cast<char>(esc);
            break;
        }
    }

    // ---- comments -------------------------------------------------------------------------------------------------

    inline bool scanComment(ScanState &s)
    {
        if (s.peek(0) != 'n' || s.peek(1) != 'o' || s.peek(2) != 't' || s.peek(3) != 'e')
            return false;
        if (isAlphaNum(s.peek(4)))
            return false;

        for (int i = 0; i < 4; ++i)
            s.advance();

        if (s.peek() != ':')
        {
            throw SyntaxError("expected ':' after 'note'", s.here());
        }
        s.advance();

        while (!s.atEnd() && isSpace(s.peek()))
            s.advance();

        if (isNewline(s.peek()) || s.atEnd())
        {
            if (!s.atEnd())
                s.advance();

            while (!s.atEnd())
            {
                if (s.peek(0) == 'e' && s.peek(1) == 'n' && s.peek(2) == 'd' && s.peek(3) == 'n' && s.peek(4) == 'o' && s.peek(5) == 't' && s.peek(6) == 'e' && !isAlphaNum(s.peek(7)))
                {
                    for (int i = 0; i < 7; ++i)
                        s.advance();
                    s.pendingSpaceBefore = true;
                    return true;
                }
                s.advance();
            }
            throw SyntaxError("unterminated multi-line comment — missing 'endnote'", s.here());
        }

        while (!s.atEnd() && !isNewline(s.peek()))
        {
            s.advance();
        }
        s.pendingSpaceBefore = true;
        return true;
    }

    // ---- string and f-string literals -----------------------------------------------------------------------------

    // Scans a string literal starting at current position.
    // Handles regular "...", single-quoted '...', and f-strings f"...".
    // Escape sequences: \" \' \\ \n \t \r
    // Inside f-strings, {expr} blocks are preserved as raw text for the parser.
    //
    // Single-quoted strings exist specifically so that an embedded expression
    // inside an f-string (e.g. f"year: {res['year']}") can contain a nested
    // string literal without prematurely closing the outer f-string: the
    // f-string scanner only watches for an unescaped '"', so a '...' literal
    // used *inside* {...} passes straight through as ordinary text and is
    // re-tokenized correctly later when the embedded expression is re-parsed.
    inline Token scanString(ScanState &s)
    {
        SourceLocation start = s.here();
        bool isFString = false;

        if (s.peek() == 'f')
        {
            if (s.peek(1) != '"')
            {
                return Token(TokenType::WORD, "", start);
            }
            isFString = true;
            s.advance();
        }

        char quote = static_cast<char>(s.peek());
        if (quote != '"' && quote != '\'')
        {
            return Token(TokenType::WORD, "", start);
        }
        // f-strings open only with a double quote; f'...' falls through as an identifier character.
        if (isFString && quote != '"')
        {
            return Token(TokenType::WORD, "", start);
        }

        s.advance();

        std::string value;

        while (!s.atEnd())
        {
            int c = s.peek();

            if (c == '\\' && !s.atEnd())
            {
                s.advance();
                int esc = s.advance();
                appendEscaped(value, esc);
                continue;
            }

            if (c == quote)
            {
                s.advance();
                TokenType tt = isFString ? TokenType::FSTRING : TokenType::STRING;
                return Token(tt, value, start, s.pendingSpaceBefore);
            }

            // A line break inside a literal is one "\n" whatever the file's line endings are.
            if (c == '\r' && s.peek(1) == '\n')
            {
                s.advance();
                continue;
            }

            value += static_cast<char>(c);
            s.advance();
        }

        throw SyntaxError("unterminated string literal", start);
    }

    // ---- pattern literals -----------------------------------------------------------------------------------------

    // Scans a pattern literal p"..." and the flag letters that follow its closing quote.
    //
    // The text between the quotes is raw: it reaches the pattern parser exactly as written, except that \" stands
    // for a double quote. Any other backslash and the character after it are kept together, so a backslash never
    // escapes the closing quote unless it is written \".
    inline Token scanPattern(ScanState &s)
    {
        SourceLocation start = s.here();
        s.advance(); // p
        s.advance(); // opening quote

        std::string value;
        for (;;)
        {
            if (s.atEnd())
                throw SyntaxError("unterminated pattern literal", start);
            int c = s.advance();
            if (c == '"')
                break;
            if (c == '\\')
            {
                if (s.atEnd())
                    throw SyntaxError("unterminated pattern literal", start);
                int next = s.advance();
                if (next != '"')
                    value += '\\';
                value += static_cast<char>(next);
                continue;
            }
            value += static_cast<char>(c);
        }

        uint8_t flags = 0;
        if (isAlphaNum(s.peek()))
        {
            const SourceLocation flagsAt = s.here();
            std::string letters;
            while (isAlphaNum(s.peek()))
                letters += static_cast<char>(s.advance());
            for (char f : letters)
            {
                uint8_t bit = f == 'i' ? kPatternFlagIgnoreCase : f == 'm' ? kPatternFlagMultiline : 0;
                if (bit == 0 || (flags & bit))
                {
                    std::string hint = f == 'g' ? "there is no 'g' flag: find_all, replace_all and split work on every match"
                                                : "the flags are 'i' (ignore case) and 'm' (line anchors), each at most once";
                    throw SyntaxError(ErrorCode::UnexpectedToken,
                                      std::string("invalid pattern flag '") + f + "' after pattern literal", flagsAt, hint);
                }
                flags |= bit;
            }
        }

        Token tok(TokenType::PATTERN, std::move(value), start, s.pendingSpaceBefore);
        tok.patternFlags = flags;
        return tok;
    }

    // ---- numbers --------------------------------------------------------------------------------------------------

    inline Token scanNumber(ScanState &s)
    {
        SourceLocation start = s.here();
        std::string value;

        while (!s.atEnd() && isDigit(s.peek()))
        {
            value += static_cast<char>(s.advance());
        }

        if (s.peek() == '.' && isDigit(s.peek(1)))
        {
            value += static_cast<char>(s.advance());
            while (!s.atEnd() && isDigit(s.peek()))
            {
                value += static_cast<char>(s.advance());
            }
        }

        // An exponent: `e` or `E`, an optional sign, and digits (1e9, 2.5E-3).
        if (!value.empty() && (s.peek() == 'e' || s.peek() == 'E'))
        {
            const int sign = s.peek(1) == '+' || s.peek(1) == '-' ? 1 : 0;
            if (isDigit(s.peek(1 + sign)))
            {
                for (int k = 0; k <= sign; ++k)
                    value += static_cast<char>(s.advance());
                while (!s.atEnd() && isDigit(s.peek()))
                    value += static_cast<char>(s.advance());
            }
        }

        if (value.empty())
        {
            throw SyntaxError("invalid number literal", start);
        }

        // A letter that touches a number never begins a valid token, so it is reported here, where the reason is known.
        if (isAlpha(s.peek()))
        {
            const int next = s.peek();
            const bool radix = value == "0" && (next == 'x' || next == 'X' || next == 'b' || next == 'B' || next == 'o' || next == 'O');
            if (next == 'e' || next == 'E')
                throw SyntaxError(ErrorCode::InvalidNumberLiteral, "an exponent needs at least one digit after '" + std::string(1, static_cast<char>(next)) + "'",
                                  s.here(), "write it as 1e3, 2.5e-3 or 4E+2");
            if (radix)
                throw SyntaxError(ErrorCode::InvalidNumberLiteral, "a number is written in decimal", start,
                                  "hexadecimal, binary and octal forms are not part of the language");
            if (next == '_')
                throw SyntaxError(ErrorCode::InvalidNumberLiteral, "a number is written without separators", s.here(),
                                  "write 1000000, or 1e6");
            throw SyntaxError(ErrorCode::InvalidNumberLiteral, "a number cannot be followed directly by a letter", s.here(),
                              "put a space or an operator between the number and the word");
        }

        return Token(TokenType::NUMBER, value, start, s.pendingSpaceBefore);
    }

    // ---- words ----------------------------------------------------------------------------------------------------

    inline Token scanIdentifier(ScanState &s)
    {
        SourceLocation start = s.here();
        std::string value;

        if (!isAlpha(s.peek()))
        {
            return Token(TokenType::WORD, "", start);
        }

        while (!s.atEnd() && isAlphaNum(s.peek()))
        {
            value += static_cast<char>(s.advance());
        }

        return Token(TokenType::WORD, value, start, s.pendingSpaceBefore);
    }

    // ---- operators and punctuation --------------------------------------------------------------------------------

    inline Token scanOperator(ScanState &s)
    {
        SourceLocation start = s.here();
        int c = s.peek();

        auto make = [&](TokenType tt, const char *label) -> Token
        {
            s.advance();
            return Token(tt, label, start, s.pendingSpaceBefore);
        };

        if (c == '>')
        {
            s.advance();
            if (s.peek() == '=')
            {
                s.advance();
                return Token(TokenType::GE, ">=", start, s.pendingSpaceBefore);
            }
            return Token(TokenType::GT, ">", start, s.pendingSpaceBefore);
        }
        if (c == '<')
        {
            s.advance();
            if (s.peek() == '=')
            {
                s.advance();
                return Token(TokenType::LE, "<=", start, s.pendingSpaceBefore);
            }
            return Token(TokenType::LT, "<", start, s.pendingSpaceBefore);
        }

        // `&`, `|`, `&&` and `||` are not operators: the words `and` and `or` are.
        auto wordOperator = [&](char ch, const char *word) -> Token
        {
            s.advance();
            const bool doubled = s.peek() == ch;
            throw SyntaxError(ErrorCode::UnexpectedToken,
                              std::string("'") + ch + (doubled ? std::string(1, ch) : std::string()) + "' is not an operator", start,
                              std::string("write '") + word + "'");
        };

        switch (c)
        {
        case '+':
            return make(TokenType::PLUS, "+");
        case '-':
            return make(TokenType::MINUS, "-");
        case '*':
            s.advance();
            if (s.peek() == '*')
            {
                s.advance();
                return Token(TokenType::POWER, "**", start, s.pendingSpaceBefore);
            }
            return Token(TokenType::STAR, "*", start, s.pendingSpaceBefore);
        case '%':
            return make(TokenType::PERCENT, "%");
        case '&':
            return wordOperator('&', "and");
        case '|':
            return wordOperator('|', "or");
        case '/':
            return make(TokenType::SLASH, "/");
        case '!':
            return make(TokenType::BANG, "!");
        case '(':
            return make(TokenType::LPAREN, "(");
        case ')':
            return make(TokenType::RPAREN, ")");
        case '[':
            return make(TokenType::LBRACKET, "[");
        case ']':
            return make(TokenType::RBRACKET, "]");
        case '{':
            return make(TokenType::LBRACE, "{");
        case '}':
            return make(TokenType::RBRACE, "}");
        case ',':
            return make(TokenType::COMMA, ",");
        case ':':
            return make(TokenType::COLON, ":");
        case '.':
            return make(TokenType::DOT, ".");
        case '~':
            return make(TokenType::TILDE, "~");
        case '=':
            return make(TokenType::ASSIGN, "=");
        default:
        {
            // Show the whole character: its first byte alone is not valid UTF-8.
            size_t len = utf8::seqLen(static_cast<unsigned char>(c));
            throw SyntaxError("Unexpected character '" + s.source.substr(static_cast<size_t>(s.offset), len) + "'", start);
        }
        }
    }

    // ---- the tokenizer --------------------------------------------------------------------------------------------

    // Source text to tokens. Besides the tokens a line is made of, it gives a NEWLINE at the end of each statement
    // and INDENT / DEDENT where the indentation changes, and records on every token whether a space came before it
    // (for the colon rule) and how deeply its line is indented (for the rule that a body is indented one level).
    // Comments (`note:` to the end of the line, `note:` on a line of its own to `endnote`) and blank lines give
    // nothing. Blocks are closed by `end`; the indentation is checked, not used to close them.
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

                if (c == 'p' && state_.peek(1) == '"')
                {
                    tokens.push_back(scanPattern(state_));
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
