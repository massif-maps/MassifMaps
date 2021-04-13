#include "Expression.h"
#include "ExpressionUtils.h"
#include "StringUtils.h"
#include "ValueConverter.h"
#include "ParserUtils.h"
#include "GeneratorUtils.h"

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

    struct CondEvaluator {
        bool operator() (std::monostate) const { return false; }
        bool operator() (bool val) const { return val; }
        bool operator() (long long val) const { return val != 0; }
        bool operator() (double val) const { return val != 0; }
        bool operator() (const std::string& str) const { return !str.empty(); }
    };
}

namespace carto { namespace mvt {
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
            return std::visit(CondEvaluator(), val1) ? val2 : val3;
        }
        throw std::invalid_argument("Illegal operator");
    }

    Value InterpolateExpression::evaluate(float t) const {
        struct Evaluator {
            explicit Evaluator(float t) : _time(t) { }
            Value operator() (const cglib::fcurve2<float>& fcurve) const {
                return Value(fcurve.evaluate(_time)(1));
            }
            Value operator() (const cglib::fcurve5<float>& fcurve) const {
                cglib::vec<float, 5> result = fcurve.evaluate(_time);
                return Value(static_cast<long long>(vt::Color(result(1), result(2), result(3), result(4)).value()));
            }

        private:
            float _time;
        };
        
        return std::visit(Evaluator(t), _fcurve);
    }

    std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> InterpolateExpression::buildFCurve(Method method, const std::vector<Value>& keyFrames) {
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

        std::vector<cglib::vec2<float>> floatKeyFramesList;
        std::vector<cglib::vec<float, 5>> colorKeyFramesList;
        for (std::size_t i = 0; i + 1 < keyFrames.size(); i += 2) {
            float key = ValueConverter<float>::convert(keyFrames[i + 0]);
            const Value& val = keyFrames[i + 1];
            if (auto str = std::get_if<std::string>(&val)) {
                vt::Color color = parseColor(*str);
                colorKeyFramesList.emplace_back(cglib::vec<float, 5> {{ key, color[0], color[1], color[2], color[3] }});
            }
            else {
                floatKeyFramesList.emplace_back(key, ValueConverter<float>::convert(val));
            }
        }
        if (!colorKeyFramesList.empty()) {
            if (!floatKeyFramesList.empty()) {
                throw std::invalid_argument("Mismatched types in interpolation lists");
            }
            return cglib::fcurve5<float>::create(type, colorKeyFramesList.begin(), colorKeyFramesList.end());
        }
        return cglib::fcurve2<float>::create(type, floatKeyFramesList.begin(), floatKeyFramesList.end());
    }
} }
