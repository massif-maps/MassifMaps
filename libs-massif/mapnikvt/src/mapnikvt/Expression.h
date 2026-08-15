/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_EXPRESSION_H_
#define _MASSIF_MAPNIKVT_EXPRESSION_H_

#include "ExpressionPredicateBase.h"
#include "ExpressionContext.h"
#include "ValueConverter.h"
#include "Transform.h"
#include "vt/Color.h"

#include <array>
#include <memory>
#include <variant>
#include <functional>
#include <vector>
#include <optional>

#include <cglib/fcurve.h>

namespace massif::mvt {
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
            AND,
            ADD,
            SUB,
            MUL,
            DIV,
            MOD,
            POW,
            CONCAT,
            NTIME,
            MIN,
            MAX,
            XOR,
            BITWISE_AND,
            NULLISH_COALESCING
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
        
        explicit InterpolateExpression(Method method, Expression timeExpr, std::vector<Expression> keyFrames) : _method(method), _timeExpr(std::move(timeExpr)), _keyFrames(std::move(keyFrames)), _fcurve(buildConstantFCurve(method, _keyFrames)) { }

        Method getMethod() const { return _method; }
        const Expression& getTimeExpression() const { return _timeExpr; }
        const std::vector<Expression>& getKeyFrames() const { return _keyFrames; }

        Value evaluate(float t, const ExpressionContext& context) const;

    private:
        static std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> buildFCurve(Method method, const std::vector<Expression>& , const ExpressionContext& context);
        static std::optional<std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>>> buildConstantFCurve(Method method, const std::vector<Expression>&);

        const Method _method;
        const Expression _timeExpr;
        const std::vector<Expression> _keyFrames;
        const std::optional<std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>>> _fcurve;
    };

    class TransformExpression final {
    public:
        using Transform = std::variant<MatrixTransform, TranslateTransform, RotateTransform, ScaleTransform, SkewXTransform, SkewYTransform>;

        explicit TransformExpression(Transform transform) : _transform(std::move(transform)) { }

        const Transform& getTransform() const { return _transform; }

        std::vector<Expression> getSubExpressions() const;

    private:
        const Transform _transform;
    };

    class FunctionExpression final {
    public:
        explicit FunctionExpression(std::string func, std::vector<Expression> args) : _func(std::move(func)), _args(std::move(args)) { }

        const std::string& getFunc() const { return _func; }
        const std::vector<Expression>& getExpressions() const { return _args; }

        bool operator == (const FunctionExpression& other) const { return _func == other._func && _args == other._args; }
        bool operator != (const FunctionExpression& other) const { return !(*this == other); }

        static Value applyFunc(const std::string& func, const std::vector<Value>& vals);

    private:static vt::Color getColor(const Value& value);
        static float getFloat(const Value& value);
        static std::string getString(const Value& value);
        std::string _func;
        std::vector<Expression> _args;
    };
}

#endif
