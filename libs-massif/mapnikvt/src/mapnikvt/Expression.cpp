#include "Expression.h"
#include "ExpressionUtils.h"
#include "TransformUtils.h"
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
        template <typename T> bool operator() (T val) const { return val != T(); }
    };
}

namespace carto::mvt {

    std::string string_repeat(std::string str, const std::size_t n)
    {
        if (n <= 0) {
            str.clear();
            str.shrink_to_fit();
            return str;
        } else if (n == 1 || str.empty()) {
            return str;
        }
        const auto period = str.size();
        if (period == 1) {
            str.append(n - 1, str.front());
            return str;
        }
        str.reserve(period * n);
        std::size_t m {2};
        for (; m < n; m *= 2) str += str;
        str.append(str.c_str(), (n - (m / 2)) * period);
        return str;
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
        case Op::NTIME:
            return Value(string_repeat(std::move(ValueConverter<std::string>::convert(val1)), ValueConverter<int>::convert(val2)));
        case Op::MIN:
            return Value(std::min(ValueConverter<float>::convert(val1), ValueConverter<float>::convert(val2)));
        case Op::MAX:
            return Value(std::max(ValueConverter<float>::convert(val1), ValueConverter<float>::convert(val2)));
        case Op::NULLISH_COALESCING:
            return std::visit(CondEvaluator(), val1) ? val1 : val2;
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

    Value InterpolateExpression::evaluate(float t, ExpressionContext context) const {
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
        auto fcurve = _fcurve ? _fcurve.value(): buildFCurve(_method, _keyFrames, context);
        return std::visit(Evaluator(t), fcurve);
    }

    std::optional<std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>>> InterpolateExpression::buildConstantFCurve(Method method, const std::vector<Expression>& keyFrames) {
        bool isConstant = true;
        for (std::size_t i = 0; i + 1 < keyFrames.size(); i += 2) {
            auto val = std::get_if<mvt::Value>(&keyFrames[i + 1]);
            if (!val) {
                isConstant = false;
            }
        }
        if (isConstant) {
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
                auto keyVal = std::get_if<mvt::Value>(&keyFrames[i + 0]);
                float key = ValueConverter<float>::convert(*keyVal);
                auto val = std::get_if<mvt::Value>(&keyFrames[i + 1]);
                if (auto str = std::get_if<std::string>(val)) {
                    vt::Color color = parseColor(*str);
                    colorKeyFramesList.emplace_back(cglib::vec<float, 5>{ { key, color[0], color[1], color[2], color[3] } });
                }
                else {
                    floatKeyFramesList.emplace_back(key, ValueConverter<float>::convert(*val));
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
        return std::optional<std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>>>();
    }
    std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> InterpolateExpression::buildFCurve(Method method, const std::vector<Expression>& keyFrames, const ExpressionContext context) {
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
            auto keyVal = std::get_if<mvt::Value>(&keyFrames[i + 0]);
            float key = ValueConverter<float>::convert(*keyVal);
            const Expression &expr = keyFrames[i + 1];
            Value val = std::visit(ExpressionEvaluator(context, nullptr), expr);
            if (auto str = std::get_if<std::string>(&val)) {
                vt::Color color = parseColor(*str);
                colorKeyFramesList.emplace_back(cglib::vec<float, 5>{ { key, color[0], color[1], color[2], color[3] } });
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

    std::vector<Expression> TransformExpression::getSubExpressions() const {
        return std::visit(TransformSubExpressionBuilder(), _transform);
    }


    vt::Color FunctionExpression::getColor(const Value& value) {
        if (auto colorVal = std::get_if<std::string>(&value)) {
            return parseColor(*colorVal);
        }
        return vt::Color::fromValue(ValueConverter<unsigned int>::convert(value));
    }

    float FunctionExpression::getFloat(const Value& value) {
        return ValueConverter<float>::convert(value);

    }
    Value FunctionExpression::applyFunc(const std::string& func, const std::vector<Value>& vals) {
        if (func == "url" && vals.size() == 1) {
            return Value(ValueConverter<std::string>::convert(vals[0]));
        }
        else if (func == "color" && vals.size() == 1) {
            unsigned int value = 0;
            vt::Color color = parseColor(ValueConverter<std::string>::convert(vals[0]));
            return Value(color.value());
        }
        else if (func == "rgb" && vals.size() == 3) {
            vt::Color color = vt::Color::fromRGBA(getFloat(vals[0]) / 255.0f, getFloat(vals[1]) / 255.0f, getFloat(vals[2]) / 255.0f, 1.0f);
            return Value(color.value());
        }
        else if (func == "rgba" && vals.size() == 4) {
            vt::Color color = vt::Color::fromRGBA(getFloat(vals[0]) / 255.0f, getFloat(vals[1]) / 255.0f, getFloat(vals[2]) / 255.0f, getFloat(vals[3]));
            return Value(color.value());
        }
        else if (func == "hsl" && vals.size() == 3) {
            vt::Color color = vt::Color::fromHSLA(getFloat(vals[0]), getFloat(vals[1]), getFloat(vals[2]), 1.0f);
            return Value(color.value());
        }
        else if (func == "hsla" && vals.size() == 4) {
            vt::Color color = vt::Color::fromHSLA(getFloat(vals[0]), getFloat(vals[1]), getFloat(vals[2]), getFloat(vals[3]));
            return Value(color.value());
        }
        else if (func == "red" && vals.size() == 1) {
            float value = getColor(vals[0]).rgba()[0] * 255.0f;
            return Value(value);
        }
        else if (func == "green" && vals.size() == 1) {
            float value = getColor(vals[0]).rgba()[1] * 255.0f;
            return Value(value);
        }
        else if (func == "blue" && vals.size() == 1) {
            float value = getColor(vals[0]).rgba()[2] * 255.0f;
            return Value(value);
        }
        else if (func == "alpha" && vals.size() == 1) {
            float value = getColor(vals[0]).alpha();
            return Value(value);
        }
        else if (func == "hue" && vals.size() == 1) {
            float value = getColor(vals[0]).hsla()[0];
            return Value(value);
        }
        else if (func == "saturation" && vals.size() == 1) {
            float value = getColor(vals[0]).hsla()[1];
            return Value(value);
        }
        else if (func == "lightness" && vals.size() == 1) {
            vt::Color color = getColor(vals[0]);
            return Value(color.hsla()[2]);
        }
        else if (func == "brightness" && vals.size() == 1) {
            vt::Color color = getColor(vals[0]);
            return Value(color.brightness()* 255.0f);
        }
        else if (func == "mix" && vals.size() == 3) {
            vt::Color color = vt::Color::mix(getColor(vals[0]), getColor(vals[1]), getFloat(vals[2]));
            return Value(color.value());
        }
        else if (func == "lighten" && vals.size() == 2) {
            vt::Color color = vt::Color::lighten(getColor(vals[0]), getFloat(vals[1]));
            return Value(color.value());
        }
        else if (func == "darken" && vals.size() == 2) {
            vt::Color color = vt::Color::lighten(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color.value());
        }
        else if (func == "saturate" && vals.size() == 2) {
            vt::Color color = vt::Color::saturate(getColor(vals[0]), getFloat(vals[1]));
            return Value(color.value());
        }
        else if (func == "desaturate" && vals.size() == 2) {
            vt::Color color = vt::Color::saturate(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color.value());
        }
        else if (func == "fadein" && vals.size() == 2) {
            vt::Color color = vt::Color::fade(getColor(vals[0]), getFloat(vals[1]));
            return Value(color.value());
        }
        else if (func == "fadeout" && vals.size() == 2) {
            vt::Color color = vt::Color::fade(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color.value());
        }
        return Value();
    }
}
