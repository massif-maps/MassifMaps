/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_COMPOSITEVECTORTILELAYER_H_
#define _MASSIF_COMPOSITEVECTORTILELAYER_H_

#include "layers/VectorTileLayer.h"

#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <mapnikvt/LayerConfigResolver.h> // ResolvedLayerConfig, held by value in the config cache

namespace massif {
    class TileDataSource;
    class VectorTileDecoder;
    class MBVectorTileDecoder;
    class ElevationDecoder;

    namespace CompositeSourceType {
        /**
         * The kind of an external data source added to a CompositeVectorTileLayer.
         */
        enum CompositeSourceType {
            /**
             * A raster tile source, drawn as a RasterTileLayer at its style slot.
             */
            COMPOSITE_SOURCE_TYPE_RASTER,
            /**
             * An RGB-encoded elevation source, drawn as a HillshadeRasterTileLayer at its style slot.
             */
            COMPOSITE_SOURCE_TYPE_HILLSHADE,
            /**
             * Another MBVT/protobuf source (including ContourTileDataSource), drawn at its style slot as its own
             * child VectorTileLayer with the master decoder, so it overzooms independently via its own MaxOverzoomLevel.
             */
            COMPOSITE_SOURCE_TYPE_VECTOR
        };
    }

    /**
     * A VectorTileLayer weaving named external data sources (raster, hillshade, vector / contour) into the master style's
     * layer order, each at its name's position in the style's "layers" array and configured by a matching '#name { ... }'
     * CartoCSS block (zoom- and parameter-dependent). Sources can be added and removed at runtime.
     */
    class CompositeVectorTileLayer : public VectorTileLayer {
    public:
        /**
         * Constructs a CompositeVectorTileLayer from a master vector data source and decoder.
         * @param dataSource The master vector tile data source.
         * @param decoder The tile decoder (must be an MBVectorTileDecoder for external source
         *                configuration and placement to work).
         */
        CompositeVectorTileLayer(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<VectorTileDecoder>& decoder);
        virtual ~CompositeVectorTileLayer();

        /**
         * Adds a named external data source, drawn as its own child layer at the style slot named 'name'.
         * @param name The source name; must match a layer name in the style "layers" array.
         * @param dataSource The external data source.
         * @param type The source type.
         * @param elevationDecoder Optional elevation decoder for hillshade sources. If null,
         *        it is resolved from the data source 'dem_encoding' metadata ("terrarium"/"mapbox").
         */
        void addExternalDataSource(const std::string& name, const std::shared_ptr<TileDataSource>& dataSource, CompositeSourceType::CompositeSourceType type, const std::shared_ptr<ElevationDecoder>& elevationDecoder = std::shared_ptr<ElevationDecoder>());
        /**
         * Adds a named MBVT/protobuf source (including ContourTileDataSource) styled by the master CartoCSS.
         * Equivalent to addExternalDataSource with COMPOSITE_SOURCE_TYPE_VECTOR.
         * @param name The source name; its layers must be declared in the master style.
         * @param dataSource The vector data source to merge.
         */
        void addVectorDataSource(const std::string& name, const std::shared_ptr<TileDataSource>& dataSource);
        /**
         * Removes the named external data source (of any type). Returns true if removed.
         * @param name The source name.
         * @return True if a source was removed.
         */
        bool removeExternalDataSource(const std::string& name);
        /**
         * Returns the names of all registered external data sources.
         * @return The registered external source names.
         */
        std::vector<std::string> getExternalDataSourceNames() const;
        /**
         * Returns the child layer a slot is drawn by (RasterTileLayer, HillshadeRasterTileLayer or VectorTileLayer), to reach
         * settings the config symbolizer does not carry, e.g. a NormalMapLightingShader, which the config pass never overwrites.
         * The child is owned by this layer - do not add it to a map.
         * @param name The source name.
         * @return The child layer, or null if no source is registered under that name.
         */
        std::shared_ptr<Layer> getExternalChildLayer(const std::string& name) const;

        /**
         * Sets the zoom level bias for this layer and for every child layer it owns (external
         * raster/hillshade/vector sources and the internal style-group layers). Sources with a
         * per-source bias set via setExternalDataSourceZoomLevelBias keep their own value.
         * @param bias The new bias value, both positive and negative fractional values are supported.
         */
        virtual void setZoomLevelBias(float bias);
        /**
         * Sets the preloading state for this layer and for every child layer it owns.
         * @param preloading The new preloading state of the layer.
         */
        virtual void setPreloading(bool preloading);
        /**
         * Sets the opacity for this layer and for every internal layer it owns (style groups and
         * depth-split vector slots). External children keep the opacity the app set on them.
         * @param opacity The opacity in range (0..1).
         */
        virtual void setOpacity(float opacity);

        /**
         * Sets the vector tile event listener for this layer and every internal style-group layer,
         * which otherwise report no clicks above the first external source slot.
         * @param eventListener The vector tile event listener.
         */
        virtual void setVectorTileEventListener(const std::shared_ptr<VectorTileEventListener>& eventListener);
        /**
         * Sets the click radius for this layer and for every internal style-group layer it owns.
         * @param radius The new click radius of vector tile features.
         */
        virtual void setClickRadius(float radius);
        /**
         * Sets the click handler layer filter for this layer and for every internal style-group
         * layer it owns.
         * @param filter The new click handler layer filter.
         * @throws std::runtime_error If the filter expression is not valid.
         */
        virtual void setClickHandlerLayerFilter(const std::string& filter);

        /**
         * Sets the zoom level bias of a single external data source, overriding the layer-wide value
         * (e.g. 1.0 on a high-resolution DEM gives the hillshade one zoom level more detail).
         * A 'zoom-level-bias' value in the style for this source wins over it.
         * @param name The source name.
         * @param bias The new bias value, both positive and negative fractional values are supported.
         * @throws std::invalid_argument If the source does not exist.
         */
        void setExternalDataSourceZoomLevelBias(const std::string& name, float bias);
        /**
         * Returns the effective zoom level bias of the given external data source.
         * @param name The source name.
         * @return The zoom level bias of the source.
         * @throws std::invalid_argument If the source does not exist.
         */
        float getExternalDataSourceZoomLevelBias(const std::string& name) const;
        /**
         * Clears the per-source zoom level bias, so the source follows the layer-wide value again.
         * @param name The source name.
         * @throws std::invalid_argument If the source does not exist.
         */
        void clearExternalDataSourceZoomLevelBias(const std::string& name);
        /**
         * Sets the maximum overzoom level of a single external data source: how many levels a coarser parent tile
         * may stand in when the source has no tile at the target zoom (e.g. a DEM above its own max zoom).
         * @param name The source name.
         * @param level The new maximum overzoom level.
         * @throws std::invalid_argument If the source does not exist.
         */
        void setExternalDataSourceMaxOverzoomLevel(const std::string& name, int level);
        /**
         * Returns the maximum overzoom level of the given external data source.
         * @param name The source name.
         * @return The maximum overzoom level of the source.
         * @throws std::invalid_argument If the source does not exist.
         */
        int getExternalDataSourceMaxOverzoomLevel(const std::string& name) const;

    protected:
        virtual void setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                   const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                   const std::weak_ptr<Options>& options,
                                   const std::weak_ptr<MapRenderer>& mapRenderer,
                                   const std::weak_ptr<TouchHandler>& touchHandler);

        virtual void loadData(const std::shared_ptr<CullState>& cullState);
        virtual void offsetLayerHorizontally(double offset);
        virtual bool isUpdateInProgress() const;
        virtual bool isTerrainDecodeSettled();
        virtual int getTerrainDecodePendingCount() const;
        virtual void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;

        virtual void collectDrapeLayers(std::vector<std::shared_ptr<TileLayer> >& drapeLayers, const ViewState& viewState);
        virtual void collectLabelLayers(std::vector<std::shared_ptr<VectorTileLayer> >& labelLayers);

        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);
        virtual bool onDrawFrame3D(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);

    private:
        struct ExternalSource {
            std::string name;
            CompositeSourceType::CompositeSourceType type;
            std::shared_ptr<TileDataSource> dataSource;
            std::shared_ptr<Layer> childLayer;
            // Per-source overrides; when unset the child follows the composite layer's own value.
            bool zoomLevelBiasSet = false;
            float zoomLevelBias = 0.0f;
            bool maxOverzoomLevelSet = false;
            int maxOverzoomLevel = 0;
        };

        // One draw step after group 0: an external child, or an internal layer for a later style-layer group.
        // The filter is applied at tile-build time, so each group needs its own layer.
        enum DrawItemKind { DRAW_ITEM_EXTERNAL, DRAW_ITEM_VT_GROUP };
        struct DrawItem {
            DrawItemKind kind;
            std::string slot;                  // DRAW_ITEM_EXTERNAL
            std::shared_ptr<Layer> groupLayer; // DRAW_ITEM_VT_GROUP; a Layer so protected virtuals are reachable via friend
            // DRAW_ITEM_EXTERNAL of a vector slot the style lists at several depths: this entry's styles only.
            std::shared_ptr<Layer> slotLayer;
        };

        static std::shared_ptr<ElevationDecoder> resolveElevationDecoder(const std::shared_ptr<TileDataSource>& dataSource);
        // includeBackground: also match the empty-named background layer (group 0 only).
        static std::string buildFilterString(const std::vector<std::string>& group, bool includeBackground = false);

        void wireChild(const std::shared_ptr<Layer>& child);
        void unwireChild(const std::shared_ptr<Layer>& child);
        std::shared_ptr<Layer> makeGroupLayer(const std::string& filter);
        std::shared_ptr<Layer> makeSlotLayer(const ExternalSource& source, const std::vector<std::string>& styleNames);
        void rebuildDrawItems();
        /** A slot's resolved config, memoised per (zoom, decoder version). Caller holds _sourceMutex. */
        mvt::ResolvedLayerConfig resolveLayerConfigCached(const std::shared_ptr<MBVectorTileDecoder>& decoder, const std::string& slot, float viewZoom);
        /** Caller holds _sourceMutex. */
        void snapshotChildTileLayers();
        void applyExternalChildZoomRange(const ExternalSource& source);
        // Caller holds _sourceMutex.
        void applyChildTileProperties(const ExternalSource& source);
        const ExternalSource* findExternalSource(const std::string& name) const;
        // Caller holds _sourceMutex.
        ExternalSource* findExternalSource(const std::string& name);
        // Throws if the source is unknown. Caller holds _sourceMutex.
        ExternalSource& getExternalSource(const std::string& name);
        const ExternalSource& getExternalSource(const std::string& name) const;
        // A source the style's 'layers' gives no slot is neither loaded nor draped.
        bool isDrawnSlot(const std::string& name) const;
        // Drawn by its own child, rather than by one slot layer per depth. Caller holds _sourceMutex.
        bool isDrawnByChild(const std::string& name) const;
        void applyConfig(const ExternalSource& source, const mvt::ResolvedLayerConfig& config, const ViewState& viewState);
        // Applies '#name' values to ContourTileDataSource generation parameters, off the render thread (loadData).
        // Only changed values are re-applied, to avoid reload loops.
        void applyVectorSourceConfigs();
        bool renderComposite(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState, bool terrain);

        std::vector<ExternalSource> _externalSources;
        std::vector<DrawItem> _drawItems;
        std::map<std::string, std::map<std::string, float> > _lastVectorConfig;
        std::map<std::string, std::map<std::string, double> > _lastChildConfig; // double: holds a 32-bit ARGB exactly

        // For wiring child layers added after setComponents().
        bool _componentsSet;
        std::weak_ptr<Options> _childOptions;
        std::weak_ptr<MapRenderer> _childMapRenderer;
        std::weak_ptr<TouchHandler> _childTouchHandler;

        mutable std::recursive_mutex _sourceMutex;

        // The children, readable without _sourceMutex. See snapshotChildTileLayers.
        mutable std::mutex _childTileLayersMutex;
        std::vector<std::shared_ptr<TileLayer> > _childTileLayers;

        struct ResolvedConfigEntry {
            unsigned int version;
            float viewZoom;
            mvt::ResolvedLayerConfig config;
        };
        std::map<std::string, ResolvedConfigEntry> _resolvedConfigCache; // see resolveLayerConfigCached
    };

}

#endif
