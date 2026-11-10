#ifndef TINYCOMPILER_BACKEND_H
#define TINYCOMPILER_BACKEND_H

#include "Quadruple.h"
#include "SymbolTable.h"
#include "TypeSystem.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 编译过程中共用的数据。
//
// 教材第 1 章图 1.1 里画的是「表格管理」，这里除了 LLVM 侧的对象之外，
// 还挂着符号表（第 8 章）和类型系统（第 7 章 7.7）。
// ---------------------------------------------------------------------------
class CodeGenContext {
public:
    llvm::LLVMContext llvmContext;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> theModule;
    TypeSystem typeSystem;
    SymbolTable symbols;

    CodeGenContext();
};

// ---------------------------------------------------------------------------
// 目标代码生成器：把中间代码（四元式）翻译成 LLVM IR。
//
// 对应教材图 1.1 里最右边那个模块。
// ---------------------------------------------------------------------------
class Backend {
public:
    explicit Backend(CodeGenContext& context);

    // 整个程序的中间代码 -> LLVM IR
    void generate(const IntermediateCode& code);

private:
    // 一个函数的四元式 -> LLVM IR
    void generateFunction(const FunctionCode& function);
    // 一条四元式
    void translateQuad(const Quadruple& quad);

    // 取符号的值：常量直接返回常量，其它从存储单元里读
    llvm::Value* valueOf(Symbol* symbol);
    // 取符号的存储位置
    llvm::Value* addressOf(Symbol* symbol);
    // 临时变量的存储单元（教材 9.3.5 临时变量的地址分配）
    llvm::Value* allocateTemporary(Symbol* symbol);
    // 字符串常量对应的全局变量
    llvm::Value* stringConstant(Symbol* symbol);
    // 标号对应的基本块
    llvm::BasicBlock* blockOf(int label);
    // 结构体成员的类型
    llvm::Type* memberTypeOf(const Quadruple& quad);

    CodeGenContext& ctx_;
    llvm::Function* currentFunction_ = nullptr;
    llvm::BasicBlock* entryBlock_ = nullptr;

    // 每个符号在 IR 里的存储位置
    std::map<Symbol*, llvm::Value*> symbolValues_;
    // 每个标号对应的基本块
    std::map<int, llvm::BasicBlock*> labelBlocks_;
    // 字符串常量对应的全局变量
    std::map<Symbol*, llvm::Value*> stringGlobals_;
    // 攒起来的实参，遇到 Call 时用掉
    std::vector<Symbol*> pendingArguments_;
};

#endif // TINYCOMPILER_BACKEND_H
