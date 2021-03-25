#include "PredicateUtils.h"
#include "Expression.h"
#include "ExpressionUtils.h"
#include "ValueConverter.h"

namespace carto { namespace mvt {
    bool PredicateEvaluator::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const {
       Value val = std::visit(ExpressionEvaluator(_context), exprPred->getExpression());
       return ValueConverter<bool>::convert(val);
    }

    bool PredicateEvaluator::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        Value val1 = std::visit(ExpressionEvaluator(_context), compPred->getExpression1());
        Value val2 = std::visit(ExpressionEvaluator(_context), compPred->getExpression2());
        return ComparisonPredicate::applyOp(compPred->getOp(), val1, val2);
    }

    Predicate PredicateMapper::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const {
        Expression expr = std::visit(ExpressionMapper(_fn), exprPred->getExpression());
        return std::make_shared<ExpressionPredicate>(std::move(expr));
    }

    Predicate PredicateMapper::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        Expression expr1 = std::visit(ExpressionMapper(_fn), compPred->getExpression1());
        Expression expr2 = std::visit(ExpressionMapper(_fn), compPred->getExpression2());
        return std::make_shared<ComparisonPredicate>(compPred->getOp(), std::move(expr1), std::move(expr2));
    }

    void PredicateDeepVisitor::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const {
        std::visit(ExpressionDeepVisitor(_fn), exprPred->getExpression());
    }

    void PredicateDeepVisitor::operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
        std::visit(ExpressionDeepVisitor(_fn), compPred->getExpression1());
        std::visit(ExpressionDeepVisitor(_fn), compPred->getExpression2());
    }

    bool PredicateDeepEqualsChecker::operator() (const std::shared_ptr<ExpressionPredicate>& exprPred1, const std::shared_ptr<ExpressionPredicate>& exprPred2) const {
        return std::visit(ExpressionDeepEqualsChecker(), exprPred1->getExpression(), exprPred2->getExpression());
    }

    bool PredicateDeepEqualsChecker::operator() (const std::shared_ptr<ComparisonPredicate>& compPred1, const std::shared_ptr<ComparisonPredicate>& compPred2) const {
        return compPred1->getOp() == compPred2->getOp() && std::visit(ExpressionDeepEqualsChecker(), compPred1->getExpression1(), compPred2->getExpression1()) && std::visit(ExpressionDeepEqualsChecker(), compPred1->getExpression2(), compPred2->getExpression2());
    }
} }
