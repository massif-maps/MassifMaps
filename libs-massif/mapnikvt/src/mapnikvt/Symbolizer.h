/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_SYMBOLIZER_H_
#define _CARTO_MAPNIKVT_SYMBOLIZER_H_

#include "FeatureCollection.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "ExpressionBinder.h"
#include "FunctionBuilder.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"
#include "Logger.h"
#include "vt/Color.h"
#include "vt/Transform.h"
#include "vt/TileLayerBuilder.h"

#include <memory>
#include <optional>
#include <mutex>
#include <functional>

#include <cglib/mat.h>

namespace carto { namespace mvt {
    class Symbolizer {
    public:
        virtual ~Symbolizer() = default;

        void setParameter(const std::string& name, const std::string& value);
        const std::map<std::string, std::string>& getParameterMap() const;
        const std::vector<Expression>& getParameterExpressions() const;

        virtual void build(const FeatureCollection& featureCollection, const FeatureExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) = 0;

    protected:
        explicit Symbolizer(std::shared_ptr<Logger> logger) : _logger(std::move(logger)) { }

        Expression parseExpression(const std::string& str) const;
        Expression parseStringExpression(const std::string& str) const;

        vt::CompOp convertCompOp(const std::string& compOp) const;
        vt::LabelOrientation convertLabelPlacement(const std::string& orientation) const;

        vt::Color convertColor(const Value& val) const;
        vt::Transform convertTransform(const Value& val) const;

        vt::CompOp getCompOp() const;

        virtual void bindParameter(const std::string& name, const std::string& value);

        static long long convertId(const Value& val);
        static long long generateId();
        static long long combineId(long long globalId, std::size_t hash);

        template <typename V>
        void bind(V* field, const Expression& expr) {
            if (!bindParameter(field, expr, std::function<V(const Value&)>(ValueConverter<V>::convert))) {
                _parameterExprs.push_back(expr);
            }
        }

        template <typename V>
        void bind(V* field, const Expression& expr, V(*convertFunc)(const Value&)) {
            if (!bindParameter(field, expr, std::function<V(const Value&)>(convertFunc))) {
                _parameterExprs.push_back(expr);
            }
        }

        template <typename V>
        void bind(V* field, const Expression& expr, V(Symbolizer::*memberconvertFunc)(const Value&) const) {
            if (!bindParameter(field, expr, [this, memberconvertFunc](const Value& val) -> V {
                return (this->*memberconvertFunc)(val);
            })) {
                _parameterExprs.push_back(expr);
            }
        }

        template <typename V>
        void bind(vt::UnaryFunction<V, vt::ViewState>* field, const Expression& expr) {
            if (!bindParameter(field, expr, std::function<V(const Value&)>(ValueConverter<V>::convert))) {
                _parameterExprs.push_back(expr);
            }
        }

        template <typename V>
        void bind(vt::UnaryFunction<V, vt::ViewState>* field, const Expression& expr, V(*convertFunc)(const Value&)) {
            if (!bindParameter(field, expr, std::function<V(const Value&)>(convertFunc))) {
                _parameterExprs.push_back(expr);
            }
        }

        template <typename V>
        void bind(vt::UnaryFunction<V, vt::ViewState>* field, const Expression& expr, V(Symbolizer::*memberconvertFunc)(const Value&) const) {
            if (!bindParameter(field, expr, [this, memberconvertFunc](const Value& val) -> V {
                return (this->*memberconvertFunc)(val);
            })) {
                _parameterExprs.push_back(expr);
            }
        }

        void updateBindings(const FeatureExpressionContext& exprContext) {
            if (!_parameterExprs.empty()) {
                _boolBinder.update(exprContext);
                _intBinder.update(exprContext);
                _longlongBinder.update(exprContext);
                _floatBinder.update(exprContext);
                _stringBinder.update(exprContext);
                _colorBinder.update(exprContext);
                _transformBinder.update(exprContext);
                _floatFunctionBinder.update(exprContext);
                _colorFunctionBinder.update(exprContext);
            }
        }

        mutable std::mutex _mutex; // guards internal state as bindings may update it

        FunctionBuilder _functionBuilder;
        
        std::shared_ptr<Logger> _logger;

    private:
        bool bindParameter(bool* field, const Expression& expr, const std::function<bool(const Value&)>& convertFunc) {
            return _boolBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(int* field, const Expression& expr, const std::function<int(const Value&)>& convertFunc) {
            return _intBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(long long* field, const Expression& expr, const std::function<long long(const Value&)>& convertFunc) {
            return _longlongBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(float* field, const Expression& expr, const std::function<float(const Value&)>& convertFunc) {
            return _floatBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(std::string* field, const Expression& expr, const std::function<std::string(const Value&)>& convertFunc) {
            return _stringBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(vt::Color* field, const Expression& expr, const std::function<vt::Color(const Value&)>& convertFunc) {
            return _colorBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(vt::Transform* field, const Expression& expr, const std::function<vt::Transform(const Value&)>& convertFunc) {
            return _transformBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(vt::FloatFunction* field, const Expression& expr, const std::function<float(const Value&)>& convertFunc) {
            return _floatFunctionBinder.bind(field, expr, convertFunc);
        }

        bool bindParameter(vt::ColorFunction* field, const Expression& expr, const std::function<vt::Color(const Value&)>& convertFunc) {
            return _colorFunctionBinder.bind(field, expr, convertFunc);
        }

        ExpressionBinder<bool> _boolBinder;
        ExpressionBinder<int> _intBinder;
        ExpressionBinder<long long> _longlongBinder;
        ExpressionBinder<float> _floatBinder;
        ExpressionBinder<std::string> _stringBinder;
        ExpressionBinder<vt::Color> _colorBinder;
        ExpressionBinder<vt::Transform> _transformBinder;
        ExpressionFunctionBinder<float> _floatFunctionBinder;
        ExpressionFunctionBinder<vt::Color> _colorFunctionBinder;

        std::map<std::string, std::string> _parameterMap;
        
        std::vector<Expression> _parameterExprs;

        std::string _compOp = "src-over";
    };
} }

#endif
