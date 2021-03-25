#include "ExpressionUtils.h"
#include "PredicateUtils.h"

namespace carto { namespace mvt {
    Value ExpressionEvaluator::operator() (const Predicate& pred) const {
        return std::visit(PredicateEvaluator(_context), pred);
    }

    Expression ExpressionMapper::operator() (const Predicate& pred) const {
        return _fn(std::visit(PredicateMapper(_fn), pred));
    }

    void ExpressionDeepVisitor::operator() (const Predicate& pred) const {
        _fn(pred);
        std::visit(PredicateDeepVisitor(_fn), pred);
    }

    bool ExpressionDeepEqualsChecker::operator() (const Predicate& pred1, const Predicate& pred2) const {
        return std::visit(PredicateDeepEqualsChecker(), pred1, pred2);
    }
} }
