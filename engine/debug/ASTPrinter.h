#pragma once

#include "../parser/ASTNodes.h"
#include "../common/TokenTypes.h"
#include <string>
#include <sstream>
#include <variant>

namespace cuff
{

    class ASTPrinter
    {
    public:
        static std::string printTokens(const std::vector<Token> &tokens)
        {
            std::ostringstream os;
            for (const auto &t : tokens)
            {
                os << "[" << tokenTypeName(t.type) << "]";
                if (!t.value.empty() && t.type != TokenType::NEWLINE && t.type != TokenType::EOF_TOKEN && t.type != TokenType::INDENT && t.type != TokenType::DEDENT)
                {
                    os << " \"" << t.value << "\"";
                }
                if (t.type == TokenType::PATTERN && t.patternFlags)
                    os << " flags=" << ((t.patternFlags & kPatternFlagIgnoreCase) ? "i" : "")
                       << ((t.patternFlags & kPatternFlagMultiline) ? "m" : "");
                os << "  @" << t.location.toString();
                if (t.hasSpaceBefore)
                    os << " <sp>";
                if (t.type == TokenType::INDENT)
                    os << " (" << t.indentLevel << ")";
                os << "\n";
            }
            return os.str();
        }

        static std::string print(const Program &program)
        {
            std::ostringstream os;
            for (const auto &stmt : program.statements)
            {
                printStmt(os, *stmt, 0);
            }
            return os.str();
        }

    private:
        static void indent(std::ostringstream &os, int depth)
        {
            for (int i = 0; i < depth; ++i)
                os << "  ";
        }

        static void printTypeSpec(std::ostringstream &os, const TypeSpec &t)
        {
            os << t.name();
        }

        static void printPlace(std::ostringstream &os, const Place &place, int depth)
        {
            switch (place.root)
            {
            case Place::Root::Self:
                os << "self";
                break;
            case Place::Root::With:
                os << "." << place.name;
                break;
            case Place::Root::Name:
                os << place.name;
                break;
            }
            for (const auto &step : place.steps)
            {
                if (step.isIndex)
                    os << "[...]";
                else
                    os << "." << step.name;
            }
            os << "\n";
            for (const auto &step : place.steps)
            {
                if (!step.isIndex)
                    continue;
                indent(os, depth + 1);
                os << "index:\n";
                printExpr(os, *step.index, depth + 2);
            }
        }

        static void printBody(std::ostringstream &os, const std::vector<std::unique_ptr<Stmt>> &body, int depth)
        {
            for (const auto &s : body)
                printStmt(os, *s, depth);
        }

        static void printFunction(std::ostringstream &os, const FunctionDecl &f, int depth)
        {
            std::string mods;
            if (f.isPrivate)
                mods += "private ";
            if (f.isAsync)
                mods += "async ";
            if (f.isPure)
                mods += "pure ";
            os << (f.isMethod ? "Method [" : "FunctionDecl [") << (mods.empty() ? "normal" : mods.substr(0, mods.size() - 1))
               << "] " << f.name << "(";
            for (size_t i = 0; i < f.params.size(); ++i)
            {
                if (i > 0)
                    os << ", ";
                if (i < f.paramTypes.size() && f.paramTypes[i].kind != TypeKind::Any)
                    os << f.paramTypes[i].name() << " ";
                os << f.params[i];
                if (i < f.paramDefaults.size() && f.paramDefaults[i])
                    os << " to ...";
            }
            os << ")\n";
            for (size_t i = 0; i < f.paramDefaults.size(); ++i)
            {
                if (!f.paramDefaults[i])
                    continue;
                indent(os, depth + 1);
                os << "default of " << f.params[i] << ":\n";
                printExpr(os, *f.paramDefaults[i], depth + 2);
            }
            printBody(os, f.body, depth + 1);
        }

        static void printStmt(std::ostringstream &os, const Stmt &stmt, int depth)
        {
            indent(os, depth);
            switch (stmt.kind)
            {
            case StmtKind::Declaration:
            {
                const auto &d = std::get<DeclarationStmt>(stmt.data);
                os << "Declaration [";
                printTypeSpec(os, d.type);
                os << "]";
                if (d.isConstant)
                    os << " (const)";
                os << " " << d.name << " to\n";
                printExpr(os, *d.value, depth + 1);
                break;
            }
            case StmtKind::Change:
            {
                const auto &c = std::get<ChangeStmt>(stmt.data);
                os << "Change ";
                printPlace(os, c.target, depth);
                indent(os, depth + 1);
                os << "to:\n";
                printExpr(os, *c.value, depth + 2);
                break;
            }
            case StmtKind::Global:
            {
                const auto &g = std::get<GlobalStmt>(stmt.data);
                os << "Global";
                for (size_t i = 0; i < g.names.size(); ++i)
                    os << (i ? ", " : " ") << g.names[i].name;
                os << "\n";
                break;
            }
            case StmtKind::FunctionDecl:
                printFunction(os, std::get<FunctionDecl>(stmt.data), depth);
                break;
            case StmtKind::ClassDecl:
            {
                const auto &c = std::get<ClassDecl>(stmt.data);
                os << "Class " << c.name;
                if (!c.parentName.empty())
                    os << " extends " << c.parentName;
                os << "\n";
                for (const auto &f : c.fields)
                {
                    indent(os, depth + 1);
                    os << "Field [";
                    printTypeSpec(os, f.type);
                    os << "]" << (f.isPrivate ? " (private)" : "") << (f.isConstant ? " (const)" : "") << " " << f.name
                       << " to\n";
                    printExpr(os, *f.defaultValue, depth + 2);
                }
                for (const auto &m : c.methods)
                    printStmt(os, *m, depth + 1);
                break;
            }
            case StmtKind::IfStmt:
            {
                const auto &ifst = std::get<IfStmt>(stmt.data);
                os << "If\n";
                for (size_t i = 0; i < ifst.branches.size(); ++i)
                {
                    const auto &br = ifst.branches[i];
                    indent(os, depth + 1);
                    if (i == 0)
                        os << "if";
                    else if (br.condition)
                        os << "else if";
                    else
                        os << "else";
                    if (br.condition)
                    {
                        os << " condition:\n";
                        printExpr(os, *br.condition, depth + 2);
                        indent(os, depth + 1);
                        os << "body:\n";
                    }
                    else
                    {
                        os << " body:\n";
                    }
                    printBody(os, br.body, depth + 2);
                }
                break;
            }
            case StmtKind::LoopStmt:
            {
                const auto &loop = std::get<LoopStmt>(stmt.data);
                const char *kindStr = loop.kind == LoopStmt::LoopKind::While  ? "while"
                                      : loop.kind == LoopStmt::LoopKind::Each ? "each"
                                                                              : "repeat";
                os << "Loop [" << kindStr << "]\n";
                if (loop.kind == LoopStmt::LoopKind::Repeat)
                {
                    indent(os, depth + 1);
                    os << "var: " << loop.repeatVar << "\n";
                    indent(os, depth + 1);
                    os << "start:\n";
                    printExpr(os, *loop.repeatStart, depth + 2);
                    indent(os, depth + 1);
                    os << "end:\n";
                    printExpr(os, *loop.repeatEnd, depth + 2);
                }
                else if (loop.kind == LoopStmt::LoopKind::Each)
                {
                    indent(os, depth + 1);
                    os << "var: " << loop.repeatVar << "\n";
                    indent(os, depth + 1);
                    os << "in:\n";
                    printExpr(os, *loop.collection, depth + 2);
                }
                else
                {
                    indent(os, depth + 1);
                    os << "condition:\n";
                    printExpr(os, *loop.condition, depth + 2);
                }
                indent(os, depth + 1);
                os << "body:\n";
                printBody(os, loop.body, depth + 2);
                break;
            }
            case StmtKind::WithStmt:
            {
                const auto &w = std::get<WithStmt>(stmt.data);
                os << "With\n";
                indent(os, depth + 1);
                os << "object:\n";
                printExpr(os, *w.object, depth + 2);
                indent(os, depth + 1);
                os << "body:\n";
                printBody(os, w.body, depth + 2);
                break;
            }
            case StmtKind::StopStmt:
                os << "Stop\n";
                break;
            case StmtKind::SkipStmt:
                os << "Skip\n";
                break;
            case StmtKind::ReturnStmt:
            {
                const auto &ret = std::get<ReturnStmt>(stmt.data);
                os << "Return\n";
                if (ret.value)
                    printExpr(os, *ret.value, depth + 1);
                break;
            }
            case StmtKind::FailStmt:
            {
                const auto &f = std::get<FailStmt>(stmt.data);
                os << "Fail\n";
                printExpr(os, *f.value, depth + 1);
                break;
            }
            case StmtKind::AwaitStmt:
            {
                const auto &aw = std::get<AwaitStmt>(stmt.data);
                os << "Await\n";
                printExpr(os, *aw.expr->call, depth + 1);
                break;
            }
            case StmtKind::UseStmt:
            {
                const auto &use = std::get<UseStmt>(stmt.data);
                if (use.isDLC)
                {
                    os << "Use";
                    for (size_t i = 0; i < use.dlcs.size(); ++i)
                        os << (i ? ", DLC:" : " DLC:") << use.dlcs[i].name;
                    os << "\n";
                }
                else
                    os << "Use " << use.name << " from " << use.path << "\n";
                break;
            }
            case StmtKind::ExprStmt:
            {
                const auto &es = std::get<ExprStmt>(stmt.data);
                os << "ExprStmt\n";
                printExpr(os, *es.expr, depth + 1);
                break;
            }
            case StmtKind::CollectionOp:
            {
                const auto &co = std::get<CollectionOpStmt>(stmt.data);
                os << "CollectionOp [" << (co.opKind == CollectionOpStmt::OpKind::Add ? "Add" : "Remove") << "] ";
                printPlace(os, co.target, depth);
                printExpr(os, *co.value, depth + 1);
                break;
            }
            case StmtKind::OrElse:
            {
                const auto &oe = std::get<OrElseStmt>(stmt.data);
                os << "OrElse";
                if (!oe.errorName.empty())
                    os << " as " << oe.errorName;
                os << "\n";
                indent(os, depth + 1);
                os << "primary:\n";
                if (oe.primaryStmt)
                    printStmt(os, *oe.primaryStmt, depth + 2);
                indent(os, depth + 1);
                os << "fallback:\n";
                printBody(os, oe.fallbackBody, depth + 2);
                break;
            }
            }
        }

        static void printExpr(std::ostringstream &os, const Expr &expr, int depth)
        {
            indent(os, depth);
            switch (expr.kind)
            {
            case ExprKind::Number:
            {
                const auto &n = std::get<NumberLiteral>(expr.data);
                os << "Number(" << n.value << ")\n";
                break;
            }
            case ExprKind::String:
            {
                const auto &s = std::get<StringLiteral>(expr.data);
                os << "String(\"" << s.value << "\")\n";
                break;
            }
            case ExprKind::Bool:
            {
                const auto &b = std::get<BoolLiteral>(expr.data);
                os << "Bool(" << (b.value ? "true" : "false") << ")\n";
                break;
            }
            case ExprKind::Empty:
                os << "Empty\n";
                break;
            case ExprKind::Pattern:
            {
                const auto &pat = std::get<PatternLiteral>(expr.data);
                os << "Pattern(" << pat.value->display() << ")\n";
                break;
            }
            case ExprKind::Self:
                os << "Self\n";
                break;
            case ExprKind::Identifier:
            {
                const auto &id = std::get<IdentifierExpr>(expr.data);
                os << "Identifier(" << id.name << ")\n";
                break;
            }
            case ExprKind::List:
            {
                const auto &list = std::get<ListLiteral>(expr.data);
                os << "List\n";
                for (const auto &e : list.elements)
                    printExpr(os, *e, depth + 1);
                break;
            }
            case ExprKind::Map:
            {
                const auto &map = std::get<MapLiteral>(expr.data);
                os << "Map\n";
                for (const auto &pair : map.pairs)
                {
                    indent(os, depth + 1);
                    os << "key:\n";
                    printExpr(os, *pair.key, depth + 2);
                    indent(os, depth + 1);
                    os << "value:\n";
                    printExpr(os, *pair.value, depth + 2);
                }
                break;
            }
            case ExprKind::FString:
            {
                const auto &fs = std::get<FStringExpr>(expr.data);
                os << "FString\n";
                for (const auto &seg : fs.segments)
                {
                    indent(os, depth + 1);
                    if (seg.isExpression)
                    {
                        os << "expr:\n";
                        printExpr(os, *seg.expr, depth + 2);
                    }
                    else
                    {
                        os << "text: \"" << seg.text << "\"\n";
                    }
                }
                break;
            }
            case ExprKind::BinaryOp:
            {
                const auto &b = std::get<BinaryOp>(expr.data);
                os << "BinaryOp(" << binOpName(b.op) << ")\n";
                printExpr(os, *b.left, depth + 1);
                printExpr(os, *b.right, depth + 1);
                break;
            }
            case ExprKind::UnaryOp:
            {
                const auto &u = std::get<UnaryOp>(expr.data);
                os << "UnaryOp(" << unOpName(u.op) << ")\n";
                printExpr(os, *u.operand, depth + 1);
                break;
            }
            case ExprKind::IndexAccess:
            {
                const auto &idx = std::get<IndexAccess>(expr.data);
                os << "IndexAccess\n";
                indent(os, depth + 1);
                os << "target:\n";
                printExpr(os, *idx.target, depth + 2);
                indent(os, depth + 1);
                os << "index:\n";
                printExpr(os, *idx.index, depth + 2);
                break;
            }
            case ExprKind::SliceAccess:
            {
                const auto &sl = std::get<SliceAccess>(expr.data);
                os << "SliceAccess\n";
                indent(os, depth + 1);
                os << "target:\n";
                printExpr(os, *sl.target, depth + 2);
                indent(os, depth + 1);
                os << "start:\n";
                printExpr(os, *sl.start, depth + 2);
                indent(os, depth + 1);
                os << "end:\n";
                printExpr(os, *sl.end, depth + 2);
                break;
            }
            case ExprKind::FunctionCall:
            {
                const auto &fc = std::get<FunctionCall>(expr.data);
                os << "FunctionCall(" << fc.functionName << ")\n";
                for (const auto &arg : fc.args)
                    printExpr(os, *arg, depth + 1);
                break;
            }
            case ExprKind::Member:
            {
                const auto &m = std::get<MemberAccess>(expr.data);
                os << "Member(" << m.name << ")\n";
                if (m.target)
                    printExpr(os, *m.target, depth + 1);
                else
                {
                    indent(os, depth + 1);
                    os << "(with object)\n";
                }
                break;
            }
            case ExprKind::MethodCall:
            {
                const auto &m = std::get<MethodCall>(expr.data);
                os << "MethodCall(" << m.name << ")\n";
                if (m.target)
                {
                    indent(os, depth + 1);
                    os << "target:\n";
                    printExpr(os, *m.target, depth + 2);
                }
                else
                {
                    indent(os, depth + 1);
                    os << "target: (with object)\n";
                }
                for (const auto &arg : m.args)
                    printExpr(os, *arg, depth + 1);
                break;
            }
            case ExprKind::SuperCall:
            {
                const auto &sc = std::get<SuperCall>(expr.data);
                os << "SuperCall(" << sc.name << ")\n";
                for (const auto &arg : sc.args)
                    printExpr(os, *arg, depth + 1);
                break;
            }
            case ExprKind::Isa:
            {
                const auto &isa = std::get<IsaExpr>(expr.data);
                os << "Isa(" << isa.className << ")\n";
                printExpr(os, *isa.target, depth + 1);
                break;
            }
            case ExprKind::Await:
            {
                const auto &aw = std::get<AwaitExpr>(expr.data);
                os << "Await\n";
                printExpr(os, *aw.call, depth + 1);
                break;
            }
            }
        }
    };

}
