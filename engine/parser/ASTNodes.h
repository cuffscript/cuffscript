#pragma once

#include "../common/SourceLocation.h"
#include "../common/NameInterner.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/StrData.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace cuff
{

    // ---- Forward declarations ----
    struct Expr;
    struct Stmt;

    // =========================================================================
    // Expression nodes
    // =========================================================================

    struct NumberLiteral
    {
        double value;
        SourceLocation loc;
        NumberLiteral(double v, SourceLocation l) : value(v), loc(l) {}
    };

    struct StringLiteral
    {
        std::string value;
        std::shared_ptr<const StrData> shared;
        SourceLocation loc;
        StringLiteral(std::string v, SourceLocation l)
            : value(std::move(v)), shared(std::make_shared<const StrData>(value)), loc(l) {}
    };

    struct BoolLiteral
    {
        bool value;
        SourceLocation loc;
        BoolLiteral(bool v, SourceLocation l) : value(v), loc(l) {}
    };

    struct EmptyLiteral
    {
        SourceLocation loc;
        explicit EmptyLiteral(SourceLocation l) : loc(l) {}
    };

    struct IdentifierExpr
    {
        std::string name;
        uint32_t nameId;
        SourceLocation loc;
        IdentifierExpr(std::string n, SourceLocation l) : name(std::move(n)), nameId(internName(name)), loc(l) {}
    };

    struct ListLiteral
    {
        std::vector<std::unique_ptr<Expr>> elements;
        SourceLocation loc;
        ListLiteral(std::vector<std::unique_ptr<Expr>> e, SourceLocation l)
            : elements(std::move(e)), loc(l) {}
    };

    struct MapLiteral
    {
        // key-value pairs: keys are expressions (typically string literals)
        struct Pair
        {
            std::unique_ptr<Expr> key;
            std::unique_ptr<Expr> value;
        };
        std::vector<Pair> pairs;
        SourceLocation loc;
        MapLiteral(std::vector<Pair> p, SourceLocation l) : pairs(std::move(p)), loc(l) {}
    };

    // f"Hello {expr} world" → segments of literal text and embedded expressions
    struct FStringExpr
    {
        struct Segment
        {
            bool isExpression;
            std::string text;           // when isExpression == false
            std::unique_ptr<Expr> expr; // when isExpression == true
        };
        std::vector<Segment> segments;
        SourceLocation loc;
        FStringExpr(std::vector<Segment> s, SourceLocation l)
            : segments(std::move(s)), loc(l) {}
    };

    // Operators are stored as enums, not strings. The interpreter dispatches
    // on them in a hot loop, and a string-compare chain there cost ~10 string
    // comparisons per operation (measured: 16M comparisons for a 635k-call
    // fib benchmark, the single largest cost in the profile).
    enum class BinOp
    {
        Add,        // +
        Sub,        // -
        Mul,        // *
        Div,        // /
        Is,         // is      (exact equality)
        IsCase,     // IS      (case-insensitive equality)
        IsNot,      // is not
        IsNotCase,  // IS not
        Greater,    // >
        Less,       // <
        GreaterEq,  // >=
        LessEq      // <=
    };

    enum class UnOp
    {
        Not,    // !
        Negate  // -
    };

    inline const char *binOpName(BinOp op)
    {
        switch (op)
        {
        case BinOp::Add: return "+";
        case BinOp::Sub: return "-";
        case BinOp::Mul: return "*";
        case BinOp::Div: return "/";
        case BinOp::Is: return "is";
        case BinOp::IsCase: return "IS";
        case BinOp::IsNot: return "is not";
        case BinOp::IsNotCase: return "IS not";
        case BinOp::Greater: return ">";
        case BinOp::Less: return "<";
        case BinOp::GreaterEq: return ">=";
        case BinOp::LessEq: return "<=";
        }
        return "?";
    }

    inline const char *unOpName(UnOp op)
    {
        return op == UnOp::Not ? "!" : "-";
    }

    struct BinaryOp
    {
        BinOp op;
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
        SourceLocation loc;
        BinaryOp(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, SourceLocation lc)
            : op(o), left(std::move(l)), right(std::move(r)), loc(lc) {}
    };

    struct UnaryOp
    {
        UnOp op;
        std::unique_ptr<Expr> operand;
        SourceLocation loc;
        UnaryOp(UnOp o, std::unique_ptr<Expr> e, SourceLocation l)
            : op(o), operand(std::move(e)), loc(l) {}
    };

    struct IndexAccess
    {
        std::unique_ptr<Expr> target;
        std::unique_ptr<Expr> index;
        SourceLocation loc;
        IndexAccess(std::unique_ptr<Expr> t, std::unique_ptr<Expr> i, SourceLocation l)
            : target(std::move(t)), index(std::move(i)), loc(l) {}
    };

    struct SliceAccess
    {
        std::unique_ptr<Expr> target;
        std::unique_ptr<Expr> start;
        std::unique_ptr<Expr> end;
        SourceLocation loc;
        SliceAccess(std::unique_ptr<Expr> t, std::unique_ptr<Expr> s, std::unique_ptr<Expr> e, SourceLocation l)
            : target(std::move(t)), start(std::move(s)), end(std::move(e)), loc(l) {}
    };

    struct FunctionCall
    {
        std::string functionName;
        uint32_t functionNameId;
        std::vector<std::unique_ptr<Expr>> args;
        SourceLocation loc;
        FunctionCall(std::string n, std::vector<std::unique_ptr<Expr>> a, SourceLocation l)
            : functionName(std::move(n)), functionNameId(internName(functionName)), args(std::move(a)), loc(l) {}
    };

    struct AwaitExpr
    {
        std::unique_ptr<FunctionCall> call;
        SourceLocation loc;
        AwaitExpr(std::unique_ptr<FunctionCall> c, SourceLocation l)
            : call(std::move(c)), loc(l) {}
    };

    struct RegexMatchExpr
    {
        std::unique_ptr<Expr> target; // the string being matched
        bool caseInsensitive;         // IS vs is
        std::string pattern;          // raw pattern string
        SourceLocation loc;
        RegexMatchExpr(std::unique_ptr<Expr> t, bool ci, std::string p, SourceLocation l)
            : target(std::move(t)), caseInsensitive(ci), pattern(std::move(p)), loc(l) {}
    };

    // =========================================================================
    // Pattern-matching command expressions (see docs/REGEX.md).
    //
    // `pattern` is filled in when the pattern was written as a literal string
    // (the common case) — it is compiled once, eagerly, at parse time, so a
    // malformed literal pattern is reported immediately as a RegexSyntaxError
    // rather than only when that line of the program happens to execute.
    // `patternExpr` is filled in instead when the pattern is a dynamic
    // expression (e.g. a variable) — compiled lazily, once, the first time
    // that expression is evaluated. Exactly one of the two is non-null/non-empty.
    // Flags is the raw suffix letters (any subset of "gim"), or empty.
    // =========================================================================

    struct PatternArg
    {
        std::string literalPattern;         // used when isLiteral == true
        std::unique_ptr<Expr> dynamicExpr;  // used when isLiteral == false
        bool isLiteral = true;
    };

    // match <target> from <pattern> [flags]
    struct MatchFromExpr
    {
        std::unique_ptr<Expr> target;
        PatternArg pattern;
        std::string flags;
        SourceLocation loc;
    };

    // find <pattern> from <target> [flags]
    struct FindExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::string flags;
        SourceLocation loc;
    };

    // replace <pattern> in <target> to <replacement> [flags]
    struct PatternReplaceExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::unique_ptr<Expr> replacement;
        std::string flags;
        SourceLocation loc;
    };

    // split <target> by <pattern>
    struct SplitExpr
    {
        std::unique_ptr<Expr> target;
        PatternArg pattern;
        SourceLocation loc;
    };

    // count <pattern> in <target> [flags]
    struct CountExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::string flags;
        SourceLocation loc;
    };

    // =========================================================================
    // Expression variant
    // =========================================================================

    enum class ExprKind
    {
        Number,
        String,
        Bool,
        Empty,
        Identifier,
        List,
        Map,
        FString,
        BinaryOp,
        UnaryOp,
        IndexAccess,
        SliceAccess,
        FunctionCall,
        Await,
        RegexMatch,
        MatchFrom,
        Find,
        PatternReplace,
        Split,
        Count
    };

    struct Expr
    {
        ExprKind kind;
        uint32_t height = 1;
        std::variant<
            NumberLiteral,
            StringLiteral,
            BoolLiteral,
            EmptyLiteral,
            IdentifierExpr,
            ListLiteral,
            MapLiteral,
            FStringExpr,
            BinaryOp,
            UnaryOp,
            IndexAccess,
            SliceAccess,
            FunctionCall,
            AwaitExpr,
            RegexMatchExpr,
            MatchFromExpr,
            FindExpr,
            PatternReplaceExpr,
            SplitExpr,
            CountExpr>
            data;

        // `height` is the length of the longest path down to a leaf. Bounding it
        // keeps evaluation, printing and destruction of the tree (all recursive)
        // safe even for left-associative chains like `1+1+1+...`, which the
        // parser builds iteratively.
        template <typename T>
        Expr(ExprKind k, T &&v) : kind(k), data(std::forward<T>(v))
        {
            height = 1 + std::visit(ChildHeight{}, data);
            if (height > limits::kMaxExprHeight)
            {
                throw SyntaxError(ErrorCode::NestingTooDeep,
                                  "expression is too long or too deeply nested (maximum depth is " +
                                      std::to_string(limits::kMaxExprHeight) + ")",
                                  std::visit([](const auto &n) { return n.loc; }, data));
            }
        }

        // AST nodes own their children through unique_ptr, so the tree as a
        // whole is move-only. Declared explicitly so an accidental copy fails
        // at the call site with a clear "deleted function" error.
        Expr(const Expr &) = delete;
        Expr &operator=(const Expr &) = delete;
        Expr(Expr &&) = default;
        Expr &operator=(Expr &&) = default;

    private:
        struct ChildHeight
        {
            static uint32_t h(const std::unique_ptr<Expr> &e) { return e ? e->height : 0; }

            template <typename T>
            uint32_t operator()(const T &) const { return 0; }

            uint32_t operator()(const ListLiteral &n) const
            {
                uint32_t m = 0;
                for (const auto &e : n.elements)
                    m = std::max(m, h(e));
                return m;
            }
            uint32_t operator()(const MapLiteral &n) const
            {
                uint32_t m = 0;
                for (const auto &p : n.pairs)
                    m = std::max({m, h(p.key), h(p.value)});
                return m;
            }
            uint32_t operator()(const FStringExpr &n) const
            {
                uint32_t m = 0;
                for (const auto &s : n.segments)
                    if (s.isExpression)
                        m = std::max(m, h(s.expr));
                return m;
            }
            uint32_t operator()(const BinaryOp &n) const { return std::max(h(n.left), h(n.right)); }
            uint32_t operator()(const UnaryOp &n) const { return h(n.operand); }
            uint32_t operator()(const IndexAccess &n) const { return std::max(h(n.target), h(n.index)); }
            uint32_t operator()(const SliceAccess &n) const
            {
                return std::max({h(n.target), h(n.start), h(n.end)});
            }
            uint32_t operator()(const FunctionCall &n) const { return args(n.args); }
            uint32_t operator()(const AwaitExpr &n) const { return n.call ? args(n.call->args) : 0; }
            uint32_t operator()(const RegexMatchExpr &n) const { return h(n.target); }
            uint32_t operator()(const MatchFromExpr &n) const
            {
                return std::max(h(n.target), h(n.pattern.dynamicExpr));
            }
            uint32_t operator()(const FindExpr &n) const
            {
                return std::max(h(n.target), h(n.pattern.dynamicExpr));
            }
            uint32_t operator()(const PatternReplaceExpr &n) const
            {
                return std::max({h(n.target), h(n.replacement), h(n.pattern.dynamicExpr)});
            }
            uint32_t operator()(const SplitExpr &n) const
            {
                return std::max(h(n.target), h(n.pattern.dynamicExpr));
            }
            uint32_t operator()(const CountExpr &n) const
            {
                return std::max(h(n.target), h(n.pattern.dynamicExpr));
            }

            static uint32_t args(const std::vector<std::unique_ptr<Expr>> &v)
            {
                uint32_t m = 0;
                for (const auto &e : v)
                    m = std::max(m, h(e));
                return m;
            }
        };
    };

    // =========================================================================
    // Statement nodes
    // =========================================================================

    struct DeclarationStmt
    {
        // set [type] [name] to [value]
        // set constant [type] [name] to [value]
        std::string varType; // "number", "str", "list", "map", "boolean", "empty"
        std::string name;
        uint32_t nameId = 0; // interned `name`, set by the parser
        bool isConstant = false;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
    };

    struct ChangeStmt
    {
        // change [name] to [value]                      -- plain reassignment
        // change [name][index]...[index] to [value]      -- indexed assignment
        //   (into a list element, or a map key — auto-vivifies missing map keys)
        // change [name] to global                        -- declare-global marker
        //   (must appear alone, in a function body, before mutating a global;
        //    `value` and `indices` are unused when toGlobal is true)
        std::string name;
        uint32_t nameId = 0; // interned `name`, set by the parser
        std::vector<std::unique_ptr<Expr>> indices;
        std::unique_ptr<Expr> value;
        bool toGlobal = false;
        SourceLocation loc;
    };

    struct FunctionDecl
    {
        // A function can be async, returnable, both, or neither (e.g.
        // `set async returnable function fetch() do: ... end`). The two
        // modifiers are independent, so they're tracked as separate flags
        // rather than a single enum.
        bool isAsync = false;
        bool isReturnable = false;
        std::string name;
        uint32_t nameId = 0; // interned `name`, set by the parser
        std::vector<std::string> params;
        std::vector<uint32_t> paramIds; // interned `params`, set by the parser
        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
    };

    struct IfStmt
    {
        struct Branch
        {
            std::unique_ptr<Expr> condition; // nullptr for else branch
            std::vector<std::unique_ptr<Stmt>> body;
        };
        std::vector<Branch> branches;
        SourceLocation loc;
    };

    struct LoopStmt
    {
        enum class LoopKind
        {
            Repeat,
            While,
            Match
        };
        LoopKind kind;

        // repeat: variable name, start expr, end expr
        std::string repeatVar;
        uint32_t repeatVarId = 0; // interned `repeatVar`, set by the parser
        std::unique_ptr<Expr> repeatStart;
        std::unique_ptr<Expr> repeatEnd;

        // while / match: condition expr
        std::unique_ptr<Expr> condition;

        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
    };

    struct StopStmt
    {
        SourceLocation loc;
        explicit StopStmt(SourceLocation l) : loc(l) {}
    };

    struct ReturnStmt
    {
        std::unique_ptr<Expr> value;
        SourceLocation loc;
        ReturnStmt(std::unique_ptr<Expr> v, SourceLocation l)
            : value(std::move(v)), loc(l) {}
    };

    struct AwaitStmt
    {
        std::unique_ptr<AwaitExpr> expr;
        SourceLocation loc;
        AwaitStmt(std::unique_ptr<AwaitExpr> e, SourceLocation l)
            : expr(std::move(e)), loc(l) {}
    };

    struct UseStmt
    {
        bool isDLC;
        std::string name;
        std::string path;
        SourceLocation loc;
    };

    struct ExprStmt
    {
        std::unique_ptr<Expr> expr;
        SourceLocation loc;
        ExprStmt(std::unique_ptr<Expr> e, SourceLocation l)
            : expr(std::move(e)), loc(l) {}
    };

    // Collection manipulation: add / remove / replace
    struct CollectionOpStmt
    {
        enum class OpKind
        {
            Add,
            Remove,
            Replace
        };
        OpKind opKind;

        // add [value] to [collectionName]
        std::unique_ptr<Expr> addValue;
        std::string collectionName;
        uint32_t collectionNameId = 0; // interned, set by the parser

        // replace [collection][index/key] to [newValue]
        // or replace [collection]["key"] to [newValue]
        std::unique_ptr<Expr> indexOrKey; // for replace
        std::unique_ptr<Expr> newValue;   // for replace

        // remove [index/key/value] from [collectionName]
        std::unique_ptr<Expr> removeValue; // for remove

        SourceLocation loc;
    };

    // or_else error handling: [stmt] or_else do: ... end
    struct OrElseStmt
    {
        // The "dangerous" statement that might fail — typically an await or function call
        std::unique_ptr<Stmt> primaryStmt;
        std::vector<std::unique_ptr<Stmt>> fallbackBody;
        SourceLocation loc;
    };

    // =========================================================================
    // Statement variant
    // =========================================================================

    enum class StmtKind
    {
        Declaration,
        Change,
        FunctionDecl,
        IfStmt,
        LoopStmt,
        StopStmt,
        ReturnStmt,
        AwaitStmt,
        UseStmt,
        ExprStmt,
        CollectionOp,
        OrElse
    };

    struct Stmt
    {
        StmtKind kind;
        std::variant<
            DeclarationStmt,
            ChangeStmt,
            FunctionDecl,
            IfStmt,
            LoopStmt,
            StopStmt,
            ReturnStmt,
            AwaitStmt,
            UseStmt,
            ExprStmt,
            CollectionOpStmt,
            OrElseStmt>
            data;

        template <typename T>
        Stmt(StmtKind k, T &&v) : kind(k), data(std::forward<T>(v)) {}

        // Move-only for the same reason as Expr above.
        Stmt(const Stmt &) = delete;
        Stmt &operator=(const Stmt &) = delete;
        Stmt(Stmt &&) = default;
        Stmt &operator=(Stmt &&) = default;
    };

    // =========================================================================
    // Program root
    // =========================================================================

    struct Program
    {
        std::vector<std::unique_ptr<Stmt>> statements;
    };

} // namespace cuff
