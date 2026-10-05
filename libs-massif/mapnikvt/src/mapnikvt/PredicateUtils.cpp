#include "PredicateUtils.h"
#include "Expression.h"
#include "ExpressionUtils.h"
#include "ValueConverter.h"

namespace {
    struct CondEvaluator {
        template <typename T> bool operator() (T val) const { return val != T(); }
    };
}

namespace massif::mvt {
    bool PredicateEvaluator::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const {
        Value val = std::visit(ExpressionEvaluator(_context, _viewState), exprPred->getExpression());
        return std::visit(CondEvaluator(), val);
    }

    bool PredicateEvaluator::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        Value val1 = std::visit(ExpressionEvaluator(_context, _viewState), compPred->getExpression1());
        Value val2 = std::visit(ExpressionEvaluator(_context, _viewState), compPred->getExpression2());
        return ComparisonPredicate::applyOp(compPred->getOp(), val1, val2);
    }

    boost::tribool PredicatePreEvaluator::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        auto extractValue = [this](const Expression& expr, Value& result) -> bool {
            if (auto val = std::get_if<Value>(&expr)) {
                result = *val;
                return true;
            }
            if (auto varExpr = std::get_if<std::shared_ptr<VariableExpression>>(&expr)) {
                if (auto var = std::get_if<Value>(&(*varExpr)->getVariableExpression())) {
                    std::string name = ValueConverter<std::string>::convert(*var);
                    if (ExpressionContext::isStyleParameterVariable(name) || ExpressionContext::isZoomVariable(name) || ExpressionContext::isRenderVariable(name)) {
                        result = _context.getVariable(name);
                        return true;
                    }
                }
            }
            return false;
        };

        Value val1, val2;
        if (!extractValue(compPred->getExpression1(), val1) || !extractValue(compPred->getExpression2(), val2)) {
            return boost::indeterminate;
        }
        return ComparisonPredicate::applyOp(compPred->getOp(), val1, val2);
    }

    bool PredicateFieldValueEvaluator::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        if (compPred->getOp() != ComparisonPredicate::Op::EQ) {
            return true;
        }
        auto fieldName = [](const Expression& expr, std::string& name) -> bool {
            if (auto varExpr = std::get_if<std::shared_ptr<VariableExpression>>(&expr)) {
                if (auto var = std::get_if<Value>(&(*varExpr)->getVariableExpression())) {
                    name = ValueConverter<std::string>::convert(*var);
                    return !(ExpressionContext::isStyleParameterVariable(name) || ExpressionContext::isZoomVariable(name) || ExpressionContext::isRenderVariable(name) || ExpressionContext::isViewStateVariable(name) || ExpressionContext::isMapnikVariable(name));
                }
            }
            return false;
        };
        // A literal only: a field compared with a style parameter is how a selection is drawn, and
        // that has to survive the parameter changing without the tile being decoded again
        auto constantValue = [](const Expression& expr, Value& value) -> bool {
            auto val = std::get_if<Value>(&expr);
            if (!val || std::holds_alternative<std::monostate>(*val)) {
                return false; // a field the feature lacks reads as null, so a test for null can hold
            }
            value = *val;
            return true;
        };

        std::string name;
        Value value;
        if (fieldName(compPred->getExpression1(), name) && constantValue(compPred->getExpression2(), value)) {
            return _mayHaveFieldValue(name, value);
        }
        if (fieldName(compPred->getExpression2(), name) && constantValue(compPred->getExpression1(), value)) {
            return _mayHaveFieldValue(name, value);
        }
        return true;
    }

    void PredicateVariableVisitor::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const {
        std::visit(ExpressionVariableVisitor(_visitor), exprPred->getExpression());
    }

    void PredicateVariableVisitor::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        std::visit(ExpressionVariableVisitor(_visitor), compPred->getExpression1());
        std::visit(ExpressionVariableVisitor(_visitor), compPred->getExpression2());
    }

    bool PredicateDeepEqualsChecker::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred1, const std::shared_ptr<ExpressionPredicate>& exprPred2) const {
        return std::visit(ExpressionDeepEqualsChecker(), exprPred1->getExpression(), exprPred2->getExpression());
    }

    bool PredicateDeepEqualsChecker::operator() (const std::shared_ptr<ComparisonPredicate>& compPred1, const std::shared_ptr<ComparisonPredicate>& compPred2) const {
        return compPred1->getOp() == compPred2->getOp() && std::visit(ExpressionDeepEqualsChecker(), compPred1->getExpression1(), compPred2->getExpression1()) && std::visit(ExpressionDeepEqualsChecker(), compPred1->getExpression2(), compPred2->getExpression2());
    }
}
