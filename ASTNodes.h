#ifndef TINYCOMPILER_ASTNODES_H
#define TINYCOMPILER_ASTNODES_H


#include <cstdint>
#include <memory>
#include <string>
#include <vector>


// 二元运算符。用枚举而不是 bison 的 token 编号，好处是语法树节点不依赖
// 语法分析器生成的头文件，AST 可以独立地打印、序列化、单元测试。
enum BinOp {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE
};

// 运算符的符号形式，给 AST 打印用
std::string OpName(BinOp op);

// ---------------------------------------------------------------------------
// AST 节点基类
//
// 所有语法树节点都继承自 Node。基类统一提供 print()（文本形式）和 toJson()
// （AST 可视化用），子类一般只需要实现其中的一两个方法：
//
//   label()         节点在树上的显示文本，必须实现
//   children()      子节点列表，可选，默认空
// ---------------------------------------------------------------------------
// 抽象语法树只描述语法，不涉及语义。翻译成中间代码是 IntermGen 的事
// （教材第 7 章的语法制导翻译），所以这里不依赖 LLVM。
// ---------------------------------------------------------------------------
class Node {
public:
    virtual ~Node() = default;

    virtual std::string label() const = 0;
    virtual std::vector<Node*> children() const { return {}; }

    // 按 prefix 缩进打印整棵子树
    void print(const std::string& prefix = "") const;
    // 输出 JSON，供 visualization/ 下的脚本绘制语法树
    std::string toJson() const;
};

// ---------------------------------------------------------------------------
// 类型引用
//
// int / double / float / char / bool / string / void
// struct Point
// int[3][4]
//
// 数组维度存放在这里，变量本身（NIdentifier）不再需要 isType / isArray 标志位。
// ---------------------------------------------------------------------------
class NTypeRef : public Node {
public:
    std::string name;              // 类型名
    std::vector<uint64_t> dims;    // 数组各维长度；为空表示不是数组

    NTypeRef() = default;
    explicit NTypeRef(std::string n) : name(std::move(n)) {}

    std::string label() const override {
        std::string s = name;
        for (uint64_t d : dims) {
            s += "[" + std::to_string(d) + "]";
        }
        return s;
    }

    bool isArray() const { return !dims.empty(); }
};

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------
class NExpression : public Node {
public:
    std::string label() const override { return "NExpression"; }
};

// 整数字面量
class NInteger : public NExpression {
public:
    int64_t value = 0;

    NInteger() = default;
    explicit NInteger(int64_t v) : value(v) {}

    std::string label() const override {
        return "NInteger:" + std::to_string(value);
    }
};

// 浮点字面量
class NDouble : public NExpression {
public:
    double value = 0.0;

    NDouble() = default;
    explicit NDouble(double v) : value(v) {}

    std::string label() const override {
        return "NDouble:" + std::to_string(value);
    }
};

// 字符串字面量
class NLiteral : public NExpression {
public:
    std::string value;

    NLiteral() = default;
    explicit NLiteral(std::string v) : value(std::move(v)) {}

    std::string label() const override {
        return "NLiteral:" + value;
    }
};

// 标识符（变量名 / 函数名 / 结构体成员名）
class NIdentifier : public NExpression {
public:
    std::string name;

    NIdentifier() = default;
    explicit NIdentifier(std::string n) : name(std::move(n)) {}

    std::string label() const override {
        return "NIdentifier:" + name;
    }
};

// 二元运算
class NBinaryOperator : public NExpression {
public:
    BinOp op = OP_ADD;
    std::shared_ptr<NExpression> lhs;
    std::shared_ptr<NExpression> rhs;

    NBinaryOperator() = default;
    NBinaryOperator(std::shared_ptr<NExpression> l, BinOp o, std::shared_ptr<NExpression> r)
        : lhs(std::move(l)), rhs(std::move(r)), op(o) {}

    std::string label() const override {
        return "NBinaryOperator:" + OpName(op);
    }
    std::vector<Node*> children() const override {
        return {lhs.get(), rhs.get()};
    }
};

// 赋值 a = b
class NAssignment : public NExpression {
public:
    std::shared_ptr<NIdentifier> lhs;
    std::shared_ptr<NExpression> rhs;

    NAssignment() = default;
    NAssignment(std::shared_ptr<NIdentifier> l, std::shared_ptr<NExpression> r)
        : lhs(std::move(l)), rhs(std::move(r)) {}

    std::string label() const override { return "NAssignment"; }
    std::vector<Node*> children() const override {
        return {lhs.get(), rhs.get()};
    }
};

// 函数调用
class NMethodCall : public NExpression {
public:
    std::shared_ptr<NIdentifier> id;
    std::vector<std::shared_ptr<NExpression>> args;

    NMethodCall() = default;
    NMethodCall(std::shared_ptr<NIdentifier> i, std::vector<std::shared_ptr<NExpression>> a)
        : id(std::move(i)), args(std::move(a)) {}

    std::string label() const override { return "NMethodCall"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        out.push_back(id.get());
        for (auto& a : args) {
            out.push_back(a.get());
        }
        return out;
    }
};

// 结构体成员访问 p.x
class NStructMember : public NExpression {
public:
    std::shared_ptr<NIdentifier> id;      // 结构体变量
    std::shared_ptr<NIdentifier> member;  // 成员名

    NStructMember() = default;
    NStructMember(std::shared_ptr<NIdentifier> i, std::shared_ptr<NIdentifier> m)
        : id(std::move(i)), member(std::move(m)) {}

    std::string label() const override { return "NStructMember"; }
    std::vector<Node*> children() const override {
        return {id.get(), member.get()};
    }
};

// 数组元素 arr[i][j]
class NArrayIndex : public NExpression {
public:
    std::shared_ptr<NIdentifier> arrayName;
    std::vector<std::shared_ptr<NExpression>> exprs;

    NArrayIndex() = default;
    NArrayIndex(std::shared_ptr<NIdentifier> n, std::vector<std::shared_ptr<NExpression>> e)
        : arrayName(std::move(n)), exprs(std::move(e)) {}

    std::string label() const override { return "NArrayIndex"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        out.push_back(arrayName.get());
        for (auto& e : exprs) {
            out.push_back(e.get());
        }
        return out;
    }
};

// 数组元素赋值 arr[i] = v
class NArrayAssignment : public NExpression {
public:
    std::shared_ptr<NArrayIndex> index;
    std::shared_ptr<NExpression> expression;

    NArrayAssignment() = default;
    NArrayAssignment(std::shared_ptr<NArrayIndex> i, std::shared_ptr<NExpression> e)
        : index(std::move(i)), expression(std::move(e)) {}

    std::string label() const override { return "NArrayAssignment"; }
    std::vector<Node*> children() const override {
        return {index.get(), expression.get()};
    }
};

// 结构体成员赋值 p.x = v
class NStructAssignment : public NExpression {
public:
    std::shared_ptr<NStructMember> member;
    std::shared_ptr<NExpression> expression;

    NStructAssignment() = default;
    NStructAssignment(std::shared_ptr<NStructMember> m, std::shared_ptr<NExpression> e)
        : member(std::move(m)), expression(std::move(e)) {}

    std::string label() const override { return "NStructAssignment"; }
    std::vector<Node*> children() const override {
        return {member.get(), expression.get()};
    }
};

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------
class NStatement : public Node {
public:
    std::string label() const override { return "NStatement"; }
};

// 语句块 { ... }
class NBlock : public NStatement {
public:
    std::vector<std::shared_ptr<NStatement>> statements;

    NBlock() = default;
    explicit NBlock(std::vector<std::shared_ptr<NStatement>> s)
        : statements(std::move(s)) {}

    std::string label() const override { return "NBlock"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        for (auto& s : statements) {
            out.push_back(s.get());
        }
        return out;
    }
};

// 表达式语句
class NExpressionStatement : public NStatement {
public:
    std::shared_ptr<NExpression> expression;

    NExpressionStatement() = default;
    explicit NExpressionStatement(std::shared_ptr<NExpression> e)
        : expression(std::move(e)) {}

    std::string label() const override { return "NExpressionStatement"; }
    std::vector<Node*> children() const override {
        return {expression.get()};
    }
};

// 变量声明 int a = 1
class NVariableDeclaration : public NStatement {
public:
    std::shared_ptr<NTypeRef> type;
    std::shared_ptr<NIdentifier> id;
    std::shared_ptr<NExpression> init;

    NVariableDeclaration() = default;
    NVariableDeclaration(std::shared_ptr<NTypeRef> t, std::shared_ptr<NIdentifier> i,
                         std::shared_ptr<NExpression> in = nullptr)
        : type(std::move(t)), id(std::move(i)), init(std::move(in)) {}

    std::string label() const override { return "NVariableDeclaration"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out{type.get(), id.get()};
        if (init) {
            out.push_back(init.get());
        }
        return out;
    }
};

// 函数声明 / 定义
class NFunctionDeclaration : public NStatement {
public:
    std::shared_ptr<NTypeRef> type;
    std::shared_ptr<NIdentifier> id;
    std::vector<std::shared_ptr<NVariableDeclaration>> params;
    std::shared_ptr<NBlock> body;
    bool isExtern = false;

    NFunctionDeclaration() = default;
    NFunctionDeclaration(std::shared_ptr<NTypeRef> t, std::shared_ptr<NIdentifier> i,
                         std::vector<std::shared_ptr<NVariableDeclaration>> p,
                         std::shared_ptr<NBlock> b, bool ext = false)
        : type(std::move(t)), id(std::move(i)), params(std::move(p)),
          body(std::move(b)), isExtern(ext) {}

    std::string label() const override { return "NFunctionDeclaration"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out{type.get(), id.get()};
        for (auto& p : params) {
            out.push_back(p.get());
        }
        if (body) {
            out.push_back(body.get());
        }
        return out;
    }
};

// 结构体声明 struct Point { ... }
class NStructDeclaration : public NStatement {
public:
    std::shared_ptr<NIdentifier> name;
    std::vector<std::shared_ptr<NVariableDeclaration>> members;

    NStructDeclaration() = default;
    NStructDeclaration(std::shared_ptr<NIdentifier> n,
                       std::vector<std::shared_ptr<NVariableDeclaration>> m)
        : name(std::move(n)), members(std::move(m)) {}

    std::string label() const override { return "NStructDeclaration"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        out.push_back(name.get());
        for (auto& m : members) {
            out.push_back(m.get());
        }
        return out;
    }
};

// return 表达式
class NReturnStatement : public NStatement {
public:
    std::shared_ptr<NExpression> expression;

    NReturnStatement() = default;
    explicit NReturnStatement(std::shared_ptr<NExpression> e)
        : expression(std::move(e)) {}

    std::string label() const override { return "NReturnStatement"; }
    std::vector<Node*> children() const override {
        return {expression.get()};
    }
};

// if / else
class NIfStatement : public NStatement {
public:
    std::shared_ptr<NExpression> condition;
    std::shared_ptr<NBlock> trueBlock;
    std::shared_ptr<NBlock> falseBlock;   // 可以为空

    NIfStatement() = default;
    NIfStatement(std::shared_ptr<NExpression> c, std::shared_ptr<NBlock> t,
                 std::shared_ptr<NBlock> f = nullptr)
        : condition(std::move(c)), trueBlock(std::move(t)), falseBlock(std::move(f)) {}

    std::string label() const override { return "NIfStatement"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out{condition.get(), trueBlock.get()};
        if (falseBlock) {
            out.push_back(falseBlock.get());
        }
        return out;
    }
};

// for / while（while 会被改写成没有 initial 和 increment 的 for）
class NForStatement : public NStatement {
public:
    std::shared_ptr<NExpression> initial;
    std::shared_ptr<NExpression> condition;
    std::shared_ptr<NExpression> increment;
    std::shared_ptr<NBlock> body;

    NForStatement() = default;
    NForStatement(std::shared_ptr<NBlock> b, std::shared_ptr<NExpression> i,
                  std::shared_ptr<NExpression> c, std::shared_ptr<NExpression> inc)
        : body(std::move(b)), initial(std::move(i)), condition(std::move(c)),
          increment(std::move(inc)) {}

    std::string label() const override { return "NForStatement"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        if (initial) {
            out.push_back(initial.get());
        }
        if (condition) {
            out.push_back(condition.get());
        }
        if (increment) {
            out.push_back(increment.get());
        }
        out.push_back(body.get());
        return out;
    }
};

// 数组初始化 int[3] a = [1, 2, 3]
class NArrayInitialization : public NStatement {
public:
    std::shared_ptr<NVariableDeclaration> declaration;
    std::vector<std::shared_ptr<NExpression>> elements;

    NArrayInitialization() = default;
    NArrayInitialization(std::shared_ptr<NVariableDeclaration> d,
                         std::vector<std::shared_ptr<NExpression>> e)
        : declaration(std::move(d)), elements(std::move(e)) {}

    std::string label() const override { return "NArrayInitialization"; }
    std::vector<Node*> children() const override {
        std::vector<Node*> out;
        out.push_back(declaration.get());
        for (auto& e : elements) {
            out.push_back(e.get());
        }
        return out;
    }
};

#endif // TINYCOMPILER_ASTNODES_H
