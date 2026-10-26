// src/tools/calculator_eval.cpp - iterative two-stack evaluator (spec 4.6).

#include "tools/calculator.h"

#include <stack>
#include <string>

namespace {

using Kind = CalcToken::Kind;

int precedence(Kind kind) {
    if (kind == Kind::Times || kind == Kind::Divide) return 2;
    if (kind == Kind::Plus || kind == Kind::Minus) return 1;
    return 0;
}

bool apply_top(std::stack<double>& values, std::stack<Kind>& ops,
               std::string* error) {
    if (ops.empty() || values.size() < 2 || precedence(ops.top()) == 0) {
        *error = "malformed expression stacks";
        return false;
    }

    const double right = values.top();
    values.pop();
    const double left = values.top();
    values.pop();
    const Kind op = ops.top();
    ops.pop();

    double result = 0.0;
    switch (op) {
        case Kind::Plus:   result = left + right; break;
        case Kind::Minus:  result = left - right; break;
        case Kind::Times:  result = left * right; break;
        case Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "unexpected operator";
            return false;
    }
    values.push(result);
    return true;
}

}  // namespace

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out,
                   std::string* error) {
    std::stack<double> values;
    std::stack<Kind> ops;

    for (const CalcToken& token : tokens) {
        const Kind kind = token.kind;
        if (kind == Kind::Number) {
            values.push(token.value);
        } else if (kind == Kind::LParen) {
            ops.push(kind);
        } else if (kind == Kind::RParen) {
            while (!ops.empty() && ops.top() != Kind::LParen) {
                if (!apply_top(values, ops, error)) return false;
            }
            if (ops.empty()) {
                *error = "mismatched parentheses";
                return false;
            }
            ops.pop();
        } else if (precedence(kind) != 0) {
            while (!ops.empty() && ops.top() != Kind::LParen &&
                   precedence(ops.top()) >= precedence(kind)) {
                if (!apply_top(values, ops, error)) return false;
            }
            ops.push(kind);
        } else {
            *error = "unexpected token";
            return false;
        }
    }

    while (!ops.empty()) {
        if (ops.top() == Kind::LParen) {
            *error = "mismatched parentheses";
            return false;
        }
        if (!apply_top(values, ops, error)) return false;
    }

    if (values.size() != 1 || out == nullptr) {
        *error = "invalid final value stack";
        return false;
    }
    *out = values.top();
    return true;
}
