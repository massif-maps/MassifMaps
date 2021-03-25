/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSIONBINDER_H_
#define _CARTO_MAPNIKVT_EXPRESSIONBINDER_H_

#include "Expression.h"
#include "ExpressionContext.h"
#include "ExpressionUtils.h"
#include "ValueConverter.h"
#include "vt/Color.h"
#include "vt/Styles.h"

#include <memory>
#include <mutex>
#include <list>
#include <functional>

namespace carto { namespace mvt {
    template <typename V>
    class ExpressionBinder final {
    public:
        ExpressionBinder() = default;

        bool bind(V* field, const Expression& expr) {
            return bind(field, expr, std::function<V(const Value&)>(ValueConverter<V>::convert));
        }

        bool bind(V* field, const Expression& expr, std::function<V(const Value& val)> convertFunc) {
            if (auto val = std::get_if<Value>(&expr)) {
                *field = convertFunc(*val);
                return true;
            }
            _bindings.emplace_back(field, expr, std::move(convertFunc));
            return false;
        }

        void update(const FeatureExpressionContext& context) const {
            for (const Binding& binding : _bindings) {
                Value val = std::visit(ExpressionEvaluator(context), binding.expr);
                *binding.field = binding.convertFunc(val);
            }
        }

    private:
        struct Binding {
            V* field;
            Expression expr;
            std::function<V(const Value&)> convertFunc;

            explicit Binding(V* field, Expression expr, std::function<V(const Value&)> convertFunc) : field(field), expr(std::move(expr)), convertFunc(std::move(convertFunc)) { }
        };

        std::vector<Binding> _bindings;
    };

    template <typename V>
    class ExpressionFunctionBinder final {
    public:
        using Function = vt::UnaryFunction<V, vt::ViewState>;

        ExpressionFunctionBinder() = default;

        bool bind(Function* field, const Expression& expr) {
            return bind(field, expr, std::function<V(const Value&)>(ValueConverter<V>::convert));
        }

        bool bind(Function* field, const Expression& expr, std::function<V(const Value&)> convertFunc) {
            if (auto val = std::get_if<Value>(&expr)) {
                *field = Function(convertFunc(*val));
                return true;
            }
            _bindings.emplace_back(field, expr, convertFunc);
            return false;
        }

        void update(const FeatureExpressionContext& context) const {
            for (const Binding& binding : _bindings) {
                Expression expr = simplifyExpression(binding.expr, context);
                *binding.field = buildFunction(expr, binding.convertFunc);
            }
        }

    private:
        struct Binding {
            Function* field;
            Expression expr;
            std::function<V(const Value&)> convertFunc;

            explicit Binding(Function* field, Expression expr, std::function<V(const Value&)> convertFunc) : field(field), expr(std::move(expr)), convertFunc(std::move(convertFunc)) { }
        };

        Function buildFunction(const Expression& expr, const std::function<V(const Value&)>& convertFunc) const {
            if (auto val = std::get_if<Value>(&expr)) {
                return Function(convertFunc(*val));
            }

            for (auto it = _functionCache.begin(); it != _functionCache.end(); it++) {
                if (std::visit(ExpressionDeepEqualsChecker(), expr, it->first)) {
                    return it->second;
                }
            }

            struct ViewStateFunction {
                ViewStateFunction() = delete;
                explicit ViewStateFunction(const Expression& expr, const std::function<V(const Value&)>& convertFunc) : _expr(expr), _convertFunc(convertFunc), _mutex(std::make_shared<std::mutex>()) { }

                V operator() (const vt::ViewState& viewState) const {
                    std::lock_guard<std::mutex> lock(*_mutex);
                    if (viewState.zoom != _cachedZoom) {
                        ViewExpressionContext context;
                        context.setZoom(viewState.zoom);
                        _cachedZoom = viewState.zoom;
                        _cachedValue = _convertFunc(std::visit(ExpressionEvaluator(context), _expr));
                    }
                    return _cachedValue;
                }

            private:
                const Expression _expr;
                const std::function<V(const Value&)> _convertFunc;
                mutable float _cachedZoom = std::numeric_limits<float>::quiet_NaN();
                mutable V _cachedValue = V();
                mutable std::shared_ptr<std::mutex> _mutex;
            };
            
            Function func(std::make_shared<std::function<V(const vt::ViewState&)>>(ViewStateFunction(expr, convertFunc)));
            
            if (_functionCache.size() >= MAX_CACHE_SIZE) {
                _functionCache.erase(_functionCache.begin()); // erase any element to keep the cache compact
            }
            _functionCache.push_back(std::make_pair(expr, func));
            return func;
        }

        static Expression simplifyExpression(const Expression& expr, const FeatureExpressionContext& context) {
            auto simplify = [&context](const Expression& expr) -> Expression {
                bool containsViewVariables = false;
                auto checkViewVariables = [&containsViewVariables, &context](const Expression& expr) {
                    if (auto varExpr = std::get_if<std::shared_ptr<VariableExpression>>(&expr)) {
                        std::string name = (*varExpr)->getVariableName(context);
                        if (ViewExpressionContext::isViewVariable(name)) {
                            containsViewVariables = true;
                        }
                    }
                };
                std::visit(ExpressionDeepVisitor(checkViewVariables), expr);
                if (containsViewVariables) {
                    return expr;
                }
                return std::visit(ExpressionEvaluator(context), expr);
            };
            return std::visit(ExpressionMapper(simplify), expr);
        }

        inline static constexpr std::size_t MAX_CACHE_SIZE = 32; // 32 is a good fit if function depends on 'discrete zoom'

        std::vector<Binding> _bindings;
        mutable std::list<std::pair<Expression, Function>> _functionCache;
    };
} }

#endif
