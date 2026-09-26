/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_POINTDETAILTILEDATASOURCE_H_
#define _MASSIF_POINTDETAILTILEDATASOURCE_H_

#include "datasources/TileDataSource.h"

#include <memory>
#include <string>
#include <vector>

namespace massif {

    /**
     * A decorator that rebuilds a coarse tile's point layer from the finer tiles beneath it, so a
     * zoom-thinned layer (e.g. OpenMapTiles `mountain_peak`) keeps its points on coarsened terrain tiles.
     * Only points are carried over; other layers and tiles at or past the detail zoom pass through.
     */
    class PointDetailTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a PointDetailTileDataSource object.
         * @param dataSource The tile data source to read from.
         * @param layerName The point layer to rebuild, e.g. "mountain_peak".
         * @param detailZoom The zoom whose tiles are read, where the source keeps every feature.
         */
        PointDetailTileDataSource(const std::shared_ptr<TileDataSource>& dataSource, const std::string& layerName, int detailZoom);
        virtual ~PointDetailTileDataSource();

        /**
         * Returns the layer that is rebuilt.
         * @return The layer name.
         */
        const std::string& getLayerName() const;

        /**
         * Returns the zoom whose tiles are read.
         * @return The detail zoom.
         */
        int getDetailZoom() const;
        /**
         * Sets the zoom whose tiles are read.
         * @param detailZoom The new detail zoom.
         */
        void setDetailZoom(int detailZoom);

        /**
         * Returns how many zoom levels below the requested tile this will reach.
         * @return The maximum number of levels. The default is 3.
         */
        int getMaxDetailLevels() const;
        /**
         * Sets how many zoom levels below the requested tile this will reach.
         * Costs 4^levels tile reads per rebuilt tile; beyond it the source's thinning stands.
         * @param levels The new maximum number of levels.
         */
        void setMaxDetailLevels(int levels);

        /**
         * Returns how many features a rebuilt tile may carry.
         * @return The maximum feature count. The default is 256.
         */
        int getMaxFeatures() const;
        /**
         * Sets how many features a rebuilt tile may carry, highest rank first.
         * Without a cap the label culler pays for every point on every frame.
         * @param maxFeatures The new maximum feature count, or 0 for no cap.
         */
        void setMaxFeatures(int maxFeatures);

        /**
         * Returns the property a rebuilt tile's features are ranked by.
         * @return The property name. The default is "ele".
         */
        std::string getRankProperty() const;
        /**
         * Sets the numeric property a rebuilt tile's features are ranked by when the cap bites.
         * A feature without a numeric value ranks last.
         * @param name The new property name.
         */
        void setRankProperty(const std::string& name);

        virtual int getMinZoom() const;
        virtual int getMaxZoom() const;
        virtual MapBounds getDataExtent() const;

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile);

    protected:
        class DataSourceListener : public TileDataSource::OnChangeListener {
        public:
            explicit DataSourceListener(PointDetailTileDataSource& dataSource);

            virtual void onTilesChanged(bool removeTiles);

        private:
            PointDetailTileDataSource& _dataSource;
        };

    private:
        std::shared_ptr<TileDataSource> _dataSource;
        const std::string _layerName;
        std::atomic<int> _detailZoom;
        std::atomic<int> _maxDetailLevels;
        std::atomic<int> _maxFeatures;
        std::string _rankProperty;
        mutable std::mutex _rankPropertyMutex;

        std::shared_ptr<DataSourceListener> _dataSourceListener;
    };

}

#endif
