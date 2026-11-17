#ifndef TINYCOMPILER_INTERMGEN_H
#define TINYCOMPILER_INTERMGEN_H

#include "ASTNodes.h"
#include "Quadruple.h"
#include "SymbolTable.h"
#include "TypeSystem.h"

// ---------------------------------------------------------------------------
// 教材第 7 章：语义分析和中间代码产生
//
//   7.1 中间语言      —— 三地址代码 / 四元式（见 Quadruple.h）
//   7.2 说明语句      —— 变量的说明，登记到符号表
//   7.3 赋值语句的翻译 —— 变量引用、数组元素引用、记录域引用
//   7.4 布尔表达式的翻译
//   7.5 控制语句的翻译 —— 用标号和跳转四元式
//   7.6 过程调用的处理
//   7.7 类型检查       —— 由 TypeSystem 负责
//
// 这个类把语法树翻译成四元式序列，顺便做静态语义检查
// （变量是否定义、类型是否匹配、作用域是否正确）。
// ---------------------------------------------------------------------------
class IntermGen {
public:
    IntermGen(SymbolTable& symbols, TypeSystem& types);

    // 整个程序 -> 中间代码
    IntermediateCode generate(NBlock& root);

private:
    // 表达式翻译：返回结果所在的符号
    Symbol* translateExpression(NExpression& node);
    // 语句翻译：往当前函数的四元式序列里追加
    void translateStatement(NStatement& node);
    // 函数的翻译
    void translateFunction(NFunctionDeclaration& node);
    // 多维数组的下标换算
    Symbol* translateArrayIndex(NArrayIndex& node, Symbol& array);

    // 新建一个标号（教材 7.5 控制语句的翻译里用）
    int newLabel();

    // 追加一条四元式
    void emit(QuadOp op, Symbol* arg1, Symbol* arg2, Symbol* result,
              int label = -1, int fieldIndex = -1);

    SymbolTable& symbols_;
    TypeSystem& types_;
    IntermediateCode code_;
    FunctionCode* current_ = nullptr;
    int nextLabel_ = 1;
};

#endif // TINYCOMPILER_INTERMGEN_H
