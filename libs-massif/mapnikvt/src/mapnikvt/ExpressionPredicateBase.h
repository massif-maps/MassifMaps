/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_EXPRESSIONPREDICATEBASE_H_
#define _MASSIF_MAPNIKVT_EXPRESSIONPREDICATEBASE_H_

#include "Value.h"

#include <memory>
#include <variant>

namespace massif::mvt {
    class ExpressionPredicate;
    class ComparisonPredicate;
    class NotPredicate;
    class OrPredicate;
    class AndPredicate;
    class VariableExpression;
    class PredicateExpression;
    class UnaryExpression;
    class BinaryExpression;
    class TertiaryExpression;
    class InterpolateExpression;
    class TransformExpression;
    class FunctionExpression;

    using Predicate = std::variant<bool, std::shared_ptr<ExpressionPredicate>, std::shared_ptr<ComparisonPredicate>, std::shared_ptr<NotPredicate>, std::shared_ptr<OrPredicate>, std::shared_ptr<AndPredicate>>;

    using Expression = std::variant<Value, Predicate, std::shared_ptr<VariableExpression>, std::shared_ptr<UnaryExpression>, std::shared_ptr<BinaryExpression>, std::shared_ptr<TertiaryExpression>, std::shared_ptr<InterpolateExpression>, std::shared_ptr<TransformExpression>, std::shared_ptr<FunctionExpression>>;
}

#endif
