#ifndef TINYCOMPILER_SYMBOLTABLE_H
#define TINYCOMPILER_SYMBOLTABLE_H

#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 教材第 8 章：符号表
//
// 8.1 符号表的作用：登记源程序中出现的每个名字，以及名字的各种属性
//     （是常量名、变量名还是过程名；类型是什么、占多少内存、地址是什么）
// 8.3 名字的作用范围：用「作用域链」组织，查找时从最内层往外找
// 8.4 符号表的内容：本文件里的 Symbol 类
// ---------------------------------------------------------------------------

// 名字的种类
enum class SymbolKind {
    Variable,   // 变量
    Function,   // 函数（过程）
    TypeName,   // 类型名
    Temporary,  // 编译期引进的临时变量，也就是教材四元式里的 T1、T2
    Constant    // 字面量常量
};

// 符号表的一个表项：一个名字的各种属性
class Symbol {
public:
    std::string name;
    SymbolKind kind = SymbolKind::Variable;

    // 类型：源语言里的类型名（"int" / "Point"），以及对应的 LLVM 类型
    std::string typeName;
    llvm::Type* llvmType = nullptr;

    // 数组各维长度；为空表示不是数组
    std::vector<uint64_t> dims;

    // 字面量常量的值。优化器用它做常量折叠和常量传播（教材第 10 章）。
    bool hasConstant = false;
    bool constantIsFloat = false;
    double constantValue = 0.0;

    // 字符串字面量的内容
    std::string stringValue;
    // 是不是字符串字面量（空字符串也是）
    bool isString = false;

    // 外部函数按可变参数处理（printf 这类）
    bool isVariadic = false;

    // 别名：公共子表达式被合并后，这个符号指向原来那个符号
    Symbol* alias = nullptr;

    bool isArray() const { return !dims.empty(); }
};

// 符号表：按作用域组织成一条链
class SymbolTable {
public:
    SymbolTable();

    // 进入 / 退出一层作用域
    void pushScope();
    void popScope();

    // 在当前作用域里登记一个名字。已经存在同名的话返回原有的表项。
    Symbol* define(const std::string& name, SymbolKind kind);

    // 引进一个临时变量 T1、T2……
    Symbol* newTemporary(SymbolKind kind = SymbolKind::Temporary);

    // 查找名字：从最内层作用域往外找，符合 C 的遮蔽规则
    Symbol* lookup(const std::string& name);

private:
    // 每一层作用域一张表：名字 -> 表项
    //
    // 表项本身放在 arena_ 里，不会随作用域退出而销毁。四元式里存的就是
    // 指向表项的指针（教材第 7 章 7.1.3 的说法），所以表项必须活得比
    // 四元式序列长。
    std::vector<std::map<std::string, Symbol*>> scopes_;
    std::vector<std::unique_ptr<Symbol>> arena_;
    int nextTempIndex_ = 1;
};

#endif // TINYCOMPILER_SYMBOLTABLE_H
