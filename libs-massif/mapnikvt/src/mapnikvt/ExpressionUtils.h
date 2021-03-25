/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSIONUTILS_H_
#define _CARTO_MAPNIKVT_EXPRESSIONUTILS_H_

#include "Expression.h"
#include "ExpressionContext.h"

#include <functional>

namespace carto { namespace mvt {
    struct ExpressionEvaluator {
        explicit ExpressionEvaluator(const ExpressionContext& context) : _context(context) { }

        Value operator() (const Value& val) const { return val; }
        Value operator() (const Predicate& pred) const;
        Value operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
            return _context.getVariable(varExpr->getVariableName(_context));
        }
        Value operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
            Value val = std::visit(*this, unaryExpr->getExpression());
            return UnaryExpression::applyOp(unaryExpr->getOp(), val);
        }
        Value operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
            Value val1 = std::visit(*this, binaryExpr->getExpression1());
            Value val2 = std::visit(*this, binaryExpr->getExpression2());
            return BinaryExpression::applyOp(binaryExpr->getOp(), val1, val2);
        }
        Value operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr) const {
            Value val1 = std::visit(*this, tertiaryExpr->getExpression1());
            Value val2 = std::visit(*this, tertiaryExpr->getExpression2());
            Value val3 = std::visit(*this, tertiaryExpr->getExpression3());
            return TertiaryExpression::applyOp(tertiaryExpr->getOp(), val1, val2, val3);
        }
        Value operator() (const std::shared_ptr<InterpolateExpression>& interpExpr) const {
            Value timeVal = std::visit(*this, interpExpr->getTimeExpression());
            return static_cast<double>(interpExpr->evaluate(ValueConverter<float>::convert(timeVal)));
        }

    private:
        const ExpressionContext& _context;
    };

    struct ExpressionMapper {
        explicit ExpressionMapper(const std::function<Expression(const Expression&)>& fn) : _fn(fn) { }

        Expression operator() (const Value& val) const { return _fn(val); }
        Expression operator() (const Predicate& pred) const;
        Expression operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
            Expression expr = std::visit(*this, varExpr->getVariableExpression());
            return _fn(std::make_shared<VariableExpression>(std::move(expr)));
        }
        Expression operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
            Expression expr = std::visit(*this, unaryExpr->getExpression());
            return _fn(std::make_shared<UnaryExpression>(unaryExpr->getOp(), std::move(expr)));
        }
        Expression operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
            Expression expr1 = std::visit(*this, binaryExpr->getExpression1());
            Expression expr2 = std::visit(*this, binaryExpr->getExpression2());
            return _fn(std::make_shared<BinaryExpression>(binaryExpr->getOp(), std::move(expr1), std::move(expr2)));
        }
        Expression operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr) const {
            Expression expr1 = std::visit(*this, tertiaryExpr->getExpression1());
            Expression expr2 = std::visit(*this, tertiaryExpr->getExpression2());
            Expression expr3 = std::visit(*this, tertiaryExpr->getExpression3());
            return _fn(std::make_shared<TertiaryExpression>(tertiaryExpr->getOp(), std::move(expr1), std::move(expr2), std::move(expr3)));
        }
        Expression operator() (const std::shared_ptr<InterpolateExpression>& interpExpr) const {
            Expression timeExpr = std::visit(*this, interpExpr->getTimeExpression());
            return _fn(std::make_shared<InterpolateExpression>(interpExpr->getMethod(), std::move(timeExpr), interpExpr->getKeyFrames()));
        }

    private:
        std::function<Expression(const Expression&)> _fn;
    };

    struct ExpressionDeepVisitor {
        explicit ExpressionDeepVisitor(const std::function<void(const Expression&)>& fn) : _fn(fn) { }

        void operator() (const Value& val) const { _fn(val); }
        void operator() (const Predicate& pred) const;
        void operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
            _fn(varExpr);
            std::visit(*this, varExpr->getVariableExpression());
        }
        void operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
            _fn(unaryExpr);
            std::visit(*this, unaryExpr->getExpression());
        }
        void operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
            _fn(binaryExpr);
            std::visit(*this, binaryExpr->getExpression1());
            std::visit(*this, binaryExpr->getExpression2());
        }
        void operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr) const {
            _fn(tertiaryExpr);
            std::visit(*this, tertiaryExpr->getExpression1());
            std::visit(*this, tertiaryExpr->getExpression2());
            std::visit(*this, tertiaryExpr->getExpression3());
        }
        void operator() (const std::shared_ptr<InterpolateExpression>& interpExpr) const {
            _fn(interpExpr);
            std::visit(*this, interpExpr->getTimeExpression());
        }

    private:
        std::function<void(const Expression&)> _fn;
    };

    struct ExpressionDeepEqualsChecker {
        bool operator() (const Value& val1, const Value& val2) const { return val1 == val2; }
        bool operator() (const Predicate& pred1, const Predicate& pred2) const;
        bool operator() (const std::shared_ptr<VariableExpression>& varExpr1, const std::shared_ptr<VariableExpression>& varExpr2) const {
            return std::visit(*this, varExpr1->getVariableExpression(), varExpr2->getVariableExpression());
        }
        bool operator() (const std::shared_ptr<UnaryExpression>& unaryExpr1, const std::shared_ptr<UnaryExpression>& unaryExpr2) const {
            return unaryExpr1->getOp() == unaryExpr2->getOp() && std::visit(*this, unaryExpr1->getExpression(), unaryExpr2->getExpression());
        }
        bool operator() (const std::shared_ptr<BinaryExpression>& binaryExpr1, const std::shared_ptr<BinaryExpression>& binaryExpr2) const {
            return binaryExpr1->getOp() == binaryExpr2->getOp() && std::visit(*this, binaryExpr1->getExpression1(), binaryExpr2->getExpression1()) && std::visit(*this, binaryExpr1->getExpression2(), binaryExpr2->getExpression2());
        }
        bool operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr1, const std::shared_ptr<TertiaryExpression>& tertiaryExpr2) const {
            return tertiaryExpr1->getOp() == tertiaryExpr2->getOp() && std::visit(*this, tertiaryExpr1->getExpression1(), tertiaryExpr2->getExpression1()) && std::visit(*this, tertiaryExpr1->getExpression2(), tertiaryExpr2->getExpression2()) && std::visit(*this, tertiaryExpr1->getExpression3(), tertiaryExpr2->getExpression3());
        }
        bool operator() (const std::shared_ptr<InterpolateExpression>& interpExpr1, const std::shared_ptr<InterpolateExpression>& interpExpr2) const {
            return interpExpr1->getMethod() == interpExpr2->getMethod() && interpExpr1->getKeyFrames() == interpExpr2->getKeyFrames() && std::visit(*this, interpExpr1->getTimeExpression(), interpExpr2->getTimeExpression());
        }
        template <typename S, typename T> bool operator() (const S&, const T&) const { return false; }
    };
} }

#endif
