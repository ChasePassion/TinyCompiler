#ifndef TINYCOMPILER_TYPESYSTEM_H
#define TINYCOMPILER_TYPESYSTEM_H

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ASTNodes.h"

// 类型系统：维护「源语言类型名」到「LLVM 类型」的对应关系，以及隐式转换规则。
//
// 源语言支持的内置类型：
//   int(32 位有符号整数) float(单精度) double(双精度)
//   char(8 位整数) bool(1 位) string(char*) void
// 另外还支持用户定义的结构体。
class TypeSystem {
public:
    explicit TypeSystem(llvm::LLVMContext& context);

    // 按类型名查找 LLVM 类型（内置类型或结构体），找不到返回 nullptr
    llvm::Type* typeByName(const std::string& name) const;

    // 类型引用对应的 LLVM 类型。数组在这里按 C 的规则「退化成指向首元素的指针」，
    // 这正是函数形参和返回值里数组应有的类型。
    llvm::Type* llvmType(const NTypeRef& type) const;

    // 数组在栈上的存储类型，例如 int[3][4] -> [12 x i32]
    // （多维展平成一维，和 C 的行主序内存布局一致）
    llvm::Type* arrayStorageType(const NTypeRef& type) const;

    // 结构体
    bool isStruct(const std::string& name) const;
    void addStructType(const std::string& name, llvm::StructType* type);
    void addStructMember(const std::string& structName, const std::string& memberType,
                         const std::string& memberName);
    // 成员在结构体里的下标，找不到返回 -1
    long getStructMemberIndex(const std::string& structName, const std::string& memberName) const;
    // 成员的类型，找不到返回 nullptr
    llvm::Type* structMemberType(const std::string& structName, const std::string& memberName) const;

    // 隐式类型转换。无法转换时打印错误并原样返回。
    llvm::Value* cast(llvm::Value* value, llvm::Type* targetType, llvm::IRBuilder<>& builder);

    // 类型的默认值：整数 0，浮点 0.0
    llvm::Constant* defaultValue(llvm::Type* type);

private:
    llvm::LLVMContext& context_;
    std::map<std::string, llvm::Type*> builtins_;
    std::map<std::string, llvm::StructType*> structs_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> structMembers_;
};

// 把 LLVM 类型翻译成可读的名字，用于错误信息
std::string DescribeType(llvm::Type* type);

#endif // TINYCOMPILER_TYPESYSTEM_H
