#pragma once

#include "../bytecode/Bytecode.h"
#include "../parser/ASTNodes.h"
#include <locale>
#include <sstream>
#include <string>

namespace cuff
{

    // Writes compiled code out the way docs/internals/bytecode.md describes it: one line for each instruction, with
    // its operands named by what they are. `cuffc --bytecode` prints every piece of code the compiler makes with it.
    class BytecodePrinter
    {
    public:
        static std::string print(const bc::Proto &p)
        {
            std::ostringstream os;
            os.imbue(std::locale::classic());
            header(os, p);
            for (size_t pc = 0; pc < p.code.size(); ++pc)
                instruction(os, p, pc);
            for (const bc::Handler &h : p.handlers)
            {
                os << "  or_else: an error in " << h.start << ".." << (h.end - 1) << " continues at " << h.target;
                if (h.errCell != bc::kNoCell)
                    os << ", the Error object in r" << h.errCell / bc::kCellBytes;
                os << "\n";
            }
            return os.str();
        }

        static const char *name(bc::Op op)
        {
            static const char *const names[] = {
#define CUFF_OP_NAME(name, operands) #name,
                CUFF_OPCODES(CUFF_OP_NAME)
#undef CUFF_OP_NAME
            };
            return op < bc::Op::Count ? names[static_cast<size_t>(op)] : "?";
        }

        // Three letters for the operands a, b and c, as CUFF_OPCODES gives them.
        static const char *operands(bc::Op op)
        {
            static const char *const kinds[] = {
#define CUFF_OP_OPERANDS(name, operands) operands,
                CUFF_OPCODES(CUFF_OP_OPERANDS)
#undef CUFF_OP_OPERANDS
            };
            return op < bc::Op::Count ? kinds[static_cast<size_t>(op)] : "---";
        }

    private:
        static void header(std::ostringstream &os, const bc::Proto &p)
        {
            os << "== ";
            switch (p.kind)
            {
            case bc::Proto::Kind::Function:
                os << (p.isPure ? "pure " : "") << (p.isAsync ? "async " : "") << (p.isMethod ? "method " : "function ")
                   << (p.decl ? p.decl->name : std::string("?"));
                break;
            case bc::Proto::Kind::TopLevel:
                os << "statement";
                break;
            case bc::Proto::Kind::Thunk:
                os << "default value";
                break;
            }
            os << ", line " << p.line;
            if (p.source && !p.source->name.empty())
                os << " of " << p.source->name;
            os << ": " << p.numCells << " registers";
            if (p.kind == bc::Proto::Kind::Function)
                os << ", " << p.nparams << " parameters";
            os << " ==\n";
        }

        static std::string constant(const Value &v)
        {
            std::string text = v.toDisplayString();
            if (v.isStr())
            {
                if (text.size() > 24)
                    text = text.substr(0, 21) + "...";
                for (char &c : text)
                    if (c == '\n' || c == '\r' || c == '\t')
                        c = ' ';
                return "\"" + text + "\"";
            }
            return text;
        }

        static std::string meta(int32_t m)
        {
            const uint32_t bits = static_cast<uint32_t>(m);
            if (bits == 0)
                return "undeclared";
            std::string out = (bits & bc::kMetaConst) ? "constant " : "";
            const uint32_t type = bits >> bc::kMetaTypeShift;
            if (type >= bc::kClassTypeBase)
                out += "class#" + std::to_string(type - bc::kClassTypeBase);
            else
            {
                TypeSpec spec;
                spec.kind = static_cast<TypeKind>(type);
                out += spec.builtinName();
            }
            if (bits & bc::kMetaLoose)
                out += " (holds empty)";
            return out;
        }

        static void operand(std::ostringstream &os, const bc::Proto &p, const bc::Instr &ins, size_t pc, char kind, int32_t v)
        {
            switch (kind)
            {
            case 'r':
                if (v == bc::kNoCell)
                    os << " -";
                else
                    os << " r" << v / bc::kCellBytes;
                break;
            case 'k':
            {
                const size_t index = static_cast<size_t>(v / bc::kConstBytes);
                os << " " << (index < p.consts.size() ? constant(p.consts[index]) : std::string("k?"));
                break;
            }
            case 'g':
                os << " g" << v;
                break;
            case 'j':
                os << " ->" << static_cast<long long>(pc) + 1 + v;
                break;
            case 'n':
                os << " '" << (static_cast<size_t>(v) < p.names.size() ? *p.names[static_cast<size_t>(v)].name : std::string("?")) << "'";
                break;
            case 'm':
                if (v != 0)
                    os << " [" << meta(v) << "]";
                break;
            case 'c':
                os << " #" << v;
                break;
            case 'f':
                break; // the name is written after the instruction
            case 't':
                os << " [" << meta(static_cast<int32_t>(bc::makeMeta(static_cast<uint32_t>(v), false))) << "]";
                break;
            case 'o':
                switch (static_cast<bc::RootKind>(ins.x & bc::kRootMask))
                {
                case bc::RootKind::Self:
                    os << " self";
                    break;
                case bc::RootKind::WithObj:
                case bc::RootKind::Local:
                    os << " r" << v / bc::kCellBytes;
                    break;
                case bc::RootKind::Global:
                    os << " g" << v;
                    break;
                case bc::RootKind::Name:
                    os << " '" << (static_cast<size_t>(v) < p.names.size() ? *p.names[static_cast<size_t>(v)].name : std::string("?")) << "'";
                    break;
                }
                break;
            case 'i':
                os << " " << v;
                break;
            default:
                break;
            }
        }

        static std::string kind(unsigned k)
        {
            TypeSpec spec;
            spec.kind = static_cast<TypeKind>(k);
            return spec.kind == TypeKind::Class ? std::string("a class") : spec.builtinName();
        }

        // What `x` says: flags for most instructions, a type to check for a few, the kind of error for Raise.
        static void flags(std::ostringstream &os, const bc::Instr &ins)
        {
            const bc::Op op = static_cast<bc::Op>(ins.op);
            switch (op)
            {
            case bc::Op::JmpIf:
            case bc::Op::JLt:
            case bc::Op::JLe:
            case bc::Op::JGt:
            case bc::Op::JGe:
            case bc::Op::JEq:
            case bc::Op::JLtK:
            case bc::Op::JLeK:
            case bc::Op::JGtK:
            case bc::Op::JGeK:
            case bc::Op::JEqK:
                os << ((ins.x & bc::kJumpIfTrue) ? "  if true" : "  if false");
                if (ins.x & bc::kJumpTicks)
                    os << ", counts a pass";
                return;
            case bc::Op::Call:
            case bc::Op::CallW:
            case bc::Op::CallM:
            case bc::Op::CallS:
                if (ins.x & bc::kCallAwaited)
                    os << "  awaited";
                return;
            case bc::Op::LoadBool:
                os << (ins.x ? " true" : " false");
                return;
            case bc::Op::ChkDecl:
            case bc::Op::ChkLoop:
            case bc::Op::DefChk:
            case bc::Op::EachNext:
                if (ins.x)
                    os << "  must be " << kind(ins.x);
                return;
            case bc::Op::Store:
                if (ins.x & bc::kKindMask)
                    os << "  must be " << kind(ins.x & bc::kKindMask);
                break;
            case bc::Op::Raise:
            {
                static const char *const errors[] = {"a constant's name", "assignment to a constant", "add or remove on a constant",
                                                     "stop outside a loop", "skip outside a loop", "return outside a function",
                                                     "a pure function assigns to a field", "a pure function changes a field's collection",
                                                     "a loop variable that is a constant"};
                os << "  " << (ins.x < sizeof errors / sizeof *errors ? errors[ins.x] : "?");
                return;
            }
            case bc::Op::PlaceChk:
            case bc::Op::PlaceSet:
            case bc::Op::CollOp:
                if (ins.x & bc::kMoveSource)
                    os << "  takes";
                return;
            default:
                break;
            }
            if (ins.x & bc::kMoveSource)
                os << "  takes";
        }

        // The name an instruction is about, where its syntax tree node has one.
        static void about(std::ostringstream &os, const bc::Proto &p, const bc::Instr &ins, size_t pc)
        {
            const void *node = pc < p.nodes.size() ? p.nodes[pc] : nullptr;
            if (!node)
                return;
            switch (static_cast<bc::Op>(ins.op))
            {
            case bc::Op::GetG:
            case bc::Op::GetGC:
                os << "  ; " << static_cast<const IdentifierExpr *>(node)->name;
                break;
            case bc::Op::SetG:
            case bc::Op::SetGU:
            case bc::Op::CheckG:
            case bc::Op::Store:
                os << "  ; " << static_cast<const AssignStmt *>(node)->target.name;
                break;
            case bc::Op::GetField:
                os << "  ; ." << static_cast<const MemberAccess *>(node)->name;
                break;
            case bc::Op::SetField:
                os << "  ; ." << static_cast<const PlaceStep *>(node)->name;
                break;
            case bc::Op::Call:
            case bc::Op::CallW:
            case bc::Op::CallM:
            case bc::Op::CallS:
            {
                const Expr *call = nullptr;
                if (ins.x & bc::kCallAwaited)
                    call = static_cast<const AwaitExpr *>(node)->call.get();
                if (const auto *fc = call ? std::get_if<FunctionCall>(&call->data) : nullptr)
                    os << "  ; " << fc->functionName;
                else if (const auto *mc = call ? std::get_if<MethodCall>(&call->data) : nullptr)
                    os << "  ; ." << mc->name;
                else if (const auto *sc = call ? std::get_if<SuperCall>(&call->data) : nullptr)
                    os << "  ; super." << sc->name;
                else if (ins.op == static_cast<uint16_t>(bc::Op::CallM))
                    os << "  ; ." << static_cast<const MethodCall *>(node)->name;
                else if (ins.op == static_cast<uint16_t>(bc::Op::CallS))
                    os << "  ; super." << static_cast<const SuperCall *>(node)->name;
                else
                    os << "  ; " << static_cast<const FunctionCall *>(node)->functionName;
                break;
            }
            default:
                break;
            }
        }

        static void instruction(std::ostringstream &os, const bc::Proto &p, size_t pc)
        {
            const bc::Instr &ins = p.code[pc];
            const bc::Op op = static_cast<bc::Op>(ins.op);
            std::string label = std::to_string(pc);
            os << "  " << std::string(label.size() < 4 ? 4 - label.size() : 0, ' ') << label << "  ";
            const std::string opText = name(op);
            os << opText << std::string(opText.size() < 11 ? 11 - opText.size() : 1, ' ');
            const char *kinds = operands(op);
            operand(os, p, ins, pc, kinds[0], ins.a);
            operand(os, p, ins, pc, kinds[1], ins.b);
            operand(os, p, ins, pc, kinds[2], ins.c);
            flags(os, ins);
            about(os, p, ins, pc);
            os << "\n";
        }
    };

}
