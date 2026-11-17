#include "Quadruple.h"

#include <cstdio>
#include <sstream>

const char* QuadOpName(QuadOp op) {
    switch (op) {
    case QuadOp::Assign:      return ":=";
    case QuadOp::Add:         return "+";
    case QuadOp::Sub:         return "-";
    case QuadOp::Mul:         return "*";
    case QuadOp::Div:         return "/";
    case QuadOp::Mod:         return "%";
    case QuadOp::UMinus:      return "uminus";
    case QuadOp::BitAnd:      return "&";
    case QuadOp::BitOr:       return "|";
    case QuadOp::BitXor:      return "^";
    case QuadOp::Shl:         return "<<";
    case QuadOp::Shr:         return ">>";
    case QuadOp::Lt:          return "<";
    case QuadOp::Le:          return "<=";
    case QuadOp::Gt:          return ">";
    case QuadOp::Ge:          return ">=";
    case QuadOp::Eq:          return "==";
    case QuadOp::Ne:          return "!=";
    case QuadOp::VarRef:      return "varref";
    case QuadOp::ArrayRef:    return "arrayref";
    case QuadOp::ArraySet:    return "arrayset";
    case QuadOp::FieldRef:    return "fieldref";
    case QuadOp::FieldSet:    return "fieldset";
    case QuadOp::Param:       return "param";
    case QuadOp::Call:        return "call";
    case QuadOp::Return:      return "return";
    case QuadOp::Label:       return "label";
    case QuadOp::Jump:        return "goto";
    case QuadOp::JumpIfFalse: return "iffalse";
    case QuadOp::JumpIfTrue:  return "iftrue";
    }
    return "?";
}

std::string Quadruple::toString() const {
    std::ostringstream out;
    out << QuadOpName(op);
    if (arg1) {
        out << " " << arg1->name;
    }
    if (arg2) {
        out << ", " << arg2->name;
    }
    if (result) {
        out << " → " << result->name;
    }
    if (label >= 0) {
        out << " L" << label;
    }
    if (fieldIndex >= 0) {
        out << " ." << fieldIndex;
    }
    return out.str();
}

void IntermediateCode::dump() const {
    // 注意：printf 的格式串里不能有中文。MSVC 会按 ANSI 码页解释格式串，
    // 中文的 UTF-8 字节会被当成格式说明符，直接把程序搞崩。
    for (const FunctionCode& function : functions) {
        std::string header = "函数 " + function.name + " : " + function.returnTypeName;
        printf("%s\n", header.c_str());
        printf("---------------------------------------------\n");
        int index = 1;
        for (const Quadruple& quad : function.quads) {
            std::string line = "(" + std::to_string(index++) + ")\t" + quad.toString();
            printf("%s\n", line.c_str());
        }
        printf("---------------------------------------------\n\n");
    }
}
