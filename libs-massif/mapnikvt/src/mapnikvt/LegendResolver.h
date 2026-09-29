#ifndef _MASSIF_MAPNIKVT_LEGENDRESOLVER_H_
#define _MASSIF_MAPNIKVT_LEGENDRESOLVER_H_

#include "Value.h"
#include "Feature.h"
#include "StyleParameterStore.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace massif::mvt {
    class Map;

    /**
     * One symbolizer that would draw a legend entry's synthetic feature, with its properties evaluated.
     */
    struct LegendSymbolizer {
        std::string type;  // Symbolizer::getTypeName
        std::string style; // the compiled style it belongs to, e.g. "transportation::casing"
        float styleOpacity = 1.0f;
        // Every property the style set, plus the type's key ones at their defaults. Colours as "#rrggbb[aa]".
        std::map<std::string, Value> values;
    };

    /**
     * The symbolizers drawing a feature of source layer 'layerName' with these fields, at this view
     * zoom and parameter state, in draw order. Empty when nothing draws it.
     */
    std::vector<LegendSymbolizer> resolveLegendEntry(const Map& map,
                                                     const std::string& layerName,
                                                     FeatureData::GeometryType geometryType,
                                                     const std::vector<std::pair<std::string, Value>>& fields,
                                                     float viewZoom,
                                                     const std::shared_ptr<const StyleParameterStore>& styleParameterStore);

    /**
     * Resolves a legend spec (see docs/features/legends.md) into the legend an app draws: each item's
     * swatch, and only the items the style draws. Throws std::invalid_argument on a malformed spec.
     */
    std::string resolveLegend(const Map& map, const std::string& specJSON, const std::shared_ptr<const StyleParameterStore>& styleParameterStore);
}

#endif
