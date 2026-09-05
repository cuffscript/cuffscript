#pragma once

#include "../common/SourceLocation.h"
#include "../common/TokenTypes.h"
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
        SourceLocation loc;
        StringLiteral(std::string v, SourceLocation l) : value(std::move(v)), loc(l) {}
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
        SourceLocation loc;
        IdentifierExpr(std::string n, SourceLocation l) : name(std::move(n)), loc(l) {}
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

    struct BinaryOp
    {
        std::string op; // "+", "-", "*", "/", "is", "IS", ">=", "<=", ">", "<"
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
        SourceLocation loc;
        BinaryOp(std::string o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, SourceLocation lc)
            : op(std::move(o)), left(std::move(l)), right(std::move(r)), loc(lc) {}
    };

    struct UnaryOp
    {
        std::string op; // "!" (NOT) or "-" (negation)
        std::unique_ptr<Expr> operand;
        SourceLocation loc;
        UnaryOp(std::string o, std::unique_ptr<Expr> e, SourceLocation l)
            : op(std::move(o)), operand(std::move(e)), loc(l) {}
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
        std::vector<std::unique_ptr<Expr>> args;
        SourceLocation loc;
        FunctionCall(std::string n, std::vector<std::unique_ptr<Expr>> a, SourceLocation l)
            : functionName(std::move(n)), args(std::move(a)), loc(l) {}
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
        RegexMatch
    };

    struct Expr
    {
        ExprKind kind;
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
            RegexMatchExpr>
            data;

        template <typename T>
        Expr(ExprKind k, T &&v) : kind(k), data(std::forward<T>(v)) {}
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
        bool isConstant = false;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
    };

    struct ChangeStmt
    {
        // change [name] to [value]
        std::string name;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
    };

    struct FunctionDecl
    {
        enum class FuncKind
        {
            Normal,
            Returnable,
            Async
        };
        FuncKind funcKind;
        std::string name;
        std::vector<std::string> params;
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
    };

    // =========================================================================
    // Program root
    // =========================================================================

    struct Program
    {
        std::vector<std::unique_ptr<Stmt>> statements;
    };

} // namespace cuff
