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

namespace massif::mvt {
    SymbolizerContext::Settings::Settings(float tileSize, std::shared_ptr<const StyleParameterStore> styleParameterStore, std::shared_ptr<const vt::Font> fallbackFont, float pixelScale, vt::StyleStateRef styleState) :
        _tileSize(tileSize), _geometryScale(1.0f), _fontScale(1.0f), _zoomLevelBias(0.0f), _pixelScale(pixelScale), _styleParameterStore(std::move(styleParameterStore)), _fallbackFont(std::move(fallbackFont)), _styleState(std::move(styleState))
    {
        // These three are read ONCE, here: they scale the geometry and the glyphs a tile is built
        // with, so they can never be live - changing one goes through a full re-decode, which
        // rebuilds these settings.
        if (_styleParameterStore) {
            std::shared_ptr<const std::map<std::string, Value>> styleParameterValueMap = _styleParameterStore->getValues();

            auto geometryScaleIt = styleParameterValueMap->find("_geometryscale");
            if (geometryScaleIt != styleParameterValueMap->end()) {
                _geometryScale = ValueConverter<float>::convert(geometryScaleIt->second);
            }

            auto fontScaleIt = styleParameterValueMap->find("_fontscale");
            if (fontScaleIt != styleParameterValueMap->end()) {
                _fontScale = ValueConverter<float>::convert(fontScaleIt->second);
            }

            auto zoomLevelBiasIt = styleParameterValueMap->find("_zoomlevelbias");
            if (zoomLevelBiasIt != styleParameterValueMap->end()) {
                _zoomLevelBias = ValueConverter<float>::convert(zoomLevelBiasIt->second);
            }
        }
    }
}
