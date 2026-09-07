/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_LAYERCONFIGSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_LAYERCONFIGSYMBOLIZER_H_

#include "Symbolizer.h"

namespace massif::mvt {
    /**
     * Base class for "external source config" symbolizers (raster / hillshade / contour).
     *
     * Unlike geometry symbolizers, a LayerConfigSymbolizer produces NO geometry - its
     * createFeatureProcessor returns an empty processor. It exists only so that CartoCSS
     * can parse and validate the corresponding '#name { ... }' block, and so that the
     * evaluated property values can be read out-of-band (per frame) by the SDK layer that
     * owns the external data source (e.g. CompositeVectorTileLayer). Reading is done via
     * Symbolizer::getPropertyNames()/getProperty() + Property::getExpression(), evaluated
     * against an ExpressionContext + ViewState - see LayerConfigResolver.
     */
    class LayerConfigSymbolizer : public Symbolizer {
    public:
        // Never emits geometry.
        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override {
            return FeatureProcessor();
        }

    protected:
        explicit LayerConfigSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindProperty("visible", &_visible);  // force-hide independent of zoom/param:: predicates
            bindProperty("opacity", &_opacity);
        }

        BoolProperty          _visible = BoolProperty(true);
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);
    };
}

#endif
