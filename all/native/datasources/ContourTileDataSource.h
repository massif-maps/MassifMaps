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
     * A tile data source generating vector contour lines on the fly from an RGB-encoded DEM data source, emitting one line
     * layer (default "contour") whose features carry 'ele' (meters) and 'div' (largest of 1000/500/250/200/100/50/20/10 dividing it).
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class ContourTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a ContourTileDataSource object.
         * @param dataSource The RGB-encoded elevation data source to generate contours from.
         * @param elevationDecoder The decoder for tiles whose 'dem_encoding' metadata names none. If null,
         *        such tiles decode as mapbox.
         */
        ContourTileDataSource(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        /**
         * Constructs a ContourTileDataSource object, inferring the elevation decoder from the
         * data source 'dem_encoding' metadata (defaults to mapbox).
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
         * Sets the base contour interval in meters: the finest interval generated, see setIntervalMultiplier.
         * @param interval The base contour interval in meters.
         */
        void setBaseInterval(float interval);

        /**
         * Sets the interval multiplier of BaseInterval used at tile zooms up to (and including) maxZoom.
         * The default table is (9, 50), (11, 10), (13, 5), (any, 1). Multipliers must nest (each a multiple of
         * the finer ones) or lines stop dead at borders between tiles of different zoom.
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
         * Sets the target grid resolution (max samples per side) used for contour tracing; lower is coarser but cheaper.
         * Over 3D terrain use 0: a line traced on a subsampled grid cuts through the displaced surface between samples.
         * @param resolution The target grid resolution (clamped to at least 8), or 0 for the DEM's own.
         */
        void setResolution(int resolution);

        /**
         * Sets the tracing grid resolution used at tile zooms up to (and including) maxZoom, overriding Resolution there.
         * Empty by default: a coarse grid at low zoom is cheap but turns contours into visible straight chords.
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
         * @return The minimum contour zoom. The default is 5.
         */
        int getMinVisibleZoom() const;
        /**
         * Sets the minimum zoom at which contour geometry is generated; below it loadTile returns an empty tile.
         * Note: in CartoCSS 'zoom' means the tile zoom, so the style must also draw that range.
         * @param zoom The minimum contour zoom.
         */
        void setMinVisibleZoom(int zoom);

        /**
         * Returns whether seamless tile edges are enabled.
         * @return True if seamless edges are enabled. The default is true.
         */
        bool isSeamlessEdgesEnabled() const;
        /**
         * Sets whether to generate seamless tile edges, fetching the east/north/north-east neighbour DEM tiles so
         * lines meet exactly at tile boundaries. Costs up to three extra DEM fetches/decodes per tile (usually cached).
         * @param enabled True to enable seamless edges.
         */
        void setSeamlessEdgesEnabled(bool enabled);

        /**
         * Returns the terrain options whose elevation manager the label stubs read.
         * @return The terrain options, or null.
         */
        std::shared_ptr<TerrainOptions> getTerrainOptions() const;
        /**
         * Sets the terrain options whose elevation manager the label stubs are walked over, so stubs reuse the
         * terrain's decoded grid instead of decoding the DEM again. Must be driven by the same elevation data
         * source this source wraps. Traced contour geometry still reads the DEM itself.
         * @param terrainOptions The terrain options, or null to decode a DEM tile of our own.
         */
        void setTerrainOptions(const std::shared_ptr<TerrainOptions>& terrainOptions);

        /**
         * Returns whether only short label stubs are generated instead of full contour lines.
         * @return True if label stubs are generated. The default is false.
         */
        bool isLabelStubsEnabled() const;
        /**
         * Sets whether to generate short label stubs (tagged 'stub' = 1) instead of full contour lines, for when the
         * lines are drawn by HillshadeRasterTileLayer.setContourEnabled. Set LabelInterval to that layer's
         * ContourInterval (or leave both at defaults) or the labels sit between the lines.
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
         * @return The simplification tolerance in tile pixels. The default is 1.5.
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

        virtual std::string getContainerMetaData(const std::string& key) const;

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

        // sampler(u, v, dhdu, dhdv) returns the height at tile-local uv and its gradient per unit of uv.
        std::shared_ptr<TileData> buildLabelStubTile(const MapTile& mapTile,
                                                     const std::function<double(double, double, double&, double&)>& sampler,
                                                     double interval);

        // MRU cache: with seamless edges each neighbour DEM tile is also decoded for itself.
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
