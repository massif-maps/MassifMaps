#include "TorqueCartoCSSMapLoader.h"
#include "CartoCSSParser.h"
#include "TorqueCartoCSSMapnikTranslator.h"
#include "mapnikvt/TorqueLayer.h"

#include <set>
#include <optional>

namespace carto { namespace css {
    std::shared_ptr<mvt::TorqueMap> TorqueCartoCSSMapLoader::loadMap(const std::string& cartoCSS) const {
        StyleSheet styleSheet;
        try {
            styleSheet = CartoCSSParser::parse(cartoCSS);
        }
        catch (const CartoCSSParser::ParserError& ex) {
            throw LoaderException(std::string("Error while parsing Torque CartoCSS: ") + ex.what());
        }
        catch (const std::exception& ex) {
            throw LoaderException(std::string("Exception while parsing Torque CartoCSS: ") + ex.what());
        }

        // Build map/torque properties
        mvt::TorqueMap::Settings mapSettings;
        mvt::TorqueMap::TorqueSettings torqueSettings;
        {
            try {
                CartoCSSCompiler compiler;
                std::map<std::string, Value> mapProperties;
                compiler.compileMap(styleSheet, mapProperties);

                loadMapSettings(mapProperties, mapSettings);
                loadTorqueSettings(mapProperties, torqueSettings);
            }
            catch (const std::exception& ex) {
                throw LoaderException(std::string("Error while building/loading map properties: ") + ex.what());
            }
        }
        auto map = std::make_shared<mvt::TorqueMap>(mapSettings, torqueSettings);

        // Find layer names and frame offsets
        std::set<std::string> layerNames;
        std::set<int> frameOffsets = { 0 };
        for (const Selector& selector : styleSheet.findRuleSetSelectors()) {
            std::optional<std::set<int>> selectorFrameOffsets;
            for (const Predicate& pred : selector.getPredicates()) {
                if (auto layerPred = std::get_if<LayerPredicate>(&pred)) {
                    layerNames.insert(layerPred->getLayerName());
                }
                else if (auto opPred = std::get_if<OpPredicate>(&pred)) {
                    if (opPred->getFieldOrVar().isField() && opPred->getFieldOrVar().getName() == "frame-offset") {
                        std::set<int> validFrameOffsets;
                        for (int frame = 0; frame < torqueSettings.frameCount; frame++) {
                            if (selectorFrameOffsets && (*selectorFrameOffsets).count(frame) == 0) {
                                continue;
                            }
                            if (OpPredicate::applyOp(opPred->getOp(), Value(static_cast<long long>(frame)), opPred->getRefValue())) {
                                validFrameOffsets.insert(frame);
                            }
                        }
                        selectorFrameOffsets = std::move(validFrameOffsets);
                    }
                }
            }
            if (selectorFrameOffsets) {
                frameOffsets.insert((*selectorFrameOffsets).begin(), (*selectorFrameOffsets).end());
            }
        }

        // Build layers
        for (const std::string& layerName : layerNames) {
            for (auto frameOffsetIt = frameOffsets.rbegin(); frameOffsetIt != frameOffsets.rend(); frameOffsetIt++) {
                int frameOffset = *frameOffsetIt;
                std::map<std::string, AttachmentStyle> attachmentStyleMap;
                try {
                    std::map<std::pair<int, int>, std::list<AttachmentPropertySets>> layerZoomAttachments;

                    ExpressionContext context;
                    std::map<std::string, Value> predefinedFieldMap;
                    context.predefinedFieldMap = &predefinedFieldMap;
                    (*context.predefinedFieldMap)["frame-offset"] = Value(static_cast<long long>(frameOffset));

                    CartoCSSCompiler compiler;
                    compiler.setContext(context);
                    compiler.setIgnoreLayerPredicates(_ignoreLayerPredicates);
                    compiler.compileLayer(styleSheet, layerName, 0, MAX_ZOOM + 1, layerZoomAttachments);

                    TorqueCartoCSSMapnikTranslator translator(_logger);
                    for (auto it = layerZoomAttachments.begin(); it != layerZoomAttachments.end(); it++) {
                        buildAttachmentStyleMap(translator, map, it->first.first, it->first.second, it->second, attachmentStyleMap);
                    }
                }
                catch (const std::exception& ex) {
                    throw LoaderException(std::string("Error while building layer ") + layerName + " properties: " + ex.what());
                }
                if (attachmentStyleMap.empty()) {
                    continue;
                }
                std::vector<AttachmentStyle> attachmentStyles = getSortedAttachmentStyles(attachmentStyleMap);

                std::vector<std::string> styleNames;
                for (const AttachmentStyle& attachmentStyle : attachmentStyles) {
                    // Build style, but ignore layer level opacity
                    std::string styleName = layerName + attachmentStyle.attachment + "#" + std::to_string(frameOffset);
                    auto style = std::make_shared<mvt::Style>(styleName, 1.0f, attachmentStyle.imageFilters, attachmentStyle.compOp, mvt::Style::FilterMode::FIRST, attachmentStyle.rules);
                    map->addStyle(style);
                    styleNames.push_back(styleName);
                }
                auto layer = std::make_shared<mvt::TorqueLayer>(layerName, frameOffset, styleNames);
                map->addLayer(layer);
            }
        }
        return map;
    }

    void TorqueCartoCSSMapLoader::loadTorqueSettings(const std::map<std::string, Value>& mapProperties, mvt::TorqueMap::TorqueSettings& torqueSettings) const {
        long long frameCount = 0;
        if (getMapProperty(mapProperties, "-torque-frame-count", frameCount)) {
            torqueSettings.frameCount = static_cast<int>(frameCount);
        }
        long long resolutionLong = 1;
        if (getMapProperty(mapProperties, "-torque-resolution", resolutionLong)) {
            torqueSettings.resolution = static_cast<float>(resolutionLong);
        } else {
            double resolutionDouble = 1;
            if (getMapProperty(mapProperties, "-torque-resolution", resolutionDouble)) {
                torqueSettings.resolution = static_cast<float>(resolutionDouble);
            }
        }
        long long animationDurationLong = 0;
        if (getMapProperty(mapProperties, "-torque-animation-duration", animationDurationLong)) {
            torqueSettings.animationDuration = static_cast<float>(animationDurationLong);
        } else {
            double animationDurationDouble = 0;
            if (getMapProperty(mapProperties, "-torque-animation-duration", animationDurationDouble)) {
                torqueSettings.animationDuration = static_cast<float>(animationDurationDouble);
            }
        }
        Color clearColor;
        if (getMapProperty(mapProperties, "-torque-clear-color", clearColor)) {
            torqueSettings.clearColor = vt::Color(clearColor.value());
        }
        getMapProperty(mapProperties, "-torque-time-attribute", torqueSettings.timeAttribute);
        getMapProperty(mapProperties, "-torque-aggregation-function", torqueSettings.aggregationFunction);
        getMapProperty(mapProperties, "-torque-data-aggregation", torqueSettings.dataAggregation);
    }
} }
