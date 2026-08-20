#include "CartoCSSMapLoader.h"
#include "CartoCSSParser.h"
#include "CartoCSSMapnikTranslator.h"
#include "mapnikvt/ParserUtils.h"
#include "mapnikvt/ValueConverter.h"

#include <picojson/picojson.h>

namespace massif::css {
    namespace {
        mvt::Value convertJSONValue(const picojson::value& value) {
            if (value.is<std::string>()) {
                return mvt::Value(std::string(value.get<std::string>()));
            }
            if (value.is<bool>()) {
                return mvt::Value(value.get<bool>());
            }
            if (value.is<std::int64_t>()) {
                return mvt::Value(static_cast<long long>(value.get<std::int64_t>()));
            }
            if (value.is<double>()) {
                return mvt::Value(value.get<double>());
            }
            // An object or an array becomes a container value the style reads with get(): a
            // parameter can hold a table (a colour per POI class) the app owns and replaces.
            if (value.is<picojson::object>()) {
                std::map<std::string, mvt::Value> members;
                const picojson::object& obj = value.get<picojson::object>();
                for (auto it = obj.begin(); it != obj.end(); it++) {
                    members[it->first] = convertJSONValue(it->second);
                }
                return mvt::Value(std::make_shared<const mvt::ValueObject>(std::move(members)));
            }
            if (value.is<picojson::array>()) {
                std::vector<mvt::Value> elements;
                const picojson::array& arr = value.get<picojson::array>();
                for (auto it = arr.begin(); it != arr.end(); it++) {
                    elements.push_back(convertJSONValue(*it));
                }
                return mvt::Value(std::make_shared<const mvt::ValueArray>(std::move(elements)));
            }
            return mvt::Value();
        }
        Value convertJSONNonMVTValue(const picojson::value& value) {
            if (value.is<std::string>()) {
                return Value(std::string(value.get<std::string>()));
            }
            if (value.is<bool>()) {
                return Value(value.get<bool>());
            }
            if (value.is<std::int64_t>()) {
                return Value(static_cast<long long>(value.get<std::int64_t>()));
            }
            if (value.is<double>()) {
                return Value(value.get<double>());
            }
            return Value();
        }
    }

    // One line per deprecated spelling per stylesheet. A plain text scan: it also hits the tokens
    // inside strings and comments, which is the right trade for a warning that costs one pass.
    void CartoCSSMapLoader::warnDeprecatedTokens(const std::string& cartoCSS, const std::string& fileName) const {
        static const std::pair<std::string, std::string> deprecatedTokens[] = {
            { "nuti::",            "param::" },
            { "nutibillboardline", "billboard-line" },
            { "nutibillboard",     "billboard" },
            { "nutipoint",         "flat" },
            { "nuticallout",       "callout" }
        };
        for (const auto& [oldToken, newToken] : deprecatedTokens) {
            for (std::string::size_type pos = cartoCSS.find(oldToken); pos != std::string::npos; pos = cartoCSS.find(oldToken, pos + 1)) {
                // 'nutibillboard' is a prefix of 'nutibillboardline' - only the longer one matches there
                std::string::size_type end = pos + oldToken.size();
                if (end < cartoCSS.size() && std::isalpha(static_cast<unsigned char>(cartoCSS[end]))) {
                    continue;
                }
                _logger->write(mvt::Logger::Severity::WARNING, fileName + ": '" + oldToken + "' is deprecated, use '" + newToken + "'");
                break;
            }
        }
    }

    std::shared_ptr<mvt::Map> CartoCSSMapLoader::loadMap(const std::string& cartoCSS) const {
        warnDeprecatedTokens(cartoCSS, "CartoCSS");

        // Parse the given CSS into stylesheet and process errors
        StyleSheet styleSheet;
        try {
            styleSheet = CartoCSSParser::parse(cartoCSS);
        }
        catch (const CartoCSSParser::ParserError& ex) {
            throw LoaderException(std::string("Error while parsing CartoCSS: ") + ex.what());
        }
        catch (const std::exception& ex) {
            throw LoaderException(std::string("Exception while parsing CartoCSS: ") + ex.what());
        }

        // Find layer names, keep the order of the names intact based on 'referencing' order
        std::vector<std::string> layerNames;
        for (const Selector& selector : styleSheet.findRuleSetSelectors()) {
            for (const Predicate& pred : selector.getPredicates()) {
                if (auto layerPred = std::get_if<LayerPredicate>(&pred)) {
                    std::string layerName = layerPred->getLayerName();
                    if (std::find(layerNames.begin(), layerNames.end(), layerName) == layerNames.end()) {
                        layerNames.push_back(layerName);
                    }
                }
            }
        }

        // Build Map
        std::map<std::string, Value> constantFieldMap;
        return buildMap(styleSheet, layerNames, std::vector<mvt::Parameter>(), std::vector<mvt::StyleParameter>(), constantFieldMap);
    }

    std::shared_ptr<mvt::Map> CartoCSSMapLoader::loadMapProject(const std::string& fileName) const {
        picojson::value mapDoc = loadMapDocument(fileName, std::set<std::string>());

        std::vector<std::string> mssFileNames;
        if (mapDoc.contains("styles")) {
            const picojson::array& stylesArr = mapDoc.get("styles").get<picojson::array>();
            for (auto jit = stylesArr.begin(); jit != stylesArr.end(); jit++) {
                mssFileNames.push_back(jit->get<std::string>());
            }
        }

        std::vector<std::string> layerNames;
        if (mapDoc.contains("layers")) {
            const picojson::array& layersArr = mapDoc.get("layers").get<picojson::array>();
            for (auto jit = layersArr.begin(); jit != layersArr.end(); jit++) {
                layerNames.insert(layerNames.begin(), jit->get<std::string>());
            }
        }

        // Combine stylesheet from individual fragments
        StyleSheet styleSheet;
        for (const std::string& mssFileName : mssFileNames) {
            std::shared_ptr<const std::vector<unsigned char>> mssData = _assetLoader->load(mssFileName);
            if (!mssData) {
                throw LoaderException(std::string("Could not load CartoCSS file ") + mssFileName);
            }
            std::string cartoCSS(reinterpret_cast<const char*>(mssData->data()), reinterpret_cast<const char*>(mssData->data() + mssData->size()));
            warnDeprecatedTokens(cartoCSS, mssFileName);
            StyleSheet mssStyleSheet;
            try {
                mssStyleSheet = CartoCSSParser::parse(cartoCSS);
            }
            catch (const CartoCSSParser::ParserError& ex) {
                std::string msg = std::string("Error while parsing file ") + mssFileName;
                if (ex.position().first != 0) {
                    msg += ", error at line " + std::to_string(ex.position().second) + ", column " + std::to_string(ex.position().first);
                }
                msg += std::string(": ") + ex.what();
                throw LoaderException(msg);
            }
            catch (const std::exception& ex) {
                throw LoaderException(std::string("Exception while parsing file ") + mssFileName + ": " + ex.what());
            }
            std::vector<StyleSheet::Element> elements(styleSheet.getElements());
            elements.insert(elements.end(), mssStyleSheet.getElements().begin(), mssStyleSheet.getElements().end());
            styleSheet = StyleSheet(elements);
        }

        // Parameters
        std::vector<mvt::Parameter> parameters;
        if (mapDoc.contains("parameters")) {
            const picojson::object& paramsObj = mapDoc.get("parameters").get<picojson::object>();
            for (auto pit = paramsObj.begin(); pit != paramsObj.end(); pit++) {
                const std::string& paramName = pit->first;
                const picojson::value& paramValue = pit->second;
                parameters.emplace_back(paramName, paramValue.to_str());
            }
        }

        // Style parameters ("nutiparameters" is the pre-rebrand key, still accepted)
        std::vector<mvt::StyleParameter> styleParameters;
        const char* styleParamsKey = mapDoc.contains("styleparameters") ? "styleparameters" : "nutiparameters";
        if (mapDoc.contains(styleParamsKey)) {
            const picojson::object& styleParamsObj = mapDoc.get(styleParamsKey).get<picojson::object>();
            for (auto pit = styleParamsObj.begin(); pit != styleParamsObj.end(); pit++) {
                const std::string& paramName = pit->first;
                const picojson::value& paramValue = pit->second;
                std::map<std::string, mvt::Value> enumMap;
                mvt::Value defaultValue;
                bool selects = false;
                if (paramValue.is<picojson::object>()) {
                    defaultValue = convertJSONValue(paramValue.get("default"));
                    // "selects": the parameter picks ONE feature out by being compared with a field,
                    // so setting it should repaint instead of decoding the tiles again
                    if (paramValue.contains("selects")) {
                        selects = mvt::ValueConverter<bool>::convert(convertJSONValue(paramValue.get("selects")));
                    }
                    if (paramValue.contains("values")) {
                        const picojson::object& valuesObj = paramValue.get("values").get<picojson::object>();
                        for (auto vit = valuesObj.begin(); vit != valuesObj.end(); vit++) {
                            enumMap[vit->first] = convertJSONValue(vit->second);
                        }
                    }
                } else if (paramValue.is<picojson::array>()) {
                    const picojson::array& valuesArr = paramValue.get<picojson::array>();
                    for (auto vit = valuesArr.rbegin(); vit != valuesArr.rend(); vit++) {
                        mvt::Value enumValue = convertJSONValue(*vit);
                        enumMap[mvt::ValueConverter<std::string>::convert(enumValue)] = enumValue;
                        defaultValue = enumValue;
                    }
                } else {
                    defaultValue = convertJSONValue(paramValue);
                }
                styleParameters.emplace_back(paramName, defaultValue, enumMap, selects);
            }
        }
        // Constant parameters (for macros)
        std::map<std::string, Value> constantFieldMap;
        if (mapDoc.contains("constants")) {
            const picojson::object& constantsObj = mapDoc.get("constants").get<picojson::object>();
            for (auto pit = constantsObj.begin(); pit != constantsObj.end(); pit++) {
                const std::string& paramName = pit->first;
                const picojson::value& paramValue = pit->second;
                std::map<std::string, mvt::Value> enumMap;
                constantFieldMap[paramName] = convertJSONNonMVTValue(paramValue);
            }
        }

        return buildMap(styleSheet, layerNames, parameters, styleParameters, constantFieldMap);
    }

    picojson::value CartoCSSMapLoader::loadMapDocument(const std::string& fileName, std::set<std::string> loadedFileNames) const {
        picojson::value mapDoc;

        if (loadedFileNames.find(fileName) != loadedFileNames.end()) {
            throw LoaderException(std::string("Cyclical reference to map description file ") + fileName);
        }
        loadedFileNames.insert(fileName);

        std::shared_ptr<const std::vector<unsigned char>> mapData = _assetLoader->load(fileName);
        if (!mapData) {
            throw LoaderException(std::string("Could not load map description file ") + fileName);
        }

        std::string mapJson(reinterpret_cast<const char*>(mapData->data()), reinterpret_cast<const char*>(mapData->data() + mapData->size()));
        std::string err = picojson::parse(mapDoc, mapJson);
        if (!err.empty()) {
            throw LoaderException(std::string("Error while parsing map description: ") + err);
        }

        // Load base project data, if defined
        if (mapDoc.contains("extends")) {
            const std::string& extendsFileName = mapDoc.get("extends").get<std::string>();
            picojson::value extendsMapDoc = loadMapDocument(extendsFileName, loadedFileNames);

            mapDoc.swap(extendsMapDoc);
            const picojson::object& extendsMapDocObj = extendsMapDoc.get<picojson::object>();
            std::vector<std::string> mergeCases = {"styleparameters", "nutiparameters", "constants"};
            for (auto it = extendsMapDocObj.begin(); it != extendsMapDocObj.end(); it++) {
                if(std::find(mergeCases.begin(), mergeCases.end(), it->first) != mergeCases.end()) {
                    // we extend
                    picojson::value& subObj = mapDoc.get(it->first);
                    const picojson::object& subOverrideObj = it->second.get<picojson::object>();
                    for (auto it2 = subOverrideObj.begin(); it2 != subOverrideObj.end(); it2++) {
                        subObj.set(it2->first, it2->second);
                    }
                } else {
                    // we override
                    mapDoc.set(it->first, it->second);
                }
            }
        }

        return mapDoc;
    }

    mvt::Map::Settings CartoCSSMapLoader::loadMapSettings(const std::map<std::string, Expression>& mapProperties) const {
        mvt::Map::Settings mapSettings;
        
        Expression backgroundColor;
        if (getMapProperty(mapProperties, "background-color", backgroundColor)) {
            mapSettings.backgroundColor.setExpression(CartoCSSMapnikTranslator::buildExpression(backgroundColor));
        }
        getMapProperty(mapProperties, "background-image", mapSettings.backgroundImage);

        Expression northPoleColor;
        if (getMapProperty(mapProperties, "north-pole-color", northPoleColor)) {
            mapSettings.northPoleColor.setExpression(CartoCSSMapnikTranslator::buildExpression(northPoleColor));
        }
        Expression southPoleColor;
        if (getMapProperty(mapProperties, "south-pole-color", southPoleColor)) {
            mapSettings.southPoleColor.setExpression(CartoCSSMapnikTranslator::buildExpression(southPoleColor));
        }

        // Sun, shadows, fog and the terrain view distance. Same treatment as every other map
        // property: whatever expression the style wrote, zoom-dependent or not, is kept as an
        // expression and evaluated per frame.
        const std::pair<const char*, mvt::FloatFunctionProperty mvt::Map::Settings::*> floatProperties[] = {
            { "sun-azimuth", &mvt::Map::Settings::sunAzimuth },
            { "sun-altitude", &mvt::Map::Settings::sunAltitude },
            { "sun-intensity", &mvt::Map::Settings::sunIntensity },
            { "ambient-intensity", &mvt::Map::Settings::ambientIntensity },
            { "building-light-intensity", &mvt::Map::Settings::buildingLightIntensity },
            { "building-ambient", &mvt::Map::Settings::buildingAmbient },
            { "building-vertical-gradient", &mvt::Map::Settings::buildingVerticalGradient },
            { "building-vertical-gradient-height", &mvt::Map::Settings::buildingVerticalGradientHeight },
            { "building-ao-ground-radius", &mvt::Map::Settings::buildingAoGroundRadius },
            { "building-ao-ground-step", &mvt::Map::Settings::buildingAoGroundStep },
            { "building-ao-ground-attenuation", &mvt::Map::Settings::buildingAoGroundAttenuation },
            { "building-ao-intensity", &mvt::Map::Settings::buildingAoIntensity },
            { "text-occlusion-opacity", &mvt::Map::Settings::textOcclusionOpacity },
            { "building-edge-radius", &mvt::Map::Settings::buildingEdgeRadius },
            { "building-roof-shade", &mvt::Map::Settings::buildingRoofShade },
            { "building-rounded-roof", &mvt::Map::Settings::buildingRoundedRoof },
            { "terrain-lighting", &mvt::Map::Settings::terrainLighting },
            { "shadow-strength", &mvt::Map::Settings::shadowStrength },
            { "shadow-bias", &mvt::Map::Settings::shadowBias },
            { "shadow-softness", &mvt::Map::Settings::shadowSoftness },
            { "shadow-distance", &mvt::Map::Settings::shadowDistance },
            { "shadow-map-size", &mvt::Map::Settings::shadowMapSize },
            { "shadow-cascades", &mvt::Map::Settings::shadowCascades },
            { "shadow-caster-margin", &mvt::Map::Settings::shadowCasterMargin },
            { "fog-enabled", &mvt::Map::Settings::fogEnabled },
            { "fog-range-start", &mvt::Map::Settings::fogRangeStart },
            { "fog-range-end", &mvt::Map::Settings::fogRangeEnd },
            { "fog-horizon-blend", &mvt::Map::Settings::fogHorizonBlend },
            { "fog-star-intensity", &mvt::Map::Settings::fogStarIntensity },
            { "terrain-max-visible-distance", &mvt::Map::Settings::terrainMaxVisibleDistance }
        };
        for (const auto& floatProperty : floatProperties) {
            Expression expr;
            if (getMapProperty(mapProperties, floatProperty.first, expr)) {
                (mapSettings.*floatProperty.second).setExpression(CartoCSSMapnikTranslator::buildExpression(expr));
            }
        }
        const std::pair<const char*, mvt::ColorFunctionProperty mvt::Map::Settings::*> colorProperties[] = {
            { "sun-color", &mvt::Map::Settings::sunColor },
            { "ambient-color", &mvt::Map::Settings::ambientColor },
            { "fog-color", &mvt::Map::Settings::fogColor },
            { "fog-high-color", &mvt::Map::Settings::fogHighColor },
            { "fog-space-color", &mvt::Map::Settings::fogSpaceColor }
        };
        for (const auto& colorProperty : colorProperties) {
            Expression expr;
            if (getMapProperty(mapProperties, colorProperty.first, expr)) {
                (mapSettings.*colorProperty.second).setExpression(CartoCSSMapnikTranslator::buildExpression(expr));
            }
        }

        getMapProperty(mapProperties, "font-directory", mapSettings.fontDirectory);
        double bufferSize = 0;
        if (getMapProperty(mapProperties, "buffer-size", bufferSize)) {
            mapSettings.bufferSize = static_cast<float>(bufferSize);
        }

        return mapSettings;
    }

    std::shared_ptr<mvt::Map> CartoCSSMapLoader::buildMap(const StyleSheet& styleSheet, const std::vector<std::string>& layerNames, const std::vector<mvt::Parameter>& parameters, const std::vector<mvt::StyleParameter>& styleParameters, std::map<std::string, Value>& constantFieldMap) const {
        // Map properties
        mvt::Map::Settings mapSettings;
        {
            try {
                CartoCSSCompiler compiler;
                std::map<std::string, Expression> mapProperties;
                compiler.compileMap(styleSheet, mapProperties, constantFieldMap);

                mapSettings = loadMapSettings(mapProperties);
            }
            catch (const std::exception& ex) {
                throw LoaderException(std::string("Error while building/loading map properties: ") + ex.what());
            }
        }
        auto map = std::make_shared<mvt::Map>(mapSettings);
        
        // Set parameters
        map->setParameters(parameters);
        map->setStyleParameters(styleParameters);

        // Compile and build layers
        for (const std::string& layerName : layerNames) {
            std::map<std::string, AttachmentStyle> attachmentStyleMap;
            try {
                std::map<std::pair<int, int>, std::list<AttachmentPropertySets>> layerZoomAttachments;

                CartoCSSCompiler compiler;
                compiler.setIgnoreLayerPredicates(_ignoreLayerPredicates);
                compiler.compileLayer(styleSheet, layerName, 0, MAX_ZOOM + 1, layerZoomAttachments, constantFieldMap);
                CartoCSSMapnikTranslator translator(_logger);
                for (auto it = layerZoomAttachments.begin(); it != layerZoomAttachments.end(); it++) {
                    updateAttachmentStyleMap(translator, map, it->first.first, it->first.second, it->second, attachmentStyleMap);
                }
            }
            catch (const std::exception& ex) {
                throw LoaderException(std::string("Error while building properties for layer ") + layerName + ": " + ex.what());
            }
            if (attachmentStyleMap.empty()) {
                continue;
            }
            std::vector<AttachmentStyle> attachmentStyles = getSortedAttachmentStyles(attachmentStyleMap);

            // Create style for each attachment
            std::vector<std::string> styleNames;
            for (const AttachmentStyle& attachmentStyle : attachmentStyles) {
                std::string styleName = layerName + attachmentStyle.attachment;
                std::shared_ptr<mvt::Style> style = buildStyle(attachmentStyle, styleName);
                map->addStyle(style);
                styleNames.push_back(styleName);
            }

            // Finally build the layer
            auto layer = std::make_shared<mvt::Layer>(layerName, styleNames);
            map->addLayer(layer);
        }
        return map;
    }

    std::shared_ptr<mvt::Style> CartoCSSMapLoader::buildStyle(const AttachmentStyle& attachmentStyle, const std::string& styleName) const {
        auto style = std::make_shared<mvt::Style>(styleName, attachmentStyle.opacity, attachmentStyle.imageFilters, attachmentStyle.compOp, mvt::Style::FilterMode::FIRST, attachmentStyle.simplify, attachmentStyle.rules);
        style->optimizeRules();
        return style;
    }

    void CartoCSSMapLoader::updateAttachmentStyleMap(const CartoCSSMapnikTranslator& translator, const std::shared_ptr<mvt::Map>& map, int minZoom, int maxZoom, const std::list<AttachmentPropertySets>& layerAttachments, std::map<std::string, AttachmentStyle>& attachmentStyleMap) const {
        for (const AttachmentPropertySets& layerAttachment : layerAttachments) {
            int layerAttachmentOrder = layerAttachment.calculateOrder();
            if (attachmentStyleMap.find(layerAttachment.getAttachment()) == attachmentStyleMap.end()) {
                attachmentStyleMap[layerAttachment.getAttachment()].attachment = layerAttachment.getAttachment();
                attachmentStyleMap[layerAttachment.getAttachment()].order = layerAttachmentOrder;
            }
            
            AttachmentStyle& attachmentStyle = attachmentStyleMap[layerAttachment.getAttachment()];
            attachmentStyle.order = std::min(attachmentStyle.order, layerAttachmentOrder);
            for (const PropertySet& propertySet : layerAttachment.getPropertySets()) {
                std::shared_ptr<const mvt::Rule> rule = translator.buildRule(propertySet, map, minZoom, maxZoom);
                if (rule && rule.get()->getSymbolizers().size() > 0) {
                    attachmentStyle.rules.push_back(rule);
                }

                // Copy opacity, image-filters and comp-op properties. These are style-level properties, not symbolizer peroperties.
                // Note that we ignore filters, this is CartoCSS design issue and represents how CartoCSS is translated to Mapnik.
                if (auto opacityProp = propertySet.findProperty("opacity")) {
                    if (auto val = std::get_if<Value>(&opacityProp->getExpression())) {
                        attachmentStyle.opacity = mvt::ValueConverter<float>::convert(translator.buildValue(*val));
                    }
                    else {
                        _logger->write(mvt::Logger::Severity::WARNING, "Opacity must be constant expression");
                    }
                }
                
                if (auto imageFiltersProp = propertySet.findProperty("image-filters")) {
                    if (auto funcExpr = std::get_if<std::shared_ptr<FunctionExpression>>(&imageFiltersProp->getExpression())) {
                        attachmentStyle.imageFilters = (*funcExpr)->getFunc() + "(";
                        for (std::size_t i = 0; i < (*funcExpr)->getArgs().size(); i++) {
                            attachmentStyle.imageFilters += (i > 0 ? "," : "");
                            if (auto val = std::get_if<Value>(&(*funcExpr)->getArgs()[i])) {
                                attachmentStyle.imageFilters += mvt::ValueConverter<std::string>::convert(translator.buildValue(*val));
                            }
                        }
                        attachmentStyle.imageFilters += ")";
                    }
                    else {
                        _logger->write(mvt::Logger::Severity::WARNING, "ImageFilters must be function expression");
                    }
                }

                if (auto compOpProp = propertySet.findProperty("comp-op")) {
                    if (auto val = std::get_if<Value>(&compOpProp->getExpression())) {
                        std::string compOp = mvt::ValueConverter<std::string>::convert(translator.buildValue(*val));
                        if (!compOp.empty()) {
                            try {
                                attachmentStyle.compOp = mvt::parseCompOp(compOp);
                            }
                            catch (const mvt::ParserException& ex) {
                                _logger->write(mvt::Logger::Severity::WARNING, std::string("Failed to parse layer CompOp: ") + ex.what());
                            }
                        }
                    }
                    else {
                        _logger->write(mvt::Logger::Severity::WARNING, "CompOp must be constant expression");
                    }
                }
                if (auto simplifyProp = propertySet.findProperty("simplify")) {
                    if (auto val = std::get_if<Value>(&simplifyProp->getExpression())) {
                        std::string simplify = mvt::ValueConverter<std::string>::convert(translator.buildValue(*val));
                        if (!simplify.empty()) {
                            try {
                                attachmentStyle.simplify = simplify;
                            }
                            catch (const mvt::ParserException& ex) {
                                _logger->write(mvt::Logger::Severity::WARNING, std::string("Failed to parse layer simplify: ") + ex.what());
                            }
                        }
                    }
                    else {
                        _logger->write(mvt::Logger::Severity::WARNING, "simplify must be constant expression");
                    }
                }
            }
        }
    }

    std::vector<CartoCSSMapLoader::AttachmentStyle> CartoCSSMapLoader::getSortedAttachmentStyles(const std::map<std::string, AttachmentStyle>& attachmentStyleMap) const {
        std::vector<AttachmentStyle> attachmentStyles;
        for (auto it = attachmentStyleMap.begin(); it != attachmentStyleMap.end(); it++) {
            auto insertIt = std::upper_bound(attachmentStyles.begin(), attachmentStyles.end(), it->second, [](const AttachmentStyle& attachmentStyle1, const AttachmentStyle& attachmentStyle2) {
                return attachmentStyle1.order < attachmentStyle2.order;
            });
            attachmentStyles.insert(insertIt, it->second);
        }
        return attachmentStyles;
    }
}
