#include "SymbolizerContext.h"
#include "PointSymbolizer.h"
#include "LineSymbolizer.h"
#include "LinePatternSymbolizer.h"
#include "PolygonSymbolizer.h"
#include "PolygonPatternSymbolizer.h"
#include "BuildingSymbolizer.h"
#include "MarkersSymbolizer.h"
#include "TextSymbolizer.h"
#include "ShieldSymbolizer.h"
#include "ValueConverter.h"

namespace carto::mvt {
    SymbolizerContext::Settings::Settings(float tileSize, std::shared_ptr<const std::map<std::string, Value>> nutiParameterValueMap, std::shared_ptr<const vt::Font> fallbackFont) :
        _tileSize(tileSize), _geometryScale(1.0f), _fontScale(1.0f), _zoomLevelBias(0.0f), _nutiParameterValueMap(std::move(nutiParameterValueMap)), _fallbackFont(std::move(fallbackFont))
    {
        auto geometryScaleIt = _nutiParameterValueMap.find("_geometryscale");
        if (geometryScaleIt != _nutiParameterValueMap.end()) {
            _geometryScale = ValueConverter<float>::convert(geometryScaleIt->second);
        }

        auto fontScaleIt = _nutiParameterValueMap.find("_fontscale");
        if (fontScaleIt != _nutiParameterValueMap.end()) {
            _fontScale = ValueConverter<float>::convert(fontScaleIt->second);
        }
        
        auto zoomLevelBiasIt = _nutiParameterValueMap.find("_zoomlevelbias");
        if (zoomLevelBiasIt != _nutiParameterValueMap.end()) {
            _zoomLevelBias = ValueConverter<float>::convert(zoomLevelBiasIt->second);
        }
    }
}
