/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEDATASOURCE_H_
#define _MASSIF_TILEDATASOURCE_H_

#include "core/MapTile.h"
#include "core/MapBounds.h"
#include "core/Variant.h"
#include "datasources/components/TileData.h"

#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include <map>

namespace massif {
    class Projection;

    /**
     * Abstract base class for tile data sources. It provides default implementation 
     * for listener registration and other common tile data source methods.
     */
    class TileDataSource : public std::enable_shared_from_this<TileDataSource> {
    public:
        /**
         * Interface for monitoring data source change events.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }
    
            /**
             * Listener method that gets called when tiles have changes and need to be updated.
             * If the removeTiles flag is set all caches should be cleared prior to updating.
             * @param removeTiles The remove tiles flag.
             */
            virtual void onTilesChanged(bool removeTiles) = 0;
        };
        
        virtual ~TileDataSource();
        
        /**
         * Returns the minimum zoom level supported by this data source.
         * @return The minimum zoom level supported (inclusive).
         */
        virtual int getMinZoom() const;
        /**
         * Returns the maximum zoom level supported by this data source.
         * @return The maximum zoom level supported (exclusive).
         */
        virtual int getMaxZoom() const;


        /**
         * Returns the maximum zoom level supported by this data source + getMaxOverzoomLevel if >= 0.
         * @return The maximum zoom level supported (exclusive).
         */
        virtual int getMaxZoomWithOverzoom() const;

        /**
         * Sets the maximum overzoom level for this datasource.
         * If a tile for the given zoom level Z is not available, SDK will try to use tiles with zoom levels Z-1, ..., Z-MaxOverzoomLevel.
         * The default is -1 (disabled).
         * @param overzoomLevel The new maximum overzoom value.
         */
        void setMaxOverzoomLevel(int overzoomLevel);

        /**
         * Gets the current maximum overzoom level for this datasource.
         * Over it the datasource will not be "drawn"
         * @return The current maximum overzoom level for this datasource.
         */
        int getMaxOverzoomLevel() const;

        bool isMaxOverzoomLevelSet() const;

        /**
         * Sets the encoding type for tiles loaded from this data source.
         * This metadata is used by HillshadeRasterTileLayer to determine how to decode elevation data.
         * @param encoding The encoding ("terrarium" or "mapbox"). Empty string clears the metadata.
         */
        void setEncoding(const std::string& encoding);

        /**
         * Gets the current encoding type.
         * @return The encoding type, or empty string if not set.
         */
        virtual std::string getEncoding() const;

        /**
         * Returns a meta data element corresponding to the key. The well-known key is
         * "dem_encoding" ("terrarium" or "mapbox"), which is the value setEncoding sets: the
         * hillshade, the contours and the terrain read their elevation decoder from it.
         * Falls back to the container's own metadata (see getMetaData) when the key was not set
         * on the source itself. If no value is found, a null variant is returned.
         * @param key The key to use.
         * @return The value corresponding to the key, or an empty variant.
         */
        Variant getMetaDataElement(const std::string& key) const;

        /**
         * Adds a new key-value pair to the meta data map. If the key already exists in the map,
         * its value will be replaced by the new value. The map is attached to every tile this
         * source loads. Takes effect for tiles loaded after the call.
         * @param key The new key.
         * @param element The new value.
         */
        void setMetaDataElement(const std::string& key, const Variant& element);

        /**
         * Reads one entry of the source's own metadata, when it has any - the MBTiles or PMTiles
         * metadata table, for instance. Sources that carry none return an empty string, as do keys
         * they do not define.
         * @param key The metadata key, as named by the container's specification.
         * @return The value, or empty string if the source does not provide it.
         */
        virtual std::string getMetaData(const std::string& key) const;

        /**
         * Reads one entry of the source's own metadata. Same as getMetaData, under the name the
         * 6.1 API uses.
         * @param key The metadata key, as named by the container's specification.
         * @return The value, or empty string if the source does not provide it.
         */
        std::string getContainerMetaData(const std::string& key) const;

        /**
         * Returns the extent of the tiles in this data source.
         * The bounds are in coordinate system of the projection of the data source.
         * @return The extent of the data source.
         */
        virtual MapBounds getDataExtent() const;

        /**
         * Returns the projection of this tile source.
         * @return The projection of this tile source.
         */
        std::shared_ptr<Projection> getProjection() const;
        
        /**
         * Loads the specified tile.
         * Note: the tile coordinate system used here is vertically flipped relative to layer tile coordinate system.
         * @param tile The tile to load.
         * @return The tile data. If the tile is not available, null may be returned.
         */
        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile) = 0;
    
        /**
         * Notifies listeners that the tiles have changed. Action taken depends on the implementation of the
         * listeners, but generally all cached tiles will be reloaded. If the removeTiles flag is set all caches will be cleared
         * prior to reloading, if it's not set then the reloaded tiles will replace the old tiles in caches as they finish loading.
         * @param removeTiles The remove tiles flag.
         */
        virtual void notifyTilesChanged(bool removeTiles);
    
        /**
         * Registers listener for data source change events.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
    
        /**
         * Unregisters listener from data source change events.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

        /**
         * Builds tag map from tile info.
         * @param tile The tile for the tag map.
         * @return The tag map that will be used for replacing tags in templates.
         */
        virtual std::map<std::string, std::string> buildTagValues(const MapTile& tile) const;

        /**
         * Builds metadata map for the tile. Can be overridden by subclasses to provide tile-specific metadata.
         * @param tile The tile for the metadata.
         * @return The metadata map that will be attached to the tile data.
         */
        virtual std::map<std::string, std::shared_ptr<Variant> > buildTileMetadata(const MapTile& tile) const;
    
    protected:
        /**
         * Constructs an abstract TileDataSource object.
         * Note: EPSG3857 projection is used. minZoom is defined to be 0, maxZoom is defined to be 24.
         */
        TileDataSource();
        /**
         * Constructs an abstract TileDataSource object.
         * Note: EPSG3857 projection is used.
         * @param minZoom The minimum zoom level supported by this data source.
         * @param maxZoom The maximum zoom level supported by this data source.
         */
        TileDataSource(int minZoom, int maxZoom);
        
        /**
         * Applies metadata to a TileData object by calling buildTileMetadata.
         * This should be called by subclasses after creating TileData in loadTile.
         * @param tileData The TileData object to apply metadata to.
         * @param tile The tile for which metadata should be built.
         */
        void applyTileMetadata(const std::shared_ptr<TileData>& tileData, const MapTile& tile) const;

        std::atomic<int> _minZoom;
        std::atomic<int> _maxZoom;
        std::atomic<int> _maxOverzoomLevel;
        const std::shared_ptr<Projection> _projection;
        std::string _encoding;
        std::map<std::string, Variant> _metaData;
    
    private:
        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _mutex;
    };
    
}

#endif
