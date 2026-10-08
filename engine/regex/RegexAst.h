#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cctype>

namespace cuff::regex
{

    inline unsigned char toggleAsciiCase(unsigned char c)
    {
        if (c >= 'a' && c <= 'z')
            return static_cast<unsigned char>(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z')
            return static_cast<unsigned char>(c - 'A' + 'a');
        return c;
    }

    inline bool isWordChar(unsigned char c)
    {
        return std::isalnum(c) || c == '_';
    }

    enum class RNodeKind
    {
        CharTest,
        Preset,
        WordBoundary,
        StartAnchor,
        EndAnchor,
        Group,
        NamedGroup,
        OneOf,
        Sequence,
        Quantified,
    };

    enum class PresetKind
    {
        Int,
        Float,
        Email,
        Phone,
        Url
    };

    struct RNode;
    using RNodePtr = std::shared_ptr<RNode>;

    struct RNode
    {
        RNodeKind kind;

        std::function<bool(unsigned char)> charTest;
        bool negated = false;
        bool isAnyCodepoint = false;
        std::string multiByteLiteral;
        bool isLiteral = false;  // a plain or escaped character: a digit run after it is text, not a repeat count

        PresetKind presetKind = PresetKind::Int;

        int groupIndex = -1;  // 1-based, only for Group
        std::string groupName;  // only for NamedGroup
        RNodePtr child;

        std::vector<std::string> alternatives;

        std::vector<RNodePtr> children;

        int minCount = 1;
        int maxCount = 1;
        bool lazy = false;

        explicit RNode(RNodeKind k) : kind(k) {}
    };

    inline RNodePtr makeNode(RNodeKind k)
    {
        return std::make_shared<RNode>(k);
    }

    inline bool classNum(unsigned char c) { return c >= '0' && c <= '9'; }
    inline bool classLow(unsigned char c) { return c >= 'a' && c <= 'z'; }
    inline bool classUp(unsigned char c) { return c >= 'A' && c <= 'Z'; }
    inline bool classLet(unsigned char c) { return classLow(c) || classUp(c); }
    inline bool classStr(unsigned char c) { return classLet(c) || classNum(c); }
    inline bool classWord(unsigned char c) { return classStr(c) || c == '_'; }
    inline bool classSp(unsigned char c) { return c == ' ' || c == '\t'; }
    inline bool classNl(unsigned char c) { return c == '\n' || c == '\r'; }
    inline bool classHex(unsigned char c) { return classNum(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

}
