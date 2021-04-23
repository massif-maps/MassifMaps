/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSION_H_
#define _CARTO_MAPNIKVT_EXPRESSION_H_

#include "ExpressionPredicateBase.h"
#include "ValueConverter.h"

#include <array>
#include <memory>
#include <variant>
#include <functional>
#include <vector>

#include <cglib/fcurve.h>

namespace carto { namespace mvt {
    class VariableExpression final {
    public:
        explicit VariableExpression(std::string variableName) : _variableExpr(Value(std::move(variableName))) { }
        explicit VariableExpression(Expression variableExpr) : _variableExpr(std::move(variableExpr)) { }

        const Expression& getVariableExpression() const { return _variableExpr; }

    private:
        const Expression _variableExpr;
    };

    class UnaryExpression final {
    public:
        enum class Op {
            NEG,
            EXP,
            LOG,
            LENGTH,
            LOWER,
            UPPER,
            CAPITALIZE
        };

        explicit UnaryExpression(Op op, Expression expr) : _op(op), _expr(std::move(expr)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression() const { return _expr; }

        static Value applyOp(Op op, const Value& val);

    private:
        const Op _op;
        const Expression _expr;
    };

    class BinaryExpression final {
    public:
        enum class Op {
            ADD,
            SUB,
            MUL,
            DIV,
            MOD,
            POW,
            CONCAT
        };

        explicit BinaryExpression(Op op, Expression expr1, Expression expr2) : _op(op), _expr1(std::move(expr1)), _expr2(std::move(expr2)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression1() const { return _expr1; }
        const Expression& getExpression2() const { return _expr2; }

        static Value applyOp(Op op, const Value& val1, const Value& val2);

    protected:
        const Op _op;
        const Expression _expr1;
        const Expression _expr2;
    };

    class TertiaryExpression final {
    public:
        enum class Op {
            REPLACE,
            CONDITIONAL
        };

        explicit TertiaryExpression(Op op, Expression expr1, Expression expr2, Expression expr3) : _op(op), _expr1(std::move(expr1)), _expr2(std::move(expr2)), _expr3(std::move(expr3)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression1() const { return _expr1; }
        const Expression& getExpression2() const { return _expr2; }
        const Expression& getExpression3() const { return _expr3; }

        static Value applyOp(Op op, const Value& val1, const Value& val2, const Value& val3);

    private:
        const Op _op;
        const Expression _expr1;
        const Expression _expr2;
        const Expression _expr3;
    };

    class InterpolateExpression final {
    public:
        enum class Method {
            STEP,
            LINEAR,
            CUBIC
        };
        
        explicit InterpolateExpression(Method method, Expression timeExpr, std::vector<Value> keyFrames) : _method(method), _timeExpr(std::move(timeExpr)), _keyFrames(std::move(keyFrames)), _fcurve(buildFCurve(method, _keyFrames)) { }

        Method getMethod() const { return _method; }
        const Expression& getTimeExpression() const { return _timeExpr; }
        const std::vector<Value>& getKeyFrames() const { return _keyFrames; }

        Value evaluate(float t) const;

    private:
        static std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> buildFCurve(Method method, const std::vector<Value>& keyFrames);

        const Method _method;
        const Expression _timeExpr;
        const std::vector<Value> _keyFrames;
        const std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> _fcurve;
    };
} }

#endif
