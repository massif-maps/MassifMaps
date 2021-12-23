/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_SYMBOLIZERPARAMETER_H_
#define _CARTO_MAPNIKVT_SYMBOLIZERPARAMETER_H_

#include "Expression.h"
#include "ExpressionContext.h"
#include "ExpressionUtils.h"
#include "ParserUtils.h"
#include "StringUtils.h"
#include "TransformUtils.h"
#include "vt/Color.h"
#include "vt/Transform.h"
#include "vt/Styles.h"

#include <memory>
#include <optional>
#include <mutex>
#include <set>
#include <functional>

namespace carto { namespace mvt {
    struct SymbolizerParameter {
        virtual ~SymbolizerParameter() = default;

        virtual bool isDefined() const = 0;
        virtual const Expression& getExpression() const = 0;
        virtual void setExpression(const Expression& expr) = 0;

    protected:
        struct DependencyChecker {
            DependencyChecker() = delete;
            explicit DependencyChecker(bool& contextVars, bool& viewStateVars) : _contextVars(contextVars), _viewStateVars(viewStateVars) { }
            
            void operator() (const std::shared_ptr<VariableExpression>& varExpr) {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    std::string name = ValueConverter<std::string>::convert(*val);
                    if (ExpressionContext::isViewStateVariable(name)) {
                        _viewStateVars = true; // view variables do not depend on expression context, just on view state
                    }
                    else {
                        _contextVars = true; // mapnik and nutiparameters are counted as 'normal' variables, as they cause dependency on expression context
                    }
                }
                else {
                    _contextVars = _viewStateVars = true; // generic expression, must assume both context and view variables are used
                }
            }
        
        private:
            bool& _contextVars;
            bool& _viewStateVars;
        };

        static vt::Color convertColor(const Value& val) {
            if (auto longVal = std::get_if<long long>(&val)) {
                return vt::Color::fromValue(static_cast<unsigned int>(*longVal));
            }
            return parseColor(ValueConverter<std::string>::convert(val));
        }
    };

    template <typename T>
    struct GenericValueParameter : SymbolizerParameter {
        virtual bool isDefined() const override { return _defined; }

        virtual const Expression& getExpression() const override { return _expr; }

        virtual void setExpression(const Expression& expr) override {
            _expr = expr;
            _defined = true;
            _contextVars = _viewStateVars = false;
            std::visit(ExpressionVariableVisitor(DependencyChecker(_contextVars, _viewStateVars)), expr);
            if (!_contextVars && !_viewStateVars) {
                _value = buildValue(ExpressionContext());
            }
        }

        T getValue(const ExpressionContext& context) const {
            if (!_contextVars && !_viewStateVars) {
                return _value;
            }
            return buildValue(context);
        }

    protected:
        GenericValueParameter() = default;
        explicit GenericValueParameter(const T& defaultValue) : _value(defaultValue), _expr(Value(defaultValue)) { }

        template <typename S>
        void initialize(const S& defaultValue) { _expr = Value(defaultValue); _value = buildValue(ExpressionContext()); }

        virtual T buildValue(const ExpressionContext& context) const = 0;

        bool _defined = false;
        bool _contextVars = false;
        bool _viewStateVars = false;
        T _value = T();
        Expression _expr;
    };

    struct ValueParameter : GenericValueParameter<Value> {
        ValueParameter() : ValueParameter(Value()) { }
        explicit ValueParameter(const Value& defaultValue) : GenericValueParameter(defaultValue) { }

    protected:
        virtual Value buildValue(const ExpressionContext& context) const override {
            return std::visit(ExpressionEvaluator(context, nullptr), _expr);
        }
    };

    struct BoolParameter : GenericValueParameter<bool> {
        BoolParameter() = delete;
        explicit BoolParameter(bool defaultValue) : GenericValueParameter(defaultValue) { }

    protected:
        virtual bool buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return ValueConverter<bool>::convert(val);
        }
    };

    struct FloatParameter : GenericValueParameter<float> {
        FloatParameter() = delete;
        explicit FloatParameter(float defaultValue) : GenericValueParameter(defaultValue) { }

    protected:
        virtual float buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return ValueConverter<float>::convert(val);
        }
    };

    struct ColorParameter : GenericValueParameter<vt::Color> {
        ColorParameter() = delete;
        explicit ColorParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::Color buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return convertColor(val);
        }
    };

    struct StringParameter : GenericValueParameter<std::string> {
        StringParameter() { initialize(std::string()); }
        explicit StringParameter(const std::string& defaultValue) : GenericValueParameter(defaultValue) { }

    protected:
        virtual std::string buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return ValueConverter<std::string>::convert(val);
        }
    };

    struct TransformParameter : GenericValueParameter<std::optional<vt::Transform>> {
        TransformParameter() { initialize(std::monostate()); }

    protected:
        virtual std::optional<vt::Transform> buildValue(const ExpressionContext& context) const override {
            if (auto transExpr = std::get_if<std::shared_ptr<TransformExpression>>(&_expr)) {
                cglib::mat3x3<float> matrix = std::visit(TransformEvaluator(context), (*transExpr)->getTransform());
                return vt::Transform::fromMatrix3(matrix);
            }
            return std::optional<vt::Transform>();
        }
    };

    struct CompOpParameter : GenericValueParameter<vt::CompOp> {
        CompOpParameter() = delete;
        explicit CompOpParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::CompOp buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return parseCompOp(ValueConverter<std::string>::convert(val));
        }
    };

    struct LineCapModeParameter : GenericValueParameter<vt::LineCapMode> {
        LineCapModeParameter() = delete;
        explicit LineCapModeParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LineCapMode buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return parseLineCapMode(ValueConverter<std::string>::convert(val));
        }
    };

    struct LineJoinModeParameter : GenericValueParameter<vt::LineJoinMode> {
        LineJoinModeParameter() = delete;
        explicit LineJoinModeParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LineJoinMode buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return parseLineJoinMode(ValueConverter<std::string>::convert(val));
        }
    };

    struct LabelOrientationParameter : GenericValueParameter<vt::LabelOrientation> {
        LabelOrientationParameter() = delete;
        explicit LabelOrientationParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LabelOrientation buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            return parseLabelOrientation(ValueConverter<std::string>::convert(val));
        }
    };

    struct MarkerTypeParameter : GenericValueParameter<std::string> {
        MarkerTypeParameter() = delete;
        explicit MarkerTypeParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::string buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            std::string markerType = toLower(ValueConverter<std::string>::convert(val));
            if (markerType.empty() || markerType == "auto") {
                return std::string();
            }
            if (markerType == "ellipse" || markerType == "arrow" || markerType == "rectangle") {
                return markerType;
            }
            throw ParserException("Invalid marker type", markerType);
        }
    };

    struct TextTransformParameter : GenericValueParameter<std::function<std::string(const std::string&)>> {
        TextTransformParameter() = delete;
        explicit TextTransformParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::function<std::string(const std::string&)> buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            std::string textTransform = toLower(ValueConverter<std::string>::convert(val));
            if (textTransform.empty() || textTransform == "none") {
                return [](const std::string& text) { return text; };
            }
            if (textTransform == "uppercase") {
                return [](const std::string& text) { return toUpper(text); };
            }
            if (textTransform == "lowercase") {
                return [](const std::string& text) { return toLower(text); };
            }
            if (textTransform == "capitalize") {
                return [](const std::string& text) { return capitalize(text); };
            }
            if (textTransform == "reverse") {
                return [](const std::string& text) { return stringReverse(text); };
            }
            throw ParserException("Invalid text transform", textTransform);
        }
    };

    struct HorizontalAlignmentParameter : GenericValueParameter<std::optional<float>> {
        HorizontalAlignmentParameter() = delete;
        explicit HorizontalAlignmentParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::optional<float> buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            std::string horizontalAlignment = toLower(ValueConverter<std::string>::convert(val));
            if (horizontalAlignment.empty() || horizontalAlignment == "auto") {
                return std::optional<float>();
            }
            if (horizontalAlignment == "left") {
                return -1.0f;
            }
            if (horizontalAlignment == "middle") {
                return 0.0f;
            }
            if (horizontalAlignment == "right") {
                return 1.0f;
            }
            throw ParserException("Invalid horizontal alignment", horizontalAlignment);
        }
    };

    struct VerticalAlignmentParameter : GenericValueParameter<std::optional<float>> {
        VerticalAlignmentParameter() = delete;
        explicit VerticalAlignmentParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::optional<float> buildValue(const ExpressionContext& context) const override {
            Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
            std::string verticalAlignment = toLower(ValueConverter<std::string>::convert(val));
            if (verticalAlignment.empty() || verticalAlignment == "auto") {
                return std::optional<float>();
            }
            if (verticalAlignment == "top") {
                return -1.0f;
            }
            if (verticalAlignment == "middle") {
                return 0.0f;
            }
            if (verticalAlignment == "bottom") {
                return 1.0f;
            }
            throw ParserException("Invalid vertical alignment", verticalAlignment);
        }
    };

    template <typename V, typename T>
    struct GenericFunctionParameter : SymbolizerParameter {
        virtual bool isDefined() const override { return _defined; }

        virtual const Expression& getExpression() const override { return _expr; }

        virtual void setExpression(const Expression& expr) override {
            _expr = expr;
            _defined = true;
            _contextVars = _viewStateVars = false;
            std::visit(ExpressionVariableVisitor(DependencyChecker(_contextVars, _viewStateVars)), expr);
            if (!_contextVars) {
                _func = buildFunction(ExpressionContext());
            }
        }

        T getFunction(const ExpressionContext& context) const {
            if (!_contextVars) {
                return _func;
            }
            return buildFunction(context);
        }

        V getStaticValue(const ExpressionContext& context) const {
            vt::ViewState viewState;
            viewState.zoom = ValueConverter<float>::convert(context.getVariable("view::zoom"));
            if (!_contextVars) {
                return _func(viewState);
            }
            return buildFunction(context)(viewState);
        }

    protected:
        GenericFunctionParameter() = default;
        template <typename S> explicit GenericFunctionParameter(const S& defaultValue) : _func(defaultValue), _expr(Value(defaultValue)) { }
        
        template <typename S>
        void initialize(const S& defaultValue) { _expr = Value(defaultValue); _func = buildFunction(ExpressionContext()); }

        virtual T buildFunction(const ExpressionContext& context) const = 0;

        bool _defined = false;
        bool _contextVars = false;
        bool _viewStateVars = false;
        T _func;
        Expression _expr;
    };

    struct FloatFunctionParameter : GenericFunctionParameter<float, vt::FloatFunction> {
        FloatFunctionParameter() = delete;
        explicit FloatFunctionParameter(float defaultValue) : GenericFunctionParameter(defaultValue) { }

    protected:
        virtual vt::FloatFunction buildFunction(const ExpressionContext& context) const override {
            if (_viewStateVars) {
                Expression expr = _expr;
                auto func = [expr, context](const vt::ViewState& viewState) -> float {
                    try {
                        Value val = std::visit(ExpressionEvaluator(context, &viewState), expr);
                        return ValueConverter<float>::convert(val);
                    }
                    catch (const std::exception&) {
                        return 0.0f;
                    }
                };
                return vt::FloatFunction(std::make_shared<std::function<float(const vt::ViewState&)>>(std::move(func)));
            } else {
                Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
                return vt::FloatFunction(ValueConverter<float>::convert(val));
            }
        }
    };

    struct ColorFunctionParameter : GenericFunctionParameter<vt::Color, vt::ColorFunction> {
        ColorFunctionParameter() = delete;
        explicit ColorFunctionParameter(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::ColorFunction buildFunction(const ExpressionContext& context) const override {
            if (_viewStateVars) {
                Expression expr = _expr;
                auto func = [expr, context](const vt::ViewState& viewState) -> vt::Color {
                    try {
                        Value val = std::visit(ExpressionEvaluator(context, &viewState), expr);
                        return convertColor(val);
                    }
                    catch (const std::exception&) {
                        return vt::Color();
                    }
                };
                return vt::ColorFunction(std::make_shared<std::function<vt::Color(const vt::ViewState&)>>(std::move(func)));
            } else {
                Value val = std::visit(ExpressionEvaluator(context, nullptr), _expr);
                return vt::ColorFunction(convertColor(val));
            }
        }
    };
} }

#endif
