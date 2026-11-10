#ifndef TINYCOMPILER_CODEGEN_H
#define TINYCOMPILER_CODEGEN_H

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ASTNodes.h"
#include "TypeSystem.h"

// 一个变量的运行时信息。
//
// 标量变量：addr 指向存放变量值的栈空间。
// 数组变量：addr 指向整块数组空间，arrayBase 指向首元素（等价于 C 里的数组退化）。
struct Symbol {
    llvm::Value* addr = nullptr;       // 标量：存储地址
    llvm::Value* arrayBase = nullptr;  // 数组：首元素指针
    llvm::Type*  type = nullptr;       // 标量类型 / 数组元素类型
    std::string  typeName;             // 源语言类型名
    std::vector<uint64_t> dims;        // 数组各维长度，空表示不是数组
};

// 作用域：一张「名字 -> 变量信息」的表
class Scope {
public:
    std::map<std::string, Symbol> symbols;
};

// 代码生成上下文：贯穿整个编译过程，维护作用域栈、类型系统和 LLVM 相关对象。
class CodeGenContext {
public:
    llvm::LLVMContext llvmContext;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> theModule;
    TypeSystem typeSystem;

    CodeGenContext();

    // 作用域栈
    void pushScope();
    void popScope();
    // 从内往外查找变量，找不到返回 nullptr
    Symbol* findSymbol(const std::string& name);
    void defineSymbol(const std::string& name, const Symbol& symbol);

    // 当前函数的返回值，函数末尾用它补一个 ret
    void setReturnValue(llvm::Value* value);
    llvm::Value* returnValue();

    // 当前正在生成的函数的返回类型，return 语句要按它做类型转换
    void setFunctionReturnType(llvm::Type* type);
    llvm::Type* currentFunctionReturnType();

    // 生成整个程序的 IR
    void generateCode(NBlock& root);

    // 根据函数声明在 Module 里创建（或复用）函数原型
    llvm::Function* declareFunction(const NFunctionDeclaration& declaration);

private:
    std::vector<Scope> scopes_;
    llvm::Value* returnValue_ = nullptr;
    llvm::Type* functionReturnType_ = nullptr;
};

// 生成 IR 失败时返回 nullptr，并打印错误信息
llvm::Value* LogErrorV(const std::string& message);

#endif // TINYCOMPILER_CODEGEN_H
