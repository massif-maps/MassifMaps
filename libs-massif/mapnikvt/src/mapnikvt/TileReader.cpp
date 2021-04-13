#include "TileReader.h"
#include "ParserUtils.h"
#include "Symbolizer.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "Rule.h"
#include "Filter.h"
#include "Map.h"

namespace carto { namespace mvt {
    TileReader::TileReader(std::shared_ptr<const Map> map, std::shared_ptr<const vt::TileTransformer> transformer, const SymbolizerContext& symbolizerContext) :
        _map(std::move(map)), _transformer(transformer), _symbolizerContext(symbolizerContext), _trueFilter(std::make_shared<Filter>(Filter::Type::FILTER, Predicate(true)))
    {
    }

    std::shared_ptr<vt::Tile> TileReader::readTile(const vt::TileId& tileId) const {
        ExpressionContext exprContext;
        exprContext.setAdjustedZoom(tileId.zoom + static_cast<int>(_symbolizerContext.getSettings().getZoomLevelBias()));
        exprContext.setNutiParameterValueMap(std::make_shared<std::map<std::string, Value>>(_symbolizerContext.getSettings().getNutiParameterValueMap()));

        std::shared_ptr<vt::TileBackground> tileBackground = createTileBackground(tileId);

        std::vector<std::shared_ptr<vt::TileLayer>> tileLayers;
        int layerIdx = 0;
        for (const std::shared_ptr<Layer>& layer : _map->getLayers()) {
            int styleIdx = 0;
            for (const std::string& styleName : layer->getStyleNames()) {
                const std::shared_ptr<Style>& style = _map->getStyle(styleName);
                if (!style) {
                    continue;
                }
                
                std::optional<vt::CompOp> compOp;
                try {
                    if (!style->getCompOp().empty()) {
                        compOp = parseCompOp(style->getCompOp());
                    }
                }
                catch (const ParserException&) {
                    // ignore the error
                }

                vt::FloatFunction opacityFunc(style->getOpacity());

                int internalIdx = layerIdx * 65536 + static_cast<int>(layer->getStyleNames().size()) * 256 + styleIdx;
                vt::TileLayerBuilder tileLayerBuilder(tileId, internalIdx, _transformer->createTileVertexTransformer(tileId), _symbolizerContext.getSettings().getTileSize(), _symbolizerContext.getSettings().getGeometryScale());
                processLayer(layer, style, exprContext, tileLayerBuilder);

                std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer(compOp, opacityFunc);
                if (!(tileLayer->getBitmaps().empty() && tileLayer->getLabels().empty() && tileLayer->getGeometries().empty() && !compOp)) {
                    tileLayers.push_back(tileLayer);
                }
                styleIdx++;
            }
            layerIdx++;
        }
        return std::make_shared<vt::Tile>(tileId, _symbolizerContext.getSettings().getTileSize(), tileBackground, tileLayers);
    }

    void TileReader::processLayer(const std::shared_ptr<const Layer>& layer, const std::shared_ptr<const Style>& style, ExpressionContext& exprContext, vt::TileLayerBuilder& layerBuilder) const {
        // Read and prefilter rules from the style
        std::vector<std::shared_ptr<const Rule>> rules = preFilterStyleRules(style, exprContext);
        if (rules.empty()) {
            return;
        }

        // Build sets of referenced fields from the rules
        std::set<std::string> filterFields, symbolizerFields;
        for (const std::shared_ptr<const Rule>& rule : rules) {
            filterFields.insert(rule->getReferencedFilterFields().begin(), rule->getReferencedFilterFields().end());
            symbolizerFields.insert(rule->getReferencedSymbolizerFields().begin(), rule->getReferencedSymbolizerFields().end());
        }
        std::set<std::string> styleFields = filterFields;
        styleFields.insert(symbolizerFields.begin(), symbolizerFields.end());

        // Check if all the fields are known. If not, then can not use explicit field sets.
        const std::set<std::string>* filterFieldsPtr = filterFields.count(std::string()) == 0 ? &filterFields : nullptr;
        const std::set<std::string>* symbolizerFieldsPtr = symbolizerFields.count(std::string()) == 0 ? &symbolizerFields : nullptr;
        const std::set<std::string>* styleFieldsPtr = styleFields.count(std::string()) == 0 ? &styleFields : nullptr;

        std::shared_ptr<const Symbolizer> currentSymbolizer;
        FeatureCollection currentFeatureCollection;
        std::unordered_map<std::shared_ptr<const FeatureData>, std::vector<std::shared_ptr<const Symbolizer>>> featureDataSymbolizersMap;

        // Create feature iterator based on the required fields
        if (auto featureIt = createFeatureIterator(layer, styleFieldsPtr)) {
            for (; featureIt->valid(); featureIt->advance()) {
                std::shared_ptr<const FeatureData> filterFeatureData = featureIt->getFeatureData(filterFieldsPtr);

                // Cache symbolizer evaluation for each feature data object
                auto symbolizersIt = featureDataSymbolizersMap.find(filterFeatureData);
                if (symbolizersIt == featureDataSymbolizersMap.end()) {
                    exprContext.setFeatureData(filterFeatureData);
                    std::vector<std::shared_ptr<const Symbolizer>> symbolizers = findFeatureSymbolizers(style, rules, exprContext);
                    symbolizersIt = featureDataSymbolizersMap.emplace(filterFeatureData, std::move(symbolizers)).first;
                }

                // Process symbolizers, try to batch as many calls together as possible
                for (const std::shared_ptr<const Symbolizer>& symbolizer : symbolizersIt->second) {
                    if (std::shared_ptr<const Geometry> geometry = featureIt->getGeometry()) {
                        std::shared_ptr<const FeatureData> symbolizerFeatureData = featureIt->getFeatureData(symbolizerFieldsPtr);

                        bool batch = false;
                        if (currentSymbolizer == symbolizer && currentFeatureCollection.size() > 0) {
                            batch = symbolizerFeatureData == currentFeatureCollection.getFeatureData(0);
                        }

                        if (!batch) {
                            if (currentSymbolizer && currentFeatureCollection.size() > 0) {
                                exprContext.setFeatureData(currentFeatureCollection.getFeatureData(0));
                                currentSymbolizer->build(currentFeatureCollection, exprContext, _symbolizerContext, layerBuilder);
                            }
                            currentFeatureCollection.clear();
                            currentSymbolizer = symbolizer;
                        }

                        currentFeatureCollection.append(featureIt->getLocalId(), Feature(featureIt->getGlobalId(), geometry, symbolizerFeatureData));
                    }
                }
            }

            // Flush the remaining batched features
            if (currentSymbolizer && currentFeatureCollection.size() > 0) {
                exprContext.setFeatureData(currentFeatureCollection.getFeatureData(0));
                currentSymbolizer->build(currentFeatureCollection, exprContext, _symbolizerContext, layerBuilder);
            }
        }
    }

    std::vector<std::shared_ptr<const Rule>> TileReader::preFilterStyleRules(const std::shared_ptr<const Style>& style, ExpressionContext& exprContext) const {
        std::vector<std::shared_ptr<const Rule>> rules;
        for (const std::shared_ptr<const Rule>& rule : style->getZoomRules(exprContext.getAdjustedZoom())) {
            if (std::shared_ptr<const Filter> filter = rule->getFilter()) {
                // Test if the filter is potentially satisfiable. If not, can skip this rule
                if (filter->getType() == Filter::Type::FILTER && filter->getPredicate()) {
                    if (!std::visit(PredicatePreEvaluator(exprContext), *filter->getPredicate())) {
                        continue;
                    }
                }
            }
            rules.push_back(rule);
        }
        return rules;
    }

    std::vector<std::shared_ptr<const Symbolizer>> TileReader::findFeatureSymbolizers(const std::shared_ptr<const Style>& style, const std::vector<std::shared_ptr<const Rule>>& rules, ExpressionContext& exprContext) const {
        PredicateEvaluator predEvaluator(exprContext, nullptr);
        bool anyMatch = false;
        std::vector<std::shared_ptr<const Symbolizer>> symbolizers;
        for (const std::shared_ptr<const Rule>& rule : rules) {
            std::shared_ptr<const Filter> filter = rule->getFilter();
            if (!filter) {
                filter = _trueFilter;
            }
            
            // Filter matching logic
            bool match = true;
            switch (filter->getType()) {
            case Filter::Type::FILTER:
                switch (style->getFilterMode()) {
                case Style::FilterMode::FIRST:
                    if (anyMatch) {
                        match = false;
                    }
                    else if (filter->getPredicate()) {
                        match = std::visit(predEvaluator, *filter->getPredicate());
                    }
                    break;
                case Style::FilterMode::ALL:
                    if (filter->getPredicate()) {
                        match = std::visit(predEvaluator, *filter->getPredicate());
                    }
                    break;
                }
                anyMatch = anyMatch || match;
                break;
            case Filter::Type::ELSEFILTER:
                match = !anyMatch;
                break;
            case Filter::Type::ALSOFILTER:
                match = anyMatch;
                break;
            }

            // If match, add all rule symbolizers to the symbolizer list
            if (match) {
                symbolizers.insert(symbolizers.end(), rule->getSymbolizers().begin(), rule->getSymbolizers().end());
            }
        }
        return symbolizers;
    }
} }
