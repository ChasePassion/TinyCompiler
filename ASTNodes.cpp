#include "ASTNodes.h"

#include "utils.h"

#include <iostream>

const char* OpName(BinOp op) {
    switch (op) {
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_AND: return "&";
    case OP_OR:  return "|";
    case OP_XOR: return "^";
    case OP_SHL: return "<<";
    case OP_SHR: return ">>";
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

std::string Node::toJson() const {
    std::string out = "{\"name\": \"" + JsonEscape(this->label()) + "\"";
    std::vector<Node*> kids = this->children();
    if (!kids.empty()) {
        out += ", \"children\": [";
        bool first = true;
        for (Node* child : kids) {
            if (!child) {
                continue;
            }
            if (!first) {
                out += ", ";
            }
            first = false;
            out += child->toJson();
        }
        out += "]";
    }
    out += "}";
    return out;
}
