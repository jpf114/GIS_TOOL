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
bool compileExpression(const std::string& expression,
                       const std::vector<std::string>& variableNames,
                       std::vector<ExpressionNode>& program,
                       std::string& error);

// 求值。除零返回 0.0 且 evaluationFailed = true（调用方可选择写 NoData）。
double evaluateExpression(const std::vector<ExpressionNode>& program,
                          const std::vector<double>& values,
                          bool& evaluationFailed);

} // namespace gis::core
