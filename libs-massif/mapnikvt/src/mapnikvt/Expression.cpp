#include "Expression.h"
#include "ExpressionUtils.h"
#include "StringUtils.h"
#include "ValueConverter.h"

#include <algorithm>

namespace {
    using carto::mvt::Value;
    using carto::mvt::ValueConverter;

    struct NegOperator {
        Value operator() (bool val) const { return Value(-static_cast<long long>(val)); }
        Value operator() (long long val) const { return Value(-val); }
        Value operator() (double val) const { return Value(-val); }
        template <typename T> Value operator() (T val) const { return Value(val); }
    };

    struct ExpOperator {
        Value operator() (long long val) const { return Value(std::exp(static_cast<double>(val))); }
        Value operator() (double val) const { return Value(std::exp(val)); }
        template <typename T> Value operator() (T val) const { return Value(val); }
    };

    struct LogOperator {
        Value operator() (long long val) const { return Value(std::log(static_cast<double>(val))); }
        Value operator() (double val) const { return Value(std::log(val)); }
        template <typename T> Value operator() (T val) const { return Value(val); }
    };

    template <template <typename T> class Op>
    struct ArithmeticOperator {
        Value operator() (bool val1, bool val2) const { return Value(Op<long long>()(static_cast<long long>(val1), static_cast<long long>(val2))); }
        Value operator() (long long val1, long long val2) const { return Value(Op<long long>()(val1, val2)); }
        Value operator() (long long val1, double val2) const { return Value(Op<double>()(static_cast<double>(val1), val2)); }
        Value operator() (double val1, long long val2) const { return Value(Op<double>()(val1, static_cast<double>(val2))); }
        Value operator() (double val1, double val2) const { return Value(Op<double>()(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(val1); }
    };

    struct AddOperator {
        Value operator() (const std::string& val1, const std::string& val2) const { return Value(val1 + val2); }
        template <typename T> Value operator() (const std::string& val1, T val2) const { return Value(val1 + ValueConverter<std::string>::convert(val2)); }
        template <typename S> Value operator() (S val1, const std::string& val2) const { return Value(ValueConverter<std::string>::convert(val1) + val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return ArithmeticOperator<std::plus>()(val1, val2); }
    };

    using SubOperator = ArithmeticOperator<std::minus>;

    using MulOperator = ArithmeticOperator<std::multiplies>;

    struct DivOperator {
        Value operator() (long long val1, long long val2) const { return val2 != 0 ? Value(val1 / val2) : Value(); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return ArithmeticOperator<std::divides>()(val1, val2); }
    };

    struct ModOperator {
        Value operator() (long long val1, long long val2) const { return val2 != 0 ? Value(val1 % val2) : Value(); }
        Value operator() (long long val1, double val2) const { return Value(std::fmod(static_cast<double>(val1), val2)); }
        Value operator() (double val1, long long val2) const { return Value(std::fmod(val1, static_cast<double>(val2))); }
        Value operator() (double val1, double val2) const { return Value(std::fmod(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(val1); }
    };

    struct PowOperator {
        Value operator() (long long val1, long long val2) const { return Value(std::pow(static_cast<double>(val1), static_cast<double>(val2))); }
        Value operator() (long long val1, double val2) const { return Value(std::pow(static_cast<double>(val1), val2)); }
        Value operator() (double val1, long long val2) const { return Value(std::pow(val1, static_cast<double>(val2))); }
        Value operator() (double val1, double val2) const { return Value(std::pow(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(val1); }
    };
}

namespace carto { namespace mvt {
    std::string VariableExpression::getVariableName(const ExpressionContext& context) const {
        return ValueConverter<std::string>::convert(std::visit(ExpressionEvaluator(context), _variableExpr)); 
    }

    Value UnaryExpression::applyOp(Op op, const Value& val) {
        switch (op) {
        case Op::NEG:
            return std::visit(NegOperator(), val);
        case Op::EXP:
            return std::visit(ExpOperator(), val);
        case Op::LOG:
            return std::visit(LogOperator(), val);
        case Op::LENGTH:
            return Value(static_cast<long long>(stringLength(ValueConverter<std::string>::convert(val))));
        case Op::UPPER:
            return Value(toUpper(ValueConverter<std::string>::convert(val)));
        case Op::LOWER:
            return Value(toLower(ValueConverter<std::string>::convert(val)));
        case Op::CAPITALIZE:
            return Value(capitalize(ValueConverter<std::string>::convert(val)));
        }
        throw std::invalid_argument("Illegal operator");
    }

    Value BinaryExpression::applyOp(Op op, const Value& val1, const Value& val2) {
        switch (op) {
        case Op::ADD:
            return std::visit(AddOperator(), val1, val2);
        case Op::SUB:
            return std::visit(SubOperator(), val1, val2);
        case Op::MUL:
            return std::visit(MulOperator(), val1, val2);
        case Op::DIV:
            return std::visit(DivOperator(), val1, val2);
        case Op::MOD:
            return std::visit(ModOperator(), val1, val2);
        case Op::POW:
            return std::visit(PowOperator(), val1, val2);
        case Op::CONCAT:
            return Value(ValueConverter<std::string>::convert(val1) + ValueConverter<std::string>::convert(val2));
        }
        throw std::invalid_argument("Illegal operator");
    }

    Value TertiaryExpression::applyOp(Op op, const Value& val1, const Value& val2, const Value& val3) {
        switch (op) {
        case Op::REPLACE:
            return Value(regexReplace(ValueConverter<std::string>::convert(val1), ValueConverter<std::string>::convert(val2), ValueConverter<std::string>::convert(val3)));
        case Op::CONDITIONAL:
            return ValueConverter<bool>::convert(val1) ? val2 : val3;
        }
        throw std::invalid_argument("Illegal operator");
    }

    float InterpolateExpression::evaluate(float t) const {
         return _fcurve.evaluate(t)(1);
    }

    cglib::fcurve2<float> InterpolateExpression::buildFCurve(Method method, const std::vector<Value>& keyFrames) {
        cglib::fcurve_type type = cglib::fcurve_type::linear;
        switch (method) {
        case Method::STEP:
            type = cglib::fcurve_type::step;
            break;
        case Method::LINEAR:
            type = cglib::fcurve_type::linear;
            break;
        case Method::CUBIC:
            type = cglib::fcurve_type::cubic;
            break;
        }

        std::vector<cglib::vec2<float>> keyFramesList;
        for (std::size_t i = 0; i + 1 < keyFrames.size(); i += 2) {
            keyFramesList.emplace_back(ValueConverter<float>::convert(keyFrames[i + 0]), ValueConverter<float>::convert(keyFrames[i + 1]));
        }

        return cglib::fcurve2<float>::create(type, keyFramesList.begin(), keyFramesList.end());
    }
} }
