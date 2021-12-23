/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_PREDICATE_H_
#define _CARTO_MAPNIKVT_PREDICATE_H_

#include "ExpressionPredicateBase.h"

namespace carto { namespace mvt {
    class ExpressionPredicate final {
    public:
        explicit ExpressionPredicate(Expression expr) : _expr(std::move(expr)) { }

        const Expression& getExpression() const { return _expr; }

    private:
        const Expression _expr;
    };

    class ComparisonPredicate final {
    public:
        enum class Op {
            EQ,
            NEQ,
            LT,
            LTE,
            GT,
            GTE,
            MATCH
        };

        explicit ComparisonPredicate(Op op, Expression expr1, Expression expr2) : _op(op), _expr1(std::move(expr1)), _expr2(std::move(expr2)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression1() const { return _expr1; }
        const Expression& getExpression2() const { return _expr2; }

        static bool applyOp(Op op, const Value& val1, const Value& val2);

    protected:
        const Op _op;
        const Expression _expr1;
        const Expression _expr2;
    };

    class NotPredicate final {
    public:
        explicit NotPredicate(Predicate pred) : _pred(std::move(pred)) { }

        const Predicate& getPredicate() const { return _pred; }

    protected:
        const Predicate _pred;
    };

    class OrPredicate final {
    public:
        explicit OrPredicate(Predicate pred1, Predicate pred2) : _pred1(std::move(pred1)), _pred2(std::move(pred2)) { }

        const Predicate& getPredicate1() const { return _pred1; }
        const Predicate& getPredicate2() const { return _pred2; }

    protected:
        const Predicate _pred1;
        const Predicate _pred2;
    };

    class AndPredicate final {
    public:
        explicit AndPredicate(Predicate pred1, Predicate pred2) : _pred1(std::move(pred1)), _pred2(std::move(pred2)) { }

        const Predicate& getPredicate1() const { return _pred1; }
        const Predicate& getPredicate2() const { return _pred2; }

    protected:
        const Predicate _pred1;
        const Predicate _pred2;
    };
} }

#endif
