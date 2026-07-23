/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_CONTOURCONFIGSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_CONTOURCONFIGSYMBOLIZER_H_

#include "LayerConfigSymbolizer.h"

namespace carto::mvt {
    /**
     * Optional config symbolizer for a contour external source (ContourTileDataSource).
     *
     * A ContourTileDataSource emits MVT line/label features, so its VISUAL styling uses the
     * ordinary line/text symbolizers on the '#contour' layer. This symbolizer only carries
     * the datasource GENERATION parameters (base interval, tracing resolution, ...). Because
     * changing them regenerates tiles, the owning SDK layer applies these to the
     * ContourTileDataSource on style/nuti changes - NOT every frame.
     */
    class ContourConfigSymbolizer : public LayerConfigSymbolizer {
    public:
        explicit ContourConfigSymbolizer(std::shared_ptr<Logger> logger) : LayerConfigSymbolizer(std::move(logger)) {
            bindProperty("base-interval",     &_baseInterval);
            bindProperty("resolution",        &_resolution);
            bindProperty("min-visible-zoom",  &_minVisibleZoom);
            bindProperty("simplify-tolerance",&_simplifyTolerance);
        }

    protected:
        FloatProperty _baseInterval      = FloatProperty(10.0f);
        FloatProperty _resolution        = FloatProperty(128.0f);
        FloatProperty _minVisibleZoom    = FloatProperty(12.0f);
        FloatProperty _simplifyTolerance = FloatProperty(1.0f);
    };
}

#endif
