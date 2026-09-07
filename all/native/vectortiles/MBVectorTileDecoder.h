/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MBVECTORTILEDECODER_H_
#define _MASSIF_MBVECTORTILEDECODER_H_

#include "vectortiles/VectorTileDecoder.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <variant>

#include <mapnikvt/Value.h>
#include <mapnikvt/StyleParameterStore.h>
#include <mapnikvt/LayerConfigResolver.h>

namespace massif {
    namespace mvt {
        class Map;
        class LayerFeatureDecoder;
        class SymbolizerContext;
        class Logger;
    }

    class AssetPackage;
    class CompiledStyleSet;
    class CartoCSSStyleSet;

    namespace TileFormat {
        /**
         * Supported binary vector tile formats.
         */
        enum TileFormat {
            /**
             * Detect the format from the tile data. The two are unambiguous in practice, but set the
             * format explicitly when the source is known - it skips the check and cannot be fooled.
             */
            TILE_FORMAT_AUTO,
            /**
             * MapBox Vector Tile, the protobuf format.
             */
            TILE_FORMAT_MVT,
            /**
             * MapLibre Tile, the columnar format. Smaller tiles and faster decoding, but the whole
             * tile is decoded at once - MVT decodes only the layers and attributes the style asks for.
             */
            TILE_FORMAT_MLT
        };
    }


    /**
     * Decoder for vector tiles in MapBox format.
     */
    class MBVectorTileDecoder : public VectorTileDecoder {
    public:
        /**
         * Constructs a decoder for MapBox vector tiles based on specified compiled style set.
         * @param compiledStyleSet The compiled style set for the tiles.
         * @throws std::runtime_error If the decoder could not be created or there are issues with the style set.
         */
        explicit MBVectorTileDecoder(const std::shared_ptr<CompiledStyleSet>& compiledStyleSet);
        /**
         * Constructs a decoder for MapBox vector tiles based on specified CartoCSS style set.
         * @param cartoCSSStyleSet The CartoCSS style set for the tiles.
         * @throws std::runtime_error If the decoder could not be created or there are issues with the style set.
         */
        explicit MBVectorTileDecoder(const std::shared_ptr<CartoCSSStyleSet>& cartoCSSStyleSet);
        virtual ~MBVectorTileDecoder();
        
        /**
         * Returns the current compiled style set used by the decoder.
         * If decoder uses non-compiled style set, null is returned.
         * @return The current style set.
         */
        std::shared_ptr<CompiledStyleSet> getCompiledStyleSet() const;
        /**
         * Sets the current compiled style set used by the decoder.
         * @param styleSet The new style set to use.
         * @throws std::runtime_error If the decoder could not be updated or there are issues with the style set.
         */
        void setCompiledStyleSet(const std::shared_ptr<CompiledStyleSet>& styleSet);
    
        /**
         * Returns the current CartoCSS style set used by the decoder.
         * If decoder uses non-CartoCSS style set, null is returned.
         * @return The current style set.
         */
        std::shared_ptr<CartoCSSStyleSet> getCartoCSSStyleSet() const;
        /**
         * Sets the current CartoCSS style set used by the decoder.
         * @param styleSet The new style set to use.
         * @throws std::runtime_error If the decoder could not be updated or there are issues with the style set.
         */
        void setCartoCSSStyleSet(const std::shared_ptr<CartoCSSStyleSet>& styleSet);

        /**
         * Returns the list of all available style parameters.
         * @return The list of all available style parameters.
         */
        std::vector<std::string> getStyleParameters() const;
        /**
         * Returns the value of the specified style parameter.
         * The style parameter must be declared in the current style.
         * @param param The parameter to return.
         * @return The value of the parameter. If parameter does not exists, empty string is returned.
         * @throws std::invalid_argument If the style parameter does not exist.
         */
        std::string getStyleParameter(const std::string& param) const;
        /**
         * Sets the value of the specified parameter.
         * The style parameter must be declared in the current style.
         * @param param The parameter to set.
         * @param value The value for the parameter.
         * @return True if the parameter was set. False if the style parameter does not exist or could not be set.
         */
        bool setStyleParameter(const std::string& param, const std::string& value);

        /**
         * Sets the values of the specified parameters.
         * The style parameters must be declared in the current style.
         * @param params The getStyleParameters to set.
         */
        void setStyleParameters(const std::map<std::string, std::string>& params);
        /**
         * Sets the values of the specified parameters.
         * The style parameters must be declared in the current style.
         * @param params The getStyleParameters to set.
         */
        void setJSONStyleParameters(const std::string& params);

        /**
         * Returns the ordered list of style layer names as declared by the style (the project
         * JSON "layers" array, or the Layer elements of a Mapnik XML style). This defines both
         * the draw order and which layers exist. CompositeVectorTileLayer uses it to place
         * external data sources in the layer order: a source whose name is not in this list has
         * no slot in the style and is not drawn, so this is the way to check a style before
         * wiring sources into it.
         * @return The ordered style layer names.
         */
        std::vector<std::string> getStyleLayerNames() const;

        /**
         * Evaluates the config symbolizer(s) of the named style layer (raster / hillshade /
         * contour) at the given fractional view zoom and the current style parameter
         * state, without decoding a tile. Honors rule zoom ranges and filter predicates.
         * Used by CompositeVectorTileLayer to drive external data source settings per frame.
         * Note: for internal use, not exposed to the public API.
         * @param layerName The style layer name.
         * @param viewZoom The fractional view zoom.
         * @return The resolved configuration (visible flag + evaluated property values).
         */
        mvt::ResolvedLayerConfig resolveLayerConfig(const std::string& layerName, float viewZoom) const;

        /**
         * Returns the { minZoom, maxZoom } range over which the named style layer's config
         * symbolizer rules are active. Used by CompositeVectorTileLayer to constrain an
         * external source child layer's visible zoom range. Returns { 0, 24 } if the layer
         * has no config rules. Note: for internal use, not exposed to the public API.
         * @param layerName The style layer name.
         * @return A two-element vector { minZoom, maxZoom }.
         */
        std::vector<int> getStyleLayerZoomRange(const std::string& layerName) const;


        /**
         * Returns the value of feature id override flag. This is intended for cases when feature ids in tile are not globally unique.
         * @return The value of feature id override flag.
         */
        bool isFeatureIdOverride() const;
        /**
         * Sets the value of feature id override flag. This is intended for cases when feature ids in tile are not globally unique.
         * @param idOverride The value of the flag.
         */
        void setFeatureIdOverride(bool idOverride);

        /**
         * Returns the value CartoCSS 'layer name ignore' flag.
         * If set to true, CSS filters like '#layer0' are ignored and the corresponding rules are applied to all filters.
         * @return The value of CartoCSS 'layer name ignore' flag. Default is false.
         */
        bool isCartoCSSLayerNamesIgnored() const;
        /**
         * Sets the value of CartoCSS 'layer name ignore' flag
         * If set to true, CSS filters like '#layer0' are ignored and the corresponding rules are applied to all filters.
         * @param ignore The value of the flag.
         */
        void setCartoCSSLayerNamesIgnored(bool ignore);

        /**
         * Returns the binary format the tiles are decoded as.
         * @return The tile format. Default is MVT.
         */
        TileFormat::TileFormat getTileFormat() const;
        /**
         * Sets the binary format the tiles are decoded as. The two formats are not distinguishable
         * from the tile data, so the source has to say which it serves.
         * @param format The tile format.
         */
        void setTileFormat(TileFormat::TileFormat format);

        /**
         * Maps a container's declared format or encoding - an MBTiles or PMTiles metadata value, a
         * TileJSON media type - onto a tile format. Matching is case-insensitive and by substring,
         * because generators spell this differently ('mvt',
         * 'application/vnd.maplibre-vector-tile'). 'pbf' is inconclusive rather than MVT, since
         * MapLibre's tilesets keep format at 'pbf' and declare MLT through encoding instead.
         * @param format The declared format or encoding string.
         * @return The tile format, or TILE_FORMAT_AUTO when the string says nothing conclusive.
         */
        static TileFormat::TileFormat parseTileFormat(const std::string& format);

        /**
         * Returns the vector tile 'layer name override'. If empty, actual layer names are used.
         * @return The 'layer name override'.
         */
        std::string getLayerNameOverride() const;
        /**
         * Sets the 'layer name override' value. If set to non-empty value, the specific layer is used from the vector tiles.
         * @param name The new 'layer name override' value. If empty, override is not used.
         */
        void setLayerNameOverride(const std::string& name);

        virtual std::shared_ptr<const mvt::Map::Settings> getMapSettings() const;

        virtual std::shared_ptr<const mvt::SymbolizerContext::Settings> getSymbolizerContextSettings() const;

        virtual void addFallbackFont(const std::shared_ptr<BinaryData>& fontData);

        virtual void setPixelScale(float pixelScale);

        virtual int getMinZoom() const;
        
        virtual int getMaxZoom() const;

        virtual std::shared_ptr<VectorTileFeature> decodeFeature(long long id, const vt::TileId& tile, const std::shared_ptr<BinaryData>& tileData, const MapBounds& tileBounds) const;

        virtual std::shared_ptr<VectorTileFeatureCollection> decodeFeatures(const vt::TileId& tile, const std::shared_ptr<BinaryData>& tileData, const MapBounds& tileBounds, const std::vector<std::string>& onlyLayers) const;

        virtual std::shared_ptr<TileMap> decodeTile(const vt::TileId& tile, const vt::TileId& targetTile, int styleZoom, const std::shared_ptr<vt::TileTransformer>& tileTransformer, const std::shared_ptr<BinaryData>& tileData) const;
    
    protected:
        void updateCurrentStyleSet(const std::variant<std::shared_ptr<CompiledStyleSet>, std::shared_ptr<CartoCSSStyleSet> >& styleSet);
        void updateSymbolizerContext();
        // Drops what a pixel-scale change invalidates, and only that - see setPixelScale.
        void resetSymbolizerContextRasterMaps();
        void updateParameterStore();
        void updateSelectionState();
        bool setStyleParameterInternal(const std::string& param, const std::string& value);
        bool areParametersRepaintable(const std::vector<std::string>& params) const;
        void updateSymbolizer();

        static const std::string DEFAULT_FALLBACK_FONT_NAME;
        static const int DEFAULT_TILE_SIZE;
        static const int STROKEMAP_SIZE;
        static const int GLYPHMAP_SIZE;
        static const std::size_t MAX_ASSETPACKAGE_SYMBOLIZER_CONTEXTS;
        
        const std::shared_ptr<mvt::Logger> _logger;
        float _pixelScale;
        TileFormat::TileFormat _tileFormat;
        bool _featureIdOverride;
        bool _cartoCSSLayerNamesIgnored;
        std::string _layerNameOverride;
        std::map<std::string, mvt::Value> _parameterValueMap;
        std::vector<std::shared_ptr<BinaryData> > _fallbackFonts;
        std::variant<std::shared_ptr<CompiledStyleSet>, std::shared_ptr<CartoCSSStyleSet> > _styleSet;
        std::string _styleAssetName; // what the current _map was loaded from, so the symbolizer
        std::shared_ptr<AssetPackage> _styleAssetPackage; // context can be rebuilt without it
        std::shared_ptr<mvt::StyleParameterStore> _parameterStore; // the values the decoded tiles read
        std::set<std::string> _liveParameters; // those of them that only a per-frame function reads
        std::string _selectionParameter; // the one that selects a feature, if the style has one
        // Its value, hashed: the tiles read it while they are drawn, so setting the selection is a
        // style-byte rewrite rather than a decode. Shared with every tile this decoder built.
        std::shared_ptr<std::atomic<std::uint64_t> > _selectionState = std::make_shared<std::atomic<std::uint64_t> >(0);
        std::shared_ptr<const mvt::Map> _map;
        std::shared_ptr<const mvt::Map::Settings> _mapSettings;
        std::shared_ptr<const mvt::SymbolizerContext> _symbolizerContext;
        std::shared_ptr<const mvt::SymbolizerContext::Settings> _symbolizerContextSettings;
        std::map<std::pair<std::string, std::shared_ptr<AssetPackage> >, std::shared_ptr<const mvt::SymbolizerContext> > _assetPackageSymbolizerContexts;

        std::shared_ptr<mvt::LayerFeatureDecoder> createFeatureDecoder(const std::shared_ptr<BinaryData>& tileData) const;
        // The one built for the last decodeFeature(s) call, reused while the caller walks one tile
        std::shared_ptr<mvt::LayerFeatureDecoder> getCachedFeatureDecoder(const std::shared_ptr<BinaryData>& tileData) const;

        mutable std::pair<std::shared_ptr<BinaryData>, std::shared_ptr<mvt::LayerFeatureDecoder> > _cachedFeatureDecoder;
    
        mutable std::mutex _mutex;
    };
        
}

#endif
