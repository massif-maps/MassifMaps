/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_BUILDINGSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_BUILDINGSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace massif::mvt {
    class BuildingSymbolizer : public GeometrySymbolizer {
    public:
        explicit BuildingSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            unbindProperty("comp-op"); // not supported for now
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
            bindProperty("height", &_height);
            bindProperty("min-height", &_minHeight);
            bindProperty("roof-shape", &_roofShape);
            bindProperty("roof-height", &_roofHeight);
            bindProperty("elevation-mode", &_elevationMode);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        ColorFunctionProperty _fill = ColorFunctionProperty("#808080");
        FloatFunctionProperty _fillOpacity = FloatFunctionProperty(1.0f);
        FloatProperty _height = FloatProperty(0.0f);
        FloatProperty _minHeight = FloatProperty(0.0f);
        // OSM roof:shape and roof:height, when the tiles carry them. 'flat' is what every extrusion
        // has always been; the rest raise a roof on top of the walls rather than capping them.
        StringProperty _roofShape = StringProperty("flat");
        FloatProperty _roofHeight = FloatProperty(0.0f);
        // 'span' stands the prism on its own chord instead of on the ground - a bridge DECK, whose
        // min-height/height are then a thickness rather than a height above the terrain.
        StringProperty _elevationMode = StringProperty("drape");

        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
