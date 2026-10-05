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
        explicit Parser(std::vector<Token> tokens) : core_(std::move(tokens)) {}

        std::unique_ptr<Program> parse()
        {
            ParseStackFloorScope stackGuard;
            auto program = std::make_unique<Program>();

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
