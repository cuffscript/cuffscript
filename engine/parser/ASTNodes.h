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

    struct Expr;
    struct Stmt;

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
        struct Pair
        {
            std::unique_ptr<Expr> key;
            std::unique_ptr<Expr> value;
        };
        std::vector<Pair> pairs;
        SourceLocation loc;
        MapLiteral(std::vector<Pair> p, SourceLocation l) : pairs(std::move(p)), loc(l) {}
    };

    struct FStringExpr
    {
        struct Segment
        {
            bool isExpression;
            std::string text;
            std::unique_ptr<Expr> expr;
        };
        std::vector<Segment> segments;
        SourceLocation loc;
        FStringExpr(std::vector<Segment> s, SourceLocation l)
            : segments(std::move(s)), loc(l) {}
    };

    enum class BinOp
    {
        Add,
        Sub,
        Mul,
        Div,
        Is,
        IsCase,
        IsNot,
        IsNotCase,
        Greater,
        Less,
        GreaterEq,
        LessEq
    };

    enum class UnOp
    {
        Not,
        Negate
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
        std::unique_ptr<Expr> target;
        bool caseInsensitive;
        std::string pattern;
        SourceLocation loc;
        RegexMatchExpr(std::unique_ptr<Expr> t, bool ci, std::string p, SourceLocation l)
            : target(std::move(t)), caseInsensitive(ci), pattern(std::move(p)), loc(l) {}
    };

    struct PatternArg
    {
        std::string literalPattern;
        std::unique_ptr<Expr> dynamicExpr;
        bool isLiteral = true;
    };

    struct MatchFromExpr
    {
        std::unique_ptr<Expr> target;
        PatternArg pattern;
        std::string flags;
        SourceLocation loc;
    };

    struct FindExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::string flags;
        SourceLocation loc;
    };

    struct PatternReplaceExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::unique_ptr<Expr> replacement;
        std::string flags;
        SourceLocation loc;
    };

    struct SplitExpr
    {
        std::unique_ptr<Expr> target;
        PatternArg pattern;
        SourceLocation loc;
    };

    struct CountExpr
    {
        PatternArg pattern;
        std::unique_ptr<Expr> target;
        std::string flags;
        SourceLocation loc;
    };

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

    struct DeclarationStmt
    {
        std::string varType;
        std::string name;
        uint32_t nameId = 0;
        bool isConstant = false;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
    };

    struct ChangeStmt
    {
        std::string name;
        uint32_t nameId = 0;
        std::vector<std::unique_ptr<Expr>> indices;
        std::unique_ptr<Expr> value;
        bool toGlobal = false;
        SourceLocation loc;
    };

    enum class ParamType : uint8_t
    {
        Any,
        Number,
        Str,
        Boolean,
        List,
        Map,
        Match
    };

    inline const char *paramTypeName(ParamType t)
    {
        switch (t)
        {
        case ParamType::Number:
            return "number";
        case ParamType::Str:
            return "str";
        case ParamType::Boolean:
            return "boolean";
        case ParamType::List:
            return "list";
        case ParamType::Map:
            return "map";
        case ParamType::Match:
            return "match";
        case ParamType::Any:
            break;
        }
        return "";
    }

    struct FunctionDecl
    {
        bool isAsync = false;
        bool isReturnable = false;
        bool isPure = false;
        std::string name;
        uint32_t nameId = 0;
        std::vector<std::string> params;
        std::vector<uint32_t> paramIds;
        std::vector<ParamType> paramTypes;
        bool hasTypedParams = false;
        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
    };

    struct IfStmt
    {
        struct Branch
        {
            std::unique_ptr<Expr> condition;
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
            While
        };
        LoopKind kind;

        std::string repeatVar;
        uint32_t repeatVarId = 0;
        std::unique_ptr<Expr> repeatStart;
        std::unique_ptr<Expr> repeatEnd;

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
        struct DLCName
        {
            std::string name;
            SourceLocation loc;
        };
        std::vector<DLCName> dlcs;  // DLC form only
        std::string name;  // module form only
        std::string path;  // module form only
        SourceLocation loc;
    };

    struct ExprStmt
    {
        std::unique_ptr<Expr> expr;
        SourceLocation loc;
        ExprStmt(std::unique_ptr<Expr> e, SourceLocation l)
            : expr(std::move(e)), loc(l) {}
    };

    struct CollectionOpStmt
    {
        enum class OpKind
        {
            Add,
            Remove,
            Replace
        };
        OpKind opKind;

        std::unique_ptr<Expr> addValue;
        std::string collectionName;
        uint32_t collectionNameId = 0;

        std::unique_ptr<Expr> indexOrKey;
        std::unique_ptr<Expr> newValue;

        std::unique_ptr<Expr> removeValue;

        SourceLocation loc;
    };

    struct OrElseStmt
    {
        std::unique_ptr<Stmt> primaryStmt;
        std::vector<std::unique_ptr<Stmt>> fallbackBody;
        SourceLocation loc;
    };

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

        Stmt(const Stmt &) = delete;
        Stmt &operator=(const Stmt &) = delete;
        Stmt(Stmt &&) = default;
        Stmt &operator=(Stmt &&) = default;
    };

    struct Program
    {
        std::vector<std::unique_ptr<Stmt>> statements;
    };

}
