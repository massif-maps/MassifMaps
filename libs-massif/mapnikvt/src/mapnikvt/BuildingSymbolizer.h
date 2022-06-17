/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_BUILDINGSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_BUILDINGSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto::mvt {
    class BuildingSymbolizer : public GeometrySymbolizer {
    public:
        explicit BuildingSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            unbindProperty("comp-op"); // not supported for now
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
            bindProperty("height", &_height);
            bindProperty("min-height", &_minHeight);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        ColorFunctionProperty _fill = ColorFunctionProperty("#808080");
        FloatFunctionProperty _fillOpacity = FloatFunctionProperty(1.0f);
        FloatProperty _height = FloatProperty(0.0f);
        FloatProperty _minHeight = FloatProperty(0.0f);

        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
