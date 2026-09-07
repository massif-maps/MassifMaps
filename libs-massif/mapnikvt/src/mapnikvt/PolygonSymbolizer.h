/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_POLYGONSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_POLYGONSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace massif::mvt {
    class PolygonSymbolizer : public GeometrySymbolizer {
    public:
        explicit PolygonSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
            bindProperty("fill-emissive-strength", &_fillEmissive);
            bindProperty("elevation-mode", &_elevationMode);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        ColorFunctionProperty _fill = ColorFunctionProperty("#808080");
        FloatFunctionProperty _fillOpacity = FloatFunctionProperty(1.0f);
        // mapbox's fill-emissive-strength: 1 draws the fill as authored, 0 hands it to the light.
        FloatFunctionProperty _fillEmissive = FloatFunctionProperty(1.0f);

        // A bridge BED is a polygon; "span" lifts it onto the deck instead of draping it.
        StringProperty _elevationMode = StringProperty("drape");

        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
