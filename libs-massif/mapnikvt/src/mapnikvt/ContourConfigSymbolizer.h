/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_CONTOURCONFIGSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_CONTOURCONFIGSYMBOLIZER_H_

#include "LayerConfigSymbolizer.h"

namespace massif::mvt {
    /**
     * Optional config symbolizer carrying a ContourTileDataSource's generation parameters; its visual styling
     * uses ordinary line/text symbolizers. Changing these regenerates tiles, so they are applied on
     * style/parameter changes, not every frame.
     */
    class ContourConfigSymbolizer : public LayerConfigSymbolizer {
    public:
        explicit ContourConfigSymbolizer(std::shared_ptr<Logger> logger) : LayerConfigSymbolizer(std::move(logger)) {
            bindProperty("base-interval",     &_baseInterval);
            bindProperty("resolution",        &_resolution);
            bindProperty("min-visible-zoom",  &_minVisibleZoom);
            bindProperty("simplify-tolerance",&_simplifyTolerance);
            bindProperty("label-stubs",       &_labelStubs);
            bindProperty("label-interval",    &_labelInterval);
        }

    protected:
        FloatProperty _baseInterval      = FloatProperty(10.0f);
        FloatProperty _resolution        = FloatProperty(128.0f);
        FloatProperty _minVisibleZoom    = FloatProperty(12.0f);
        FloatProperty _simplifyTolerance = FloatProperty(1.0f);
        BoolProperty  _labelStubs        = BoolProperty(false);
        FloatProperty _labelInterval     = FloatProperty(0.0f);
    };
}

#endif
