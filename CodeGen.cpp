#include "CodeGen.h"

#include "ASTNodes.h"
#include "TypeSystem.h"
#include "utils.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

namespace {

// 统一的 GEP 封装。LLVM 22 起 GEP 必须显式给出元素类型（指针是不透明的，
// 无法再从指针类型反推元素类型），所以这里统一收口，改版本时只改这一处。
Value* createGEP(CodeGenContext& ctx, Type* elementType, Value* pointer,
                 ArrayRef<Value*> indices) {
    return ctx.builder.CreateInBoundsGEP(elementType, pointer, indices);
}

// 常量 0（i32）
Value* zero(CodeGenContext& ctx) {
    return ConstantInt::get(Type::getInt32Ty(ctx.llvmContext), 0);
}

// 把值转成布尔：整数非 0 为真，浮点非 0 为真。
// 注意不能把整数直接截断成 1 位，那样 2 会变成 0（假）。
Value* toBoolean(CodeGenContext& ctx, Value* value) {
    Type* type = value->getType();
    if (type->isIntegerTy()) {
        return ctx.builder.CreateICmpNE(value, ConstantInt::get(type, 0), "tobool");
    }
    if (type->isFloatingPointTy()) {
        return ctx.builder.CreateFCmpONE(value, ConstantFP::get(type, 0.0), "tobool");
    }
    return value;
}

// 统一两个操作数的类型：
//   1. 只要有一边是浮点，两边都提升到 double
//   2. 两边都是整数时，统一到较宽的那个（至少 32 位）
void unifyOperands(CodeGenContext& ctx, Value*& left, Value*& right) {
    Type* leftType = left->getType();
    Type* rightType = right->getType();

    if (leftType->isFloatingPointTy() || rightType->isFloatingPointTy()) {
        Type* doubleType = ctx.typeSystem.typeByName("double");
        if (leftType != doubleType) {
            left = ctx.typeSystem.cast(left, doubleType, ctx.builder);
        }
        if (rightType != doubleType) {
            right = ctx.typeSystem.cast(right, doubleType, ctx.builder);
        }
        return;
    }

    if (leftType->isIntegerTy() && rightType->isIntegerTy()) {
        unsigned width = std::max(leftType->getIntegerBitWidth(),
                                  rightType->getIntegerBitWidth());
        if (width < 32) {
            width = 32;
        }
        Type* target = Type::getIntNTy(ctx.llvmContext, width);
        if (leftType != target) {
            left = ctx.typeSystem.cast(left, target, ctx.builder);
        }
        if (rightType != target) {
            right = ctx.typeSystem.cast(right, target, ctx.builder);
        }
    }
}

// 多维数组展平成一维后的下标：((e0 * d1) + e1) * d2 + e2 ...
Value* flatIndex(CodeGenContext& ctx, const Symbol& symbol,
                 const std::vector<std::shared_ptr<NExpression>>& exprs) {
    if (exprs.size() != symbol.dims.size()) {
        LogError("数组下标的个数和数组维度对不上");
        return nullptr;
    }
    Type* intType = ctx.typeSystem.typeByName("int");
    Value* result = nullptr;
    for (size_t i = 0; i < exprs.size(); ++i) {
        Value* element = exprs[i]->codeGen(ctx);
        if (!element) {
            return nullptr;
        }
        element = ctx.typeSystem.cast(element, intType, ctx.builder);
        if (!result) {
            result = element;
            continue;
        }
        Value* stride = ConstantInt::get(intType, symbol.dims[i]);
        result = ctx.builder.CreateAdd(ctx.builder.CreateMul(result, stride, "mul"), element, "idx");
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// CodeGenContext
// ---------------------------------------------------------------------------

CodeGenContext::CodeGenContext()
    : builder(llvmContext), typeSystem(llvmContext) {
    theModule = std::make_unique<llvm::Module>("main", llvmContext);
}

void CodeGenContext::pushScope() {
    scopes_.emplace_back();
}

void CodeGenContext::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

Symbol* CodeGenContext::findSymbol(const std::string& name) {
    // 从最内层作用域往外找，符合 C 的遮蔽规则
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto it = scope->symbols.find(name);
        if (it != scope->symbols.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void CodeGenContext::defineSymbol(const std::string& name, const Symbol& symbol) {
    if (scopes_.empty()) {
        scopes_.emplace_back();
    }
    scopes_.back().symbols[name] = symbol;
}

void CodeGenContext::setReturnValue(Value* value) {
    returnValue_ = value;
}

Value* CodeGenContext::returnValue() {
    return returnValue_;
}

void CodeGenContext::setFunctionReturnType(Type* type) {
    functionReturnType_ = type;
}

Type* CodeGenContext::currentFunctionReturnType() {
    return functionReturnType_;
}

Function* CodeGenContext::declareFunction(const NFunctionDeclaration& declaration) {
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : declaration.params) {
        llvm::Type* type = typeSystem.llvmType(*param->type);
        if (!type) {
            LogError("未知的参数类型：" + param->type->name);
            return nullptr;
        }
        paramTypes.push_back(type);
    }

    llvm::Type* returnType = typeSystem.llvmType(*declaration.type);
    if (!returnType) {
        LogError("未知的返回类型：" + declaration.type->name);
        return nullptr;
    }

    // 外部函数按可变参数处理，这样 printf("%d", x) 这种调用才能正常工作。
    llvm::FunctionType* functionType =
        llvm::FunctionType::get(returnType, paramTypes, declaration.isExtern);

    // 已经声明过的函数直接复用，避免同一个名字被定义两次
    llvm::Function* existing = theModule->getFunction(declaration.id->name);
    if (existing) {
        return existing;
    }
    return llvm::Function::Create(functionType, llvm::GlobalValue::ExternalLinkage,
                                  declaration.id->name, theModule.get());
}

void CodeGenContext::generateCode(NBlock& root) {
    // 顶层只有函数和结构体声明。为了让代码生成有合法的插入点，这里建一个
    // 临时函数；它不属于 Module，最后会被删掉，不会出现在目标文件里。
    llvm::FunctionType* dummyType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(llvmContext), false);
    llvm::Function* dummy = llvm::Function::Create(
        dummyType, llvm::GlobalValue::InternalLinkage, "tiny.global", theModule.get());
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvmContext, "entry", dummy);

    builder.SetInsertPoint(entry);
    pushScope();

    // 第一遍：先把所有函数原型建好，这样调用后面才定义的函数也能找到。
    // 结构体类型也要在这一遍注册，否则形参里用到结构体时会找不到类型。
    for (auto& statement : root.statements) {
        if (auto* structure = dynamic_cast<NStructDeclaration*>(statement.get())) {
            structure->codeGen(*this);
        } else if (auto* function = dynamic_cast<NFunctionDeclaration*>(statement.get())) {
            declareFunction(*function);
        }
    }

    // 第二遍：真正生成代码
    root.codeGen(*this);

    popScope();

    builder.ClearInsertionPoint();
    dummy->eraseFromParent();
}

// ---------------------------------------------------------------------------
// 字面量与标识符
// ---------------------------------------------------------------------------

llvm::Value* NInteger::codeGen(CodeGenContext& ctx) {
    // getSigned：负数要按有符号解释，不能当成无符号的大数
    return llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx.llvmContext), this->value);
}

llvm::Value* NDouble::codeGen(CodeGenContext& ctx) {
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx.llvmContext), this->value);
}

llvm::Value* NLiteral::codeGen(CodeGenContext& ctx) {
    // 全局字符串的类型是 [N x i8]，要取首元素的指针才能当 char* 用
    llvm::GlobalVariable* global = ctx.builder.CreateGlobalString(this->value, "str");
    std::vector<llvm::Value*> indices{zero(ctx), zero(ctx)};
    return createGEP(ctx, global->getValueType(), global, indices);
}

llvm::Value* NIdentifier::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->name);
    }
    // 数组在表达式里退化成指向首元素的指针
    if (!symbol->dims.empty()) {
        return symbol->arrayBase;
    }
    return ctx.builder.CreateLoad(symbol->type, symbol->addr, this->name.c_str());
}

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

llvm::Value* NBinaryOperator::codeGen(CodeGenContext& ctx) {
    llvm::Value* left = this->lhs->codeGen(ctx);
    llvm::Value* right = this->rhs->codeGen(ctx);
    if (!left || !right) {
        return nullptr;
    }

    unifyOperands(ctx, left, right);
    bool isFloat = left->getType()->isFloatingPointTy();

    switch (this->op) {
    case OP_ADD: return isFloat ? ctx.builder.CreateFAdd(left, right, "add")
                                : ctx.builder.CreateAdd(left, right, "add");
    case OP_SUB: return isFloat ? ctx.builder.CreateFSub(left, right, "sub")
                                : ctx.builder.CreateSub(left, right, "sub");
    case OP_MUL: return isFloat ? ctx.builder.CreateFMul(left, right, "mul")
                                : ctx.builder.CreateMul(left, right, "mul");
    case OP_DIV: return isFloat ? ctx.builder.CreateFDiv(left, right, "div")
                                : ctx.builder.CreateSDiv(left, right, "div");
    case OP_MOD:
        if (isFloat) return LogErrorV("浮点数不支持取模运算");
        return ctx.builder.CreateSRem(left, right, "mod");
    case OP_AND:
        if (isFloat) return LogErrorV("浮点数不支持按位与运算");
        return ctx.builder.CreateAnd(left, right, "and");
    case OP_OR:
        if (isFloat) return LogErrorV("浮点数不支持按位或运算");
        return ctx.builder.CreateOr(left, right, "or");
    case OP_XOR:
        if (isFloat) return LogErrorV("浮点数不支持按位异或运算");
        return ctx.builder.CreateXor(left, right, "xor");
    case OP_SHL:
        if (isFloat) return LogErrorV("浮点数不支持移位运算");
        return ctx.builder.CreateShl(left, right, "shl");
    case OP_SHR:
        if (isFloat) return LogErrorV("浮点数不支持移位运算");
        return ctx.builder.CreateAShr(left, right, "shr");
    // 本语言的整数是有符号的，比较运算统一用有符号版本
    case OP_LT: return isFloat ? ctx.builder.CreateFCmpOLT(left, right, "cmp")
                               : ctx.builder.CreateICmpSLT(left, right, "cmp");
    case OP_LE: return isFloat ? ctx.builder.CreateFCmpOLE(left, right, "cmp")
                               : ctx.builder.CreateICmpSLE(left, right, "cmp");
    case OP_GT: return isFloat ? ctx.builder.CreateFCmpOGT(left, right, "cmp")
                               : ctx.builder.CreateICmpSGT(left, right, "cmp");
    case OP_GE: return isFloat ? ctx.builder.CreateFCmpOGE(left, right, "cmp")
                               : ctx.builder.CreateICmpSGE(left, right, "cmp");
    case OP_EQ: return isFloat ? ctx.builder.CreateFCmpOEQ(left, right, "cmp")
                               : ctx.builder.CreateICmpEQ(left, right, "cmp");
    case OP_NE: return isFloat ? ctx.builder.CreateFCmpONE(left, right, "cmp")
                               : ctx.builder.CreateICmpNE(left, right, "cmp");
    }
    return LogErrorV("未知的二元运算符");
}

llvm::Value* NAssignment::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->lhs->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->lhs->name);
    }
    llvm::Value* value = this->rhs->codeGen(ctx);
    if (!value) {
        return nullptr;
    }
    value = ctx.typeSystem.cast(value, symbol->type, ctx.builder);
    ctx.builder.CreateStore(value, symbol->addr);
    return value;
}

llvm::Value* NMethodCall::codeGen(CodeGenContext& ctx) {
    llvm::Function* callee = ctx.theModule->getFunction(this->id->name);
    if (!callee) {
        return LogErrorV("找不到函数：" + this->id->name);
    }
    if (this->args.size() < callee->arg_size()) {
        return LogErrorV("函数 " + this->id->name + " 需要的参数比给出的多");
    }

    std::vector<llvm::Value*> argv;
    for (size_t i = 0; i < this->args.size(); ++i) {
        llvm::Value* value = this->args[i]->codeGen(ctx);
        if (!value) {
            return nullptr;
        }
        // 固定参数按形参类型做隐式转换；可变参数不做限制
        if (i < callee->arg_size()) {
            value = ctx.typeSystem.cast(value, callee->getArg(i)->getType(), ctx.builder);
        }
        argv.push_back(value);
    }
    return ctx.builder.CreateCall(callee, argv, "call");
}

llvm::Value* NStructMember::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->id->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->id->name);
    }
    if (!symbol->type || !symbol->type->isStructTy()) {
        return LogErrorV(this->id->name + " 不是结构体");
    }
    std::string structName = symbol->type->getStructName().str();
    long memberIndex = ctx.typeSystem.getStructMemberIndex(structName, this->member->name);
    if (memberIndex < 0) {
        return LogErrorV("结构体 " + structName + " 里没有成员 " + this->member->name);
    }
    std::vector<llvm::Value*> indices{zero(ctx), ConstantInt::get(ctx.typeSystem.typeByName("int"), memberIndex)};
    llvm::Value* memberPtr = createGEP(ctx, symbol->type, symbol->addr, indices);
    llvm::Type* memberType = ctx.typeSystem.structMemberType(structName, this->member->name);
    if (!memberType) {
        return LogErrorV("结构体 " + structName + " 里没有成员 " + this->member->name);
    }
    return ctx.builder.CreateLoad(memberType, memberPtr, this->member->name.c_str());
}

llvm::Value* NArrayIndex::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->arrayName->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->arrayName->name);
    }
    if (symbol->dims.empty()) {
        return LogErrorV(this->arrayName->name + " 不是数组");
    }
    llvm::Value* index = flatIndex(ctx, *symbol, this->exprs);
    if (!index) {
        return nullptr;
    }
    llvm::Value* elementPtr = createGEP(ctx, symbol->type, symbol->arrayBase, {index});
    return ctx.builder.CreateLoad(symbol->type, elementPtr, "elem");
}

llvm::Value* NArrayAssignment::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->index->arrayName->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->index->arrayName->name);
    }
    if (symbol->dims.empty()) {
        return LogErrorV(this->index->arrayName->name + " 不是数组");
    }
    llvm::Value* value = this->expression->codeGen(ctx);
    if (!value) {
        return nullptr;
    }
    value = ctx.typeSystem.cast(value, symbol->type, ctx.builder);
    llvm::Value* index = flatIndex(ctx, *symbol, this->index->exprs);
    if (!index) {
        return nullptr;
    }
    llvm::Value* elementPtr = createGEP(ctx, symbol->type, symbol->arrayBase, {index});
    ctx.builder.CreateStore(value, elementPtr);
    return value;
}

llvm::Value* NStructAssignment::codeGen(CodeGenContext& ctx) {
    Symbol* symbol = ctx.findSymbol(this->member->id->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->member->id->name);
    }
    if (!symbol->type || !symbol->type->isStructTy()) {
        return LogErrorV(this->member->id->name + " 不是结构体");
    }
    std::string structName = symbol->type->getStructName().str();
    long memberIndex = ctx.typeSystem.getStructMemberIndex(structName, this->member->member->name);
    if (memberIndex < 0) {
        return LogErrorV("结构体 " + structName + " 里没有成员 " + this->member->member->name);
    }
    llvm::Type* memberType = ctx.typeSystem.structMemberType(structName, this->member->member->name);

    llvm::Value* value = this->expression->codeGen(ctx);
    if (!value) {
        return nullptr;
    }
    if (memberType) {
        value = ctx.typeSystem.cast(value, memberType, ctx.builder);
    }
    std::vector<llvm::Value*> indices{zero(ctx), ConstantInt::get(ctx.typeSystem.typeByName("int"), memberIndex)};
    llvm::Value* memberPtr = createGEP(ctx, symbol->type, symbol->addr, indices);
    ctx.builder.CreateStore(value, memberPtr);
    return value;
}

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------

llvm::Value* NBlock::codeGen(CodeGenContext& ctx) {
    llvm::Value* last = nullptr;
    for (auto& statement : this->statements) {
        last = statement->codeGen(ctx);
    }
    return last;
}

llvm::Value* NExpressionStatement::codeGen(CodeGenContext& ctx) {
    return this->expression->codeGen(ctx);
}

llvm::Value* NVariableDeclaration::codeGen(CodeGenContext& ctx) {
    Symbol symbol;
    symbol.typeName = this->type->name;
    symbol.dims = this->type->dims;

    if (this->type->isArray()) {
        llvm::Type* elementType = ctx.typeSystem.typeByName(this->type->name);
        if (!elementType) {
            return LogErrorV("未知的类型：" + this->type->name);
        }
        llvm::Type* storageType = ctx.typeSystem.arrayStorageType(*this->type);
        llvm::Value* slot = ctx.builder.CreateAlloca(storageType, nullptr, this->id->name.c_str());
        symbol.type = elementType;
        symbol.addr = slot;
        // 指向首元素的指针，等价于 C 里数组退化成的指针
        symbol.arrayBase = createGEP(ctx, storageType, slot, {zero(ctx), zero(ctx)});
    } else {
        llvm::Type* type = ctx.typeSystem.llvmType(*this->type);
        if (!type) {
            return LogErrorV("未知的类型：" + this->type->name);
        }
        symbol.type = type;
        symbol.addr = ctx.builder.CreateAlloca(type, nullptr, this->id->name.c_str());
    }
    ctx.defineSymbol(this->id->name, symbol);

    if (this->init) {
        llvm::Value* value = this->init->codeGen(ctx);
        if (!value) {
            return nullptr;
        }
        value = ctx.typeSystem.cast(value, symbol.type, ctx.builder);
        ctx.builder.CreateStore(value, symbol.addr);
    }
    return symbol.addr;
}

llvm::Value* NFunctionDeclaration::codeGen(CodeGenContext& ctx) {
    llvm::Function* function = ctx.theModule->getFunction(this->id->name);
    if (!function) {
        function = ctx.declareFunction(*this);
        if (!function) {
            return nullptr;
        }
    }
    if (this->isExtern) {
        return function;
    }

    // 记住当前的插入位置，函数体生成完再恢复
    llvm::BasicBlock* previousBlock = ctx.builder.GetInsertBlock();
    llvm::BasicBlock::iterator previousPoint = ctx.builder.GetInsertPoint();

    ctx.setReturnValue(nullptr);
    ctx.setFunctionReturnType(function->getReturnType());
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx.llvmContext, "entry", function);
    ctx.builder.SetInsertPoint(entry);
    ctx.pushScope();

    // 把形参绑定到局部变量
    unsigned index = 0;
    for (auto& param : this->params) {
        if (index >= function->arg_size()) {
            break;
        }
        llvm::Argument* argument = function->getArg(index++);
        argument->setName(param->id->name);

        Symbol symbol;
        symbol.typeName = param->type->name;
        symbol.dims = param->type->dims;

        if (param->type->isArray()) {
            // 数组形参退化成指针，直接用传进来的指针
            symbol.type = ctx.typeSystem.typeByName(param->type->name);
            symbol.arrayBase = argument;
        } else {
            llvm::Type* type = ctx.typeSystem.llvmType(*param->type);
            if (!type) {
                LogError("未知的参数类型：" + param->type->name);
                break;
            }
            symbol.type = type;
            symbol.addr = ctx.builder.CreateAlloca(type, nullptr, param->id->name.c_str());
            ctx.builder.CreateStore(argument, symbol.addr);
        }
        ctx.defineSymbol(param->id->name, symbol);
    }

    if (this->body) {
        this->body->codeGen(ctx);
    }
    ctx.popScope();

    // 函数体最后如果还没有 ret，补一个
    llvm::BasicBlock* current = ctx.builder.GetInsertBlock();
    if (current && !current->getTerminator()) {
        llvm::Value* value = ctx.returnValue();
        if (!value) {
            value = ctx.typeSystem.defaultValue(function->getReturnType());
        }
        if (value) {
            ctx.builder.CreateRet(value);
        }
    }

    // 恢复插入位置
    if (previousBlock) {
        ctx.builder.SetInsertPoint(previousBlock, previousPoint);
    }
    return function;
}

llvm::Value* NStructDeclaration::codeGen(CodeGenContext& ctx) {
    // 声明阶段已经注册过就不用重复注册了
    if (ctx.typeSystem.isStruct(this->name->name)) {
        return nullptr;
    }

    std::vector<llvm::Type*> memberTypes;
    for (auto& member : this->members) {
        llvm::Type* type = ctx.typeSystem.typeByName(member->type->name);
        if (!type) {
            return LogErrorV("未知的成员类型：" + member->type->name);
        }
        memberTypes.push_back(type);
    }

    llvm::StructType* structType = llvm::StructType::create(ctx.llvmContext, this->name->name);
    ctx.typeSystem.addStructType(this->name->name, structType);
    for (auto& member : this->members) {
        ctx.typeSystem.addStructMember(this->name->name, member->type->name, member->id->name);
    }
    structType->setBody(memberTypes);
    return nullptr;
}

llvm::Value* NReturnStatement::codeGen(CodeGenContext& ctx) {
    llvm::Value* value = this->expression->codeGen(ctx);
    if (!value) {
        return nullptr;
    }
    // 返回值要转换成函数声明的返回类型
    llvm::Type* expected = ctx.currentFunctionReturnType();
    if (expected && !expected->isVoidTy()) {
        value = ctx.typeSystem.cast(value, expected, ctx.builder);
    }
    ctx.setReturnValue(value);
    ctx.builder.CreateRet(value);
    return value;
}

llvm::Value* NIfStatement::codeGen(CodeGenContext& ctx) {
    llvm::Value* condition = this->condition->codeGen(ctx);
    if (!condition) {
        return nullptr;
    }
    condition = toBoolean(ctx, condition);

    llvm::Function* function = ctx.builder.GetInsertBlock()->getParent();
    if (!function) {
        return LogErrorV("if 语句必须写在函数里");
    }

    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(ctx.llvmContext, "then", function);
    llvm::BasicBlock* elseBlock =
        this->falseBlock ? llvm::BasicBlock::Create(ctx.llvmContext, "else", function) : nullptr;
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(ctx.llvmContext, "ifcont", function);

    ctx.builder.CreateCondBr(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);

    // then 分支
    ctx.builder.SetInsertPoint(thenBlock);
    ctx.pushScope();
    this->trueBlock->codeGen(ctx);
    ctx.popScope();
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateBr(mergeBlock);
    }

    // else 分支
    if (elseBlock) {
        ctx.builder.SetInsertPoint(elseBlock);
        ctx.pushScope();
        this->falseBlock->codeGen(ctx);
        ctx.popScope();
        if (!ctx.builder.GetInsertBlock()->getTerminator()) {
            ctx.builder.CreateBr(mergeBlock);
        }
    }

    ctx.builder.SetInsertPoint(mergeBlock);

    // 分支里可能还有嵌套的 if，它们会在合并块之后继续创建新的块。
    // 把合并块挪到函数块列表的末尾，保证列表顺序和实际的执行顺序一致。
    llvm::BasicBlock* lastBlock = &function->back();
    if (lastBlock != mergeBlock) {
        mergeBlock->moveAfter(lastBlock);
    }
    return nullptr;
}

llvm::Value* NForStatement::codeGen(CodeGenContext& ctx) {
    llvm::Function* function = ctx.builder.GetInsertBlock()->getParent();
    if (!function) {
        return LogErrorV("for 语句必须写在函数里");
    }

    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(ctx.llvmContext, "for.cond", function);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(ctx.llvmContext, "for.body", function);
    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(ctx.llvmContext, "for.end", function);

    // 初始化语句在外层作用域里执行
    if (this->initial) {
        this->initial->codeGen(ctx);
    }

    ctx.builder.CreateBr(condBlock);

    // 条件
    ctx.builder.SetInsertPoint(condBlock);
    llvm::Value* condition = this->condition ? this->condition->codeGen(ctx) : nullptr;
    if (!condition) {
        return nullptr;
    }
    condition = toBoolean(ctx, condition);
    ctx.builder.CreateCondBr(condition, bodyBlock, endBlock);

    // 循环体
    ctx.builder.SetInsertPoint(bodyBlock);
    ctx.pushScope();
    this->body->codeGen(ctx);
    ctx.popScope();

    // 递增语句
    if (this->increment) {
        this->increment->codeGen(ctx);
    }
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateBr(condBlock);
    }

    ctx.builder.SetInsertPoint(endBlock);

    // 同理，循环体里可能还有嵌套的块，结束块要排在它们后面
    llvm::BasicBlock* lastBlock = &function->back();
    if (lastBlock != endBlock) {
        endBlock->moveAfter(lastBlock);
    }
    return nullptr;
}

llvm::Value* NArrayInitialization::codeGen(CodeGenContext& ctx) {
    this->declaration->codeGen(ctx);
    Symbol* symbol = ctx.findSymbol(this->declaration->id->name);
    if (!symbol) {
        return LogErrorV("未定义的变量：" + this->declaration->id->name);
    }

    Type* intType = ctx.typeSystem.typeByName("int");
    for (size_t i = 0; i < this->elements.size(); ++i) {
        llvm::Value* value = this->elements[i]->codeGen(ctx);
        if (!value) {
            return nullptr;
        }
        value = ctx.typeSystem.cast(value, symbol->type, ctx.builder);
        llvm::Value* index = llvm::ConstantInt::get(intType, i);
        llvm::Value* elementPtr = createGEP(ctx, symbol->type, symbol->arrayBase, {index});
        ctx.builder.CreateStore(value, elementPtr);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 错误处理
// ---------------------------------------------------------------------------

llvm::Value* LogErrorV(const std::string& message) {
    LogError(message);
    return nullptr;
}
