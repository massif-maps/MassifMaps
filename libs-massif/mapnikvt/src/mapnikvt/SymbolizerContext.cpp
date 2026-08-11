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
    SymbolizerContext::Settings::Settings(float tileSize, std::shared_ptr<const NutiParameterStore> nutiParameterStore, std::shared_ptr<const vt::Font> fallbackFont, float pixelScale) :
        _tileSize(tileSize), _geometryScale(1.0f), _fontScale(1.0f), _zoomLevelBias(0.0f), _pixelScale(pixelScale), _nutiParameterStore(std::move(nutiParameterStore)), _fallbackFont(std::move(fallbackFont))
    {
        // These three are read ONCE, here: they scale the geometry and the glyphs a tile is built
        // with, so they can never be live - changing one goes through a full re-decode, which
        // rebuilds these settings.
        if (_nutiParameterStore) {
            std::shared_ptr<const std::map<std::string, Value>> nutiParameterValueMap = _nutiParameterStore->getValues();

            auto geometryScaleIt = nutiParameterValueMap->find("_geometryscale");
            if (geometryScaleIt != nutiParameterValueMap->end()) {
                _geometryScale = ValueConverter<float>::convert(geometryScaleIt->second);
            }

            auto fontScaleIt = nutiParameterValueMap->find("_fontscale");
            if (fontScaleIt != nutiParameterValueMap->end()) {
                _fontScale = ValueConverter<float>::convert(fontScaleIt->second);
            }

            auto zoomLevelBiasIt = nutiParameterValueMap->find("_zoomlevelbias");
            if (zoomLevelBiasIt != nutiParameterValueMap->end()) {
                _zoomLevelBias = ValueConverter<float>::convert(zoomLevelBiasIt->second);
            }
        }
    }
}
