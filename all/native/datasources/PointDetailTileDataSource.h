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
     * A decorator that rebuilds a COARSE tile out of the finer tiles beneath it, for a point layer.
     *
     * The problem it exists for: a point layer in a vector tile set is thinned by zoom, and not
     * merely simplified. OpenMapTiles' `mountain_peak` drops a summit below a per-elevation minimum
     * zoom and declusters what is left at a radius that is constant in TILE PIXELS - so the ground
     * radius doubles with every level the tile coarsens by. Terrain makes a map ask for very coarse
     * tiles on purpose (`TerrainOptions::setMaxTileZoomCoarsening`, a floor of
     * `cameraTileZoom - coarsening` in TileLayer), and a peak-finder view asks for the coarsest of
     * all, because the ground it draws reaches a hundred kilometres. Which is how a panorama ends up
     * naming three summits on a horizon that has fifty.
     *
     * This reads the tiles at a fixed DETAIL zoom instead, and re-emits their points into whatever
     * tile was asked for. The answer is a pure function of (zoom, x, y) - no camera, no viewpoint,
     * nothing collected in advance - so a label set does not change as the view turns, and the
     * source works over any tile set, local or remote, without knowing how it was generated.
     *
     * Two bounds, and both matter:
     *
     *  - `maxDetailLevels` caps how far down it will reach. A level costs FOUR times the tiles, so
     *    3 is 64 tile reads for one rebuilt tile and 6 would be 4096.
     *  - `maxFeatures` caps what comes back out. Merging undoes the source's own declustering, so a
     *    rebuilt tile would otherwise hand the label culler every point there is and let it sort
     *    them out per frame - which is the flicker this is meant to remove, not a fix for it. The
     *    cap keeps the highest-ranked, by a numeric property named at construction.
     *
     * Only POINT geometry is carried over. Lines and polygons would need clipping against the
     * output tile, and they are not what a thinned label layer is made of. Layers that are not named
     * pass through from the coarse tile untouched, so a style can draw this source's summit names
     * over its own roads.
     *
     * A tile at or below the detail zoom is passed through unchanged - there is nothing to gain.
     */
    class PointDetailTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a PointDetailTileDataSource object.
         * @param dataSource The tile data source to read from.
         * @param layerName The point layer to rebuild, e.g. "mountain_peak".
         * @param detailZoom The zoom whose tiles are read. Where the source keeps every feature -
         *                   for OpenMapTiles that is the generator's own maximum zoom.
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
         *
         * The read cost is 4^levels tiles for every rebuilt tile, so this is the one number that
         * decides whether the source is affordable. Where the detail zoom is further down than
         * this allows, the tiles are read from as far as it does allow and the rest of the thinning
         * stands.
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
         *
         * 0 is no cap. The count is then whatever the tiles below actually hold, which is bounded
         * by their own thinning rather than by this - but the label culler pays for every one of
         * them on every frame, placed or not.
         * @param maxFeatures The new maximum feature count, or 0 for no cap.
         */
        void setMaxFeatures(int maxFeatures);

        /**
         * Returns the property a rebuilt tile's features are ranked by.
         * @return The property name. The default is "ele".
         */
        std::string getRankProperty() const;
        /**
         * Sets the property a rebuilt tile's features are ranked by when the cap bites.
         *
         * Read as a number; a feature without it, or with one that is not a number, ranks last. For
         * summits that is "ele", which is both the obvious order to read a panorama in and the one
         * the generator itself thins by.
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
