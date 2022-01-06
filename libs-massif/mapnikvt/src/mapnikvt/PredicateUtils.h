/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_PREDICATEUTILS_H_
#define _CARTO_MAPNIKVT_PREDICATEUTILS_H_

#include "Predicate.h"
#include "ExpressionContext.h"

#include <map>
#include <functional>

#include <boost/logic/tribool.hpp>

namespace carto::mvt {
    struct PredicateEvaluator {
        explicit PredicateEvaluator(const ExpressionContext& context, const vt::ViewState* viewState) : _context(context), _viewState(viewState) { }

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
        const vt::ViewState* _viewState;
    };

    struct PredicatePreEvaluator {
        explicit PredicatePreEvaluator(const ExpressionContext& context) : _context(context) { }

        boost::tribool operator() (bool val) const { return val; }
        boost::tribool operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const { return boost::indeterminate; }
        boost::tribool operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const;
        boost::tribool operator() (const std::shared_ptr<NotPredicate>& notPred) const {
            return !std::visit(*this, notPred->getPredicate());
        }
        boost::tribool operator() (const std::shared_ptr<OrPredicate>& orPred) const {
            return std::visit(*this, orPred->getPredicate1()) || std::visit(*this, orPred->getPredicate2());
        }
        boost::tribool operator() (const std::shared_ptr<AndPredicate>& andPred) const {
            return std::visit(*this, andPred->getPredicate1()) && std::visit(*this, andPred->getPredicate2());
        }

    private:
        const ExpressionContext& _context;
    };

    struct PredicateVariableVisitor {
        explicit PredicateVariableVisitor(std::function<void(const std::shared_ptr<VariableExpression>&)> visitor) : _visitor(std::move(visitor)) { }

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
        std::function<void(const std::shared_ptr<VariableExpression>&)> _visitor;
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
}

#endif
