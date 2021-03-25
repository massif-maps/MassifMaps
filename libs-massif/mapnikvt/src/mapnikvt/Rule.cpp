#include "Rule.h"
#include "Symbolizer.h"
#include "Filter.h"
#include "Expression.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "ExpressionUtils.h"

namespace carto { namespace mvt {
    void Rule::gatherReferencedFields(std::vector<Expression>& fields) const {
        auto gatherFields = [&fields](const Expression& expr) {
            if (auto varExpr = std::get_if<std::shared_ptr<VariableExpression>>(&expr)) {
                if (std::find_if(fields.begin(), fields.end(), [&](const Expression& expr) {
                    return std::visit(ExpressionDeepEqualsChecker(), expr, (*varExpr)->getVariableExpression());
                }) == fields.end()) {
                    fields.push_back((*varExpr)->getVariableExpression());
                }
            }
        };
        if (_filter) {
            if (_filter->getPredicate()) {
                std::visit(PredicateDeepVisitor(gatherFields), *_filter->getPredicate());
            }
        }
        std::for_each(_symbolizers.begin(), _symbolizers.end(), [&](const std::shared_ptr<Symbolizer>& symbolizer) {
            for (const Expression& expr : symbolizer->getParameterExpressions()) {
                std::visit(ExpressionDeepVisitor(gatherFields), expr);
            }
        });
    }
} }
