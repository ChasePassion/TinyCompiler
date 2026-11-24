#include "TypeSystem.h"

#include "utils.h"

#include <algorithm>

using namespace llvm;

TypeSystem::TypeSystem(LLVMContext& context) : context_(context) {
    builtins_["int"]    = Type::getInt32Ty(context);
    builtins_["string"] = PointerType::getUnqual(Type::getInt8Ty(context));
    builtins_["void"]   = Type::getVoidTy(context);
}

Type* TypeSystem::typeByName(const std::string& name) const {
    auto builtin = builtins_.find(name);
    if (builtin != builtins_.end()) {
        return builtin->second;
    }
    return nullptr;
}

Value* TypeSystem::cast(Value* value, Type* targetType, IRBuilder<>& builder) {
    if (!value || !targetType) {
        return value;
    }
    Type* from = value->getType();
    if (from == targetType) {
        return value;
    }

    // 整数 -> 整数：位宽变窄用 trunc，变宽用 sext
    if (from->isIntegerTy() && targetType->isIntegerTy()) {
        unsigned fromBits = from->getIntegerBitWidth();
        unsigned toBits = targetType->getIntegerBitWidth();
        if (fromBits > toBits) {
            return builder.CreateTrunc(value, targetType, "cast");
        }
        return builder.CreateSExt(value, targetType, "cast");
    }

    LogError("无法把 " + DescribeType(from) + " 转换成 " + DescribeType(targetType));
    return value;
}

Type* TypeSystem::unify(Type* left, Type* right) const {
    if (!left || !right) {
        return left ? left : right;
    }
    if (left == right) {
        return left;
    }
    if (left->isIntegerTy() && right->isIntegerTy()) {
        unsigned width = std::max(left->getIntegerBitWidth(), right->getIntegerBitWidth());
        if (width < 32) {
            width = 32;
        }
        return Type::getIntNTy(context_, width);
    }
    return left;
}

std::string DescribeType(Type* type) {
    if (!type) {
        return "<空>";
    }
    switch (type->getTypeID()) {
    case Type::VoidTyID:    return "void";
    case Type::IntegerTyID: return "int" + std::to_string(type->getIntegerBitWidth());
    case Type::PointerTyID: return "pointer";
    case Type::ArrayTyID:   return "array";
    default:                return "unknown";
    }
}
