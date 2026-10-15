#pragma once

#include <string>
#include <vector>

namespace gis::core {

struct ExpressionNode {
    enum class Op { Number, Variable, Add, Subtract, Multiply, Divide, Negate } op = Op::Number;
    double number = 0.0;
    int variableIndex = -1;
};

// 把表达式编译为逆波兰序列。variableNames 顺序即 evaluateExpression 的 values 顺序。
// 未知变量、非法字符、括号不配对、空表达式均返回 false 并给出可读 error。
// 输入卫生界限同样返回 false 并给出带实际数值与上限的 error：长度 > 1024 字符、
// 或编译产物 > 512 个节点（二者都是输入卫生与逐像元成本口径，不是内存安全：
// 解析与求值都在堆容器上迭代进行，没有递归爆栈风险，故不设嵌套深度上限）。
// 返回 false 时 program 保持为空。
bool compileExpression(const std::string& expression,
                       const std::vector<std::string>& variableNames,
                       std::vector<ExpressionNode>& program,
                       std::string& error);

// 求值。除零返回 0.0 且 evaluationFailed = true（调用方可选择写 NoData）。
// "除零"的确切判据是**绝对**阈值 |除数| < 1e-15（见 expression_engine.cpp 的
// kNearZeroDivisorEpsilon），不是与操作数量级比较的相对判据；前提假定波段量级 ≳1e-14
// （DN / 反射率口径），量级更小的数据会在这个阈值下把条件良好的比值误判为除零。
// 任何失败路径（除零、变量下标越界、栈空或栈不足）返回值恒为 0.0，evaluationFailed 是
// 唯一信号；判据命中后立即停止求值，0.0 不会继续参与后续运算。
double evaluateExpression(const std::vector<ExpressionNode>& program,
                          const std::vector<double>& values,
                          bool& evaluationFailed);

} // namespace gis::core
