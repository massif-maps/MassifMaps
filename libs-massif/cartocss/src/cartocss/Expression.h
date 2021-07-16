/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_EXPRESSION_H_
#define _CARTO_CARTOCSS_EXPRESSION_H_

#include "Value.h"
#include "FieldOrVar.h"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace carto { namespace css {
    class StringExpression;
    class ListExpression;
    class UnaryExpression;
    class BinaryExpression;
    class ConditionalExpression;
    class FunctionExpression;

    using Expression = std::variant<Value, FieldOrVar, std::shared_ptr<StringExpression>, std::shared_ptr<ListExpression>, std::shared_ptr<UnaryExpression>, std::shared_ptr<BinaryExpression>, std::shared_ptr<ConditionalExpression>, std::shared_ptr<FunctionExpression>>;

    class StringExpression final {
    public:
        explicit StringExpression(std::string str) : _string(std::move(str)) { }

        const std::string& getString() const { return _string; }

        bool operator == (const StringExpression& other) const { return _string == other._string; }
        bool operator != (const StringExpression& other) const { return !(*this == other); }

    private:
        std::string _string;
    };

    class ListExpression final {
    public:
        explicit ListExpression(std::vector<Expression> exprs) : _exprs(std::move(exprs)) { }

        const std::vector<Expression>& getExpressions() const { return _exprs; }

        bool operator == (const ListExpression& other) const { return _exprs == other._exprs; }
        bool operator != (const ListExpression& other) const { return !(*this == other); }

    private:
        std::vector<Expression> _exprs;
    };

    class UnaryExpression final {
    public:
        enum class Op {
            NOT,
            NEG
        };

        explicit UnaryExpression(Op op, Expression expr) : _op(op), _expr(std::move(expr)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression() const { return _expr; }

        bool operator == (const UnaryExpression& other) const { return _op == other._op && _expr == other._expr; }
        bool operator != (const UnaryExpression& other) const { return !(*this == other); }

        static Value applyOp(Op op, const Value& val);

    private:
        Op _op;
        Expression _expr;
    };

    class BinaryExpression final {
    public:
        enum class Op {
            AND,
            OR,
            EQ,
            NEQ,
            LT,
            LTE,
            GT,
            GTE,
            MATCH,
            ADD,
            SUB,
            MUL,
            DIV
        };

        explicit BinaryExpression(Op op, Expression expr1, Expression expr2) : _op(op), _expr1(std::move(expr1)), _expr2(std::move(expr2)) { }

        Op getOp() const { return _op; }
        const Expression& getExpression1() const { return _expr1; }
        const Expression& getExpression2() const { return _expr2; }

        bool operator == (const BinaryExpression& other) const { return _op == other._op && _expr1 == other._expr1 && _expr2 == other._expr2; }
        bool operator != (const BinaryExpression& other) const { return !(*this == other); }

        static Value applyOp(Op op, const Value& val1, const Value& val2);

    private:
        Op _op;
        Expression _expr1;
        Expression _expr2;
    };

    class ConditionalExpression final {
    public:
        explicit ConditionalExpression(Expression cond, Expression expr1, Expression expr2) : _cond(std::move(cond)), _expr1(std::move(expr1)), _expr2(std::move(expr2)) { }

        const Expression& getCondition() const { return _cond; }
        const Expression& getExpression1() const { return _expr1; }
        const Expression& getExpression2() const { return _expr2; }

        bool operator == (const ConditionalExpression& other) const { return _cond == other._cond && _expr1 == other._expr1 && _expr2 == other._expr2; }
        bool operator != (const ConditionalExpression& other) const { return !(*this == other); }

    private:
        Expression _cond;
        Expression _expr1;
        Expression _expr2;
    };

    class FunctionExpression final {
    public:
        explicit FunctionExpression(std::string func, const std::vector<Expression>& args) : _func(std::move(func)), _args(std::move(args)) { }

        const std::string& getFunc() const { return _func; }
        const std::vector<Expression>& getArgs() const { return _args; }

        bool operator == (const FunctionExpression& other) const { return _func == other._func && _args == other._args; }
        bool operator != (const FunctionExpression& other) const { return !(*this == other); }

        static Value applyFunc(const std::string& func, const std::vector<Value>& vals);

    private:
        static Color getColor(const Value& value);
        static float getFloat(const Value& value);
        static std::string getString(const Value& value);

        std::string _func;
        std::vector<Expression> _args;
    };
} }

#endif
