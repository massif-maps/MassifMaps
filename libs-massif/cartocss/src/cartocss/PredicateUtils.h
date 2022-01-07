/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_PREDICATEUTILS_H_
#define _CARTO_CARTOCSS_PREDICATEUTILS_H_

#include "Value.h"
#include "Expression.h"
#include "ExpressionUtils.h"

#include <memory>
#include <optional>
#include <utility>
#include <functional>

namespace carto::css {
    struct PredicateContext {
        std::string layerName;
        ExpressionContext expressionContext;
    };

    struct PredicateEvaluator {
        explicit PredicateEvaluator(const PredicateContext& context) : _context(context) { }
        
        boost::tribool operator() (const MapPredicate& mapPred) const {
            return _context.layerName.empty();
        }
        
        boost::tribool operator() (const LayerPredicate& layerPred) const {
            return _context.layerName == layerPred.getLayerName();
        }
        
        boost::tribool operator() (const ClassPredicate& classPred) const {
            if (!_context.expressionContext.fieldMap) {
                return boost::indeterminate;
            }
            auto it = _context.expressionContext.fieldMap->find("class");
            if (it == _context.expressionContext.fieldMap->end()) {
                return boost::indeterminate;
            }
            if (auto classValue = std::get_if<std::string>(&it->second)) {
                return *classValue == classPred.getClass();
            }
            return false;
        }
        
        boost::tribool operator() (const AttachmentPredicate& attachmentPred) const {
            return boost::indeterminate;
        }
        
        boost::tribool operator() (const OpPredicate& opPred) const {
            std::optional<Value> fieldOrVarValue;
            if (opPred.getFieldOrVar().isVar()) {
                if (_context.expressionContext.variableMap) {
                    auto it = _context.expressionContext.variableMap->find(opPred.getFieldOrVar().getName());
                    if (it != _context.expressionContext.variableMap->end()) {
                        Expression result = std::visit(ExpressionEvaluator(_context.expressionContext), it->second);
                        if (auto val = std::get_if<Value>(&result)) {
                            fieldOrVarValue = *val;
                        }
                    }
                }
            }
            else {
                if (_context.expressionContext.fieldMap) {
                    auto it = _context.expressionContext.fieldMap->find(opPred.getFieldOrVar().getName());
                    if (it != _context.expressionContext.fieldMap->end()) {
                        fieldOrVarValue = it->second;
                    }
                }
                if (_context.expressionContext.predefinedFieldMap) {
                    auto it = _context.expressionContext.predefinedFieldMap->find(opPred.getFieldOrVar().getName());
                    if (it != _context.expressionContext.predefinedFieldMap->end()) {
                        fieldOrVarValue = it->second;
                    }
                }
            }

            if (fieldOrVarValue) {
                return OpPredicate::applyOp(opPred.getOp(), *fieldOrVarValue, opPred.getRefValue());
            }
            return boost::indeterminate;
        }

        boost::tribool operator() (const WhenPredicate& whenPred) const {
            Expression expr = std::visit(ExpressionEvaluator(_context.expressionContext), whenPred.getExpression());
            if (auto val = std::get_if<Value>(&expr)) {
                return std::visit(CondEvaluator(), *val);
            }
            return boost::indeterminate;
        }

    private:
        struct CondEvaluator {
            template <typename T>
            bool operator() (const T& val) const { return val != T(); }
        };

        const PredicateContext& _context;
    };

    struct PredicateContainsChecker {
        boost::tribool operator() (const MapPredicate& mapPred1, const MapPredicate& mapPred2) const { return mapPred1 == mapPred2; }
        boost::tribool operator() (const LayerPredicate& layerPred1, const LayerPredicate& layerPred2) const { return layerPred1 == layerPred2; }
        boost::tribool operator() (const ClassPredicate& classPred1, const ClassPredicate& classPred2) const { return classPred1 == classPred2; }
        boost::tribool operator() (const AttachmentPredicate& attachmentPred1, const AttachmentPredicate& attachmentPred2) const { return attachmentPred1 == attachmentPred2; }

        boost::tribool operator() (const OpPredicate& opPred1, const OpPredicate& opPred2) const {
            if (opPred1.getFieldOrVar() == opPred2.getFieldOrVar()) {
                const Value& val1 = opPred1.getRefValue();
                const Value& val2 = opPred2.getRefValue();
                if (opPred2.getOp() == OpPredicate::Op::EQ) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::NEQ) {
                    return !OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred1.getOp() == OpPredicate::Op::LT && (opPred2.getOp() == OpPredicate::Op::LT || opPred2.getOp() == OpPredicate::Op::LTE)) {
                    return !OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred1.getOp() == OpPredicate::Op::LTE && (opPred2.getOp() == OpPredicate::Op::LT || opPred2.getOp() == OpPredicate::Op::LTE)) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::GT && (opPred2.getOp() == OpPredicate::Op::GT || opPred2.getOp() == OpPredicate::Op::GTE)) {
                    return !OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred1.getOp() == OpPredicate::Op::GTE && (opPred2.getOp() == OpPredicate::Op::GT || opPred2.getOp() == OpPredicate::Op::GTE)) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::MATCH) {
                    return val1 == val2 ? boost::tribool(true) : boost::indeterminate;
                }
                return false;
            }
            return boost::indeterminate;
        }

        boost::tribool operator() (const WhenPredicate& whenPred1, const WhenPredicate& whenPred2) const {
            if (std::visit(ExpressionDeepEqualsChecker(), whenPred1.getExpression(), whenPred2.getExpression())) {
                return true;
            }
            return boost::indeterminate;
        }

        template <typename S, typename T> boost::tribool operator() (const S& pred1, const T& pred2) const { return boost::indeterminate; }
    };

    struct PredicateIntersectsChecker {
        boost::tribool operator() (const MapPredicate& mapPred1, const MapPredicate& mapPred2) const { return mapPred1 == mapPred2; }
        boost::tribool operator() (const LayerPredicate& layerPred1, const LayerPredicate& layerPred2) const { return layerPred1 == layerPred2; }
        boost::tribool operator() (const ClassPredicate& classPred1, const ClassPredicate& classPred2) const { return classPred1 == classPred2; }
        boost::tribool operator() (const AttachmentPredicate& attachmentPred1, const AttachmentPredicate& attachmentPred2) const { return attachmentPred1 == attachmentPred2; }

        boost::tribool operator() (const OpPredicate& opPred1, const OpPredicate& opPred2) const {
            if (opPred1.getFieldOrVar() == opPred2.getFieldOrVar()) {
                const Value& val1 = opPred1.getRefValue();
                const Value& val2 = opPred2.getRefValue();
                if (opPred1.getOp() == OpPredicate::Op::EQ) {
                    return OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred2.getOp() == OpPredicate::Op::EQ) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::LTE && (opPred2.getOp() == OpPredicate::Op::GT || opPred2.getOp() == OpPredicate::Op::GTE)) {
                    return OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred1.getOp() == OpPredicate::Op::LT && (opPred2.getOp() == OpPredicate::Op::GT || opPred2.getOp() == OpPredicate::Op::GTE)) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::GTE && (opPred2.getOp() == OpPredicate::Op::LT || opPred2.getOp() == OpPredicate::Op::LTE)) {
                    return OpPredicate::applyOp(opPred2.getOp(), val1, val2);
                }
                if (opPred1.getOp() == OpPredicate::Op::GT && (opPred2.getOp() == OpPredicate::Op::LT || opPred2.getOp() == OpPredicate::Op::LTE)) {
                    return OpPredicate::applyOp(opPred1.getOp(), val2, val1);
                }
                if (opPred1.getOp() == OpPredicate::Op::MATCH) {
                    return val1 == val2 ? boost::tribool(true) : boost::indeterminate;
                }
                return true;
            }
            return boost::indeterminate;
        }

        boost::tribool operator() (const WhenPredicate& whenPred1, const WhenPredicate& whenPred2) const {
            if (std::visit(ExpressionDeepEqualsChecker(), whenPred1.getExpression(), whenPred2.getExpression())) {
                return true;
            }
            return boost::indeterminate;
        }

        template <typename S, typename T> boost::tribool operator() (const S& pred1, const T& pred2) const { return boost::indeterminate; }
    };
}

#endif
