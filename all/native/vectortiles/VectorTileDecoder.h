/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VECTORTILEDECODER_H_
#define _MASSIF_VECTORTILEDECODER_H_

#include "graphics/Color.h"

#include <memory>
#include <string>
#include <mutex>
#include <map>
#include <vector>

#include <cglib/mat.h>

#include <mapnikvt/Map.h>
#include <mapnikvt/SymbolizerContext.h>

namespace massif {
    namespace vt {
        struct TileId;
        class Tile;
        class TileTransformer;
    }

    class BinaryData;
    class VectorTileFeature;
    class VectorTileFeatureCollection;
    class MapBounds;

    /**
     * Abstract base class for vector tile decoders.
     */
    class VectorTileDecoder {
    public:
        typedef std::map<int, std::shared_ptr<const vt::Tile> > TileMap;

        /**
         * Interface for monitoring decoder parameter change events.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }
    
            /**
             * Listener method that gets called when decoder parameters have changed and need to be updated.
             */
            virtual void onDecoderChanged() = 0;

            /**
             * Listener method that gets called when only the appearance of the already decoded tiles
             * has changed - a style parameter that nothing but a per-frame colour or width reads.
             * Redrawing is enough, the tiles do not have to be decoded again.
             */
            virtual void onDecoderRefreshed() { onDecoderChanged(); }
        };
    
        virtual ~VectorTileDecoder();
    
        /**
         * Returns map settings.
         * @return Map settings.
         */
        virtual std::shared_ptr<const mvt::Map::Settings> getMapSettings() const = 0;

        /**
         * Returns symbolizer context settings.
         * @return Symbolizer context settings.
         */
        virtual std::shared_ptr<const mvt::SymbolizerContext::Settings> getSymbolizerContextSettings() const = 0;

        /**
         * Adds a fallback font. The font should be TTF or OTF font. It is used for glyphs not found in style defined fonts.
         * @param fontData The font binary.
         */
        virtual void addFallbackFont(const std::shared_ptr<BinaryData>& fontData) = 0;

        /**
         * Sets the screen pixels per style pixel of the view the decoded tiles are drawn in.
         * The layer pushes it when it joins a map; a decoder that does not care may ignore it.
         * @param pixelScale The display scale factor (DPI / 160).
         */
        virtual void setPixelScale(float pixelScale) { }

        /**
         * Sets the size a tile is drawn at, in style pixels. A style's sizes are fractions of it,
         * so it has to follow Options::TileDrawSize or they scale with the tile.
         * @param tileSize The tile draw size, in style pixels.
         */
        virtual void setTileSize(float tileSize) { }

        /**
         * Returns minimum zoom level supported for by the decoder (or style).
         * @return Minimum supported zoom level.
         */
        virtual int getMinZoom() const = 0;
    
        /**
         * Returns maximum zoom level supported for by the decoder (or style).
         * @return Maximum supported zoom level.
         */
        virtual int getMaxZoom() const = 0;

        /**
         * Decodes the specified feature from the tile.
         * @param id The id of the feature to decode.
         * @param tile The tile coordinates.
         * @param tileData The tile data to use.
         * @param tileBounds The bounds for the tile (used for coordinate transformation).
         * @return The feature, if found. Null if not found.
         */
        virtual std::shared_ptr<VectorTileFeature> decodeFeature(long long id, const vt::TileId& tile, const std::shared_ptr<BinaryData>& tileData, const MapBounds& tileBounds) const = 0;

        /**
         * Decodes all features from the tile.
         * @param tile The tile coordinates.
         * @param tileData The tile data to use.
         * @param tileBounds The bounds for the tile (used for coordinate transformation).
         * @param onlyLayers layers to filter (can be much faster)
         * @return The list of tile features.
         */
        virtual std::shared_ptr<VectorTileFeatureCollection> decodeFeatures(const vt::TileId& tile, const std::shared_ptr<BinaryData>& tileData, const MapBounds& tileBounds, const std::vector<std::string>& onlyLayers) const = 0;

        /**
         * Loads the specified vector tile.
         * @param tile The id of the tile to load.
         * @param targetTile The target tile id that will be created from the data.
         * @param styleZoom The zoom the style rules are matched at, usually the target tile's zoom.
         * @param tileData The tile data to decode.
         * @return The vector tile data, for each frame. If the tile is not available, null is returned.
         */
        virtual std::shared_ptr<TileMap> decodeTile(const vt::TileId& tile, const vt::TileId& targetTile, int styleZoom, const std::shared_ptr<vt::TileTransformer>& tileTransformer, const std::shared_ptr<BinaryData>& tileData) const = 0;
    
        /**
         * Notifies listeners that the decoder parameters have changed. Action taken depends on the implementation of the
         * listeners, but generally all cached tiles will be reloaded. 
         */
        virtual void notifyDecoderChanged();
        virtual void notifyDecoderRefreshed();
        
        /**
         * Registers listener for decoder change events.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
    
        /**
         * Unregisters listener from decoder change events.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        
    protected:
        VectorTileDecoder();

        static cglib::mat3x3<float> calculateTileTransform(const massif::vt::TileId& tileId, const massif::vt::TileId& targetTileId);
        
    private:
        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;
    };
        
}

#endif
