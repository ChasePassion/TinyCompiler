#include "IntermGen.h"

#include "utils.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

#include <algorithm>

using namespace llvm;

namespace {

// 两个操作数的类型统一：
//   只要有一边是浮点，两边都提升到 double
//   两边都是整数时统一到较宽的那个（至少 32 位）
Type* unifyTypes(Type* left, Type* right) {
    if (!left || !right) {
        return left ? left : right;
    }
    if (left == right) {
        return left;
    }
    if (left->isFloatingPointTy() || right->isFloatingPointTy()) {
        return Type::getDoubleTy(left->getContext());
    }
    if (left->isIntegerTy() && right->isIntegerTy()) {
        unsigned width = std::max(left->getIntegerBitWidth(), right->getIntegerBitWidth());
        if (width < 32) {
            width = 32;
        }
        return Type::getIntNTy(left->getContext(), width);
    }
    return left;
}

} // namespace

IntermGen::IntermGen(SymbolTable& symbols, TypeSystem& types)
    : symbols_(symbols), types_(types) {}

int IntermGen::newLabel() {
    return nextLabel_++;
}

void IntermGen::emit(QuadOp op, Symbol* arg1, Symbol* arg2, Symbol* result,
                      int label, int fieldIndex) {
    Quadruple quad;
    quad.op = op;
    quad.arg1 = arg1;
    quad.arg2 = arg2;
    quad.result = result;
    quad.label = label;
    quad.fieldIndex = fieldIndex;
    current_->quads.push_back(quad);
}

IntermediateCode IntermGen::generate(NBlock& root) {
    for (auto& statement : root.statements) {
        if (auto* function = dynamic_cast<NFunctionDeclaration*>(statement.get())) {
            translateFunction(*function);
        } else if (auto* structure = dynamic_cast<NStructDeclaration*>(statement.get())) {
            translateStatement(*structure);
        }
    }
    return std::move(code_);
}

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

Symbol* IntermGen::translateExpression(NExpression& node) {
    // 整数字面量：它的值编译期就知道，直接做成常量符号
    if (auto* n = dynamic_cast<NInteger*>(&node)) {
        Symbol* symbol = symbols_.newTemporary(SymbolKind::Constant);
        symbol->typeName = "int";
        symbol->llvmType = types_.typeByName("int");
        symbol->hasConstant = true;
        symbol->constantIsFloat = false;
        symbol->constantValue = static_cast<double>(n->value);
        return symbol;
    }

    if (auto* n = dynamic_cast<NDouble*>(&node)) {
        Symbol* symbol = symbols_.newTemporary(SymbolKind::Constant);
        symbol->typeName = "double";
        symbol->llvmType = types_.typeByName("double");
        symbol->hasConstant = true;
        symbol->constantIsFloat = true;
        symbol->constantValue = n->value;
        return symbol;
    }

    if (auto* n = dynamic_cast<NLiteral*>(&node)) {
        Symbol* symbol = symbols_.newTemporary(SymbolKind::Constant);
        symbol->typeName = "string";
        symbol->llvmType = types_.typeByName("string");
        symbol->isString = true;
        symbol->stringValue = n->value;
        return symbol;
    }

    // 变量引用（教材 7.3.3 记录中域的引用）
    if (auto* n = dynamic_cast<NIdentifier*>(&node)) {
        Symbol* variable = symbols_.lookup(n->name);
        if (!variable) {
            LogError("未定义的变量：" + n->name);
            return nullptr;
        }
        Symbol* temp = symbols_.newTemporary();
        temp->typeName = variable->typeName;
        if (variable->isArray()) {
            // 数组退化成指向首元素的指针
            temp->llvmType = llvm::PointerType::getUnqual(variable->llvmType);
        } else {
            temp->llvmType = variable->llvmType;
        }
        emit(QuadOp::VarRef, variable, nullptr, temp);
        return temp;
    }

    // 二元运算
    if (auto* n = dynamic_cast<NBinaryOperator*>(&node)) {
        Symbol* left = translateExpression(*n->lhs);
        Symbol* right = translateExpression(*n->rhs);
        if (!left || !right) {
            return nullptr;
        }
        Type* unified = unifyTypes(left->llvmType, right->llvmType);

        Symbol* temp = symbols_.newTemporary();
        switch (n->op) {
        case OP_ADD: temp->llvmType = unified; emit(QuadOp::Add, left, right, temp); break;
        case OP_SUB: temp->llvmType = unified; emit(QuadOp::Sub, left, right, temp); break;
        case OP_MUL: temp->llvmType = unified; emit(QuadOp::Mul, left, right, temp); break;
        case OP_DIV: temp->llvmType = unified; emit(QuadOp::Div, left, right, temp); break;
        case OP_MOD: temp->llvmType = unified; emit(QuadOp::Mod, left, right, temp); break;
        case OP_AND: temp->llvmType = unified; emit(QuadOp::BitAnd, left, right, temp); break;
        case OP_OR:  temp->llvmType = unified; emit(QuadOp::BitOr, left, right, temp); break;
        case OP_XOR: temp->llvmType = unified; emit(QuadOp::BitXor, left, right, temp); break;
        case OP_SHL: temp->llvmType = unified; emit(QuadOp::Shl, left, right, temp); break;
        case OP_SHR: temp->llvmType = unified; emit(QuadOp::Shr, left, right, temp); break;
        // 比较运算的结果是 int（0 或 1），和 C 一样
        case OP_LT: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Lt, left, right, temp); break;
        case OP_LE: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Le, left, right, temp); break;
        case OP_GT: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Gt, left, right, temp); break;
        case OP_GE: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Ge, left, right, temp); break;
        case OP_EQ: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Eq, left, right, temp); break;
        case OP_NE: temp->llvmType = types_.typeByName("int"); emit(QuadOp::Ne, left, right, temp); break;
        }
        temp->typeName = (temp->llvmType == types_.typeByName("double")) ? "double" : "int";
        return temp;
    }

    // 赋值语句（教材 7.3.1）
    if (auto* n = dynamic_cast<NAssignment*>(&node)) {
        Symbol* variable = symbols_.lookup(n->lhs->name);
        if (!variable) {
            LogError("未定义的变量：" + n->lhs->name);
            return nullptr;
        }
        Symbol* value = translateExpression(*n->rhs);
        if (!value) {
            return nullptr;
        }
        emit(QuadOp::Assign, value, nullptr, variable);
        return value;
    }

    // 函数调用（教材 7.6 过程调用的处理）
    if (auto* n = dynamic_cast<NMethodCall*>(&node)) {
        Symbol* function = symbols_.lookup(n->id->name);
        if (!function) {
            LogError("找不到函数：" + n->id->name);
            return nullptr;
        }
        // 先把实参都算出来，再统一传递。
        // 这样每个调用的实参四元式是连续的，后端才好对应：
        //   param t1        ← 内层调用的实参
        //   call g → t2
        //   param t2        ← 外层调用的实参
        //   call f → t3
        std::vector<Symbol*> arguments;
        for (auto& argument : n->args) {
            Symbol* value = translateExpression(*argument);
            if (!value) {
                return nullptr;
            }
            arguments.push_back(value);
        }
        for (Symbol* value : arguments) {
            emit(QuadOp::Param, value, nullptr, nullptr);
        }
        Symbol* temp = symbols_.newTemporary();
        temp->typeName = function->typeName;
        temp->llvmType = function->llvmType;
        emit(QuadOp::Call, function, nullptr, temp);
        return temp;
    }

    // 结构体成员引用（教材 7.3.3）
    if (auto* n = dynamic_cast<NStructMember*>(&node)) {
        Symbol* variable = symbols_.lookup(n->id->name);
        if (!variable) {
            LogError("未定义的变量：" + n->id->name);
            return nullptr;
        }
        long index = types_.getStructMemberIndex(variable->typeName, n->member->name);
        if (index < 0) {
            LogError("结构体 " + variable->typeName + " 里没有成员 " + n->member->name);
            return nullptr;
        }
        Symbol* temp = symbols_.newTemporary();
        temp->llvmType = types_.structMemberType(variable->typeName, n->member->name);
        temp->typeName = variable->typeName;
        emit(QuadOp::FieldRef, variable, nullptr, temp, -1, static_cast<int>(index));
        return temp;
    }

    // 数组元素引用（教材 7.3.2）
    if (auto* n = dynamic_cast<NArrayIndex*>(&node)) {
        Symbol* variable = symbols_.lookup(n->arrayName->name);
        if (!variable) {
            LogError("未定义的变量：" + n->arrayName->name);
            return nullptr;
        }
        Symbol* index = translateArrayIndex(*n, *variable);
        if (!index) {
            return nullptr;
        }
        Symbol* temp = symbols_.newTemporary();
        temp->llvmType = variable->llvmType;
        temp->typeName = variable->typeName;
        emit(QuadOp::ArrayRef, variable, index, temp);
        return temp;
    }

    // 数组元素赋值
    if (auto* n = dynamic_cast<NArrayAssignment*>(&node)) {
        Symbol* variable = symbols_.lookup(n->index->arrayName->name);
        if (!variable) {
            LogError("未定义的变量：" + n->index->arrayName->name);
            return nullptr;
        }
        Symbol* index = translateArrayIndex(*n->index, *variable);
        if (!index) {
            return nullptr;
        }
        Symbol* value = translateExpression(*n->expression);
        if (!value) {
            return nullptr;
        }
        emit(QuadOp::ArraySet, value, index, variable);
        return value;
    }

    // 结构体成员赋值
    if (auto* n = dynamic_cast<NStructAssignment*>(&node)) {
        Symbol* variable = symbols_.lookup(n->member->id->name);
        if (!variable) {
            LogError("未定义的变量：" + n->member->id->name);
            return nullptr;
        }
        long index = types_.getStructMemberIndex(variable->typeName, n->member->member->name);
        if (index < 0) {
            LogError("结构体 " + variable->typeName + " 里没有成员 " + n->member->member->name);
            return nullptr;
        }
        Symbol* value = translateExpression(*n->expression);
        if (!value) {
            return nullptr;
        }
        emit(QuadOp::FieldSet, value, nullptr, variable, -1, static_cast<int>(index));
        return value;
    }

    return nullptr;
}

// 多维数组的下标换算：((e0 * d1) + e1) * d2 + e2 ...
Symbol* IntermGen::translateArrayIndex(NArrayIndex& node, Symbol& array) {
    if (node.exprs.size() != array.dims.size()) {
        LogError("数组 " + array.name + " 的下标个数和数组维度对不上");
        return nullptr;
    }
    Type* intType = types_.typeByName("int");

    Symbol* result = nullptr;
    for (size_t i = 0; i < node.exprs.size(); ++i) {
        Symbol* element = translateExpression(*node.exprs[i]);
        if (!element) {
            return nullptr;
        }
        if (!result) {
            result = element;
            continue;
        }
        // result = result * dims[i] + element
        Symbol* stride = symbols_.newTemporary(SymbolKind::Constant);
        stride->typeName = "int";
        stride->llvmType = intType;
        stride->hasConstant = true;
        stride->constantValue = static_cast<double>(array.dims[i]);

        Symbol* product = symbols_.newTemporary();
        product->typeName = "int";
        product->llvmType = intType;
        emit(QuadOp::Mul, result, stride, product);

        Symbol* sum = symbols_.newTemporary();
        sum->typeName = "int";
        sum->llvmType = intType;
        emit(QuadOp::Add, product, element, sum);
        result = sum;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------

void IntermGen::translateStatement(NStatement& node) {
    // 说明语句（教材 7.2）：登记到符号表
    if (auto* n = dynamic_cast<NVariableDeclaration*>(&node)) {
        Symbol* symbol = symbols_.define(n->id->name, SymbolKind::Variable);
        symbol->typeName = n->type->name;
        symbol->llvmType = types_.typeByName(n->type->name);
        symbol->dims = n->type->dims;
        if (!symbol->llvmType) {
            LogError("未知的类型：" + n->type->name);
            return;
        }
        // 记到当前函数的局部变量表里，后端按它分配存储单元（教材 9.3.4）
        if (current_) {
            current_->locals.push_back(symbol);
        }
        if (n->init) {
            Symbol* value = translateExpression(*n->init);
            if (!value) {
                return;
            }
            emit(QuadOp::Assign, value, nullptr, symbol);
        }
        return;
    }

    // 数组初始化
    if (auto* n = dynamic_cast<NArrayInitialization*>(&node)) {
        translateStatement(*n->declaration);
        Symbol* variable = symbols_.lookup(n->declaration->id->name);
        if (!variable) {
            return;
        }
        Type* intType = types_.typeByName("int");
        for (size_t i = 0; i < n->elements.size(); ++i) {
            Symbol* value = translateExpression(*n->elements[i]);
            if (!value) {
                return;
            }
            Symbol* index = symbols_.newTemporary(SymbolKind::Constant);
            index->typeName = "int";
            index->llvmType = intType;
            index->hasConstant = true;
            index->constantValue = static_cast<double>(i);
            emit(QuadOp::ArraySet, value, index, variable);
        }
        return;
    }

    // 表达式语句
    if (auto* n = dynamic_cast<NExpressionStatement*>(&node)) {
        translateExpression(*n->expression);
        return;
    }

    // return 语句
    if (auto* n = dynamic_cast<NReturnStatement*>(&node)) {
        Symbol* value = translateExpression(*n->expression);
        if (!value) {
            return;
        }
        emit(QuadOp::Return, value, nullptr, nullptr);
        return;
    }

    // 控制语句（教材 7.5 控制流语句）
    if (auto* n = dynamic_cast<NIfStatement*>(&node)) {
        Symbol* condition = translateExpression(*n->condition);
        if (!condition) {
            return;
        }
        int elseLabel = newLabel();
        int endLabel = newLabel();

        emit(QuadOp::JumpIfFalse, condition, nullptr, nullptr, elseLabel);

        symbols_.pushScope();
        for (auto& statement : n->trueBlock->statements) {
            translateStatement(*statement);
        }
        symbols_.popScope();

        emit(QuadOp::Jump, nullptr, nullptr, nullptr, endLabel);

        emit(QuadOp::Label, nullptr, nullptr, nullptr, elseLabel);
        symbols_.pushScope();
        if (n->falseBlock) {
            for (auto& statement : n->falseBlock->statements) {
                translateStatement(*statement);
            }
        }
        symbols_.popScope();

        emit(QuadOp::Label, nullptr, nullptr, nullptr, endLabel);
        return;
    }

    // 循环语句（教材 7.5 控制流语句）
    if (auto* n = dynamic_cast<NForStatement*>(&node)) {
        int condLabel = newLabel();
        int endLabel = newLabel();

        if (n->initial) {
            translateExpression(*n->initial);
        }

        emit(QuadOp::Label, nullptr, nullptr, nullptr, condLabel);
        Symbol* condition = translateExpression(*n->condition);
        if (!condition) {
            return;
        }
        emit(QuadOp::JumpIfFalse, condition, nullptr, nullptr, endLabel);

        symbols_.pushScope();
        for (auto& statement : n->body->statements) {
            translateStatement(*statement);
        }
        symbols_.popScope();

        if (n->increment) {
            translateExpression(*n->increment);
        }
        emit(QuadOp::Jump, nullptr, nullptr, nullptr, condLabel);
        emit(QuadOp::Label, nullptr, nullptr, nullptr, endLabel);
        return;
    }

    // 结构体说明：在类型系统里登记结构体类型
    if (auto* n = dynamic_cast<NStructDeclaration*>(&node)) {
        std::vector<Type*> memberTypes;
        for (auto& member : n->members) {
            Type* type = types_.typeByName(member->type->name);
            if (!type) {
                LogError("未知的成员类型：" + member->type->name);
                return;
            }
            memberTypes.push_back(type);
        }
        StructType* structType = StructType::create(types_.context(), n->name->name);
        types_.addStructType(n->name->name, structType);
        for (auto& member : n->members) {
            types_.addStructMember(n->name->name, member->type->name, member->id->name);
        }
        structType->setBody(memberTypes);
        symbols_.define(n->name->name, SymbolKind::TypeName);
        return;
    }
}

// ---------------------------------------------------------------------------
// 函数
// ---------------------------------------------------------------------------

void IntermGen::translateFunction(NFunctionDeclaration& node) {
    // 先登记函数，函数体里才能递归调用自己
    Symbol* function = symbols_.define(node.id->name, SymbolKind::Function);
    function->typeName = node.type->name;
    function->llvmType = types_.typeByName(node.type->name);
    // 外部函数按可变参数处理，这样 printf("hi %d", x) 才能正常工作
    function->isVariadic = node.isExtern;

    FunctionCode code;
    code.name = node.id->name;
    code.returnTypeName = node.type->name;

    symbols_.pushScope();
    for (auto& parameter : node.params) {
        Symbol* symbol = symbols_.define(parameter->id->name, SymbolKind::Variable);
        symbol->typeName = parameter->type->name;
        symbol->llvmType = types_.typeByName(parameter->type->name);
        symbol->dims = parameter->type->dims;
        symbol->isFuncArg = true;
        code.params.push_back(symbol);
    }

    FunctionCode* saved = current_;
    current_ = &code;
    if (node.body) {
        for (auto& statement : node.body->statements) {
            translateStatement(*statement);
        }
    }
    current_ = saved;
    symbols_.popScope();

    code_.functions.push_back(std::move(code));
}
