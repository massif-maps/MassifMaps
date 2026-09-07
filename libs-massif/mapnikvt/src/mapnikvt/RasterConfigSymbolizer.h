/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_RASTERCONFIGSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_RASTERCONFIGSYMBOLIZER_H_

#include "LayerConfigSymbolizer.h"

namespace massif::mvt {
    /**
     * Config symbolizer for a raster external source. Carries appearance settings that the
     * owning SDK layer applies to a RasterTileLayer every frame. Zoom- and style-parameter-dependent.
     * CartoCSS: '#name { raster-opacity: ...; raster-comp-op: ...; raster-filter-mode: ...; }'
     */
    class RasterConfigSymbolizer : public LayerConfigSymbolizer {
    public:
        explicit RasterConfigSymbolizer(std::shared_ptr<Logger> logger) : LayerConfigSymbolizer(std::move(logger)) {
            bindProperty("filter-mode", &_filterMode);  // nearest | bilinear | bicubic
            // 'opacity' and 'comp-op' are inherited (LayerConfigSymbolizer / Symbolizer).
        }

    protected:
        StringProperty _filterMode = StringProperty("bilinear");
    };
}

#endif
