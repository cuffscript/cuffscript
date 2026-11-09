#pragma once

#include "../common/SourceText.h"
#include "../common/SourceLocation.h"
#include "../common/NameInterner.h"
#include "../common/TokenTypes.h"
#include "../common/CuffError.h"
#include "../common/Limits.h"
#include "../common/StrData.h"
#include "../regex/PatternData.h"
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
        bool inWith = false; // written inside a `with` block of the same function: the object's members are looked at first
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
        Mod,
        Pow,
        And,
        Or,
        Is,
        IsNot,
        Matches,
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
        case BinOp::Mod: return "%";
        case BinOp::Pow: return "**";
        case BinOp::And: return "and";
        case BinOp::Or: return "or";
        case BinOp::Is: return "is";
        case BinOp::IsNot: return "is not";
        case BinOp::Matches: return "matches";
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

    struct PatternLiteral
    {
        std::shared_ptr<const PatternData> value;
        SourceLocation loc;
        PatternLiteral(std::shared_ptr<const PatternData> v, SourceLocation l) : value(std::move(v)), loc(l) {}
    };

    struct SelfExpr
    {
        SourceLocation loc;
        explicit SelfExpr(SourceLocation l) : loc(l) {}
    };

    struct FunctionCall
    {
        std::string functionName;
        uint32_t functionNameId;
        std::vector<std::unique_ptr<Expr>> args;
        SourceLocation loc;
        bool inWith = false; // written inside a `with` block of the same function: the object's methods are looked at first
        FunctionCall(std::string n, std::vector<std::unique_ptr<Expr>> a, SourceLocation l)
            : functionName(std::move(n)), functionNameId(internName(functionName)), args(std::move(a)), loc(l) {}
    };

    // target.name. A null target is the leading-dot form `.name`, the member of the innermost `with` object.
    struct MemberAccess
    {
        std::unique_ptr<Expr> target;
        std::string name;
        uint32_t nameId;
        SourceLocation loc;
        MemberAccess(std::unique_ptr<Expr> t, std::string n, SourceLocation l)
            : target(std::move(t)), name(std::move(n)), nameId(internName(name)), loc(l) {}
    };

    // target.name(args), or `.name(args)` with a null target.
    struct MethodCall
    {
        std::unique_ptr<Expr> target;
        std::string name;
        uint32_t nameId;
        std::vector<std::unique_ptr<Expr>> args;
        SourceLocation loc;
        MethodCall(std::unique_ptr<Expr> t, std::string n, std::vector<std::unique_ptr<Expr>> a, SourceLocation l)
            : target(std::move(t)), name(std::move(n)), nameId(internName(name)), args(std::move(a)), loc(l) {}
    };

    struct SuperCall
    {
        std::string name;
        uint32_t nameId;
        std::vector<std::unique_ptr<Expr>> args;
        SourceLocation loc;
        SuperCall(std::string n, std::vector<std::unique_ptr<Expr>> a, SourceLocation l)
            : name(std::move(n)), nameId(internName(name)), args(std::move(a)), loc(l) {}
    };

    struct IsaExpr
    {
        std::unique_ptr<Expr> target;
        std::string className;
        uint32_t classNameId;
        SourceLocation loc;
        IsaExpr(std::unique_ptr<Expr> t, std::string c, SourceLocation l)
            : target(std::move(t)), className(std::move(c)), classNameId(internName(className)), loc(l) {}
    };

    // `await call`: the call is a FunctionCall, MethodCall or SuperCall expression.
    struct AwaitExpr
    {
        std::unique_ptr<Expr> call;
        SourceLocation loc;
        AwaitExpr(std::unique_ptr<Expr> c, SourceLocation l)
            : call(std::move(c)), loc(l) {}
    };

    enum class ExprKind
    {
        Number,
        String,
        Bool,
        Empty,
        Pattern,
        Identifier,
        Self,
        List,
        Map,
        FString,
        BinaryOp,
        UnaryOp,
        IndexAccess,
        SliceAccess,
        FunctionCall,
        Member,
        MethodCall,
        SuperCall,
        Isa,
        Await
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
            PatternLiteral,
            IdentifierExpr,
            SelfExpr,
            ListLiteral,
            MapLiteral,
            FStringExpr,
            BinaryOp,
            UnaryOp,
            IndexAccess,
            SliceAccess,
            FunctionCall,
            MemberAccess,
            MethodCall,
            SuperCall,
            IsaExpr,
            AwaitExpr>
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

        // A long chain such as 1 + 1 + 1 + ... is a tree as tall as the chain, and the default destructor would
        // recurse once per node, so a program the parser accepts could overflow a small stack just by being
        // freed. A tall tree is taken apart iteratively instead: each node's children are moved onto a work list
        // and released one at a time, so no node is destroyed while it still has children of its own. (Below
        // kShallowHeight the ordinary recursive destruction is bounded and cheaper.)
        ~Expr()
        {
            if (height <= kShallowHeight)
                return;
            std::vector<std::unique_ptr<Expr>> pending;
            std::visit(ChildTaker{pending}, data);
            while (!pending.empty())
            {
                std::unique_ptr<Expr> node = std::move(pending.back());
                pending.pop_back();
                if (node->height > kShallowHeight)
                {
                    std::visit(ChildTaker{pending}, node->data);
                    node->height = 1;
                }
            }
        }

    private:
        static constexpr uint32_t kShallowHeight = 64;

        // Moves every child expression of a node into `out`, leaving the node childless. Mirrors ChildHeight:
        // a node type that is added to the variant needs a line in both.
        struct ChildTaker
        {
            std::vector<std::unique_ptr<Expr>> &out;

            void take(std::unique_ptr<Expr> &e)
            {
                if (e)
                    out.push_back(std::move(e));
            }
            void takeAll(std::vector<std::unique_ptr<Expr>> &v)
            {
                for (auto &e : v)
                    take(e);
            }

            template <typename T>
            void operator()(T &) {}

            void operator()(ListLiteral &n) { takeAll(n.elements); }
            void operator()(MapLiteral &n)
            {
                for (auto &p : n.pairs)
                {
                    take(p.key);
                    take(p.value);
                }
            }
            void operator()(FStringExpr &n)
            {
                for (auto &s : n.segments)
                    take(s.expr);
            }
            void operator()(BinaryOp &n)
            {
                take(n.left);
                take(n.right);
            }
            void operator()(UnaryOp &n) { take(n.operand); }
            void operator()(IndexAccess &n)
            {
                take(n.target);
                take(n.index);
            }
            void operator()(SliceAccess &n)
            {
                take(n.target);
                take(n.start);
                take(n.end);
            }
            void operator()(FunctionCall &n) { takeAll(n.args); }
            void operator()(MemberAccess &n) { take(n.target); }
            void operator()(MethodCall &n)
            {
                take(n.target);
                takeAll(n.args);
            }
            void operator()(SuperCall &n) { takeAll(n.args); }
            void operator()(IsaExpr &n) { take(n.target); }
            void operator()(AwaitExpr &n) { take(n.call); }
        };

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
            uint32_t operator()(const MemberAccess &n) const { return h(n.target); }
            uint32_t operator()(const MethodCall &n) const { return std::max(h(n.target), args(n.args)); }
            uint32_t operator()(const SuperCall &n) const { return args(n.args); }
            uint32_t operator()(const IsaExpr &n) const { return h(n.target); }
            uint32_t operator()(const AwaitExpr &n) const { return h(n.call); }

            static uint32_t args(const std::vector<std::unique_ptr<Expr>> &v)
            {
                uint32_t m = 0;
                for (const auto &e : v)
                    m = std::max(m, h(e));
                return m;
            }
        };
    };

    enum class TypeKind : uint8_t
    {
        Any, // any value
        Empty,
        Number,
        Str,
        Boolean,
        List,
        Map,
        Match,
        Pattern,
        Object,
        Class
    };

    // A type as written in a declaration or parameter: a built-in type, or the name of a class.
    struct TypeSpec
    {
        TypeKind kind = TypeKind::Any;
        std::string className; // kind == Class
        uint32_t classNameId = 0;

        const char *builtinName() const
        {
            switch (kind)
            {
            case TypeKind::Empty: return "empty";
            case TypeKind::Number: return "number";
            case TypeKind::Str: return "str";
            case TypeKind::Boolean: return "boolean";
            case TypeKind::List: return "list";
            case TypeKind::Map: return "map";
            case TypeKind::Match: return "match";
            case TypeKind::Pattern: return "pattern";
            case TypeKind::Object: return "object";
            case TypeKind::Any: return "any";
            case TypeKind::Class:
                break;
            }
            return "";
        }
        std::string name() const { return kind == TypeKind::Class ? className : std::string(builtinName()); }
    };

    inline bool builtinTypeFromName(const std::string &word, TypeKind &out)
    {
        static const struct
        {
            const char *word;
            TypeKind kind;
        } table[] = {{"number", TypeKind::Number}, {"str", TypeKind::Str},     {"boolean", TypeKind::Boolean},
                     {"list", TypeKind::List},     {"map", TypeKind::Map},     {"match", TypeKind::Match},
                     {"pattern", TypeKind::Pattern}, {"object", TypeKind::Object}, {"empty", TypeKind::Empty},
                     {"any", TypeKind::Any}};
        for (const auto &e : table)
        {
            if (word == e.word)
            {
                out = e.kind;
                return true;
            }
        }
        return false;
    }

    struct DeclarationStmt
    {
        TypeSpec type;
        std::string name;
        uint32_t nameId = 0;
        bool isConstant = false;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
        SourceLocation typeLoc; // the type word, where an unknown class is reported
        SourceLocation nameLoc; // the variable's name, where a wrong value or a bad constant name is reported
    };

    // Where an assignment, `add` or `remove` writes: a variable, `self`, or a member of the innermost `with` object,
    // followed by any number of [index] and .field steps.
    struct PlaceStep
    {
        bool isIndex = false;
        std::unique_ptr<Expr> index; // isIndex
        std::string name;            // !isIndex
        uint32_t nameId = 0;
        SourceLocation loc;
    };

    struct Place
    {
        enum class Root
        {
            Name,
            Self,
            With
        };
        Root root = Root::Name;
        std::string name; // Root::Name, and the first member of Root::With
        uint32_t nameId = 0;
        bool inWith = false; // Root::Name inside a `with` block
        std::vector<PlaceStep> steps;
        SourceLocation loc;
    };

    struct AssignStmt
    {
        Place target;
        std::unique_ptr<Expr> value;
        SourceLocation loc;
    };

    struct GlobalStmt
    {
        struct Name
        {
            std::string name;
            uint32_t nameId = 0;
            SourceLocation loc;
        };
        std::vector<Name> names;
        SourceLocation loc;
    };

    struct FunctionDecl
    {
        bool isAsync = false;
        bool isPure = false;
        bool isPrivate = false; // methods only
        bool isMethod = false;
        std::string name;
        uint32_t nameId = 0;
        std::vector<std::string> params;
        std::vector<uint32_t> paramIds;
        std::vector<TypeSpec> paramTypes;
        std::vector<std::unique_ptr<Expr>> paramDefaults; // null where the parameter has no default
        size_t requiredParams = 0;
        bool hasTypedParams = false;
        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
        SourceRef source; // the text `loc` and everything in `body` point into
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
            While,
            Each
        };
        LoopKind kind;

        TypeSpec varType;      // the type declared for the loop variable of Repeat and Each
        SourceLocation typeLoc;
        std::string repeatVar; // the loop variable of Repeat and Each
        uint32_t repeatVarId = 0;
        std::unique_ptr<Expr> repeatStart;
        std::unique_ptr<Expr> repeatEnd;

        std::unique_ptr<Expr> collection; // Each

        std::unique_ptr<Expr> condition;

        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
    };

    struct WithStmt
    {
        std::unique_ptr<Expr> object;
        std::vector<std::unique_ptr<Stmt>> body;
        SourceLocation loc;
    };

    struct StopStmt
    {
        SourceLocation loc;
        explicit StopStmt(SourceLocation l) : loc(l) {}
    };

    struct SkipStmt
    {
        SourceLocation loc;
        explicit SkipStmt(SourceLocation l) : loc(l) {}
    };

    struct ReturnStmt
    {
        std::unique_ptr<Expr> value;
        SourceLocation loc;
        ReturnStmt(std::unique_ptr<Expr> v, SourceLocation l)
            : value(std::move(v)), loc(l) {}
    };

    struct FailStmt
    {
        std::unique_ptr<Expr> value;
        SourceLocation loc;
        FailStmt(std::unique_ptr<Expr> v, SourceLocation l)
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
            Remove
        };
        OpKind opKind;
        bool atPosition = false; // `remove at N from ...`: the value is a position, not an element or a key
        std::unique_ptr<Expr> value;
        Place target;
        SourceLocation loc;
    };

    struct OrElseStmt
    {
        std::unique_ptr<Stmt> primaryStmt;
        std::vector<std::unique_ptr<Stmt>> fallbackBody;
        TypeSpec errorType;    // `or_else as TYPE NAME`
        std::string errorName; // empty when the handler takes no error
        uint32_t errorNameId = 0;
        SourceLocation loc;
        SourceLocation typeLoc;
    };

    struct FieldDecl
    {
        TypeSpec type;
        std::string name;
        uint32_t nameId = 0;
        bool isConstant = false;
        bool isPrivate = false;
        std::unique_ptr<Expr> defaultValue;
        SourceLocation loc;
    };

    struct ClassDecl
    {
        std::string name;
        uint32_t nameId = 0;
        std::string parentName; // empty without `extends`
        uint32_t parentNameId = 0;
        SourceLocation parentLoc;
        std::vector<FieldDecl> fields;
        std::vector<std::unique_ptr<Stmt>> methods; // FunctionDecl statements
        SourceLocation loc;
        SourceRef source;
    };

    enum class StmtKind
    {
        Declaration,
        Assign,
        Global,
        FunctionDecl,
        ClassDecl,
        IfStmt,
        LoopStmt,
        WithStmt,
        StopStmt,
        SkipStmt,
        ReturnStmt,
        FailStmt,
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
            AssignStmt,
            GlobalStmt,
            FunctionDecl,
            ClassDecl,
            IfStmt,
            LoopStmt,
            WithStmt,
            StopStmt,
            SkipStmt,
            ReturnStmt,
            FailStmt,
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
        SourceRef source;
    };

}
