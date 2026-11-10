#include "Optimizer.h"

#include "utils.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

Optimizer::Optimizer(SymbolTable& symbols) : symbols_(symbols) {}

void Optimizer::optimize(IntermediateCode& code) {
    for (FunctionCode& function : code.functions) {
        optimizeFunction(function);
    }
}

void Optimizer::optimizeFunction(FunctionCode& function) {
    // 反复做，直到不再有变化（删除一条四元式可能让别的四元式也能折叠）
    bool changed = true;
    int rounds = 0;
    while (changed && rounds < 10) {
        changed = false;
        changed |= foldConstants(function);
        changed |= removeCommonSubexpressions(function);
        changed |= removeDeadCode(function);
        ++rounds;
    }
}

// ---------------------------------------------------------------------------
// 常量折叠和常量传播
// ---------------------------------------------------------------------------

bool Optimizer::isComputable(QuadOp op) {
    switch (op) {
    case QuadOp::Add: case QuadOp::Sub: case QuadOp::Mul:
    case QuadOp::Div: case QuadOp::Mod:
    case QuadOp::BitAnd: case QuadOp::BitOr: case QuadOp::BitXor:
    case QuadOp::Shl: case QuadOp::Shr:
    case QuadOp::Lt: case QuadOp::Le: case QuadOp::Gt:
    case QuadOp::Ge: case QuadOp::Eq: case QuadOp::Ne:
        return true;
    default:
        return false;
    }
}

double Optimizer::compute(QuadOp op, const Symbol* left, const Symbol* right) {
    double a = left->constantValue;
    double b = right->constantValue;
    bool isFloat = left->constantIsFloat || right->constantIsFloat;

    switch (op) {
    case QuadOp::Add: return a + b;
    case QuadOp::Sub: return a - b;
    case QuadOp::Mul: return a * b;
    case QuadOp::Div: return b == 0 ? 0 : a / b;
    case QuadOp::Mod: return b == 0 ? 0 : std::fmod(a, b);
    case QuadOp::BitAnd: return isFloat ? 0 : static_cast<double>(
        static_cast<int64_t>(a) & static_cast<int64_t>(b));
    case QuadOp::BitOr: return isFloat ? 0 : static_cast<double>(
        static_cast<int64_t>(a) | static_cast<int64_t>(b));
    case QuadOp::BitXor: return isFloat ? 0 : static_cast<double>(
        static_cast<int64_t>(a) ^ static_cast<int64_t>(b));
    case QuadOp::Shl: return isFloat ? 0 : static_cast<double>(
        static_cast<int64_t>(a) << static_cast<int64_t>(b));
    case QuadOp::Shr: return isFloat ? 0 : static_cast<double>(
        static_cast<int64_t>(a) >> static_cast<int64_t>(b));
    // 比较运算的结果是 0 或 1
    case QuadOp::Lt: return isFloat ? (a < b ? 1 : 0) : (a < b ? 1 : 0);
    case QuadOp::Le: return (a <= b) ? 1 : 0;
    case QuadOp::Gt: return (a > b) ? 1 : 0;
    case QuadOp::Ge: return (a >= b) ? 1 : 0;
    case QuadOp::Eq: return (a == b) ? 1 : 0;
    case QuadOp::Ne: return (a != b) ? 1 : 0;
    default: return 0;
    }
}

bool Optimizer::foldConstants(FunctionCode& function) {
    bool changed = false;
    std::vector<Quadruple> kept;
    kept.reserve(function.quads.size());

    for (Quadruple& quad : function.quads) {
        bool foldable = isComputable(quad.op) && quad.arg1 && quad.arg2 &&
                        quad.arg1->hasConstant && quad.arg2->hasConstant && quad.result;
        if (foldable) {
            // 两个操作数都是编译期已知的值，直接算出结果。
            // 结果写进符号表，后面用到这个符号的地方就都知道它的值了
            // —— 这就是常量传播。
            double value = compute(quad.op, quad.arg1, quad.arg2);
            quad.result->hasConstant = true;
            quad.result->constantIsFloat = quad.result->llvmType &&
                                           quad.result->llvmType->isFloatingPointTy();
            quad.result->constantValue = value;
            changed = true;
            continue;   // 这条四元式不用了
        }
        kept.push_back(quad);
    }
    function.quads.swap(kept);
    return changed;
}

// ---------------------------------------------------------------------------
// 删除公共子表达式
// ---------------------------------------------------------------------------

bool Optimizer::removeCommonSubexpressions(FunctionCode& function) {
    bool changed = false;
    // 每个基本块里，已经算过的表达式 -> 它的结果符号
    std::map<std::string, Symbol*> computed;

    std::vector<Quadruple> kept;
    kept.reserve(function.quads.size());

    for (Quadruple& quad : function.quads) {
        // 标号意味着新基本块开始，公共子表达式不能跨基本块
        if (quad.op == QuadOp::Label) {
            computed.clear();
            kept.push_back(quad);
            continue;
        }

        bool reuseable = isComputable(quad.op) && quad.arg1 && quad.arg2 && quad.result;
        if (reuseable) {
            // key 用符号的地址而不是名字：不同作用域里可能有同名的变量，
            // 用名字做 key 会把它们误判成公共子表达式
            std::ostringstream key;
            key << static_cast<int>(quad.op) << " " << quad.arg1 << " " << quad.arg2;
            auto it = computed.find(key.str());
            if (it != computed.end()) {
                // 这个表达式前面算过了，结果直接用前面那个
                quad.result->alias = it->second;
                changed = true;
                continue;   // 这条四元式不用了
            }
            computed[key.str()] = quad.result;
        }
        kept.push_back(quad);
    }
    function.quads.swap(kept);
    return changed;
}

// ---------------------------------------------------------------------------
// 删除死代码
// ---------------------------------------------------------------------------

bool Optimizer::removeDeadCode(FunctionCode& function) {
    // 先数一下每个符号被用了多少次
    std::map<Symbol*, int> useCount;
    for (const Quadruple& quad : function.quads) {
        if (quad.arg1) {
            ++useCount[quad.arg1];
        }
        if (quad.arg2) {
            ++useCount[quad.arg2];
        }
    }

    bool changed = false;
    std::vector<Quadruple> kept;
    kept.reserve(function.quads.size());

    for (Quadruple& quad : function.quads) {
        // 纯计算的四元式，如果结果没人用就可以删掉
        bool pure = quad.op == QuadOp::Add || quad.op == QuadOp::Sub ||
                    quad.op == QuadOp::Mul || quad.op == QuadOp::Div ||
                    quad.op == QuadOp::Mod || quad.op == QuadOp::BitAnd ||
                    quad.op == QuadOp::BitOr || quad.op == QuadOp::BitXor ||
                    quad.op == QuadOp::Shl || quad.op == QuadOp::Shr ||
                    quad.op == QuadOp::Lt || quad.op == QuadOp::Le ||
                    quad.op == QuadOp::Gt || quad.op == QuadOp::Ge ||
                    quad.op == QuadOp::Eq || quad.op == QuadOp::Ne;
        if (pure && quad.result) {
            auto it = useCount.find(quad.result);
            if (it != useCount.end() && it->second == 0) {
                changed = true;
                continue;   // 没人用它的结果，删掉
            }
        }
        kept.push_back(quad);
    }
    function.quads.swap(kept);
    return changed;
}
