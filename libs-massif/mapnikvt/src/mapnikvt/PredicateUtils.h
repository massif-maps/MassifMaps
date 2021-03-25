/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_PREDICATEUTILS_H_
#define _CARTO_MAPNIKVT_PREDICATEUTILS_H_

#include "Predicate.h"
#include "ExpressionContext.h"

#include <functional>

namespace carto { namespace mvt {
    struct PredicateEvaluator {
        explicit PredicateEvaluator(const ExpressionContext& context) : _context(context) { }

        bool operator() (bool val) const { return val; }
        bool operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const;
        bool operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const;
        bool operator() (const std::shared_ptr<NotPredicate>& notPred) const {
            return !std::visit(*this, notPred->getPredicate());
        }
        bool operator() (const std::shared_ptr<OrPredicate>& orPred) const {
            return std::visit(*this, orPred->getPredicate1()) || std::visit(*this, orPred->getPredicate2());
        }
        bool operator() (const std::shared_ptr<AndPredicate>& andPred) const {
            return std::visit(*this, andPred->getPredicate1()) && std::visit(*this, andPred->getPredicate2());
        }

    private:
        const ExpressionContext& _context;
    };

    struct PredicateMapper {
        explicit PredicateMapper(const std::function<Expression(const Expression&)>& fn) : _fn(fn) { }

        Predicate operator() (bool val) const { return val; }
        Predicate operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const;
        Predicate operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const;
        Predicate operator() (const std::shared_ptr<NotPredicate>& notPred) const {
            return std::make_shared<NotPredicate>(std::visit(*this, notPred->getPredicate()));
        }
        Predicate operator() (const std::shared_ptr<OrPredicate>& orPred) const {
            return std::make_shared<OrPredicate>(std::visit(*this, orPred->getPredicate1()), std::visit(*this, orPred->getPredicate2()));
        }
        Predicate operator() (const std::shared_ptr<AndPredicate>& andPred) const {
            return std::make_shared<AndPredicate>(std::visit(*this, andPred->getPredicate1()), std::visit(*this, andPred->getPredicate2()));
        }

    private:
        std::function<Expression(const Expression&)> _fn;
    };

    struct PredicateDeepVisitor {
        explicit PredicateDeepVisitor(const std::function<void(const Expression&)>& fn) : _fn(fn) { }

        void operator() (bool val) const { }
        void operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const;
        void operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const;
        void operator() (const std::shared_ptr<NotPredicate>& notPred) const {
            std::visit(*this, notPred->getPredicate());
        }
        void operator() (const std::shared_ptr<OrPredicate>& orPred) const {
            std::visit(*this, orPred->getPredicate1());
            std::visit(*this, orPred->getPredicate2());
        }
        void operator() (const std::shared_ptr<AndPredicate>& andPred) const {
            std::visit(*this, andPred->getPredicate1());
            std::visit(*this, andPred->getPredicate2());
        }

    private:
        std::function<void(const Expression&)> _fn;
    };

    struct PredicateDeepEqualsChecker {
        bool operator() (bool val1, bool val2) const { return val1 == val2; }
        bool operator() (const std::shared_ptr<ExpressionPredicate>& exprPred1, const std::shared_ptr<ExpressionPredicate>& exprPred2) const;
        bool operator() (const std::shared_ptr<ComparisonPredicate>& compPred1, const std::shared_ptr<ComparisonPredicate>& compPred2) const;
        bool operator() (const std::shared_ptr<NotPredicate>& notPred1, const std::shared_ptr<NotPredicate>& notPred2) const {
            return std::visit(*this, notPred1->getPredicate(), notPred2->getPredicate());
        }
        bool operator() (const std::shared_ptr<OrPredicate>& orPred1, const std::shared_ptr<OrPredicate>& orPred2) const {
            return std::visit(*this, orPred1->getPredicate1(), orPred2->getPredicate1()) && std::visit(*this, orPred1->getPredicate2(), orPred2->getPredicate2());
        }
        bool operator() (const std::shared_ptr<AndPredicate>& andPred1, const std::shared_ptr<AndPredicate>& andPred2) const {
            return std::visit(*this, andPred1->getPredicate1(), andPred2->getPredicate1()) && std::visit(*this, andPred1->getPredicate2(), andPred2->getPredicate2());
        }
        template <typename S, typename T> bool operator() (const S& pred1, const T& pred2) const { return false; }
    };
} }

#endif
