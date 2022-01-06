/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_FUNCTIONBUILDER_H_
#define _CARTO_MAPNIKVT_FUNCTIONBUILDER_H_

#include <mutex>

#include "vt/Styles.h"

namespace carto::mvt {
    class FloatFunctionBuilder final {
    public:
        vt::FloatFunction createFloatFunction(float value) const {
            return vt::FloatFunction(value);
        }

        vt::FloatFunction createScaledFloatFunction(const vt::FloatFunction& func, float scale) const {
            if (scale == 1) {
                return func;
            }
            if (!func.function()) {
                return vt::FloatFunction(func.value() * scale);
            }

            std::lock_guard<std::mutex> lock(_mutex);
            auto key = std::make_pair(func, scale);
            if (_cachedScaledFunction.first == key) {
                return _cachedScaledFunction.second;
            }
            vt::FloatFunction scaledFunc(std::make_shared<std::function<float(const vt::ViewState&)>>([func, scale](const vt::ViewState& viewState) {
                return func(viewState) * scale;
            }));
            _cachedScaledFunction = std::make_pair(key, scaledFunc);
            return scaledFunc;
        }

    private:
        mutable std::pair<std::pair<vt::FloatFunction, float>, vt::FloatFunction> _cachedScaledFunction;
        mutable std::mutex _mutex;
    };

    class ColorFunctionBuilder final {
    public:
        vt::ColorFunction createColorFunction(const vt::Color& value) const {
            return vt::ColorFunction(value);
        }

        vt::ColorFunction createColorOpacityFunction(const vt::ColorFunction& color, const vt::FloatFunction& opacity) const {
            if (opacity == vt::FloatFunction(1)) {
                return color;
            }
            if (!color.function() && !opacity.function()) {
                return vt::ColorFunction(vt::Color::fromColorOpacity(color.value(), opacity.value()));
            }

            std::lock_guard<std::mutex> lock(_mutex);
            auto key = std::make_pair(color, opacity);
            if (_cachedColorOpacityFunction.first == key) {
                return _cachedColorOpacityFunction.second;
            }
            vt::ColorFunction colorOpacityFunc(std::make_shared<std::function<vt::Color(const vt::ViewState&)>>([color, opacity](const vt::ViewState& viewState) {
                return vt::Color::fromColorOpacity((color)(viewState), (opacity)(viewState));
            }));
            _cachedColorOpacityFunction = std::make_pair(key, colorOpacityFunc);
            return colorOpacityFunc;
        }

    private:
        mutable std::pair<std::pair<vt::ColorFunction, vt::FloatFunction>, vt::ColorFunction> _cachedColorOpacityFunction;
        mutable std::mutex _mutex;
    };
}

#endif
