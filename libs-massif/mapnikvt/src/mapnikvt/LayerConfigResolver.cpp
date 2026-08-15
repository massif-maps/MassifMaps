#include "LayerConfigResolver.h"
#include "Map.h"
#include "Layer.h"
#include "Style.h"
#include "Rule.h"
#include "Filter.h"
#include "Symbolizer.h"
#include "LayerConfigSymbolizer.h"
#include "ExpressionContext.h"
#include "ExpressionUtils.h"
#include "PredicateUtils.h"

#include <algorithm>
#include <limits>

namespace massif::mvt {
    ResolvedLayerConfig resolveLayerConfig(const Map& map, const std::string& layerName, float viewZoom,
                                           const std::shared_ptr<const StyleParameterStore>& styleParameterStore) {
        ResolvedLayerConfig result;

        const std::shared_ptr<Layer>& layer = map.getLayer(layerName);
        if (!layer) {
            return result;
        }

        // Build the evaluation context: adjusted (integer) zoom for rule/predicate selection,
        // style parameter map for param:: predicates/expressions, and a ViewState carrying the
        // fractional view zoom for zoom-dependent property functions (view::zoom).
        ExpressionContext exprContext;
        exprContext.setAdjustedZoom(static_cast<int>(viewZoom));
        if (styleParameterStore) {
            exprContext.setStyleParameterStore(styleParameterStore);
        }
        vt::ViewState viewState;
        viewState.zoom = viewZoom;

        PredicateEvaluator predEvaluator(exprContext, &viewState);

        for (const std::string& styleName : layer->getStyleNames()) {
            const std::shared_ptr<Style>& style = map.getStyle(styleName);
            if (!style) {
                continue;
            }
            for (const std::shared_ptr<const Rule>& rule : style->getZoomRules(exprContext.getAdjustedZoom())) {
                // Apply the rule filter predicate (zoom / param::) if present.
                if (const std::shared_ptr<const Filter>& filter = rule->getFilter()) {
                    if (filter->getType() == Filter::Type::FILTER && filter->getPredicate()) {
                        if (!std::visit(predEvaluator, *filter->getPredicate())) {
                            continue;
                        }
                    }
                }
                for (const std::shared_ptr<const Symbolizer>& symbolizer : rule->getSymbolizers()) {
                    auto configSymbolizer = std::dynamic_pointer_cast<const LayerConfigSymbolizer>(symbolizer);
                    if (!configSymbolizer) {
                        continue;
                    }
                    result.visible = true;
                    // Evaluate every property the style actually set. Evaluating the raw
                    // property Expression handles view::zoom (via viewState), 'zoom' and
                    // param:: (via exprContext) uniformly, regardless of the property type.
                    for (const std::string& propertyName : configSymbolizer->getPropertyNames()) {
                        const Property* property = configSymbolizer->getProperty(propertyName);
                        if (!property || !property->isDefined()) {
                            continue;
                        }
                        Value value = std::visit(ExpressionEvaluator(exprContext, &viewState), property->getExpression());
                        result.values[propertyName] = value;
                    }
                }
            }
        }

        // An explicit 'visible: false' in the style hides the source regardless of matches.
        auto it = result.values.find("visible");
        if (it != result.values.end() && !ValueConverter<bool>::convert(it->second)) {
            result.visible = false;
        }
        return result;
    }

    std::pair<int, int> resolveLayerZoomRange(const Map& map, const std::string& layerName) {
        const std::shared_ptr<Layer>& layer = map.getLayer(layerName);
        if (!layer) {
            return { 0, 24 };
        }

        int minZoom = std::numeric_limits<int>::max();
        int maxZoom = std::numeric_limits<int>::min();
        bool found = false;
        for (const std::string& styleName : layer->getStyleNames()) {
            const std::shared_ptr<Style>& style = map.getStyle(styleName);
            if (!style) {
                continue;
            }
            for (const std::shared_ptr<const Rule>& rule : style->getRules()) {
                bool isConfigRule = false;
                for (const std::shared_ptr<const Symbolizer>& symbolizer : rule->getSymbolizers()) {
                    if (std::dynamic_pointer_cast<const LayerConfigSymbolizer>(symbolizer)) {
                        isConfigRule = true;
                        break;
                    }
                }
                if (!isConfigRule) {
                    continue;
                }
                found = true;
                minZoom = std::min(minZoom, rule->getMinZoom());
                maxZoom = std::max(maxZoom, rule->getMaxZoom());
            }
        }
        if (!found) {
            return { 0, 24 };
        }
        return { minZoom, maxZoom };
    }
}
