#pragma once

#include "common/CuffError.h"
#include "common/Limits.h"
#include "common/SourceText.h"
#include "common/Token.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "tokenizer/Tokenizer.h"
#include <memory>
#include <vector>

namespace cuff
{

    // Source text to Program: the one place the front-end stages are chained (tokenize, lex, parse). The
    // engine entry points, the interactive shell and the module loader all go through it, so a change to the
    // pipeline lands everywhere at once. `rawTokens` / `lexedTokens`, when given, receive the token streams
    // for the --ast dump.
    inline std::unique_ptr<Program> parseProgram(const SourceRef &source, std::vector<Token> *rawTokens = nullptr,
                                                 std::vector<Token> *lexedTokens = nullptr)
    {
        if (source->text.size() > limits::kMaxSourceBytes)
        {
            throw SyntaxError(ErrorCode::SourceTooLarge,
                              "source is larger than the limit of " + limits::formatBytes(limits::kMaxSourceBytes),
                              SourceLocation());
        }
        Tokenizer tokenizer(source->text);
        std::vector<Token> raw = tokenizer.tokenize();
        if (rawTokens)
            *rawTokens = raw;
        Lexer lexer(std::move(raw));
        std::vector<Token> lexed = lexer.lex();
        if (lexedTokens)
            *lexedTokens = lexed;
        Parser parser(std::move(lexed), source);
        return parser.parse();
    }

}
