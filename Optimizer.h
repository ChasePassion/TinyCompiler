#ifndef TINYCOMPILER_OPTIMIZER_H
#define TINYCOMPILER_OPTIMIZER_H

#include "Quadruple.h"
#include "SymbolTable.h"

// ---------------------------------------------------------------------------
// 教材第 10 章：中间代码优化
//
//   10.3 局部优化
//        10.3.1 基本块优化：合并常量、删除死代码
//        10.3.3 删除公共子表达式
//   10.4 全局优化
//        10.4.2 全局数据流优化和常量传播
//
// 这里做三件事：
//   1. 常量折叠和常量传播：算式的两个操作数都是编译期已知的值时，
//      直接算出结果写进符号表，这条四元式就可以删掉了
//   2. 删除公共子表达式：同一个基本块里算过一次的表达式不再算第二次
//   3. 删除死代码：结果从来没人用的四元式删掉
// ---------------------------------------------------------------------------
class Optimizer {
public:
    explicit Optimizer(SymbolTable& symbols);

    // 优化整个程序的中间代码
    void optimize(IntermediateCode& code);

private:
    void optimizeFunction(FunctionCode& function);

    // 常量折叠 + 常量传播
    bool foldConstants(FunctionCode& function);
    // 删除公共子表达式
    bool removeCommonSubexpressions(FunctionCode& function);
    // 删除死代码（结果没人用）
    bool removeDeadCode(FunctionCode& function);

    // 一个算式是不是可以折叠的
    static bool isComputable(QuadOp op);
    // 算出两个常量的结果
    static double compute(QuadOp op, const Symbol* left, const Symbol* right);

    SymbolTable& symbols_;
};

#endif // TINYCOMPILER_OPTIMIZER_H
