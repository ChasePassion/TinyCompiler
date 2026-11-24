#include "ASTNodes.h"

#include <iostream>

std::string OpName(BinOp op) {
    switch (op) {
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_LT:  return "<";
    case OP_LE:  return "<=";
    case OP_GT:  return ">";
    case OP_GE:  return ">=";
    case OP_EQ:  return "==";
    case OP_NE:  return "!=";
    }
    return "?";
}

void Node::print(const std::string& prefix) const {
    std::cout << prefix << this->label() << "\n";
    std::string nextPrefix = prefix + "--";
    for (Node* child : this->children()) {
        if (child) {
            child->print(nextPrefix);
        }
    }
}
