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

namespace carto::mvt {
    struct ExpressionEvaluator {
        explicit ExpressionEvaluator(const ExpressionContext& context, const vt::ViewState* viewState) : _context(context), _viewState(viewState) { }

        Value operator() (const Value& val) const { return val; }
        Value operator() (const Predicate& pred) const;
        Value operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
            Value var = std::visit(*this, varExpr->getVariableExpression());
            std::string name = ValueConverter<std::string>::convert(var);
            if (_viewState) {
                return _context.getViewStateVariable(*_viewState, name);
            }
            return _context.getVariable(name);
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
            return interpExpr->evaluate(ValueConverter<float>::convert(timeVal));
        }
        Value operator() (const std::shared_ptr<TransformExpression>& transExpr) const {
            return Value();
        }

    private:
        const ExpressionContext& _context;
        const vt::ViewState* _viewState;
    };

    struct ExpressionVariableVisitor {
        explicit ExpressionVariableVisitor(std::function<void(const std::shared_ptr<VariableExpression>&)> visitor) : _visitor(std::move(visitor)) { }

        void operator() (const Value& val) const { }
        void operator() (const Predicate& pred) const;
        void operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
            _visitor(varExpr);
            std::visit(*this, varExpr->getVariableExpression());
        }
        void operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
            std::visit(*this, unaryExpr->getExpression());
        }
        void operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
            std::visit(*this, binaryExpr->getExpression1());
            std::visit(*this, binaryExpr->getExpression2());
        }
        void operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr) const {
            std::visit(*this, tertiaryExpr->getExpression1());
            std::visit(*this, tertiaryExpr->getExpression2());
            std::visit(*this, tertiaryExpr->getExpression3());
        }
        void operator() (const std::shared_ptr<InterpolateExpression>& interpExpr) const {
            std::visit(*this, interpExpr->getTimeExpression());
        }
        void operator() (const std::shared_ptr<TransformExpression>& transExpr) const {
            for (const Expression& subExpr : transExpr->getSubExpressions()) {
                std::visit(*this, subExpr);
            }
        }

    private:
        std::function<void(const std::shared_ptr<VariableExpression>&)> _visitor;
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
        bool operator() (const std::shared_ptr<TransformExpression>& transExpr1, const std::shared_ptr<TransformExpression>& transExpr2) const {
            std::vector<Expression> subExprs1 = transExpr1->getSubExpressions();
            std::vector<Expression> subExprs2 = transExpr2->getSubExpressions();
            if (subExprs1.size() != subExprs2.size()) {
                return false;
            }
            for (std::size_t i = 0; i < subExprs1.size(); i++) {
                if (!std::visit(*this, subExprs1[i], subExprs2[i])) {
                    return false;
                }
            }
            return true;
        }
        template <typename S, typename T> bool operator() (const S&, const T&) const { return false; }
    };
}

#endif
