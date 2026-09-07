#include "Expression.h"
#include "ExpressionUtils.h"
#include "TransformUtils.h"
#include "StringUtils.h"
#include "ValueConverter.h"
#include "ParserUtils.h"
#include "GeneratorUtils.h"

#include <algorithm>
#include <functional>

namespace {
    using massif::mvt::Value;
    using massif::mvt::ValueConverter;

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
        template <typename S, typename T> Value operator() (S val1, T val2) const { return ArithmeticOperator<std::modulus>()(val1, val2); }
    };

    struct PowOperator {
        Value operator() (long long val1, long long val2) const { return Value(std::pow(static_cast<double>(val1), static_cast<double>(val2))); }
        Value operator() (long long val1, double val2) const { return Value(std::pow(static_cast<double>(val1), val2)); }
        Value operator() (double val1, long long val2) const { return Value(std::pow(val1, static_cast<double>(val2))); }
        Value operator() (double val1, double val2) const { return Value(std::pow(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(val1); }
    };

    struct AndOperator {
        Value operator() (long long val1, long long val2) const { return Value(val1 & val2); }
        Value operator() (long long val1, double val2) const { return Value(val1 & static_cast<long>(val2)); }
        Value operator() (double val1, long long val2) const { return Value(static_cast<long>(val1) & val2); }
        Value operator() (double val1, double val2) const { return Value(static_cast<long>(val1) & static_cast<long>(val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return ArithmeticOperator<std::bit_and>()(val1, val2); }
    };

    struct XorOperator {
        Value operator() (long long val1, long long val2) const { return Value(val1 ^ val2); }
        Value operator() (long long val1, double val2) const { return Value(val1 ^ static_cast<long>(val2)); }
        Value operator() (double val1, long long val2) const { return Value(static_cast<long>(val1) ^ val2); }
        Value operator() (double val1, double val2) const { return Value(static_cast<long>(val1) ^ static_cast<long>(val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return ArithmeticOperator<std::bit_xor>()(val1, val2); }
    };

    struct CondEvaluator {
        template <typename T> bool operator() (T val) const { return val != T(); }
    };
}

namespace massif::mvt {

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
        case Op::BITWISE_AND:
            return std::visit(AndOperator(), val1, val2);
        case Op::XOR:
            return std::visit(XorOperator(), val1, val2);
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

    /**
     * Where a LINEAR curve has to be sampled to give mapbox's exponential result.
     *
     * Between two key frames mapbox uses `s = (b^(x-x0) - 1) / (b^(x1-x0) - 1)` where a linear
     * curve uses `(x-x0) / (x1-x0)`. Feeding the linear curve `x0 + s * (x1 - x0)` therefore
     * yields the exponential value exactly, with no second curve type to implement and no change
     * to cglib. Outside the key range, and for a base of 1, this is the identity.
     *
     * The key POSITIONS have to be constants for this, which every zoom ramp's are; a computed one
     * falls through and interpolates linearly, as it did before.
     */
    float InterpolateExpression::remapExponential(float t) const {
        if (!(_base > 0) || _base == 1.0f) {
            return t;
        }
        float prevKey = 0;
        bool havePrev = false;
        for (std::size_t i = 0; i + 1 < _keyFrames.size(); i += 2) {
            auto keyVal = std::get_if<mvt::Value>(&_keyFrames[i]);
            if (!keyVal) {
                return t;
            }
            float key = ValueConverter<float>::convert(*keyVal);
            if (havePrev && t >= prevKey && t <= key && key > prevKey) {
                float span = key - prevKey;
                float s = (std::pow(_base, t - prevKey) - 1.0f) / (std::pow(_base, span) - 1.0f);
                return prevKey + s * span;
            }
            prevKey = key;
            havePrev = true;
        }
        return t;
    }

    Value InterpolateExpression::evaluate(float t, const ExpressionContext& context) const {
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
        // The constant curve is evaluated in place: it owns a vector of key frames, so
        // taking it by value copied (and heap-allocated) that vector on every evaluation -
        // and this runs per style parameter per draw call.
        if (_discrete) {
            return evaluateDiscrete(t, context);
        }
        // Past the last key the curve HOLDS, and likewise before the first one - mapbox's
        // `interpolate`, and the only reading that makes sense of a zoom ramp: cglib extrapolates,
        // so Standard's POI minimum-distance (16, 6) -> (17, 4) went NEGATIVE by z19 and the
        // culler stopped thinning anything.
        if (_keyRange) {
            t = std::min(std::max(t, _keyRange->first), _keyRange->second);
        }
        float time = (_method == Method::EXPONENTIAL ? remapExponential(t) : t);
        if (_fcurve) {
            return std::visit(Evaluator(time), *_fcurve);
        }
        return std::visit(Evaluator(time), buildFCurve(_method, _keyFrames, context));
    }

    /**
     * A step whose values are not interpolatable: the key frame is returned verbatim, which is all
     * a step ever meant. Below the first key it is the first value, as mapbox's own base is.
     */
    Value InterpolateExpression::evaluateDiscrete(float t, const ExpressionContext& context) const {
        if (_keyFrames.size() < 2) {
            return Value();
        }
        Value result = std::visit(ExpressionEvaluator(context, nullptr), _keyFrames[1]);
        for (std::size_t i = 2; i + 1 < _keyFrames.size(); i += 2) {
            auto keyVal = std::get_if<mvt::Value>(&_keyFrames[i]);
            if (!keyVal || ValueConverter<float>::convert(*keyVal) > t) {
                break;
            }
            result = std::visit(ExpressionEvaluator(context, nullptr), _keyFrames[i + 1]);
        }
        return result;
    }

    bool InterpolateExpression::discreteKeyFrames(Method method, const std::vector<Expression>& keyFrames) {
        if (method != Method::STEP) {
            return false; // linear/cubic/exponential over such values has no meaning to fall back to
        }
        for (std::size_t i = 0; i + 1 < keyFrames.size(); i += 2) {
            auto val = std::get_if<mvt::Value>(&keyFrames[i + 1]);
            if (!val) {
                continue;
            }
            if (auto str = std::get_if<std::string>(val)) {
                vt::Color color;
                if (!tryParseColor(*str, color)) {
                    return true;
                }
            }
        }
        return false;
    }

    /** The key positions, when every one of them is a constant - a computed key cannot be clamped. */
    std::optional<std::pair<float, float>> InterpolateExpression::constantKeyRange(const std::vector<Expression>& keyFrames) {
        std::optional<std::pair<float, float>> range;
        for (std::size_t i = 0; i + 1 < keyFrames.size(); i += 2) {
            auto keyVal = std::get_if<mvt::Value>(&keyFrames[i]);
            if (!keyVal) {
                return std::nullopt;
            }
            float key = ValueConverter<float>::convert(*keyVal);
            range = range ? std::make_pair(std::min(range->first, key), std::max(range->second, key))
                          : std::make_pair(key, key);
        }
        return range;
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
                case Method::EXPONENTIAL:
                    // Same key frames as a linear curve; the CURVE is linear and the INPUT is
                    // remapped before it (see evaluate), which is exactly mapbox's definition.
                    type = cglib::fcurve_type::linear;
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
    std::variant<cglib::fcurve2<float>, cglib::fcurve5<float>> InterpolateExpression::buildFCurve(Method method, const std::vector<Expression>& keyFrames, const ExpressionContext& context) {
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
        case Method::EXPONENTIAL:
            type = cglib::fcurve_type::linear; // the input is remapped instead - see remapExponential
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
        // Table lookups: a style parameter can hold an object or an array, so a style reads a
        // per-class value out of one the app owns instead of declaring a parameter per class.
        if (func == "get" && (vals.size() == 2 || vals.size() == 3)) {
            Value value = getValueElement(vals[0], vals[1]);
            if (std::get_if<std::monostate>(&value) && vals.size() == 3) {
                return vals[2]; // the fallback for a key the table does not have
            }
            return value;
        }
        else if (func == "has" && vals.size() == 2) {
            Value value = getValueElement(vals[0], vals[1]);
            return Value(std::get_if<std::monostate>(&value) == nullptr);
        }
        else if (func == "length" && vals.size() == 1) {
            if (auto str = std::get_if<std::string>(&vals[0])) {
                return Value(static_cast<long long>(stringLength(*str)));
            }
            return Value(getValueSize(vals[0]));
        }
        else if (func == "url" && vals.size() == 1) {
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
