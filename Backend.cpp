#include "Backend.h"

#include "utils.h"

#include <cstdio>

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

using namespace llvm;

Backend::Backend(CodeGenContext& context) : ctx_(context) {}

CodeGenContext::CodeGenContext()
    : builder(llvmContext), typeSystem(llvmContext) {
    theModule = std::make_unique<llvm::Module>("main", llvmContext);
}

// ---------------------------------------------------------------------------
// 整个程序
// ---------------------------------------------------------------------------

void Backend::generate(const IntermediateCode& code) {
    for (const FunctionCode& function : code.functions) {
        generateFunction(function);
    }
}

void Backend::generateFunction(const FunctionCode& function) {
    // 1) 建立函数原型
    std::vector<Type*> paramTypes;
    for (Symbol* parameter : function.params) {
        paramTypes.push_back(parameter->llvmType);
    }
    Type* returnType = ctx_.typeSystem.typeByName(function.returnTypeName);
    if (!returnType) {
        LogError("未知的返回类型：" + function.returnTypeName);
        return;
    }
    // 外部函数是可变参数的
    bool isVariadic = false;
    if (Symbol* functionSymbol = ctx_.symbols.lookup(function.name)) {
        isVariadic = functionSymbol->isVariadic;
    }
    FunctionType* functionType = FunctionType::get(returnType, paramTypes, isVariadic);

    Function* fn = ctx_.theModule->getFunction(function.name);
    if (!fn) {
        fn = Function::Create(functionType, GlobalValue::ExternalLinkage,
                              function.name, ctx_.theModule.get());
    }

    // 只有声明没有函数体（比如 extern 声明），到此为止
    if (function.quads.empty()) {
        return;
    }

    // 2) 给每个标号先建好基本块（可能有向前跳转）
    labelBlocks_.clear();
    symbolValues_.clear();
    stringGlobals_.clear();
    pendingArguments_.clear();

    entryBlock_ = BasicBlock::Create(ctx_.llvmContext, "entry", fn);
    for (const Quadruple& quad : function.quads) {
        if (quad.op == QuadOp::Label && !labelBlocks_.count(quad.label)) {
            labelBlocks_[quad.label] = BasicBlock::Create(
                ctx_.llvmContext, "L" + std::to_string(quad.label), fn);
        }
    }

    currentFunction_ = fn;
    ctx_.builder.SetInsertPoint(entryBlock_);

    // 3) 形参：分配存储单元并保存实参（教材 9.3.4 地址分配）
    unsigned index = 0;
    for (Symbol* parameter : function.params) {
        Argument* argument = fn->getArg(index++);
        argument->setName(parameter->name);
        if (parameter->isArray()) {
            // 数组形参退化成指针，直接用传进来的指针
            symbolValues_[parameter] = argument;
        } else {
            Value* slot = ctx_.builder.CreateAlloca(parameter->llvmType, nullptr,
                                                    parameter->name.c_str());
            ctx_.builder.CreateStore(argument, slot);
            symbolValues_[parameter] = slot;
        }
    }

    // 4) 局部变量：教材 9.3.4 地址分配
    for (Symbol* local : function.locals) {
        if (symbolValues_.count(local)) {
            continue;
        }
        if (local->isArray()) {
            uint64_t count = 1;
            for (uint64_t dimension : local->dims) {
                count *= dimension;
            }
            Type* arrayType = ArrayType::get(local->llvmType, count);
            Value* slot = ctx_.builder.CreateAlloca(arrayType, nullptr,
                                                    local->name.c_str());
            // 数组变量保存首元素的指针，等价于 C 里数组退化成的指针
            std::vector<Value*> indices{ctx_.builder.getInt32(0), ctx_.builder.getInt32(0)};
            symbolValues_[local] = ctx_.builder.CreateInBoundsGEP(
                arrayType, slot, indices, local->name.c_str());
        } else {
            symbolValues_[local] = ctx_.builder.CreateAlloca(
                local->llvmType, nullptr, local->name.c_str());
        }
    }

    // 5) 翻译四元式
    for (const Quadruple& quad : function.quads) {
        translateQuad(quad);
    }

    // 6) 函数末尾补一条 return
    BasicBlock* last = ctx_.builder.GetInsertBlock();
    if (last && !last->getTerminator()) {
        if (returnType->isVoidTy()) {
            ctx_.builder.CreateRetVoid();
        } else {
            ctx_.builder.CreateRet(Constant::getNullValue(returnType));
        }
    }
    currentFunction_ = nullptr;
}

// ---------------------------------------------------------------------------
// 一条四元式
// ---------------------------------------------------------------------------

void Backend::translateQuad(const Quadruple& quad) {
    switch (quad.op) {
    case QuadOp::Assign: {
        Value* value = valueOf(quad.arg1);
        if (!value) {
            return;
        }
        value = ctx_.typeSystem.cast(value, quad.result->llvmType, ctx_.builder);
        llvm::Value* slot = addressOf(quad.result);
        ctx_.builder.CreateStore(value, slot);
        break;
    }

    case QuadOp::VarRef: {
        Value* value = valueOf(quad.arg1);
        if (!value) {
            return;
        }
        ctx_.builder.CreateStore(value, addressOf(quad.result));
        break;
    }

    case QuadOp::ArrayRef: {
        Value* base = addressOf(quad.arg1);
        Value* index = valueOf(quad.arg2);
        if (!base || !index) {
            return;
        }
        Value* elementPtr = ctx_.builder.CreateInBoundsGEP(
            quad.arg1->llvmType, base, {index}, "elem");
        Value* value = ctx_.builder.CreateLoad(quad.result->llvmType, elementPtr, "elem");
        ctx_.builder.CreateStore(value, addressOf(quad.result));
        break;
    }

    case QuadOp::ArraySet: {
        Value* base = addressOf(quad.result);
        Value* index = valueOf(quad.arg2);
        Value* value = valueOf(quad.arg1);
        if (!base || !index || !value) {
            return;
        }
        value = ctx_.typeSystem.cast(value, quad.result->llvmType, ctx_.builder);
        Value* elementPtr = ctx_.builder.CreateInBoundsGEP(
            quad.result->llvmType, base, {index}, "elem");
        ctx_.builder.CreateStore(value, elementPtr);
        break;
    }

    case QuadOp::FieldRef: {
        Value* base = addressOf(quad.arg1);
        if (!base) {
            return;
        }
        std::vector<Value*> indices{ctx_.builder.getInt32(0),
                                    ctx_.builder.getInt32(quad.fieldIndex)};
        Value* memberPtr = ctx_.builder.CreateInBoundsGEP(
            quad.arg1->llvmType, base, indices, "member");
        Value* value = ctx_.builder.CreateLoad(quad.result->llvmType, memberPtr, "member");
        ctx_.builder.CreateStore(value, addressOf(quad.result));
        break;
    }

    case QuadOp::FieldSet: {
        Value* base = addressOf(quad.result);
        Value* value = valueOf(quad.arg1);
        if (!base || !value) {
            return;
        }
        value = ctx_.typeSystem.cast(value, memberTypeOf(quad), ctx_.builder);
        std::vector<Value*> indices{ctx_.builder.getInt32(0),
                                    ctx_.builder.getInt32(quad.fieldIndex)};
        Value* memberPtr = ctx_.builder.CreateInBoundsGEP(
            quad.result->llvmType, base, indices, "member");
        ctx_.builder.CreateStore(value, memberPtr);
        break;
    }

    case QuadOp::Add: case QuadOp::Sub: case QuadOp::Mul:
    case QuadOp::Div: case QuadOp::Mod:
    case QuadOp::BitAnd: case QuadOp::BitOr: case QuadOp::BitXor:
    case QuadOp::Shl: case QuadOp::Shr: {
        Value* left = valueOf(quad.arg1);
        Value* right = valueOf(quad.arg2);
        if (!left || !right) {
            return;
        }
        // 两个操作数的类型统一（教材 7.7 类型检查）
        Type* target = ctx_.typeSystem.unify(left->getType(), right->getType());
        left = ctx_.typeSystem.cast(left, target, ctx_.builder);
        right = ctx_.typeSystem.cast(right, target, ctx_.builder);

        Value* value = nullptr;
        bool isFloat = target->isFloatingPointTy();
        switch (quad.op) {
        case QuadOp::Add:  value = isFloat ? ctx_.builder.CreateFAdd(left, right, "add")
                                           : ctx_.builder.CreateAdd(left, right, "add"); break;
        case QuadOp::Sub:  value = isFloat ? ctx_.builder.CreateFSub(left, right, "sub")
                                           : ctx_.builder.CreateSub(left, right, "sub"); break;
        case QuadOp::Mul:  value = isFloat ? ctx_.builder.CreateFMul(left, right, "mul")
                                           : ctx_.builder.CreateMul(left, right, "mul"); break;
        case QuadOp::Div:  value = isFloat ? ctx_.builder.CreateFDiv(left, right, "div")
                                           : ctx_.builder.CreateSDiv(left, right, "div"); break;
        case QuadOp::Mod:  value = ctx_.builder.CreateSRem(left, right, "mod"); break;
        case QuadOp::BitAnd: value = ctx_.builder.CreateAnd(left, right, "and"); break;
        case QuadOp::BitOr:  value = ctx_.builder.CreateOr(left, right, "or"); break;
        case QuadOp::BitXor: value = ctx_.builder.CreateXor(left, right, "xor"); break;
        case QuadOp::Shl: value = ctx_.builder.CreateShl(left, right, "shl"); break;
        case QuadOp::Shr: value = ctx_.builder.CreateAShr(left, right, "shr"); break;
        default: break;
        }
        if (value) {
            ctx_.builder.CreateStore(value, addressOf(quad.result));
        }
        break;
    }

    case QuadOp::UMinus: {
        Value* value = valueOf(quad.arg1);
        if (!value) {
            return;
        }
        value = ctx_.typeSystem.cast(value, quad.result->llvmType, ctx_.builder);
        ctx_.builder.CreateStore(ctx_.builder.CreateNeg(value, "neg"),
                                 addressOf(quad.result));
        break;
    }

    // 比较运算：结果扩展成 int，和 C 的语义一致
    case QuadOp::Lt: case QuadOp::Le: case QuadOp::Gt:
    case QuadOp::Ge: case QuadOp::Eq: case QuadOp::Ne: {
        Value* left = valueOf(quad.arg1);
        Value* right = valueOf(quad.arg2);
        if (!left || !right) {
            return;
        }
        Type* target = ctx_.typeSystem.unify(left->getType(), right->getType());
        left = ctx_.typeSystem.cast(left, target, ctx_.builder);
        right = ctx_.typeSystem.cast(right, target, ctx_.builder);

        Value* comparison = nullptr;
        bool isFloat = target->isFloatingPointTy();
        switch (quad.op) {
        case QuadOp::Lt: comparison = isFloat ? ctx_.builder.CreateFCmpOLT(left, right, "cmp")
                                             : ctx_.builder.CreateICmpSLT(left, right, "cmp"); break;
        case QuadOp::Le: comparison = isFloat ? ctx_.builder.CreateFCmpOLE(left, right, "cmp")
                                             : ctx_.builder.CreateICmpSLE(left, right, "cmp"); break;
        case QuadOp::Gt: comparison = isFloat ? ctx_.builder.CreateFCmpOGT(left, right, "cmp")
                                             : ctx_.builder.CreateICmpSGT(left, right, "cmp"); break;
        case QuadOp::Ge: comparison = isFloat ? ctx_.builder.CreateFCmpOGE(left, right, "cmp")
                                             : ctx_.builder.CreateICmpSGE(left, right, "cmp"); break;
        case QuadOp::Eq: comparison = isFloat ? ctx_.builder.CreateFCmpOEQ(left, right, "cmp")
                                             : ctx_.builder.CreateICmpEQ(left, right, "cmp"); break;
        case QuadOp::Ne: comparison = isFloat ? ctx_.builder.CreateFCmpONE(left, right, "cmp")
                                             : ctx_.builder.CreateICmpNE(left, right, "cmp"); break;
        default: break;
        }
        if (comparison) {
            // 比较的结果是 1 位，扩展成 int
            Value* extended = ctx_.builder.CreateZExt(comparison, quad.result->llvmType, "bool");
            ctx_.builder.CreateStore(extended, addressOf(quad.result));
        }
        break;
    }

    case QuadOp::Param:
        pendingArguments_.push_back(quad.arg1);
        break;

    case QuadOp::Call: {
        Function* callee = ctx_.theModule->getFunction(quad.arg1->name);
        if (!callee) {
            LogError("找不到函数：" + quad.arg1->name);
            pendingArguments_.clear();
            return;
        }
        std::vector<Value*> arguments;
        unsigned index = 0;
        for (Symbol* argument : pendingArguments_) {
            Value* value = valueOf(argument);
            if (!value) {
                pendingArguments_.clear();
                return;
            }
            if (index < callee->arg_size()) {
                value = ctx_.typeSystem.cast(value, callee->getArg(index)->getType(), ctx_.builder);
            }
            arguments.push_back(value);
            ++index;
        }
        pendingArguments_.clear();

        Value* call = ctx_.builder.CreateCall(callee, arguments, "call");
        if (quad.result) {
            ctx_.builder.CreateStore(call, addressOf(quad.result));
        }
        break;
    }

    case QuadOp::Return: {
        Value* value = valueOf(quad.arg1);
        if (!value) {
            return;
        }
        if (currentFunction_ && !currentFunction_->getReturnType()->isVoidTy()) {
            value = ctx_.typeSystem.cast(value, currentFunction_->getReturnType(), ctx_.builder);
        }
        ctx_.builder.CreateRet(value);
        break;
    }

    case QuadOp::Label: {
        BasicBlock* block = blockOf(quad.label);
        if (block) {
            // 切到标号对应的块之前，先给当前块补一条跳转
            // （当前块可能还没有终结指令，比如 else 分支的末尾）
            BasicBlock* current = ctx_.builder.GetInsertBlock();
            if (current && !current->getTerminator()) {
                ctx_.builder.CreateBr(block);
            }
            ctx_.builder.SetInsertPoint(block);
        }
        break;
    }

    case QuadOp::Jump: {
        BasicBlock* block = blockOf(quad.label);
        if (block && !ctx_.builder.GetInsertBlock()->getTerminator()) {
            ctx_.builder.CreateBr(block);
        }
        break;
    }

    case QuadOp::JumpIfFalse:
    case QuadOp::JumpIfTrue: {
        Value* condition = valueOf(quad.arg1);
        if (!condition) {
            return;
        }
        BasicBlock* current = ctx_.builder.GetInsertBlock();
        if (current->getTerminator()) {
            return;
        }
        BasicBlock* target = blockOf(quad.label);
        BasicBlock* fallthrough = BasicBlock::Create(ctx_.llvmContext, "cont", currentFunction_);
        // 分支的条件必须是 i1。比较运算的结果是 int，这里转一下
        Value* test = condition;
        if (!test->getType()->isIntegerTy(1)) {
            test = ctx_.builder.CreateICmpNE(test, ConstantInt::get(test->getType(), 0), "tobool");
        }
        if (quad.op == QuadOp::JumpIfFalse) {
            ctx_.builder.CreateCondBr(test, fallthrough, target);
        } else {
            ctx_.builder.CreateCondBr(test, target, fallthrough);
        }
        ctx_.builder.SetInsertPoint(fallthrough);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// 取符号的值和地址
// ---------------------------------------------------------------------------

llvm::Value* Backend::valueOf(Symbol* symbol) {
    if (!symbol) {
        return nullptr;
    }
    // 别名：公共子表达式被合并后指向原来那个符号
    if (symbol->alias) {
        return valueOf(symbol->alias);
    }
    // 常量：编译期就知道的值
    if (symbol->hasConstant) {
        if (symbol->constantIsFloat) {
            return llvm::ConstantFP::get(symbol->llvmType, symbol->constantValue);
        }
        return llvm::ConstantInt::get(symbol->llvmType,
                                      static_cast<uint64_t>(
                                          static_cast<int64_t>(symbol->constantValue)),
                                      true);
    }
    // 字符串常量
    if (symbol->kind == SymbolKind::Constant && symbol->isString) {
        return stringConstant(symbol);
    }
    llvm::Value* slot = addressOf(symbol);
    if (!slot) {
        return nullptr;
    }
    if (symbol->isArray()) {
        // 数组变量的「值」就是指向首元素的指针（数组退化），不需要再读一次
        return slot;
    }
    return ctx_.builder.CreateLoad(symbol->llvmType, slot, symbol->name.c_str());
}

llvm::Value* Backend::addressOf(Symbol* symbol) {
    if (!symbol) {
        return nullptr;
    }
    if (symbol->alias) {
        return addressOf(symbol->alias);
    }
    auto it = symbolValues_.find(symbol);
    if (it != symbolValues_.end()) {
        return it->second;
    }
    return allocateTemporary(symbol);
}

// 临时变量的存储单元放在函数入口（教材 9.3.5 临时变量的地址分配）。
// 放在入口块的最前面：mem2reg 要求 alloca 在入口块里才能被优化掉。
llvm::Value* Backend::allocateTemporary(Symbol* symbol) {
    llvm::BasicBlock* current = ctx_.builder.GetInsertBlock();
    llvm::BasicBlock::iterator point = ctx_.builder.GetInsertPoint();
    if (entryBlock_) {
        if (entryBlock_->empty()) {
            ctx_.builder.SetInsertPoint(entryBlock_);
        } else {
            ctx_.builder.SetInsertPoint(entryBlock_, entryBlock_->begin());
        }
    }
    llvm::Value* slot = ctx_.builder.CreateAlloca(symbol->llvmType, nullptr,
                                                  symbol->name.c_str());
    if (current) {
        ctx_.builder.SetInsertPoint(current, point);
    }
    symbolValues_[symbol] = slot;
    return slot;
}

llvm::Value* Backend::stringConstant(Symbol* symbol) {
    auto it = stringGlobals_.find(symbol);
    if (it != stringGlobals_.end()) {
        return it->second;
    }
    llvm::Value* global = ctx_.builder.CreateGlobalString(symbol->stringValue, "str");
    stringGlobals_[symbol] = global;
    return global;
}

llvm::Type* Backend::memberTypeOf(const Quadruple& quad) {
    Type* type = quad.result->llvmType;
    if (!type || !type->isStructTy()) {
        return nullptr;
    }
    auto* structType = llvm::cast<llvm::StructType>(type);
    if (quad.fieldIndex < 0 ||
        static_cast<unsigned>(quad.fieldIndex) >= structType->getNumElements()) {
        return nullptr;
    }
    return structType->getStructElementType(quad.fieldIndex);
}

llvm::BasicBlock* Backend::blockOf(int label) {
    auto it = labelBlocks_.find(label);
    if (it != labelBlocks_.end()) {
        return it->second;
    }
    llvm::BasicBlock* block = llvm::BasicBlock::Create(
        ctx_.llvmContext, "L" + std::to_string(label), currentFunction_);
    labelBlocks_[label] = block;
    return block;
}
