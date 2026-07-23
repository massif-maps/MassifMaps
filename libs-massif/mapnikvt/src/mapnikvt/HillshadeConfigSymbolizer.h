/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_HILLSHADECONFIGSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_HILLSHADECONFIGSYMBOLIZER_H_

#include "LayerConfigSymbolizer.h"

namespace carto::mvt {
    /**
     * Config symbolizer for a hillshade external source. Carries the settings that the
     * owning SDK layer applies to a HillshadeRasterTileLayer every frame. All numeric
     * settings are zoom- and nuti-dependent (FloatFunctionProperty), so e.g.
     * 'hillshade-exaggeration: linear(zoom, [5,0.3], [12,1.0])' works.
     * CartoCSS: '#name { hillshade-exaggeration: ...; hillshade-shadow-color: ...; ... }'
     */
    class HillshadeConfigSymbolizer : public LayerConfigSymbolizer {
    public:
        explicit HillshadeConfigSymbolizer(std::shared_ptr<Logger> logger) : LayerConfigSymbolizer(std::move(logger)) {
            bindProperty("exaggeration",           &_exaggeration);
            bindProperty("height-scale",           &_heightScale);
            bindProperty("contrast",               &_contrast);
            bindProperty("illumination-direction", &_illumDir);
            bindProperty("shadow-color",           &_shadowColor);
            bindProperty("highlight-color",        &_highlightColor);
            bindProperty("accent-color",           &_accentColor);
            bindProperty("method",                 &_method);
            bindProperty("contour-interval",       &_contourInterval);
            bindProperty("contour-color",          &_contourColor);
            bindProperty("contour-width",          &_contourWidth);
            // 'opacity' and 'comp-op' are inherited.
        }

    protected:
        FloatFunctionProperty _exaggeration    = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _heightScale     = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _contrast        = FloatFunctionProperty(0.5f);
        FloatFunctionProperty _illumDir        = FloatFunctionProperty(335.0f);
        ColorProperty         _shadowColor     = ColorProperty("#000000");
        ColorProperty         _highlightColor  = ColorProperty("#ffffff");
        ColorProperty         _accentColor     = ColorProperty("#000000");
        StringProperty        _method          = StringProperty("standard");
        FloatFunctionProperty _contourInterval = FloatFunctionProperty(0.0f);  // 0 = contours off
        ColorProperty         _contourColor    = ColorProperty("#804000");
        FloatFunctionProperty _contourWidth    = FloatFunctionProperty(1.0f);
    };
}

#endif
