#include <gis/core/expression_engine.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stack>

namespace gis::core {
namespace {

int precedence(ExpressionNode::Op op) {
    switch (op) {
        case ExpressionNode::Op::Add:
        case ExpressionNode::Op::Subtract: return 1;
        case ExpressionNode::Op::Multiply:
        case ExpressionNode::Op::Divide: return 2;
        case ExpressionNode::Op::Negate: return 3;
        default: return 0;
    }
}

bool isVariableStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isVariableChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

} // namespace

bool compileExpression(const std::string& expression,
                       const std::vector<std::string>& variableNames,
                       std::vector<ExpressionNode>& program,
                       std::string& error) {
    program.clear();
    error.clear();
    if (expression.empty()) {
        error = "表达式为空";
        return false;
    }

    std::stack<ExpressionNode::Op> ops;
    std::vector<ExpressionNode> output;
    bool expectOperand = true;
    size_t i = 0;
    int depth = 0;

    auto findVariable = [&](const std::string& name) -> int {
        for (size_t k = 0; k < variableNames.size(); ++k) {
            if (variableNames[k] == name) return static_cast<int>(k);
        }
        return -1;
    };

    // 运算符入栈前先弹出"优先级更高"或"同优先级且左结合"的栈顶运算符。
    // 一元负号是唯一的右结合运算符：栈顶与待压入者同为 Negate 时不弹出，
    // 于是 --B1 产出 [B1, Negate, Negate]（自内向外求值 = B1），
    // 而不是像严格 >= 那样把第一个 Negate 在操作数之前写进 output（求值期栈空 → 静默判失败）。
    // ops.top() == Op::Variable 是左括号哨兵（见下方 '(' 分支），哨兵永不参与弹出比较。
    auto pushOperator = [&](ExpressionNode::Op op) {
        while (!ops.empty() && ops.top() != ExpressionNode::Op::Variable) {
            const ExpressionNode::Op top = ops.top();
            const bool bothNegate =
                top == ExpressionNode::Op::Negate && op == ExpressionNode::Op::Negate;
            const bool popTop = precedence(top) > precedence(op) ||
                                (precedence(top) == precedence(op) && !bothNegate);
            if (popTop) {
                ExpressionNode node;
                node.op = top;
                output.push_back(node);
                ops.pop();
            } else {
                break;
            }
        }
        ops.push(op);
    };

    while (i < expression.size()) {
        const char c = expression[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            if (!expectOperand) { error = "表达式语法错误：操作数相邻，缺少运算符"; return false; }
            const size_t start = i;
            while (i < expression.size() &&
                   (std::isdigit(static_cast<unsigned char>(expression[i])) || expression[i] == '.')) {
                ++i;
            }
            const std::string text = expression.substr(start, i - start);
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0') { error = "非法数字: " + text; return false; }
            ExpressionNode node;
            node.op = ExpressionNode::Op::Number;
            node.number = value;
            output.push_back(node);
            expectOperand = false;
            continue;
        }

        if (isVariableStart(c)) {
            const size_t start = i;
            while (i < expression.size() && isVariableChar(expression[i])) ++i;
            const std::string name = expression.substr(start, i - start);
            const int index = findVariable(name);
            if (index < 0) { error = "未知变量: " + name; return false; }
            if (!expectOperand) { error = "表达式语法错误：操作数相邻，缺少运算符"; return false; }
            ExpressionNode node;
            node.op = ExpressionNode::Op::Variable;
            node.variableIndex = index;
            output.push_back(node);
            expectOperand = false;
            continue;
        }

        if (c == '(') {
            // 左括号哨兵：借用 Op::Variable 作哨兵值。Op 枚举是任务 5 依赖的对外契约，
            // 不为消除哨兵而新增枚举项。哨兵只活在 ops 里、绝不进入 output：
            // ')' 分支弹到哨兵后即 pop 丢弃，收尾循环遇到哨兵直接判"括号不配对"。
            ops.push(ExpressionNode::Op::Variable);
            ++depth;
            expectOperand = true;
            ++i;
            continue;
        }

        if (c == ')') {
            if (depth <= 0) { error = "括号不配对：多余的右括号"; return false; }
            while (!ops.empty() && ops.top() != ExpressionNode::Op::Variable) {
                ExpressionNode node;
                node.op = ops.top();
                output.push_back(node);
                ops.pop();
            }
            if (ops.empty()) { error = "括号不配对"; return false; }
            ops.pop();
            --depth;
            expectOperand = false;
            ++i;
            continue;
        }

        // expectOperand 为真时 '+' / '-' 处于一元位置。
        switch (c) {
            case '+':
            case '-': {
                if (!expectOperand) {  // 二元位置
                    pushOperator(c == '+' ? ExpressionNode::Op::Add : ExpressionNode::Op::Subtract);
                    expectOperand = true;
                } else if (c == '-') {  // 一元负号：压 Negate，仍期望操作数（允许 --B1、-(B1)）
                    pushOperator(ExpressionNode::Op::Negate);
                    expectOperand = true;
                }
                // 一元正号（expectOperand 且 c == '+'）在数学上是恒等，什么都不压，
                // 只跳过该字符并保持 expectOperand = true。
                break;
            }
            case '*':
            case '/': {
                if (expectOperand) {
                    error = "表达式语法错误：缺少操作数";
                    return false;
                }
                pushOperator(c == '*' ? ExpressionNode::Op::Multiply : ExpressionNode::Op::Divide);
                expectOperand = true;
                break;
            }
            default:
                error = std::string("非法字符: ") + c;
                return false;
        }
        ++i;
    }

    if (expectOperand) { error = "表达式结尾缺少操作数"; return false; }
    while (!ops.empty()) {
        if (ops.top() == ExpressionNode::Op::Variable) { error = "括号不配对：缺少右括号"; return false; }
        ExpressionNode node;
        node.op = ops.top();
        output.push_back(node);
        ops.pop();
    }
    if (depth != 0) { error = "括号不配对"; return false; }
    program = std::move(output);
    return true;
}

double evaluateExpression(const std::vector<ExpressionNode>& program,
                          const std::vector<double>& values,
                          bool& evaluationFailed) {
    evaluationFailed = false;
    std::vector<double> stack;
    stack.reserve(program.size());

    for (const auto& node : program) {
        switch (node.op) {
            case ExpressionNode::Op::Number: stack.push_back(node.number); break;
            case ExpressionNode::Op::Variable: {
                if (node.variableIndex < 0 ||
                    static_cast<size_t>(node.variableIndex) >= values.size()) {
                    evaluationFailed = true;
                    return 0.0;
                }
                stack.push_back(values[static_cast<size_t>(node.variableIndex)]);
                break;
            }
            case ExpressionNode::Op::Negate: {
                if (stack.empty()) { evaluationFailed = true; return 0.0; }
                stack.back() = -stack.back();
                break;
            }
            case ExpressionNode::Op::Add:
            case ExpressionNode::Op::Subtract:
            case ExpressionNode::Op::Multiply:
            case ExpressionNode::Op::Divide: {
                if (stack.size() < 2) { evaluationFailed = true; return 0.0; }
                const double b = stack.back(); stack.pop_back();
                const double a = stack.back(); stack.pop_back();
                double result = 0.0;
                switch (node.op) {
                    case ExpressionNode::Op::Add: result = a + b; break;
                    case ExpressionNode::Op::Subtract: result = a - b; break;
                    case ExpressionNode::Op::Multiply: result = a * b; break;
                    case ExpressionNode::Op::Divide:
                        if (std::abs(b) < 1e-15) { evaluationFailed = true; result = 0.0; }
                        else { result = a / b; }
                        break;
                    default: break;
                }
                stack.push_back(result);
                break;
            }
        }
    }

    if (stack.size() != 1) { evaluationFailed = true; return 0.0; }
    return stack.front();
}

} // namespace gis::core
