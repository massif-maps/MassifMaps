#include "Expression.h"
#include "mapnikvt/StringUtils.h"

#include <stdexcept>
#include <utility>

#include <boost/lexical_cast.hpp>

namespace {
    using carto::css::Color;
    using carto::css::Value;

    struct NotOp {
        Value operator() (bool val) const { return Value(!val); }
        template <typename T> Value operator() (T val) const { throw std::invalid_argument("Unexpected type in ! operator"); }
    };
    
    struct NegOp {
        Value operator() (long long val) const { return Value(-val); }
        Value operator() (double val) const { return Value(-val); }
        template <typename T> Value operator() (T val) const { throw std::invalid_argument("Unexpected type in - operator"); }
    };

    struct AndOp {
        Value operator() (bool val1, bool val2) const { return Value(val1 && val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary && operator"); }
    };

    struct OrOp {
        Value operator() (bool val1, bool val2) const { return Value(val1 || val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary || operator"); }
    };

    template <template <typename> class OpImpl, bool NullResult, bool MismatchResult>
    struct CompOp  {
        Value operator() (std::monostate, std::monostate) const { return Value(NullResult); }
        Value operator() (bool val1, long long val2) const { return Value(OpImpl<long long>()(static_cast<long long>(val1), val2)); }
        Value operator() (bool val1, double val2) const { return Value(OpImpl<double>()(static_cast<double>(val1), val2)); }
        Value operator() (long long val1, bool val2) const { return Value(OpImpl<long long>()(val1, static_cast<long long>(val2))); }
        Value operator() (long long val1, double val2) const { return Value(OpImpl<double>()(static_cast<double>(val1), val2)); }
        Value operator() (double val1, bool val2) const { return Value(OpImpl<double>()(val1, static_cast<double>(val2))); }
        Value operator() (double val1, long long val2) const { return Value(OpImpl<double>()(val1, static_cast<double>(val2))); }
        template <typename T> Value operator() (T val1, T val2) const { return Value(OpImpl<T>()(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(MismatchResult); }
    };

    struct MatchOp {
        Value operator() (const std::string& val1, const std::string& val2) const { return Value(carto::mvt::regexMatch(val1, val2)); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { return Value(false); }
    };

    struct AddOp {
        Value operator() (const std::string& val1, const std::string& val2) const { return Value(val1 + val2); }
        Value operator() (const std::string& val1, std::monostate) const { return Value(val1); }
        Value operator() (std::monostate, const std::string& val2) const { return Value(val2); }
        template <typename T> Value operator() (const std::string& val1, T val2) const { return Value(val1 + boost::lexical_cast<std::string>(val2)); }
        template <typename S> Value operator() (S val1, const std::string& val2) const { return Value(boost::lexical_cast<std::string>(val1) + val2); }
        Value operator() (long long val1, long long val2) const { return Value(val1 + val2); }
        Value operator() (long long val1, double val2) const { return Value(static_cast<double>(val1) + val2); }
        Value operator() (double val1, long long val2) const { return Value(val1 + static_cast<double>(val2)); }
        Value operator() (double val1, double val2) const { return Value(val1 + val2); }
        Value operator() (Color val1, Color val2) const { return Value(val1 + val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary + operator"); }
    };

    struct SubOp {
        Value operator() (long long val1, long long val2) const { return Value(val1 - val2); }
        Value operator() (long long val1, double val2) const { return Value(static_cast<double>(val1) - val2); }
        Value operator() (double val1, long long val2) const { return Value(val1 - static_cast<double>(val2)); }
        Value operator() (double val1, double val2) const { return Value(val1 - val2); }
        Value operator() (Color val1, Color val2) const { return Value(val1 - val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary - operator"); }
    };

    struct MulOp {
        Value operator() (long long val1, long long val2) const { return Value(val1 * val2); }
        Value operator() (long long val1, double val2) const { return Value(static_cast<double>(val1) * val2); }
        Value operator() (double val1, long long val2) const { return Value(val1 * static_cast<double>(val2)); }
        Value operator() (double val1, double val2) const { return Value(val1 * val2); }
        Value operator() (Color val1, Color val2) const { return Value(val1 * val2); }
        Value operator() (Color val1, long long val2) const { return Value(val1 * static_cast<float>(val2)); }
        Value operator() (Color val1, double val2) const { return Value(val1 * static_cast<float>(val2)); }
        Value operator() (long long val1, Color val2) const { return Value(static_cast<float>(val1) * val2); }
        Value operator() (double val1, Color val2) const { return Value(static_cast<float>(val1) * val2); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary * operator"); }
    };

    struct DivOp {
        Value operator() (long long val1, long long val2) const { return val2 == 0 ? Value() : Value(static_cast<double>(val1) / static_cast<double>(val2)); }
        Value operator() (long long val1, double val2) const { return Value(static_cast<double>(val1) / val2); }
        Value operator() (double val1, long long val2) const { return Value(val1 / static_cast<double>(val2)); }
        Value operator() (double val1, double val2) const { return Value(val1 / val2); }
        Value operator() (Color val1, Color val2) const { return Value(val1 / val2); }
        Value operator() (Color val1, long long val2) const { return Value(val1 * (1.0f / static_cast<float>(val2))); }
        Value operator() (Color val1, double val2) const { return Value(val1 * (1.0f / static_cast<float>(val2))); }
        template <typename S, typename T> Value operator() (S val1, T val2) const { throw std::invalid_argument("Unexpected types in binary / operator"); }
    };
}

namespace carto { namespace css {
    Value UnaryExpression::applyOp(Op op, const Value& val) {
        switch (op) {
        case Op::NOT:
            return std::visit(NotOp(), val);
        case Op::NEG:
            return std::visit(NegOp(), val);
        }
        throw std::invalid_argument("Unsupported unary operation");
    }

    Value BinaryExpression::applyOp(Op op, const Value& val1, const Value& val2) {
        switch (op) {
        case Op::AND:
            return std::visit(AndOp(), val1, val2);
        case Op::OR:
            return std::visit(OrOp(), val1, val2);
        case Op::EQ:
            return std::visit(CompOp<std::equal_to, true, false>(), val1, val2);
        case Op::NEQ:
            return std::visit(CompOp<std::not_equal_to, false, true>(), val1, val2);
        case Op::LT:
            return std::visit(CompOp<std::less, false, false>(), val1, val2);
        case Op::LTE:
            return std::visit(CompOp<std::less_equal, true, false>(), val1, val2);
        case Op::GT:
            return std::visit(CompOp<std::greater, false, false>(), val1, val2);
        case Op::GTE:
            return std::visit(CompOp<std::greater_equal, true, false>(), val1, val2);
        case Op::MATCH:
            return std::visit(MatchOp(), val1, val2);
        case Op::ADD:
            return std::visit(AddOp(), val1, val2);
        case Op::SUB:
            return std::visit(SubOp(), val1, val2);
        case Op::MUL:
            return std::visit(MulOp(), val1, val2);
        case Op::DIV:
            return std::visit(DivOp(), val1, val2);
        }
        throw std::invalid_argument("Unsupported binary operation");
    }

    Value FunctionExpression::applyFunc(const std::string& func, const std::vector<Value>& vals) {
        if (func == "url" && vals.size() == 1) {
            return Value(getString(vals[0]));
        }
        else if (func == "rgb" && vals.size() == 3) {
            Color color = Color::fromRGBA(getFloat(vals[0]) / 255.0f, getFloat(vals[1]) / 255.0f, getFloat(vals[2]) / 255.0f, 1.0f);
            return Value(color);
        }
        else if (func == "rgba" && vals.size() == 4) {
            Color color = Color::fromRGBA(getFloat(vals[0]) / 255.0f, getFloat(vals[1]) / 255.0f, getFloat(vals[2]) / 255.0f, getFloat(vals[3]));
            return Value(color);
        }
        else if (func == "hsl" && vals.size() == 3) {
            Color color = Color::fromHSLA(getFloat(vals[0]), getFloat(vals[1]), getFloat(vals[2]), 1.0f);
            return Value(color);
        }
        else if (func == "hsla" && vals.size() == 4) {
            Color color = Color::fromHSLA(getFloat(vals[0]), getFloat(vals[1]), getFloat(vals[2]), getFloat(vals[3]));
            return Value(color);
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
            float value = getColor(vals[0]).hsla()[2];
            return Value(value);
        }
        else if (func == "mix" && vals.size() == 3) {
            Color color = Color::mix(getColor(vals[0]), getColor(vals[1]), getFloat(vals[2]));
            return Value(color);
        }
        else if (func == "lighten" && vals.size() == 2) {
            Color color = Color::lighten(getColor(vals[0]), getFloat(vals[1]));
            return Value(color);
        }
        else if (func == "darken" && vals.size() == 2) {
            Color color = Color::lighten(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color);
        }
        else if (func == "saturate" && vals.size() == 2) {
            Color color = Color::saturate(getColor(vals[0]), getFloat(vals[1]));
            return Value(color);
        }
        else if (func == "desaturate" && vals.size() == 2) {
            Color color = Color::saturate(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color);
        }
        else if (func == "fadein" && vals.size() == 2) {
            Color color = Color::fade(getColor(vals[0]), getFloat(vals[1]));
            return Value(color);
        }
        else if (func == "fadeout" && vals.size() == 2) {
            Color color = Color::fade(getColor(vals[0]), -getFloat(vals[1]));
            return Value(color);
        }
        return Value();
    }

    Color FunctionExpression::getColor(const Value& value) {
        if (auto colorVal = std::get_if<Color>(&value)) {
            return *colorVal;
        }
        throw std::invalid_argument("Wrong type, expecting color");
    }
    
    float FunctionExpression::getFloat(const Value& value) {
        if (auto longVal = std::get_if<long long>(&value)) {
            return static_cast<float>(*longVal);
        }
        else if (auto doubleVal = std::get_if<double>(&value)) {
            return static_cast<float>(*doubleVal);
        }
        throw std::invalid_argument("Wrong type, expecting float");
    }

    std::string FunctionExpression::getString(const Value& value) {
        if (auto strVal = std::get_if<std::string>(&value)) {
            return *strVal;
        }
        throw std::invalid_argument("Wrong type, expecting string");
    }
} }
