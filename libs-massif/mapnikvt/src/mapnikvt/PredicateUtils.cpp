#include "PredicateUtils.h"
#include "Expression.h"
#include "ExpressionUtils.h"
#include "ValueConverter.h"

namespace {
    struct CondEvaluator {
        template <typename T> bool operator() (T val) const { return val != T(); }
    };
}

namespace carto { namespace mvt {
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
                    if (ExpressionContext::isNutiVariable(name) || ExpressionContext::isZoomVariable(name)) {
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
} }
