#pragma once

#include "parser/ASTNodes.h"
#include "common/TokenTypes.h"
#include <string>
#include <sstream>
#include <variant>

namespace cuff {

// Pretty-prints an AST tree and token streams for debugging and verification.
class ASTPrinter {
public:
    static std::string printTokens(const std::vector<Token>& tokens) {
        std::ostringstream os;
        for (const auto& t : tokens) {
            os << "[" << tokenTypeName(t.type) << "]";
            if (!t.value.empty()
                && t.type != TokenType::NEWLINE
                && t.type != TokenType::EOF_TOKEN
                && t.type != TokenType::INDENT
                && t.type != TokenType::DEDENT) {
                os << " \"" << t.value << "\"";
            }
            os << "  @" << t.location.toString();
            if (t.hasSpaceBefore) os << " <sp>";
            if (t.type == TokenType::INDENT) os << " (" << t.indentLevel << ")";
            os << "\n";
        }
        return os.str();
    }

    static std::string print(const Program& program) {
        std::ostringstream os;
        for (const auto& stmt : program.statements) {
            printStmt(os, *stmt, 0);
        }
        return os.str();
    }

private:
    static void indent(std::ostringstream& os, int depth) {
        for (int i = 0; i < depth; ++i) os << "  ";
    }

    static void printStmt(std::ostringstream& os, const Stmt& stmt, int depth) {
        indent(os, depth);
        switch (stmt.kind) {
        case StmtKind::Declaration: {
            const auto& d = std::get<DeclarationStmt>(stmt.data);
            os << "Declaration [" << d.varType << "]";
            if (d.isConstant) os << " (const)";
            os << " " << d.name << " to\n";
            printExpr(os, *d.value, depth + 1);
            break;
        }
        case StmtKind::Change: {
            const auto& c = std::get<ChangeStmt>(stmt.data);
            os << "Change " << c.name << " to\n";
            printExpr(os, *c.value, depth + 1);
            break;
        }
        case StmtKind::FunctionDecl: {
            const auto& f = std::get<FunctionDecl>(stmt.data);
            const char* kindStr = "normal";
            if (f.funcKind == FunctionDecl::FuncKind::Returnable) kindStr = "returnable";
            else if (f.funcKind == FunctionDecl::FuncKind::Async) kindStr = "async";
            os << "FunctionDecl [" << kindStr << "] " << f.name << "(";
            for (size_t i = 0; i < f.params.size(); ++i) {
                if (i > 0) os << ", ";
                os << f.params[i];
            }
            os << ")\n";
            for (const auto& s : f.body) printStmt(os, *s, depth + 1);
            break;
        }
        case StmtKind::IfStmt: {
            const auto& ifst = std::get<IfStmt>(stmt.data);
            os << "If\n";
            for (size_t i = 0; i < ifst.branches.size(); ++i) {
                const auto& br = ifst.branches[i];
                indent(os, depth + 1);
                if (i == 0) os << "if";
                else if (br.condition) os << "else if";
                else os << "else";
                if (br.condition) {
                    os << " condition:\n";
                    printExpr(os, *br.condition, depth + 2);
                    indent(os, depth + 1);
                    os << "body:\n";
                } else {
                    os << " body:\n";
                }
                for (const auto& s : br.body) printStmt(os, *s, depth + 2);
            }
            break;
        }
        case StmtKind::LoopStmt: {
            const auto& loop = std::get<LoopStmt>(stmt.data);
            const char* kindStr = "repeat";
            if (loop.kind == LoopStmt::LoopKind::While) kindStr = "while";
            else if (loop.kind == LoopStmt::LoopKind::Match) kindStr = "match";
            os << "Loop [" << kindStr << "]\n";
            if (loop.kind == LoopStmt::LoopKind::Repeat) {
                indent(os, depth + 1); os << "var: " << loop.repeatVar << "\n";
                indent(os, depth + 1); os << "start:\n";
                printExpr(os, *loop.repeatStart, depth + 2);
                indent(os, depth + 1); os << "end:\n";
                printExpr(os, *loop.repeatEnd, depth + 2);
            } else {
                indent(os, depth + 1); os << "condition:\n";
                printExpr(os, *loop.condition, depth + 2);
            }
            indent(os, depth + 1); os << "body:\n";
            for (const auto& s : loop.body) printStmt(os, *s, depth + 2);
            break;
        }
        case StmtKind::StopStmt:
            os << "Stop\n";
            break;
        case StmtKind::ReturnStmt: {
            const auto& ret = std::get<ReturnStmt>(stmt.data);
            os << "Return\n";
            if (ret.value) printExpr(os, *ret.value, depth + 1);
            break;
        }
        case StmtKind::AwaitStmt: {
            const auto& aw = std::get<AwaitStmt>(stmt.data);
            os << "Await " << aw.expr->call->functionName << "(...)\n";
            for (const auto& arg : aw.expr->call->args) printExpr(os, *arg, depth + 1);
            break;
        }
        case StmtKind::UseStmt: {
            const auto& use = std::get<UseStmt>(stmt.data);
            if (use.isDLC) os << "Use DLC:" << use.name << "\n";
            else os << "Use " << use.name << " from " << use.path << "\n";
            break;
        }
        case StmtKind::ExprStmt: {
            const auto& es = std::get<ExprStmt>(stmt.data);
            os << "ExprStmt\n";
            printExpr(os, *es.expr, depth + 1);
            break;
        }
        case StmtKind::CollectionOp: {
            const auto& co = std::get<CollectionOpStmt>(stmt.data);
            const char* opStr = "Add";
            if (co.opKind == CollectionOpStmt::OpKind::Remove) opStr = "Remove";
            else if (co.opKind == CollectionOpStmt::OpKind::Replace) opStr = "Replace";
            os << "CollectionOp [" << opStr << "] " << co.collectionName << "\n";
            if (co.opKind == CollectionOpStmt::OpKind::Add && co.addValue)
                printExpr(os, *co.addValue, depth + 1);
            if (co.opKind == CollectionOpStmt::OpKind::Replace) {
                if (co.indexOrKey) { indent(os, depth+1); os << "index/key:\n"; printExpr(os, *co.indexOrKey, depth+2); }
                if (co.newValue) { indent(os, depth+1); os << "new value:\n"; printExpr(os, *co.newValue, depth+2); }
            }
            if (co.opKind == CollectionOpStmt::OpKind::Remove && co.removeValue)
                printExpr(os, *co.removeValue, depth + 1);
            break;
        }
        case StmtKind::OrElse: {
            const auto& oe = std::get<OrElseStmt>(stmt.data);
            os << "OrElse\n";
            indent(os, depth + 1); os << "primary:\n";
            if (oe.primaryStmt) printStmt(os, *oe.primaryStmt, depth + 2);
            indent(os, depth + 1); os << "fallback:\n";
            for (const auto& s : oe.fallbackBody) printStmt(os, *s, depth + 2);
            break;
        }
        }
    }

    static void printExpr(std::ostringstream& os, const Expr& expr, int depth) {
        indent(os, depth);
        switch (expr.kind) {
        case ExprKind::Number: {
            const auto& n = std::get<NumberLiteral>(expr.data);
            os << "Number(" << n.value << ")\n";
            break;
        }
        case ExprKind::String: {
            const auto& s = std::get<StringLiteral>(expr.data);
            os << "String(\"" << s.value << "\")\n";
            break;
        }
        case ExprKind::Bool: {
            const auto& b = std::get<BoolLiteral>(expr.data);
            os << "Bool(" << (b.value ? "true" : "false") << ")\n";
            break;
        }
        case ExprKind::Empty:
            os << "Empty\n";
            break;
        case ExprKind::Identifier: {
            const auto& id = std::get<IdentifierExpr>(expr.data);
            os << "Identifier(" << id.name << ")\n";
            break;
        }
        case ExprKind::List: {
            const auto& list = std::get<ListLiteral>(expr.data);
            os << "List\n";
            for (const auto& e : list.elements) printExpr(os, *e, depth + 1);
            break;
        }
        case ExprKind::Map: {
            const auto& map = std::get<MapLiteral>(expr.data);
            os << "Map\n";
            for (const auto& pair : map.pairs) {
                indent(os, depth + 1); os << "key:\n";
                printExpr(os, *pair.key, depth + 2);
                indent(os, depth + 1); os << "value:\n";
                printExpr(os, *pair.value, depth + 2);
            }
            break;
        }
        case ExprKind::FString: {
            const auto& fs = std::get<FStringExpr>(expr.data);
            os << "FString\n";
            for (const auto& seg : fs.segments) {
                indent(os, depth + 1);
                if (seg.isExpression) {
                    os << "expr:\n";
                    printExpr(os, *seg.expr, depth + 2);
                } else {
                    os << "text: \"" << seg.text << "\"\n";
                }
            }
            break;
        }
        case ExprKind::BinaryOp: {
            const auto& b = std::get<BinaryOp>(expr.data);
            os << "BinaryOp(" << b.op << ")\n";
            printExpr(os, *b.left, depth + 1);
            printExpr(os, *b.right, depth + 1);
            break;
        }
        case ExprKind::UnaryOp: {
            const auto& u = std::get<UnaryOp>(expr.data);
            os << "UnaryOp(" << u.op << ")\n";
            printExpr(os, *u.operand, depth + 1);
            break;
        }
        case ExprKind::IndexAccess: {
            const auto& idx = std::get<IndexAccess>(expr.data);
            os << "IndexAccess\n";
            indent(os, depth + 1); os << "target:\n"; printExpr(os, *idx.target, depth + 2);
            indent(os, depth + 1); os << "index:\n";  printExpr(os, *idx.index, depth + 2);
            break;
        }
        case ExprKind::SliceAccess: {
            const auto& sl = std::get<SliceAccess>(expr.data);
            os << "SliceAccess\n";
            indent(os, depth + 1); os << "target:\n"; printExpr(os, *sl.target, depth + 2);
            indent(os, depth + 1); os << "start:\n";  printExpr(os, *sl.start, depth + 2);
            indent(os, depth + 1); os << "end:\n";    printExpr(os, *sl.end, depth + 2);
            break;
        }
        case ExprKind::FunctionCall: {
            const auto& fc = std::get<FunctionCall>(expr.data);
            os << "FunctionCall(" << fc.functionName << ")\n";
            for (const auto& arg : fc.args) printExpr(os, *arg, depth + 1);
            break;
        }
        case ExprKind::Await: {
            const auto& aw = std::get<AwaitExpr>(expr.data);
            os << "Await(" << aw.call->functionName << ")\n";
            for (const auto& arg : aw.call->args) printExpr(os, *arg, depth + 1);
            break;
        }
        case ExprKind::RegexMatch: {
            const auto& rm = std::get<RegexMatchExpr>(expr.data);
            os << "RegexMatch(" << (rm.caseInsensitive ? "IS" : "is") << ")\n";
            indent(os, depth + 1); os << "target:\n"; printExpr(os, *rm.target, depth + 2);
            indent(os, depth + 1); os << "pattern: \"" << rm.pattern << "\"\n";
            break;
        }
        }
    }
};

} // namespace cuff
