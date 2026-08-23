/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CONTOURTILEDATASOURCE_H_
#define _MASSIF_CONTOURTILEDATASOURCE_H_

#include "datasources/TileDataSource.h"
#include "components/DirectorPtr.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace massif {
    class Bitmap;
    class TerrainOptions;
    class ElevationDecoder;

    /**
     * A tile data source that generates vector contour lines on the fly from an
     * RGB-encoded elevation (DEM) tile data source. The wrapped elevation data source
     * is shared (e.g. with a HillshadeRasterTileLayer) so terrain tiles are fetched
     * only once.
     *
     * The generated tiles are standard Mapbox Vector Tiles containing a single line
     * layer (default name "contour"). Each contour feature carries two attributes:
     *   - 'ele': the contour elevation in meters.
     *   - 'div': the largest "nice" divisor of the elevation (1000, 500, 250, 200,
     *            100, 50, 20 or 10), matching the gdal_contour based pipeline. This
     *            lets CartoCSS filter contour importance by elevation, e.g.
     *            #contour[div=500][zoom>=12] { ... }.
     *
     * Attach the source to a normal VectorTileLayer to render lines and labels
     * ([ele] as the label text) fully from the decoder style.
     *
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class ContourTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a ContourTileDataSource object.
         * @param dataSource The RGB-encoded elevation data source to generate contours from.
         * @param elevationDecoder The decoder used to convert RGB pixels to elevation. If null,
         *        the decoder is inferred from the data source 'encoding' metadata (defaults to terrarium).
         */
        ContourTileDataSource(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        /**
         * Constructs a ContourTileDataSource object, inferring the elevation decoder from the
         * data source 'encoding' metadata (defaults to terrarium).
         * @param dataSource The RGB-encoded elevation data source to generate contours from.
         */
        explicit ContourTileDataSource(const std::shared_ptr<TileDataSource>& dataSource);
        virtual ~ContourTileDataSource();

        /**
         * Returns the name of the generated vector tile layer.
         * @return The layer name. The default is "contour".
         */
        std::string getLayerName() const;
        /**
         * Sets the name of the generated vector tile layer. This must match the layer id used in the CartoCSS style.
         * @param name The layer name.
         */
        void setLayerName(const std::string& name);

        /**
         * Returns the base contour interval in meters.
         * @return The base contour interval in meters. The default is 10.
         */
        float getBaseInterval() const;
        /**
         * Sets the base contour interval in meters. This is the FINEST interval generated; coarser
         * tile zooms generate a multiple of it, see setIntervalMultiplier.
         * @param interval The base contour interval in meters.
         */
        void setBaseInterval(float interval);

        /**
         * Sets the interval multiplier used at tile zooms up to (and including) maxZoom. A tile
         * carries every elevation that is a multiple of BaseInterval x multiplier, and each contour
         * carries 'div' so the style picks per camera zoom which of them to draw.
         *
         * The default table is (9, 50), (11, 10), (13, 5), (any, 1): 500m, 100m, 50m, 10m for a 10m base.
         *
         * TWO RULES when changing it:
         *  - the multipliers must NEST - each one a multiple of the finer ones - or a line stops
         *    dead at the border between tiles of different zoom (200 and 500 share no elevation);
         *  - cost tracks the number of contours emitted, so a multiplier twice as fine is about
         *    twice the tracing, the geometry and the draw. Make it no finer than what the style
         *    actually draws at the camera zoom where tiles of that zoom are used.
         * @param maxZoom The highest tile zoom this multiplier applies to, or -1 for every zoom above the other entries.
         * @param multiplier The multiplier of BaseInterval, >= 1.
         */
        void setIntervalMultiplier(int maxZoom, float multiplier);
        /**
         * Returns the interval multiplier that applies at the given tile zoom.
         * @param zoom The tile zoom.
         * @return The multiplier of BaseInterval.
         */
        float getIntervalMultiplier(int zoom) const;
        /**
         * Removes every interval multiplier entry, so BaseInterval is used at every zoom.
         */
        void clearIntervalMultipliers();

        /**
         * Returns the target grid resolution used for contour tracing.
         * @return The target grid resolution, 0 for the DEM's own. The default is 128.
         */
        int getResolution() const;
        /**
         * Sets the target grid resolution used for contour tracing. The DEM is subsampled so that
         * the traced grid is at most this many samples per side. Lower values produce coarser but
         * much cheaper geometry (fewer vertices to trace, simplify, upload and drape over terrain).
         * Over 3D TERRAIN use 0 (the DEM's own resolution): the surface is displaced by every texel
         * of the same tile, so a line traced on a subsampled grid follows a height field the ground
         * does not have and cuts through everything between its samples.
         * @param resolution The target grid resolution (clamped to at least 8), or 0 for the DEM's own.
         */
        void setResolution(int resolution);

        /**
         * Sets the tracing grid resolution used at tile zooms up to (and including) maxZoom,
         * overriding Resolution there. Tracing cost is roughly quadratic in this, and a low zoom
         * tile covers so much ground that a fine grid buys nothing, so this is the cheapest knob to
         * turn for zoomed-out frames - but a costly one for QUALITY: a tile is drawn at roughly the
         * same screen size whatever its zoom, so a grid that shrinks with zoom puts contour vertices
         * hundreds of metres apart and the lines read as straight chords. Empty by default for that
         * reason: Resolution applies at every zoom, and low zoom saves through the interval instead.
         * @param maxZoom The highest tile zoom this resolution applies to, or -1 for every zoom above the other entries.
         * @param resolution The target grid resolution (clamped to at least 8), or 0 for the DEM's own.
         */
        void setResolutionForZoom(int maxZoom, int resolution);
        /**
         * Returns the tracing grid resolution that applies at the given tile zoom.
         * @param zoom The tile zoom.
         * @return The target grid resolution, 0 for the DEM's own.
         */
        int getResolutionForZoom(int zoom) const;
        /**
         * Removes every per-zoom resolution entry, so Resolution is used at every zoom.
         */
        void clearResolutionsForZoom();

        /**
         * Returns the minimum zoom at which contour geometry is generated.
         * @return The minimum contour zoom. The default is 12.
         */
        int getMinVisibleZoom() const;
        /**
         * Sets the minimum zoom at which contour geometry is generated. Below this zoom loadTile returns
         * an empty (but valid) tile without fetching or tracing the DEM. Note: in CartoCSS 'zoom' means the
         * TILE zoom, so the style must also draw the desired zoom range for contours to actually appear.
         * @param zoom The minimum contour zoom.
         */
        void setMinVisibleZoom(int zoom);

        /**
         * Returns whether seamless tile edges are enabled.
         * @return True if seamless edges are enabled. The default is true.
         */
        bool isSeamlessEdgesEnabled() const;
        /**
         * Sets whether to generate seamless tile edges. When enabled, the east/north/north-east neighbour
         * DEM tiles are fetched so that a tile's east and north edges use the exact same elevation samples
         * as the adjacent tiles, removing the small gaps where contour lines meet at tile boundaries.
         * This costs up to three extra DEM tile fetches/decodes per tile (they are usually cached).
         * @param enabled True to enable seamless edges.
         */
        void setSeamlessEdgesEnabled(bool enabled);

        /**
         * Returns the terrain options whose elevation manager the label stubs read.
         * @return The terrain options, or null.
         */
        std::shared_ptr<TerrainOptions> getTerrainOptions() const;
        /**
         * Sets the terrain options whose ELEVATION MANAGER the label stubs are generated from.
         * With it, a stub tile costs no tile of its own: the seeds are walked over the elevation
         * grid the 3D terrain has already fetched and decoded for that tile, which is how tangram
         * generates contour labels (core/src/style/contourTextStyle.cpp reads the tile's own
         * elevation raster). Without it, the source loads and decodes the DEM tile a second time -
         * measured at 44% of a tile decode thread, half of it in the image decode alone.
         *
         * The terrain options must be driven by the SAME elevation data source this tile source
         * wraps, or the labels state heights the map does not show. Only the stubs use it; traced
         * contour geometry keeps reading the DEM at its own resolution, which the terrain's
         * mesh-capped elevation level cannot supply.
         * @param terrainOptions The terrain options, or null to decode a DEM tile of our own.
         */
        void setTerrainOptions(const std::shared_ptr<TerrainOptions>& terrainOptions);

        /**
         * Returns whether only short label stubs are generated instead of full contour lines.
         * @return True if label stubs are generated. The default is false.
         */
        bool isLabelStubsEnabled() const;
        /**
         * Sets whether to generate short label stubs instead of full contour lines. A stub is a
         * ~20 point polyline lying ON a contour, long enough to lay the elevation text along and
         * nothing more: a grid of 4x4 seeds per tile is walked down the elevation gradient onto the
         * nearest contour level and then along it. The tile then carries a handful of tiny features
         * instead of the full traced geometry, which is what makes the labels affordable when the
         * contour LINES are drawn by the terrain shader
         * (HillshadeRasterTileLayer.setContourEnabled) rather than from this geometry.
         *
         * The features keep the same layer name and the same 'ele'/'div' attributes, so the existing
         * text rules style them unchanged; they additionally carry 'stub' = 1, so a style that also
         * draws contour LINES from this layer can exclude them with a [stub=0] filter.
         *
         * The stub levels must match the levels the shader draws, or the labels sit between the
         * lines: set LabelInterval to the layer's ContourInterval (or leave both at their zoom
         * defaults).
         * @param enabled True to generate label stubs only.
         */
        void setLabelStubsEnabled(bool enabled);

        /**
         * Returns the contour interval used for label stubs.
         * @return The label interval in meters, or 0 to follow the zoom-dependent interval. The default is 0.
         */
        float getLabelInterval() const;
        /**
         * Sets the contour interval used for label stubs, in meters. Use 0 to follow the same
         * zoom-dependent interval the traced geometry uses.
         * @param interval The label interval in meters.
         */
        void setLabelInterval(float interval);

        /**
         * Returns the simplification tolerance in tile pixels.
         * @return The simplification tolerance in tile pixels. The default is 1.0.
         */
        float getSimplifyTolerance() const;
        /**
         * Sets the simplification tolerance in tile pixels. Use 0.0 to disable simplification.
         * @param tolerance The simplification tolerance in tile pixels.
         */
        void setSimplifyTolerance(float tolerance);

        virtual int getMinZoom() const;
        virtual int getMaxZoom() const;
        virtual MapBounds getDataExtent() const;
        virtual std::string getEncoding() const;

        virtual std::string getMetaData(const std::string& key) const;

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile);

    protected:
        class DataSourceListener : public TileDataSource::OnChangeListener {
        public:
            explicit DataSourceListener(ContourTileDataSource& dataSource);
            virtual void onTilesChanged(bool removeTiles);
        private:
            ContourTileDataSource& _dataSource;
        };

        std::shared_ptr<ElevationDecoder> resolveDecoder(const std::shared_ptr<TileData>& tileData) const;
        double getIntervalForZoom(int zoom) const;
        static long long computeDiv(long long ele);

        /**
         * Returns the decoded DEM bitmap of a tile, through a small MRU cache. With seamless edges
         * every tile also reads its east/north/north-east neighbours, and each of those is a tile
         * that is decoded for itself as well - the cache turns those 3 extra image decodes per
         * tile back into (mostly) one.
         */
        /**
         * The contour label stubs for a tile, walked over a height field given as tile-local uv:
         * height plus its gradient, in elevation units per unit of uv. Two things provide it - the
         * grid resampled from a DEM bitmap this source decoded itself, and the elevation grid the
         * 3D terrain already holds (see setTerrainOptions) - and the walk is the same either way.
         */
        std::shared_ptr<TileData> buildLabelStubTile(const MapTile& mapTile,
                                                     const std::function<double(double, double, double&, double&)>& sampler,
                                                     double interval);

        std::shared_ptr<Bitmap> loadCachedBitmap(const MapTile& tile);
        void cacheBitmap(const MapTile& tile, const std::shared_ptr<Bitmap>& bitmap);

        const DirectorPtr<TileDataSource> _dataSource;
        const std::shared_ptr<ElevationDecoder> _elevationDecoder;

        std::atomic<float> _baseInterval;
        std::atomic<float> _simplifyTolerance;
        std::atomic<int> _resolution;
        // (maxZoom, value) rungs, ascending; maxZoom -1 is the open-ended last rung. Guarded by _mutex.
        std::vector<std::pair<int, float> > _intervalMultipliers;
        std::vector<std::pair<int, int> > _zoomResolutions;
        std::atomic<int> _minVisibleZoom;
        std::atomic<bool> _seamlessEdges;
        std::atomic<bool> _labelStubs;
        std::atomic<float> _labelInterval;
        std::string _layerName;
        std::shared_ptr<TerrainOptions> _terrainOptions;
        mutable std::mutex _mutex;

        static const std::size_t MAX_CACHED_BITMAPS;
        std::vector<std::pair<long long, std::shared_ptr<Bitmap> > > _bitmapCache; // most recent first
        std::mutex _bitmapCacheMutex;

    private:
        std::shared_ptr<DataSourceListener> _dataSourceListener;
    };

}

#endif
