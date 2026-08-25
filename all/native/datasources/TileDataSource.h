/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEDATASOURCE_H_
#define _MASSIF_TILEDATASOURCE_H_

#include "core/MapTile.h"
#include "core/MapBounds.h"
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
         * Returns a copy of the data source meta data map. The changes you make to this map are NOT reflected in the actual meta data of the source.
         * The map is attached to every tile this source loads, and consumers read their settings
         * from it - "dem_encoding" ("mapbox" or "terrarium") selects the elevation decoder, for instance.
         * A wrapper source with no map of its own answers with its wrapped source's.
         * @return A copy of the data source meta data map.
         */
        std::map<std::string, Variant> getMetaData() const;
        /**
         * Sets a new meta data map for the data source. Old meta data values will be lost.
         * Takes effect for tiles loaded after the call.
         * @param metaData The new meta data map for this data source.
         */
        void setMetaData(const std::map<std::string, Variant>& metaData);

        /**
         * Returns true if the specified key exists in the data source meta data map, or in the
         * container's own metadata.
         * @param key The key to check.
         * @return True if the meta data element exists.
         */
        bool containsMetaDataKey(const std::string& key) const;
        /**
         * Returns a meta data element corresponding to the key. Falls back to the container's own
         * metadata (see getContainerMetaData) when the key was not set on the source itself.
         * If no value is found null variant is returned.
         * @param key The key to use.
         * @return The value corresponding to the key, or an empty variant.
         */
        Variant getMetaDataElement(const std::string& key) const;
        /**
         * Adds a new key-value pair to the meta data map. If the key already exists in the map,
         * it's value will be replaced by the new value.
         * Takes effect for tiles loaded after the call.
         * @param key The new key.
         * @param element The new value.
         */
        void setMetaDataElement(const std::string& key, const Variant& element);

        /**
         * Reads one entry of the source's own metadata, when it has any - the MBTiles or PMTiles
         * metadata table, for instance. Sources that carry none return an empty string, as do keys
         * they do not define. Unlike the meta data map above this is read-only and is NOT attached
         * to the loaded tiles: a container's metadata can be tens of kilobytes.
         * @param key The metadata key, as named by the container's specification.
         * @return The value, or empty string if the source does not provide it.
         */
        virtual std::string getContainerMetaData(const std::string& key) const;

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
         * The shared, immutable meta data map attached to every tile this source loads. Null when
         * the source carries none. Wrapper sources hand their child's map through unchanged.
         * @return The shared meta data map, or null.
         */
        virtual std::shared_ptr<const std::map<std::string, Variant> > getMetaDataPtr() const;

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
         * Attaches this source's meta data map to a TileData object. Subclasses call it after
         * creating TileData in loadTile. Costs one atomic increment - the map is shared, not copied.
         * Does nothing if the tile is null.
         * @param tileData The TileData object to attach the meta data to.
         */
        void applyTileMetaData(const std::shared_ptr<TileData>& tileData) const;

        std::atomic<int> _minZoom;
        std::atomic<int> _maxZoom;
        std::atomic<int> _maxOverzoomLevel;
        const std::shared_ptr<Projection> _projection;
        // Immutable and shared with every tile loaded so far, so a setter copies before writing.
        std::shared_ptr<const std::map<std::string, Variant> > _metaData;

    private:
        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _mutex;
    };
    
}

#endif
