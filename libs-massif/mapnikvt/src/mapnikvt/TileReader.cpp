#include "TileReader.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "Rule.h"
#include "Filter.h"
#include "Symbolizer.h"
#include "Map.h"

namespace carto::mvt {
    TileReader::TileReader(std::shared_ptr<const Map> map, std::shared_ptr<const vt::TileTransformer> transformer, const SymbolizerContext& symbolizerContext, std::shared_ptr<Logger> logger) :
        _map(std::move(map)), _transformer(transformer), _symbolizerContext(symbolizerContext), _logger(std::move(logger)), _trueFilter(std::make_shared<Filter>(Filter::Type::FILTER, Predicate(true)))
    {
    }

    std::shared_ptr<vt::Tile> TileReader::readTile(const vt::TileId& tileId) const {
        ExpressionContext exprContext;
        exprContext.setTileId(tileId);
        exprContext.setAdjustedZoom(tileId.zoom + static_cast<int>(_symbolizerContext.getSettings().getZoomLevelBias()));
        exprContext.setNutiParameterValueMap(std::make_shared<std::map<std::string, Value>>(_symbolizerContext.getSettings().getNutiParameterValueMap()));

        std::vector<std::shared_ptr<vt::TileLayer>> tileLayers;

        if (std::shared_ptr<vt::TileBackground> tileBackground = createTileBackground(tileId)) {
            vt::TileLayerBuilder tileLayerBuilder(std::string(), -1, tileId, _transformer, _symbolizerContext.getSettings().getTileSize(), _symbolizerContext.getSettings().getGeometryScale());
            tileLayerBuilder.addBackground(tileBackground);
            std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
            tileLayers.push_back(std::move(tileLayer));
        }

        int layerIdx = 0;
        for (const std::shared_ptr<Layer>& layer : _map->getLayers()) {
            int styleIdx = 0;
            for (const std::string& styleName : layer->getStyleNames()) {
                int styleLayerIdx = layerIdx * 65536 + static_cast<int>(layer->getStyleNames().size()) * 256 + styleIdx;

                const std::shared_ptr<Style>& style = _map->getStyle(styleName);
                if (!style) {
                    continue;
                }
                
                vt::TileLayerBuilder tileLayerBuilder(styleName, styleLayerIdx, tileId, _transformer, _symbolizerContext.getSettings().getTileSize(), _symbolizerContext.getSettings().getGeometryScale());
                tileLayerBuilder.setOpacityFunc(vt::FloatFunction(style->getOpacity()));
                tileLayerBuilder.setCompOp(style->getCompOp());
                processLayer(layer, style, exprContext, tileLayerBuilder);

                std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
                if (!(tileLayer->getBackgrounds().empty() && tileLayer->getBitmaps().empty() && tileLayer->getLabels().empty() && tileLayer->getGeometries().empty() && !tileLayer->getCompOp())) {
                    tileLayers.push_back(std::move(tileLayer));
                }
                styleIdx++;
            }
            layerIdx++;
        }
        return std::make_shared<vt::Tile>(tileId, _symbolizerContext.getSettings().getTileSize(), std::move(tileLayers));
    }

    void TileReader::processLayer(const std::shared_ptr<const Layer>& layer, const std::shared_ptr<const Style>& style, ExpressionContext& exprContext, vt::TileLayerBuilder& layerBuilder) const {
        // Read and prefilter rules from the style
        std::vector<std::shared_ptr<const Rule>> rules = preFilterStyleRules(style, exprContext);
        if (rules.empty()) {
            return;
        }

        // Build sets of referenced fields from the rules
        std::set<std::string> filterFields, symbolizerFields;
        bool explicitFilterFeatureId = false, explicitSymbolizerFeatureId = false;

        auto gatherFields = [](const std::string& field, std::set<std::string>& fields, bool& explicitFeatureId) -> bool {
            if (ExpressionContext::isMapnikVariable(field)) {
                explicitFeatureId = explicitFeatureId || field == "mapnik::feature_id";
                return false;
            }
            else if (ExpressionContext::isViewStateVariable(field) || ExpressionContext::isNutiVariable(field) || ExpressionContext::isZoomVariable(field)) {
                return false;
            }
            fields.insert(field);
            return true;
        };

        for (const std::shared_ptr<const Rule>& rule : rules) {
            for (const std::string& field : rule->getReferencedFilterFields()) {
                gatherFields(field, filterFields, explicitFilterFeatureId);
            }
            
            for (const std::string& field : rule->getReferencedSymbolizerFields()) {
                gatherFields(field, symbolizerFields, explicitSymbolizerFeatureId);
            }
        }

        std::set<std::string> styleFields(filterFields);
        styleFields.insert(symbolizerFields.begin(), symbolizerFields.end());

        // Check if all the fields are known. If not, then can not use explicit field sets.
        const std::set<std::string>* filterFieldsPtr = filterFields.count(std::string()) == 0 ? &filterFields : nullptr;
        const std::set<std::string>* symbolizerFieldsPtr = symbolizerFields.count(std::string()) == 0 ? &symbolizerFields : nullptr;
        const std::set<std::string>* styleFieldsPtr = styleFields.count(std::string()) == 0 ? &styleFields : nullptr;

        FeatureCollection batchFeatureCollection;
        std::shared_ptr<Symbolizer::FeatureProcessor> batchFeatureProcessor;
        std::unordered_map<std::shared_ptr<const FeatureData>, std::vector<std::shared_ptr<const Symbolizer>>> featureDataSymbolizerMap;
        std::unordered_map<std::shared_ptr<const Symbolizer>, std::unordered_map<std::shared_ptr<const FeatureData>, std::shared_ptr<Symbolizer::FeatureProcessor>>> symbolizerFeatureDataProcessorMap;

        // Create feature iterator based on the required fields
        if (auto featureIt = createFeatureIterator(layer, styleFieldsPtr)) {
            for (; featureIt->valid(); featureIt->advance()) {
                // Find symbolizers for the feature data. Cache rule evaluation for each feature data object.
                std::shared_ptr<const FeatureData> filterFeatureData = featureIt->getFeatureData(explicitFilterFeatureId, filterFieldsPtr);
                auto symbolizersIt = featureDataSymbolizerMap.find(filterFeatureData);
                if (symbolizersIt == featureDataSymbolizerMap.end()) {
                    exprContext.setFeatureData(filterFeatureData);
                    std::vector<std::shared_ptr<const Symbolizer>> symbolizers = findFeatureSymbolizers(style, rules, exprContext);
                    symbolizersIt = featureDataSymbolizerMap.emplace(filterFeatureData, std::move(symbolizers)).first;
                }
                if (symbolizersIt->second.empty()) {
                    continue;
                }

                // Read geometry
                std::shared_ptr<const Geometry> geometry = featureIt->getGeometry();
                if (!geometry) {
                    continue;
                }

                // Process symbolizers
                std::shared_ptr<const FeatureData> symbolizerFeatureData = featureIt->getFeatureData(explicitSymbolizerFeatureId, symbolizerFieldsPtr);
                for (const std::shared_ptr<const Symbolizer>& symbolizer : symbolizersIt->second) {
                    std::unordered_map<std::shared_ptr<const FeatureData>, std::shared_ptr<Symbolizer::FeatureProcessor>>& featureDataProcessorMap = symbolizerFeatureDataProcessorMap[symbolizer];

                    // Create and cache processor for each feature data object
                    auto processorIt = featureDataProcessorMap.find(symbolizerFeatureData);
                    if (processorIt == featureDataProcessorMap.end()) {
                        exprContext.setFeatureData(symbolizerFeatureData);
                        Symbolizer::FeatureProcessor featureProcessor;
                        try {
                            featureProcessor = symbolizer->createFeatureProcessor(exprContext, _symbolizerContext);
                        } catch (const std::exception& ex) {
                            _logger->write(Logger::Severity::ERROR, "Failed to create feature processor: " + std::string(ex.what()));
                        }
                        processorIt = featureDataProcessorMap.emplace(symbolizerFeatureData, featureProcessor ? std::make_shared<Symbolizer::FeatureProcessor>(std::move(featureProcessor)) : std::shared_ptr<Symbolizer::FeatureProcessor>()).first;
                    }
                    const std::shared_ptr<Symbolizer::FeatureProcessor>& featureProcessor = processorIt->second;

                    // Try to batch as many features as possible before processing the features
                    bool batch = (featureProcessor == batchFeatureProcessor && (batchFeatureCollection.size() == 0 || batchFeatureCollection.getFeatureData(0) == symbolizerFeatureData));
                    if (!batch) {
                        if (batchFeatureProcessor) {
                            try {
                                (*batchFeatureProcessor)(batchFeatureCollection, layerBuilder);
                            } catch (const std::exception& ex) {
                                _logger->write(Logger::Severity::ERROR, "Failed to process feature batch: " + std::string(ex.what()));
                            }
                        }
                        batchFeatureCollection.clear();
                        batchFeatureProcessor = featureProcessor;
                    }

                    if (batchFeatureProcessor) {
                        batchFeatureCollection.append(featureIt->getLocalId(), Feature(featureIt->getFeatureId(), geometry, symbolizerFeatureData));
                    }
                }
            }

            // Process the remaining batched features
            if (batchFeatureProcessor) {
                try {
                    (*batchFeatureProcessor)(batchFeatureCollection, layerBuilder);
                } catch (const std::exception& ex) {
                    _logger->write(Logger::Severity::ERROR, "Failed to process feature batch: " + std::string(ex.what()));
                }
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
}
