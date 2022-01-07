/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_PREDICATE_H_
#define _CARTO_CARTOCSS_PREDICATE_H_

#include "Value.h"
#include "FieldOrVar.h"
#include "Expression.h"

#include <variant>

#include <boost/logic/tribool.hpp>

namespace carto::css {
    class MapPredicate;
    class LayerPredicate;
    class ClassPredicate;
    class AttachmentPredicate;
    class OpPredicate;
    class WhenPredicate;

    using Predicate = std::variant<MapPredicate, LayerPredicate, ClassPredicate, AttachmentPredicate, OpPredicate, WhenPredicate>;

    class MapPredicate final {
    public:
        MapPredicate() = default;

        bool operator == (const MapPredicate& other) const { return true; }
        bool operator != (const MapPredicate& other) const { return !(*this == other); }
    };

    class LayerPredicate final {
    public:
        explicit LayerPredicate(std::string layerName) : _layerName(std::move(layerName)) { }

        const std::string& getLayerName() const { return _layerName; }

        bool operator == (const LayerPredicate& other) const { return _layerName == other._layerName; }
        bool operator != (const LayerPredicate& other) const { return !(*this == other); }

    private:
        std::string _layerName;
    };

    class ClassPredicate final {
    public:
        explicit ClassPredicate(std::string cls) : _cls(std::move(cls)) { }

        const std::string& getClass() const { return _cls; }

        bool operator == (const ClassPredicate& other) const { return _cls == other._cls; }
        bool operator != (const ClassPredicate& other) const { return !(*this == other); }

    private:
        std::string _cls;
    };

    class AttachmentPredicate final {
    public:
        explicit AttachmentPredicate(std::string attachment) : _attachment(std::move(attachment)) { }

        const std::string& getAttachment() const { return _attachment; }

        bool operator == (const AttachmentPredicate& other) const { return _attachment == other._attachment; }
        bool operator != (const AttachmentPredicate& other) const { return !(*this == other); }

    private:
        std::string _attachment;
    };

    class OpPredicate final {
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

        explicit OpPredicate(Op op, const FieldOrVar& fieldOrVar, Value refValue) : _op(op), _fieldOrVar(fieldOrVar), _refValue(std::move(refValue)) { }

        Op getOp() const { return _op; }
        const FieldOrVar& getFieldOrVar() const { return _fieldOrVar; }
        const Value& getRefValue() const { return _refValue; }

        bool operator == (const OpPredicate& other) const { return _op == other._op && _fieldOrVar == other._fieldOrVar && _refValue == other._refValue; }
        bool operator != (const OpPredicate& other) const { return !(*this == other); }

        static boost::tribool applyOp(Op op, const Value& val1, const Value& val2);

    private:
        Op _op;
        FieldOrVar _fieldOrVar;
        Value _refValue;
    };

    class WhenPredicate final {
    public:
        explicit WhenPredicate(Expression expr) : _expr(std::move(expr)) { }

        const Expression& getExpression() const { return _expr; }

        bool operator == (const WhenPredicate& other) const { return _expr == other._expr; }
        bool operator != (const WhenPredicate& other) const { return !(*this == other); }

    private:
        Expression _expr;
    };
}

#endif
