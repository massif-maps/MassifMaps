/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_SYMBOLIZERCONTEXT_H_
#define _MASSIF_MAPNIKVT_SYMBOLIZERCONTEXT_H_

#include "ExpressionContext.h"
#include "vt/BitmapManager.h"
#include "vt/FontManager.h"
#include "vt/StrokeMap.h"
#include "vt/GlyphMap.h"
#include "vt/TileGeometry.h"

#include <memory>

namespace massif::mvt {
    class SymbolizerContext final {
    public:
        struct Settings {
            explicit Settings(float tileSize, std::shared_ptr<const StyleParameterStore> styleParameterStore, std::shared_ptr<const vt::Font> fallbackFont, float pixelScale = 1.0f, vt::StyleStateRef styleState = vt::StyleStateRef());

            float getTileSize() const { return _tileSize; }
            float getGeometryScale() const { return _geometryScale; }
            float getFontScale() const { return _fontScale; }
            float getZoomLevelBias() const { return _zoomLevelBias; }
            // Screen pixels per style pixel - the renderer scales everything by it, so a label's
            // size in style units only becomes a size in pixels here. Glyph rasterization needs it
            // (see pickGlyphRenderSize); nothing else does.
            float getPixelScale() const { return _pixelScale; }

            const std::shared_ptr<const StyleParameterStore>& getStyleParameterStore() const { return _styleParameterStore; }
            std::shared_ptr<const std::map<std::string, Value>> getStyleParameterValueMap() const { return _styleParameterStore ? _styleParameterStore->getValues() : std::shared_ptr<const std::map<std::string, Value>>(); }
            const std::shared_ptr<const vt::Font>& getFallbackFont() const { return _fallbackFont; }
            // The hash of the value the selecting parameter holds, handed to the tiles as they are
            // built so a change to it is answered by a repaint - see SelectionParameter.
            const vt::StyleStateRef& getStyleState() const { return _styleState; }

        private:
            float _tileSize;
            float _geometryScale;
            float _fontScale;
            float _zoomLevelBias;
            float _pixelScale;
            
            std::shared_ptr<const StyleParameterStore> _styleParameterStore;
            std::shared_ptr<const vt::Font> _fallbackFont;
            vt::StyleStateRef _styleState;
        };

        explicit SymbolizerContext(std::shared_ptr<vt::BitmapManager> bitmapManager, std::shared_ptr<vt::FontManager> fontManager, std::shared_ptr<vt::StrokeMap> strokeMap, std::shared_ptr<vt::GlyphMap> glyphMap, const Settings& settings) : _bitmapManager(std::move(bitmapManager)), _fontManager(std::move(fontManager)), _strokeMap(std::move(strokeMap)), _glyphMap(std::move(glyphMap)), _settings(settings) { }

        std::shared_ptr<vt::BitmapManager> getBitmapManager() const { return _bitmapManager; }
        std::shared_ptr<vt::FontManager> getFontManager() const { return _fontManager; }
        std::shared_ptr<vt::StrokeMap> getStrokeMap() const { return _strokeMap; }
        std::shared_ptr<vt::GlyphMap> getGlyphMap() const { return _glyphMap; }
        const Settings& getSettings() const { return _settings; }

    private:
        const std::shared_ptr<vt::BitmapManager> _bitmapManager;
        const std::shared_ptr<vt::FontManager> _fontManager;
        const std::shared_ptr<vt::StrokeMap> _strokeMap;
        const std::shared_ptr<vt::GlyphMap> _glyphMap;
        const Settings _settings;
    };
}

#endif
