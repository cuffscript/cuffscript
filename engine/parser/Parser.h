#pragma once

#include "ParserCore.h"
#include "ASTNodes.h"
#include "StatementParser.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include <memory>
#include <vector>

namespace cuff
{

    class Parser
    {
    public:
        // `source` is the text the tokens were scanned from; it lets an error found later, while a function runs,
        // be drawn against the right text (see SourceText.h).
        explicit Parser(std::vector<Token> tokens, SourceRef source = nullptr) : core_(std::move(tokens), std::move(source)) {}

        std::unique_ptr<Program> parse()
        {
            ParseStackFloorScope stackGuard;
            auto program = std::make_unique<Program>();
            program->source = core_.source;

            while (!core_.atEnd())
            {
                core_.skipNewlines();
                if (core_.atEnd())
                    break;

                auto stmt = StatementParser::parseStatement(core_);
                if (stmt)
                {
                    program->statements.push_back(std::move(stmt));
                }

                core_.skipNewlines();
            }

            return program;
        }

        ParserCore &core() { return core_; }

    private:
        ParserCore core_;
    };

}
