#ifndef TINYCOMPILER_QUADRUPLE_H
#define TINYCOMPILER_QUADRUPLE_H

#include "SymbolTable.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 教材第 7 章 7.1.3：三地址代码 —— 四元式
//
//   一个四元式是一个带有四个域的记录结构，这四个域分别称为
//   op、arg1、arg2 及 result：
//
//     op     运算符的内部码
//     arg1   左操作数
//     arg2   右操作数
//     result 结果
//
//   按教材的说法，arg1、arg2 和 result 的内容都是一个指针，
//   指向有关名字的符号表表项（临时变量名也要填入符号表）。
//
//   x := y op z   →  op = op, arg1 = y, arg2 = z, result = x
//   x := -y       →  op = uminus, arg1 = y（不用 arg2 域）
//   跳转语句       →  目标标号放在 result 域里
// ---------------------------------------------------------------------------

enum class QuadOp {
    Assign,       // :=
    Add, Sub, Mul, Div, Mod,
    UMinus,       // 一元负
    BitAnd, BitOr, BitXor, Shl, Shr,
    Lt, Le, Gt, Ge, Eq, Ne,
    VarRef,       // x := y        取变量的值
    ArrayRef,     // x := y[i]     取数组元素
    ArraySet,     // y[i] := x     给数组元素赋值
    FieldRef,     // x := y.f      取结构体的域
    FieldSet,     // y.f := x      给结构体的域赋值
    Param,        // 传递实参
    Call,         // x := f(...)
    Return,       // return x
    Label,        // 定义标号
    Jump,         // goto L
    JumpIfFalse,  // if x == 0 goto L
    JumpIfTrue    // if x != 0 goto L
};

// 算符的名字，调试打印中间代码时用
const char* QuadOpName(QuadOp op);

class Quadruple {
public:
    QuadOp op = QuadOp::Assign;
    Symbol* arg1 = nullptr;
    Symbol* arg2 = nullptr;
    Symbol* result = nullptr;

    // 跳转语句的目标标号。教材里标号是放在 result 域中的整数，
    // 这里单独用一个域表示，含义一样。
    int label = -1;

    // 结构体域的序号（FieldRef 用）
    int fieldIndex = -1;

    std::string toString() const;
};

// 一个函数的中间代码
struct FunctionCode {
    std::string name;
    std::string returnTypeName;
    std::vector<Symbol*> params;
    // 函数里定义的局部变量（教材 9.3.4 地址分配要用）
    std::vector<Symbol*> locals;
    std::vector<Quadruple> quads;
    // 是只有声明（extern）还是真的有函数体
    bool isDefinition = true;
};

// 整个程序的中间代码
class IntermediateCode {
public:
    std::vector<FunctionCode> functions;

    // 按教材的样子打印中间代码
    void dump() const;
};

#endif // TINYCOMPILER_QUADRUPLE_H
