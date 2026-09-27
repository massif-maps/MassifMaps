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
     * Base class for "external source config" symbolizers (raster / hillshade / contour). Emits no geometry:
     * it only lets CartoCSS parse the '#name { ... }' block so the SDK layer owning the external source
     * can evaluate its properties per frame - see LayerConfigResolver.
     */
    class LayerConfigSymbolizer : public Symbolizer {
    public:
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
