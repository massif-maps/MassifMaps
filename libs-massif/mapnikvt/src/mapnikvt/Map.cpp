#include "Map.h"

namespace massif::mvt {
    void Map::setStyleParameters(const std::vector<StyleParameter>& styleParameters) {
        _styleParameterMap.clear();
        for (const StyleParameter& styleParam : styleParameters) {
            _styleParameterMap.insert({ styleParam.getName(), styleParam });
        }
    }
    
    void Map::setParameters(const std::vector<Parameter>& parameters) {
        _parameterMap.clear();
        for (const Parameter& param : parameters) {
            _parameterMap.insert({ param.getName(), param });
        }
    }

    void Map::clearStyles() {
        _styles.clear();
        _styleMap.clear();
    }

    void Map::addStyle(const std::shared_ptr<Style>& style) {
        _styles.push_back(style);
        _styleMap[style->getName()] = style;
    }

    const std::shared_ptr<Style>& Map::getStyle(const std::string& name) const {
        auto it = _styleMap.find(name);
        if (it == _styleMap.end()) {
            static const std::shared_ptr<Style> nullStyle;
            return nullStyle;
        }
        return it->second;
    }

    void Map::clearFontSets() {
        _fontSets.clear();
        _fontSetMap.clear();
    }

    void Map::addFontSet(const std::shared_ptr<FontSet>& fontSet) {
        _fontSets.push_back(fontSet);
        _fontSetMap[fontSet->getName()] = fontSet;
    }

    const std::shared_ptr<FontSet>& Map::getFontSet(const std::string& name) const {
        auto it = _fontSetMap.find(name);
        if (it == _fontSetMap.end()) {
            static const std::shared_ptr<FontSet> nullFontSet;
            return nullFontSet;
        }
        return it->second;
    }

    void Map::clearLayers() {
        _layers.clear();
        _layerMap.clear();
    }

    void Map::addLayer(const std::shared_ptr<Layer>& layer) {
        _layers.push_back(layer);
        _layerMap[layer->getName()] = layer;
    }

    const std::shared_ptr<Layer>& Map::getLayer(const std::string& name) const {
        auto it = _layerMap.find(name);
        if (it == _layerMap.end()) {
            static const std::shared_ptr<Layer> nullLayer;
            return nullLayer;
        }
        return it->second;
    }
}
