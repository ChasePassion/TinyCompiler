#include "TypeSystem.h"

#include "utils.h"

#include <llvm/IR/Constants.h>

using namespace llvm;

TypeSystem::TypeSystem(LLVMContext& context) : context_(context) {
    builtins_["int"]    = Type::getInt32Ty(context);
    builtins_["float"]  = Type::getFloatTy(context);
    builtins_["double"] = Type::getDoubleTy(context);
    builtins_["char"]   = Type::getInt8Ty(context);
    builtins_["bool"]   = Type::getInt1Ty(context);
    builtins_["string"] = PointerType::getUnqual(Type::getInt8Ty(context));
    builtins_["void"]   = Type::getVoidTy(context);
}

Type* TypeSystem::typeByName(const std::string& name) const {
    auto builtin = builtins_.find(name);
    if (builtin != builtins_.end()) {
        return builtin->second;
    }
    auto structIt = structs_.find(name);
    if (structIt != structs_.end()) {
        return structIt->second;
    }
    return nullptr;
}

void TypeSystem::addStructType(const std::string& name, StructType* type) {
    structs_[name] = type;
    structMembers_[name].clear();
}

void TypeSystem::addStructMember(const std::string& structName,
                                  const std::string& memberType,
                                  const std::string& memberName) {
    structMembers_[structName].push_back(std::make_pair(memberType, memberName));
}

long TypeSystem::getStructMemberIndex(const std::string& structName,
                                       const std::string& memberName) const {
    auto it = structMembers_.find(structName);
    if (it == structMembers_.end()) {
        return -1;
    }
    const auto& members = it->second;
    for (size_t i = 0; i < members.size(); ++i) {
        if (members[i].second == memberName) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

Value* TypeSystem::cast(Value* value, Type* targetType, IRBuilder<>& builder) {
    if (!value || !targetType) {
        return value;
    }
    Type* from = value->getType();
    if (from == targetType) {
        return value;
    }

    // 整数 -> 整数
    if (from->isIntegerTy() && targetType->isIntegerTy()) {
        if (targetType->isIntegerTy(1)) {
            // 转成 bool：非 0 即为真。
            // 注意不能直接截断成 1 位，那样 2 会变成 0（假）。
            return builder.CreateICmpNE(value, ConstantInt::get(from, 0), "tobool");
        }
        unsigned fromBits = from->getIntegerBitWidth();
        unsigned toBits = targetType->getIntegerBitWidth();
        if (fromBits > toBits) {
            return builder.CreateTrunc(value, targetType, "cast");
        }
        return builder.CreateSExt(value, targetType, "cast");
    }

    // 整数 -> 浮点
    if (from->isIntegerTy() && targetType->isFloatingPointTy()) {
        return builder.CreateSIToFP(value, targetType, "cast");
    }

    // 浮点 -> 整数
    if (from->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder.CreateFPToSI(value, targetType, "cast");
    }

    // 浮点 -> 浮点
    if (from->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        bool fromIsDouble = (from == builtins_["double"]);
        bool toIsDouble = (targetType == builtins_["double"]);
        if (fromIsDouble && !toIsDouble) {
            return builder.CreateFPTrunc(value, targetType, "cast");
        }
        if (!fromIsDouble && toIsDouble) {
            return builder.CreateFPExt(value, targetType, "cast");
        }
        return value;
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
    if (left->isFloatingPointTy() || right->isFloatingPointTy()) {
        return Type::getDoubleTy(context_);
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
    case Type::HalfTyID:    return "half";
    case Type::FloatTyID:   return "float";
    case Type::DoubleTyID:  return "double";
    case Type::IntegerTyID: return "int" + std::to_string(type->getIntegerBitWidth());
    case Type::PointerTyID: return "pointer";
    case Type::ArrayTyID:   return "array";
    case Type::StructTyID:  return type->getStructName().str();
    default:                return "unknown";
    }
}
