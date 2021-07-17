/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_EXPRESSIONUTILS_H_
#define _CARTO_CARTOCSS_EXPRESSIONUTILS_H_

#include "Value.h"
#include "Expression.h"

#include <stdexcept>
#include <memory>
#include <map>
#include <utility>
#include <functional>

namespace carto { namespace css {
    struct ExpressionContext {
        std::map<std::string, Value>* predefinedVariableMap = nullptr;
        std::map<std::string, Expression>* variableMap = nullptr;

        std::map<std::string, Value>* predefinedFieldMap = nullptr;
        std::map<std::string, Value>* fieldMap = nullptr;
    };

    struct ExpressionEvaluator {
        explicit ExpressionEvaluator(const ExpressionContext& context) : _context(context) { }

        Expression operator() (const Value& val) const { return val; }

        Expression operator() (const FieldOrVar& fieldOrVar) const {
            if (!fieldOrVar.isField()) {
                if (_context.predefinedVariableMap) {
                    auto it = _context.predefinedVariableMap->find(fieldOrVar.getName());
                    if (it != _context.predefinedVariableMap->end()) {
                        return it->second;
                    }
                }
                if (_context.variableMap) {
                    auto it = _context.variableMap->find(fieldOrVar.getName());
                    if (it != _context.variableMap->end()) {
                        return std::visit(*this, it->second);
                    }
                }
            }
            else {
                if (_context.predefinedFieldMap) {
                    auto it = _context.predefinedFieldMap->find(fieldOrVar.getName());
                    if (it != _context.predefinedFieldMap->end()) {
                        return it->second;
                    }
                }
                if (_context.fieldMap) {
                    auto it = _context.fieldMap->find(fieldOrVar.getName());
                    if (it != _context.fieldMap->end()) {
                        return it->second;
                    }
                }
            }
            return fieldOrVar;
        }

        Expression operator() (const std::shared_ptr<StringExpression>& strExpr) const {
            // Note: In theory we should should recurse here into parsed expression, there are border cases when this may be needed
            return strExpr;
        }

        Expression operator() (const std::shared_ptr<ListExpression>& listExpr) const {
            std::vector<Expression> exprs;
            exprs.reserve(listExpr->getExpressions().size());
            for (const Expression& expr : listExpr->getExpressions()) {
                exprs.push_back(std::visit(*this, expr));
            }
            return std::make_shared<ListExpression>(std::move(exprs));
        }

        Expression operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
            Expression result = std::visit(*this, unaryExpr->getExpression());
            if (auto val = std::get_if<Value>(&result)) {
                return UnaryExpression::applyOp(unaryExpr->getOp(), *val);
            }
            return std::make_shared<UnaryExpression>(unaryExpr->getOp(), std::move(result));
        }

        Expression operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
            Expression result1 = std::visit(*this, binaryExpr->getExpression1());
            Expression result2 = std::visit(*this, binaryExpr->getExpression2());
            if (auto val1 = std::get_if<Value>(&result1)) {
                if (auto val2 = std::get_if<Value>(&result2)) {
                    return BinaryExpression::applyOp(binaryExpr->getOp(), *val1, *val2);
                }
            }
            return std::make_shared<BinaryExpression>(binaryExpr->getOp(), std::move(result1), std::move(result2));
        }

        Expression operator() (const std::shared_ptr<ConditionalExpression>& condExpr) const {
            Expression cond = std::visit(*this, condExpr->getCondition());
            Expression result1 = std::visit(*this, condExpr->getExpression1());
            Expression result2 = std::visit(*this, condExpr->getExpression2());
            if (auto condVal = std::get_if<Value>(&cond)) {
                if (auto boolVal = std::get_if<bool>(condVal)) {
                    return *boolVal ? result1 : result2;
                }
                else {
                    throw std::invalid_argument("Condition type error, expecting boolean");
                }
            }
            return std::make_shared<ConditionalExpression>(std::move(cond), std::move(result1), std::move(result2));
        }

        Expression operator() (const std::shared_ptr<FunctionExpression>& funcExpr) const {
            std::vector<Expression> results;
            results.reserve(funcExpr->getArgs().size());
            bool allValues = true;
            for (const Expression& expr : funcExpr->getArgs()) {
                results.push_back(std::visit(*this, expr));
                allValues = allValues && std::holds_alternative<Value>(results.back());
            }
            if (allValues) {
                std::vector<Value> values;
                values.reserve(results.size());
                for (const Expression& result : results) {
                    values.push_back(std::get<Value>(result));
                }
                Value result = FunctionExpression::applyFunc(funcExpr->getFunc(), values);
                if (result != Value()) {
                    return result;
                }
            }
            return std::make_shared<FunctionExpression>(funcExpr->getFunc(), std::move(results));
        }

    private:
        const ExpressionContext& _context;
    };

    struct ExpressionDeepEqualsChecker {
        bool operator() (const Value& val1, const Value& val2) const { return val1 == val2; }
        bool operator() (const FieldOrVar& fieldOrVar1, const FieldOrVar& fieldOrVar2) const { return fieldOrVar1 == fieldOrVar2; }

        bool operator() (const std::shared_ptr<StringExpression>& strExpr1, const std::shared_ptr<StringExpression>& strExpr2) const {
            return strExpr1->getString() == strExpr2->getString();
        }

        bool operator() (const std::shared_ptr<ListExpression>& listExpr1, const std::shared_ptr<ListExpression>& listExpr2) const {
            if (listExpr1->getExpressions().size() != listExpr2->getExpressions().size()) {
                return false;
            }
            return std::equal(listExpr1->getExpressions().begin(), listExpr1->getExpressions().end(), listExpr2->getExpressions().begin(), [this](const Expression& expr1, const Expression& expr2) {
                return std::visit(*this, expr1, expr2);
            });
        }

        bool operator() (const std::shared_ptr<UnaryExpression>& unaryExpr1, const std::shared_ptr<UnaryExpression>& unaryExpr2) const {
            return unaryExpr1->getOp() == unaryExpr2->getOp() && std::visit(*this, unaryExpr1->getExpression(), unaryExpr2->getExpression());
        }

        bool operator() (const std::shared_ptr<BinaryExpression>& binaryExpr1, const std::shared_ptr<BinaryExpression>& binaryExpr2) const {
            return binaryExpr1->getOp() == binaryExpr2->getOp() && std::visit(*this, binaryExpr1->getExpression1(), binaryExpr2->getExpression1()) && std::visit(*this, binaryExpr1->getExpression2(), binaryExpr2->getExpression2());
        }

        bool operator() (const std::shared_ptr<ConditionalExpression>& condExpr1, const std::shared_ptr<ConditionalExpression>& condExpr2) const {
            return std::visit(*this, condExpr1->getCondition(), condExpr2->getCondition()) && std::visit(*this, condExpr1->getExpression1(), condExpr2->getExpression1()) && std::visit(*this, condExpr1->getExpression2(), condExpr2->getExpression2());
        }

        bool operator() (const std::shared_ptr<FunctionExpression>& funcExpr1, const std::shared_ptr<FunctionExpression>& funcExpr2) const {
            if (funcExpr1->getFunc() != funcExpr2->getFunc() || funcExpr1->getArgs().size() != funcExpr2->getArgs().size()) {
                return false;
            }
            return std::equal(funcExpr1->getArgs().begin(), funcExpr1->getArgs().end(), funcExpr2->getArgs().begin(), [this](const Expression& expr1, const Expression& expr2) {
                return std::visit(*this, expr1, expr2);
            });
        }

        template <typename S, typename T> bool operator() (const S& expr1, const T& expr2) const { return false; }
    };
} }

#endif
