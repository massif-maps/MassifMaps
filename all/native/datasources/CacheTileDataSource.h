/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CACHETILEDATASOURCE_H_
#define _MASSIF_CACHETILEDATASOURCE_H_

#include "datasources/TileDataSource.h"
#include "components/DirectorPtr.h"

namespace massif {
    
    /**
     * A tile data source that loads tiles from another tile data source and caches them.
     */
    class CacheTileDataSource : public TileDataSource {
    public:
        virtual ~CacheTileDataSource();

        virtual int getMinZoom() const;
        virtual int getMaxZoom() const;

        virtual MapBounds getDataExtent() const;

        /**
         * The cache's own meta data if it has any, else the wrapped source's: a cache is
         * transparent unless it is configured. Every public meta data accessor goes through it.
         * @return The shared meta data map, or null.
         */
        virtual std::shared_ptr<const std::map<std::string, Variant> > getMetaDataPtr() const;

        virtual std::string getContainerMetaData(const std::string& key) const;

        virtual void notifyTilesChanged(bool removeTiles);

        /**
         * Returns the original data source that the cache uses.
         * @return The original data source.
         */
        std::shared_ptr<TileDataSource> getDataSource() const;
        
        /**
         * Clear the cache.
         */
        virtual void clear() = 0;
        
        /**
         * Returns the tile cache capacity.
         * @return The tile cache capacity in bytes.
         */
        virtual std::size_t getCapacity() const = 0;        
        /**
         * Sets the cache capacity.
         * @param capacityInBytes The new tile cache capacity in bytes.
         */
        virtual void setCapacity(std::size_t capacityInBytes) = 0;

    protected:
        class DataSourceListener : public TileDataSource::OnChangeListener {
        public:
            explicit DataSourceListener(CacheTileDataSource& cacheDataSource);
            
            virtual void onTilesChanged(bool removeTiles);
            
        private:
            CacheTileDataSource& _cacheDataSource;
        };
        
        CacheTileDataSource(const std::shared_ptr<TileDataSource>& dataSource);

        /**
         * Overlays the cache's OWN meta data entries onto a tile, leaving the map the tile already
         * carries otherwise untouched - that map came from the leaf source that produced the tile,
         * which through a wrapper like OrderedTileDataSource is the only place the tile's real
         * settings are known. A no-op for the usual unconfigured cache, and for a null tile.
         * @param tileData The tile data to overlay the meta data onto.
         */
        void applyCacheTileMetaData(const std::shared_ptr<TileData>& tileData) const;

        const DirectorPtr<TileDataSource> _dataSource;
        
    private:
        std::shared_ptr<DataSourceListener> _dataSourceListener;
    };
    
}

#endif
