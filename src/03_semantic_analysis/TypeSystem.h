#ifndef TINYCOMPILER_TYPESYSTEM_H
#define TINYCOMPILER_TYPESYSTEM_H

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <map>
#include <string>

// 类型系统：维护「源语言类型名」到「LLVM 类型」的对应关系，以及隐式转换规则。
//
// 源语言支持的内置类型：
//   int(32 位有符号整数) float(单精度) double(双精度)
//   char(8 位整数) bool(1 位) string(char*) void
class TypeSystem {
public:
    explicit TypeSystem(llvm::LLVMContext& context);

    // 按类型名查找 LLVM 类型，找不到返回 nullptr
    llvm::Type* typeByName(const std::string& name) const;

    // 隐式类型转换。无法转换时打印错误并原样返回。
    llvm::Value* cast(llvm::Value* value, llvm::Type* targetType, llvm::IRBuilder<>& builder);

    // 两个类型的统一（教材 7.7 类型检查）：
    //   只要有一边是浮点，统一成 double
    //   两边都是整数时，统一到较宽的那个（至少 32 位）
    llvm::Type* unify(llvm::Type* left, llvm::Type* right) const;

private:
    llvm::LLVMContext& context_;
    std::map<std::string, llvm::Type*> builtins_;
};

// 把 LLVM 类型翻译成可读的名字，用于错误信息
std::string DescribeType(llvm::Type* type);

#endif // TINYCOMPILER_TYPESYSTEM_H
