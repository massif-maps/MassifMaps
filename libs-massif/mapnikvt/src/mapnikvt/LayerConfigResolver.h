/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_LAYERCONFIGRESOLVER_H_
#define _CARTO_MAPNIKVT_LAYERCONFIGRESOLVER_H_

#include "Value.h"

#include <map>
#include <memory>
#include <string>

namespace carto::mvt {
    class Map;

    /**
     * Evaluated configuration of a single LayerConfigSymbolizer-based style layer
     * (raster / hillshade / contour) at a given view zoom and nuti parameter state.
     */
    struct ResolvedLayerConfig {
        // False if the layer does not exist, no rule matches the current zoom/nuti state,
        // it carries no config symbolizer, or its 'visible' property evaluates to false.
        bool visible = false;
        // Evaluated values of the config symbolizer properties that the style actually set
        // (only 'defined' properties are included), e.g. "opacity", "exaggeration",
        // "shadow-color", "comp-op". The SDK layer applies its own defaults for the rest.
        std::map<std::string, Value> values;
    };

    /**
     * Evaluate the config symbolizer(s) of the named layer in 'map' without decoding a tile.
     * Honors the rule zoom range (via view zoom) and any filter predicate (zoom + nuti::).
     *
     * @param map        The compiled style map.
     * @param layerName  The style layer name (e.g. "hillshade").
     * @param viewZoom   The fractional view zoom (view::zoom), used both for rule selection
     *                   and for evaluating zoom-dependent property expressions.
     * @param nutiValues The current nuti parameter value map (may be null).
     */
    ResolvedLayerConfig resolveLayerConfig(const Map& map,
                                           const std::string& layerName,
                                           float viewZoom,
                                           const std::shared_ptr<const std::map<std::string, Value>>& nutiValues);
}

#endif
