/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_BUILDINGSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_BUILDINGSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto { namespace mvt {
    class BuildingSymbolizer : public GeometrySymbolizer {
    public:
        explicit BuildingSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            unbindParameter("comp-op"); // not supported for now
            bindParameter("fill", &_fill);
            bindParameter("fill-opacity", &_fillOpacity);
            bindParameter("height", &_height);
            bindParameter("min-height", &_minHeight);
        }

        virtual void build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const override;

    protected:
        ColorFunctionParameter _fill = ColorFunctionParameter("#808080");
        FloatFunctionParameter _fillOpacity = FloatFunctionParameter(1.0f);
        FloatParameter _height = FloatParameter(0.0f);
        FloatParameter _minHeight = FloatParameter(0.0f);

        ColorFunctionBuilder _fillFuncBuilder;
    };
} }

#endif
