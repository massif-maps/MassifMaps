/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_PROPERTIES_H_
#define _MASSIF_MAPNIKVT_PROPERTIES_H_

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

namespace massif::mvt {
    struct Property {
        virtual ~Property() = default;

        virtual bool isDefined() const = 0;
        virtual const Expression& getExpression() const = 0;
        virtual void setExpression(const Expression& expr) = 0;

        /**
         * True when this property reads style parameters and nothing fixed at decode time (feature field,
         * mapnik:: variable, zoom), so a parameter change is a redraw. Only function-valued properties can be live.
         */
        virtual bool isLiveCapable() const { return false; }

        /**
         * True when a symbolizer also reads this value at decode time (glyph raster size, marker bitmap),
         * so it is baked into the tile and never live. Set by Symbolizer::bindProperty.
         */
        bool isBakedAtDecode() const { return _bakedAtDecode; }
        void setBakedAtDecode(bool bakedAtDecode) { _bakedAtDecode = bakedAtDecode; }

        /**
         * True when the only parameter this property reads is the selecting one, through a comparison with
         * a feature field, so it folds to two constant style slots. Set by resolveSelectionParameter.
         */
        bool isSelectionFoldable() const { return _selectionFoldable; }
        void setSelectionFoldable(bool selectionFoldable) { _selectionFoldable = selectionFoldable; }

        /**
         * The raw value as drawn in this context and view state, falling back to the default as a
         * decode does. Not converted to the property's type: see convertColor for a colour.
         */
        Value evaluate(const ExpressionContext& context, const vt::ViewState& viewState) const {
            return evalExpression(getExpression(), context, &viewState, _defaultValue);
        }

        static vt::Color convertColor(const Value& val) {
            if (auto longVal = std::get_if<long long>(&val)) {
                return vt::Color::fromValue(static_cast<unsigned int>(*longVal));
            }
            return parseColor(ValueConverter<std::string>::convert(val));
        }

    protected:
        /**
         * An unset result (missing field or parameter) falls back to the declared default: as "" or 0
         * a colour threw and dropped the feature's processor, and a width of 0 dropped the line.
         * A malformed non-empty value still throws.
         */
        static Value evalExpression(const Expression& expr, const ExpressionContext& context, const vt::ViewState* viewState, const Value& defaultValue) {
            Value val = std::visit(ExpressionEvaluator(context, viewState), expr);
            if (std::holds_alternative<std::monostate>(val)) {
                return defaultValue;
            }
            return val;
        }

        bool _bakedAtDecode = false;
        bool _selectionFoldable = false;
        Value _defaultValue; // what an unset evaluation falls back to; see evalExpression

        struct DependencyChecker {
            DependencyChecker() = delete;
            explicit DependencyChecker(bool& contextVars, bool& viewStateVars, bool& styleParamVars) : _contextVars(contextVars), _viewStateVars(viewStateVars), _styleParamVars(styleParamVars) { }

            void operator() (const std::shared_ptr<VariableExpression>& varExpr) {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    std::string name = ValueConverter<std::string>::convert(*val);
                    if (ExpressionContext::isViewStateVariable(name)) {
                        _viewStateVars = true; // view variables do not depend on expression context, just on view state
                    }
                    else if (ExpressionContext::isStyleParameterVariable(name)) {
                        _styleParamVars = true; // parameters live in a store that can be swapped after decoding
                    }
                    else {
                        _contextVars = true; // mapnik/render variables, feature fields and zoom are fixed when the tile is decoded
                    }
                }
                else {
                    _contextVars = _viewStateVars = _styleParamVars = true; // generic expression, must assume everything is used
                }
            }

        private:
            bool& _contextVars;
            bool& _viewStateVars;
            bool& _styleParamVars;
        };
    };

    template <typename T>
    struct GenericValueProperty : Property {
        virtual bool isDefined() const override { return _defined; }

        virtual const Expression& getExpression() const override { return _expr; }

        virtual void setExpression(const Expression& expr) override {
            _expr = expr;
            _defined = true;
            _contextVars = _viewStateVars = _styleParamVars = false;
            std::visit(ExpressionVariableVisitor(DependencyChecker(_contextVars, _viewStateVars, _styleParamVars)), expr);
            if (!_contextVars && !_viewStateVars && !_styleParamVars) {
                _value = buildValue(ExpressionContext());
            }
        }

        T getValue(const ExpressionContext& context) const {
            if (!_contextVars && !_viewStateVars && !_styleParamVars) {
                return _value;
            }
            return buildValue(context);
        }

    protected:
        GenericValueProperty() = default;
        explicit GenericValueProperty(const T& defaultValue) : _value(defaultValue), _expr(Value(defaultValue)) { _defaultValue = Value(defaultValue); }

        // _defaultValue before buildValue: the build reads it.
        template <typename S>
        void initialize(const S& defaultValue) { _expr = Value(defaultValue); _defaultValue = Value(defaultValue); _value = buildValue(ExpressionContext()); }

        virtual T buildValue(const ExpressionContext& context) const = 0;

        bool _defined = false;
        bool _contextVars = false;
        bool _viewStateVars = false;
        bool _styleParamVars = false;
        T _value = T();
        Expression _expr;
    };

    struct ValueProperty : GenericValueProperty<Value> {
        ValueProperty() : ValueProperty(Value()) { }
        explicit ValueProperty(const Value& defaultValue) : GenericValueProperty(defaultValue) { }

    protected:
        virtual Value buildValue(const ExpressionContext& context) const override {
            return evalExpression(_expr, context, nullptr, _defaultValue);
        }
    };

    struct BoolProperty : GenericValueProperty<bool> {
        BoolProperty() = delete;
        explicit BoolProperty(bool defaultValue) : GenericValueProperty(defaultValue) { }

    protected:
        virtual bool buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return ValueConverter<bool>::convert(val);
        }
    };

    struct FloatProperty : GenericValueProperty<float> {
        FloatProperty() = delete;
        explicit FloatProperty(float defaultValue) : GenericValueProperty(defaultValue) { }

    protected:
        virtual float buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return ValueConverter<float>::convert(val);
        }
    };

    struct ColorProperty : GenericValueProperty<vt::Color> {
        ColorProperty() = delete;
        explicit ColorProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::Color buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return convertColor(val);
        }
    };

    struct StringProperty : GenericValueProperty<std::string> {
        StringProperty() { initialize(std::string()); }
        explicit StringProperty(const std::string& defaultValue) : GenericValueProperty(defaultValue) { }

    protected:
        virtual std::string buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return ValueConverter<std::string>::convert(val);
        }
    };

    struct TransformProperty : GenericValueProperty<std::optional<vt::Transform>> {
        TransformProperty() { initialize(std::monostate()); }

    protected:
        virtual std::optional<vt::Transform> buildValue(const ExpressionContext& context) const override {
            if (auto transExpr = std::get_if<std::shared_ptr<TransformExpression>>(&_expr)) {
                cglib::mat3x3<float> matrix = std::visit(TransformEvaluator(context), (*transExpr)->getTransform());
                return vt::Transform::fromMatrix3(matrix);
            }
            return std::optional<vt::Transform>();
        }
    };

    struct CompOpProperty : GenericValueProperty<vt::CompOp> {
        CompOpProperty() = delete;
        explicit CompOpProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::CompOp buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return parseCompOp(ValueConverter<std::string>::convert(val));
        }
    };

    struct LineCapModeProperty : GenericValueProperty<vt::LineCapMode> {
        LineCapModeProperty() = delete;
        explicit LineCapModeProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LineCapMode buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return parseLineCapMode(ValueConverter<std::string>::convert(val));
        }
    };

    struct LineJoinModeProperty : GenericValueProperty<vt::LineJoinMode> {
        LineJoinModeProperty() = delete;
        explicit LineJoinModeProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LineJoinMode buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return parseLineJoinMode(ValueConverter<std::string>::convert(val));
        }
    };

    struct LabelOrientationProperty : GenericValueProperty<vt::LabelOrientation> {
        LabelOrientationProperty() = delete;
        explicit LabelOrientationProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::LabelOrientation buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
            return parseLabelOrientation(ValueConverter<std::string>::convert(val));
        }
    };

    struct MarkerTypeProperty : GenericValueProperty<std::string> {
        MarkerTypeProperty() = delete;
        explicit MarkerTypeProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::string buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
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

    struct TextTransformProperty : GenericValueProperty<std::function<std::string(const std::string&)>> {
        TextTransformProperty() = delete;
        explicit TextTransformProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::function<std::string(const std::string&)> buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
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

    struct HorizontalAlignmentProperty : GenericValueProperty<std::optional<float>> {
        HorizontalAlignmentProperty() = delete;
        explicit HorizontalAlignmentProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::optional<float> buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
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

    struct VerticalAlignmentProperty : GenericValueProperty<std::optional<float>> {
        VerticalAlignmentProperty() = delete;
        explicit VerticalAlignmentProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual std::optional<float> buildValue(const ExpressionContext& context) const override {
            Value val = evalExpression(_expr, context, nullptr, _defaultValue);
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
    struct GenericFunctionProperty : Property {
        virtual bool isDefined() const override { return _defined; }

        virtual const Expression& getExpression() const override { return _expr; }

        virtual bool isLiveCapable() const override { return _styleParamVars && !_contextVars; }

        virtual void setExpression(const Expression& expr) override {
            _expr = expr;
            _defined = true;
            _contextVars = _viewStateVars = _styleParamVars = false;
            std::visit(ExpressionVariableVisitor(DependencyChecker(_contextVars, _viewStateVars, _styleParamVars)), expr);
            if (!_contextVars && !_styleParamVars) {
                _func = buildFunction(_expr, ExpressionContext());
            }
        }

        T getFunction(const ExpressionContext& context) const {
            // View state only: hand every tile and feature the same object, which the renderer memoises.
            if (!_contextVars && !_styleParamVars) {
                return _func;
            }
            if (!_contextVars) {
                // One object per store, not per feature, or the renderer can neither memoise nor batch it.
                const StyleParameterStore* store = context.getStyleParameterStore().get();
                std::lock_guard<std::mutex> lock(_liveFuncMutex);
                for (const std::pair<const StyleParameterStore*, T>& liveFunc : _liveFuncs) {
                    if (liveFunc.first == store) {
                        return liveFunc.second;
                    }
                }
                if (_liveFuncs.size() >= MAX_LIVE_FUNCS) {
                    _liveFuncs.clear();
                }
                _liveFuncs.emplace_back(store, buildFunction(_expr, context));
                return _liveFuncs.back().second;
            }
            if (!_viewStateVars) {
                return buildFunction(_expr, context);
            }
            // The feature half folded now; features that fold alike share one object, which the
            // renderer then evaluates once a frame instead of once per label.
            Expression folded = foldContextExpressions(_expr, context);
            bool contextVars = false, viewStateVars = false, styleParamVars = false;
            std::visit(ExpressionVariableVisitor(DependencyChecker(contextVars, viewStateVars, styleParamVars)), folded);
            if (contextVars) {
                return buildFunction(folded, context);
            }
            const StyleParameterStore* store = context.getStyleParameterStore().get();
            std::lock_guard<std::mutex> lock(_liveFuncMutex);
            for (const FoldedFunc& foldedFunc : _foldedFuncs) {
                if (foldedFunc.store == store && std::visit(ExpressionDeepEqualsChecker(), foldedFunc.expr, folded)) {
                    return foldedFunc.func;
                }
            }
            if (_foldedFuncs.size() >= MAX_FOLDED_FUNCS) {
                _foldedFuncs.clear();
            }
            _foldedFuncs.push_back(FoldedFunc { store, folded, buildFunction(folded, context) });
            return _foldedFuncs.back().func;
        }

        V getStaticValue(const ExpressionContext& context) const {
            vt::ViewState viewState;
            viewState.zoom = ValueConverter<float>::convert(context.getVariable("view::zoom"));
            viewState.rotation = ValueConverter<float>::convert(context.getVariable("view::rotation"));
            viewState.tilt = ValueConverter<float>::convert(context.getVariable("view::tilt"));
            return getFunction(context)(viewState);
        }

        // Map::Settings copies these, so the live-function cache is left out of the copy. The base is
        // copied explicitly, or the copy loses its _defaultValue.
        GenericFunctionProperty(const GenericFunctionProperty& other) : Property(other),
            _defined(other._defined), _contextVars(other._contextVars), _viewStateVars(other._viewStateVars), _styleParamVars(other._styleParamVars), _func(other._func), _expr(other._expr) { }

        GenericFunctionProperty& operator = (const GenericFunctionProperty& other) {
            if (this != &other) {
                Property::operator = (other);
                _defined = other._defined;
                _contextVars = other._contextVars;
                _viewStateVars = other._viewStateVars;
                _styleParamVars = other._styleParamVars;
                _func = other._func;
                _expr = other._expr;
                std::lock_guard<std::mutex> lock(_liveFuncMutex);
                _liveFuncs.clear();
                _foldedFuncs.clear();
            }
            return *this;
        }

    protected:
        bool readsLiveStyleParams(const ExpressionContext& context) const {
            return _styleParamVars && !(_selectionFoldable && context.hasStyleParameterOverride());
        }

        // An expression that also reads a feature field re-decodes on a parameter change anyway, so a closure
        // there would only split the batches.
        bool foldsStyleParams(const ExpressionContext& context) const {
            return !(readsLiveStyleParams(context) && !_contextVars);
        }

        GenericFunctionProperty() = default;
        template <typename S> explicit GenericFunctionProperty(const S& defaultValue) : _func(defaultValue), _expr(Value(defaultValue)) { _defaultValue = Value(defaultValue); }

        // _defaultValue before buildFunction: the build reads it.
        template <typename S>
        void initialize(const S& defaultValue) { _expr = Value(defaultValue); _defaultValue = Value(defaultValue); _func = buildFunction(_expr, ExpressionContext()); }

        virtual T buildFunction(const Expression& expr, const ExpressionContext& context) const = 0;

        bool _defined = false;
        bool _contextVars = false;
        bool _viewStateVars = false;
        bool _styleParamVars = false;
        T _func;
        Expression _expr;

        // The live functions, cached per parameter store (see getFunction). Symbolizers are shared
        // between the tile decoding threads, hence the mutex.
        static constexpr std::size_t MAX_LIVE_FUNCS = 4;
        mutable std::mutex _liveFuncMutex;
        mutable std::vector<std::pair<const StyleParameterStore*, T>> _liveFuncs;
        // Feature-bound functions by their folded expression; same mutex.
        struct FoldedFunc {
            const StyleParameterStore* store;
            Expression expr;
            T func;
        };
        static constexpr std::size_t MAX_FOLDED_FUNCS = 16;
        mutable std::vector<FoldedFunc> _foldedFuncs;
    };

    struct FloatFunctionProperty : GenericFunctionProperty<float, vt::FloatFunction> {
        FloatFunctionProperty() = delete;
        explicit FloatFunctionProperty(float defaultValue) : GenericFunctionProperty(defaultValue) { }

    protected:
        virtual vt::FloatFunction buildFunction(const Expression& expr, const ExpressionContext& context) const override {
            if (_viewStateVars || !foldsStyleParams(context)) {
                // By value: this function outlives the property that built it.
                Value defaultValue = _defaultValue;
                auto func = [expr, context, defaultValue](const vt::ViewState& viewState) -> float {
                    try {
                        Value val = evalExpression(expr, context, &viewState, defaultValue);
                        return ValueConverter<float>::convert(val);
                    }
                    catch (const std::exception&) {
                        return 0.0f;
                    }
                };
                return vt::FloatFunction(std::make_shared<std::function<float(const vt::ViewState&)>>(std::move(func)));
            } else {
                Value val = evalExpression(expr, context, nullptr, _defaultValue);
                return vt::FloatFunction(ValueConverter<float>::convert(val));
            }
        }
    };

    struct ColorFunctionProperty : GenericFunctionProperty<vt::Color, vt::ColorFunction> {
        ColorFunctionProperty() = delete;
        explicit ColorFunctionProperty(const std::string& defaultValue) { initialize(defaultValue); }

    protected:
        virtual vt::ColorFunction buildFunction(const Expression& expr, const ExpressionContext& context) const override {
            if (_viewStateVars || !foldsStyleParams(context)) {
                // By value: this function outlives the property that built it.
                Value defaultValue = _defaultValue;
                auto func = [expr, context, defaultValue](const vt::ViewState& viewState) -> vt::Color {
                    try {
                        Value val = evalExpression(expr, context, &viewState, defaultValue);
                        return convertColor(val);
                    }
                    catch (const std::exception&) {
                        return vt::Color();
                    }
                };
                return vt::ColorFunction(std::make_shared<std::function<vt::Color(const vt::ViewState&)>>(std::move(func)));
            } else {
                Value val = evalExpression(expr, context, nullptr, _defaultValue);
                return vt::ColorFunction(convertColor(val));
            }
        }
    };
}

#endif
