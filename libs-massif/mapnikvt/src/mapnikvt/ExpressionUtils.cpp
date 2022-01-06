#include "ExpressionUtils.h"
#include "PredicateUtils.h"

namespace carto::mvt {
    Value ExpressionEvaluator::operator() (const Predicate& pred) const {
        return std::visit(PredicateEvaluator(_context, _viewState), pred);
    }

    void ExpressionVariableVisitor::operator() (const Predicate& pred) const {
        std::visit(PredicateVariableVisitor(_visitor), pred);
    }

    bool ExpressionDeepEqualsChecker::operator() (const Predicate& pred1, const Predicate& pred2) const {
        return std::visit(PredicateDeepEqualsChecker(), pred1, pred2);
    }
}
