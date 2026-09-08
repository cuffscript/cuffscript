#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cctype>

// =========================================================================
// CuffScript's regular-expression dialect (docs/REGEX.md) intentionally has
// no `\d`, `\w`, `^`, `$`, etc. Instead every atom is either a plain literal
// character or a bracketed word-token like [num]/[one:...]/<name:...>.
//
// This file defines the compiled pattern AST that engine/regex/RegexParser.h
// produces and engine/regex/RegexMatcher.h executes. It is intentionally a
// flat, uniform node shape (one struct with tagged fields) rather than a
// polymorphic class hierarchy, matching the style already used for the
// language's own AST in engine/parser/ASTNodes.h — keeps the whole engine
// consistent and makes it easy to add a new atom kind later (add an enum
// value + fields here, a case in RegexParser.h, and a case in
// RegexMatcher.h; nothing else changes).
// =========================================================================

namespace cuff::regex
{

    // Toggle the ASCII case of a character. Used uniformly by the matcher to
    // implement case-insensitive matching for *every* atom kind (literals,
    // classes, presets, alternatives) without needing every predicate to know
    // about case-insensitivity itself: the matcher just also tries the
    // opposite-case character whenever ci is requested.
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
        CharTest,     // single-character predicate (literal, [num]/[let]/.../[hex], custom sets, negation)
        Preset,       // variable-length preset: [int] [float] [email] [phone] [url]
        WordBoundary, // [edge]
        StartAnchor,  // [start]
        EndAnchor,    // [end]
        Group,        // (...)          numbered capture
        NamedGroup,   // <name:...>     named capture
        OneOf,        // [one:a|b|c]    literal alternatives
        Sequence,     // concatenation of already-quantified children
        Quantified,   // wraps exactly one child with a repetition count
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

        // CharTest
        std::function<bool(unsigned char)> charTest; // case-sensitive predicate
        bool negated = false;                        // informational only (already folded into charTest)

        // Preset
        PresetKind presetKind = PresetKind::Int;

        // Group / NamedGroup
        int groupIndex = -1;      // 1-based, only for Group
        std::string groupName;    // only for NamedGroup
        RNodePtr child;           // Group / NamedGroup / Quantified wrap exactly one child

        // OneOf
        std::vector<std::string> alternatives;

        // Sequence
        std::vector<RNodePtr> children;

        // Quantified
        int minCount = 1;
        int maxCount = 1; // -1 means unbounded
        bool lazy = false;

        explicit RNode(RNodeKind k) : kind(k) {}
    };

    inline RNodePtr makeNode(RNodeKind k)
    {
        return std::make_shared<RNode>(k);
    }

    // ---- Character-class predicate builders (case-sensitive; the matcher
    // layers case-insensitivity on top uniformly via toggleAsciiCase) ----

    inline bool classNum(unsigned char c) { return c >= '0' && c <= '9'; }
    inline bool classLow(unsigned char c) { return c >= 'a' && c <= 'z'; }
    inline bool classUp(unsigned char c) { return c >= 'A' && c <= 'Z'; }
    inline bool classLet(unsigned char c) { return classLow(c) || classUp(c); }
    inline bool classStr(unsigned char c) { return classLet(c) || classNum(c); }
    inline bool classWord(unsigned char c) { return classStr(c) || c == '_'; }
    inline bool classSp(unsigned char c) { return c == ' ' || c == '\t'; }
    inline bool classNl(unsigned char c) { return c == '\n' || c == '\r'; }
    inline bool classHex(unsigned char c) { return classNum(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    inline bool classAny(unsigned char c) { return c != '\n'; }

} // namespace cuff::regex
