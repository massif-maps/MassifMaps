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

namespace massif {
    class TileDataSource;
    class VectorTileDecoder;
    class ElevationDecoder;
    namespace mvt { struct ResolvedLayerConfig; }

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
             * Another MBVT/protobuf source (including ContourTileDataSource), drawn at its style
             * slot as its own child VectorTileLayer using the master decoder, filtered to its own
             * layer name. Kept separate (not merged) so it overzooms independently via its own
             * MaxOverzoomLevel and does not need the DEM at the target zoom.
             */
            COMPOSITE_SOURCE_TYPE_VECTOR
        };
    }

    /**
     * A VectorTileLayer that can weave named external data sources (raster, hillshade,
     * merged vector / contour) into the master CartoCSS style's layer order.
     *
     * Each external source is placed at the position of a matching layer name in the style
     * project's "layers" array, and configured by a matching '#name { ... }' block in the
     * CartoCSS (e.g. raster-opacity, hillshade-exaggeration), including zoom- and style-parameter-
     * parameter-dependent expressions. Raster and hillshade sources are rendered as their
     * own draped child layers interleaved between the master style layers; merged vector
     * sources are folded into the master source and styled normally.
     *
     * Sources can be added and removed at runtime.
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
         * Adds a named external data source. For raster and hillshade types the source is
         * drawn as its own child layer at the style slot named 'name'. For the vector type
         * the source is merged into the master source (see addVectorDataSource).
         * @param name The source name; must match a layer name in the style "layers" array.
         * @param dataSource The external data source.
         * @param type The source type.
         * @param elevationDecoder Optional elevation decoder for hillshade sources. If null,
         *        it is resolved from the data source 'encoding' metadata ("terrarium"/"mapbox").
         */
        void addExternalDataSource(const std::string& name, const std::shared_ptr<TileDataSource>& dataSource, CompositeSourceType::CompositeSourceType type, const std::shared_ptr<ElevationDecoder>& elevationDecoder = std::shared_ptr<ElevationDecoder>());
        /**
         * Adds a named MBVT/protobuf source (including ContourTileDataSource) merged into the
         * master source and styled by the master CartoCSS. Equivalent to addExternalDataSource
         * with COMPOSITE_SOURCE_TYPE_VECTOR.
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
         * Returns whether single-pass segmented rendering is enabled (Milestone 6, optional).
         * @return True if single-pass rendering is enabled. The default is false.
         */
        bool isSinglePassRenderingEnabled() const;
        /**
         * Sets whether to use the optional single-pass segmented renderer instead of the
         * default one-vt-pass-per-segment path. Intended for A/B comparison; currently a
         * no-op placeholder until the single-pass renderer lands.
         * @param enabled True to enable single-pass rendering.
         */
        void setSinglePassRenderingEnabled(bool enabled);

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
         * Sets the zoom level bias of a single external data source, overriding the layer-wide value.
         * Use this to fetch a source at a different resolution from the base map - e.g. a bias of 1.0
         * on a high-resolution DEM source makes the hillshade use one zoom level more detail.
         * Note that if the style defines a 'zoom-level-bias' value for this source, the style value
         * wins - the same precedence the other per-source config values have.
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
         * Sets the maximum overzoom level of a single external data source. Overzooming reuses a
         * coarser parent tile when the source has no tile at the target zoom level, which is how a
         * low-resolution DEM keeps covering the map above its own max zoom.
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
            std::shared_ptr<Layer> childLayer; // raster/hillshade child; null for merged vector
            // Per-source overrides. When unset the child follows the composite layer's own value.
            bool zoomLevelBiasSet = false;
            float zoomLevelBias = 0.0f;
            bool maxOverzoomLevelSet = false;
            int maxOverzoomLevel = 0;
        };

        // One ordered draw step after the layer's own group-0 render: an external raster/hillshade
        // child, or an internal VectorTileLayer for a later style-layer group with a fixed filter.
        // The filter is applied at tile-build time, so each group needs its own layer.
        enum DrawItemKind { DRAW_ITEM_EXTERNAL, DRAW_ITEM_VT_GROUP };
        struct DrawItem {
            DrawItemKind kind;
            std::string slot;                  // external source name (DRAW_ITEM_EXTERNAL)
            std::shared_ptr<Layer> groupLayer; // internal group layer (DRAW_ITEM_VT_GROUP); held as
                                               // Layer so protected virtuals are reachable via friend
        };

        static std::shared_ptr<ElevationDecoder> resolveElevationDecoder(const std::shared_ptr<TileDataSource>& dataSource);
        // includeBackground: also match the empty-named per-tile background layer (only the bottom
        // group 0 should, so the style Map background-color is drawn once at the bottom).
        static std::string buildFilterString(const std::vector<std::string>& group, bool includeBackground = false);

        void wireChild(const std::shared_ptr<Layer>& child);
        void unwireChild(const std::shared_ptr<Layer>& child);
        std::shared_ptr<Layer> makeGroupLayer(const std::string& filter);
        void rebuildDrawItems();
        void applyExternalChildZoomRange(const ExternalSource& source);
        // Pushes the tile-selection properties (zoom level bias, max overzoom level, preloading)
        // down to a child layer, honouring the source's per-source overrides. Caller holds _sourceMutex.
        void applyChildTileProperties(const ExternalSource& source);
        const ExternalSource* findExternalSource(const std::string& name) const;
        // Non-const variant, for the per-source property setters. Caller holds _sourceMutex.
        ExternalSource* findExternalSource(const std::string& name);
        // Same, but throws if the source is unknown. Caller holds _sourceMutex.
        ExternalSource& getExternalSource(const std::string& name);
        const ExternalSource& getExternalSource(const std::string& name) const;
        // Whether the style's 'layers' gives this source a slot, i.e. whether anything would ever
        // draw it. A source without one is not loaded and not draped.
        bool isDrawnSlot(const std::string& name) const;
        void applyConfig(const ExternalSource& source, const mvt::ResolvedLayerConfig& config, const ViewState& viewState);
        // Applies '#name' config symbolizer values to merged vector sources whose generation
        // parameters live on the data source (currently ContourTileDataSource). Called off the
        // render thread (from loadData); only re-applies changed values to avoid reload loops.
        void applyVectorSourceConfigs();
        bool renderComposite(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState, bool terrain);

        std::vector<ExternalSource> _externalSources;
        std::vector<DrawItem> _drawItems;
        std::map<std::string, std::map<std::string, float> > _lastVectorConfig; // per-source applied contour params
        std::map<std::string, std::map<std::string, double> > _lastChildConfig; // per-source last-applied config values (double: holds a 32-bit ARGB exactly)
        bool _singlePassRenderingEnabled;

        // Cached component handles for wiring child layers added after setComponents().
        bool _componentsSet;
        std::weak_ptr<Options> _childOptions;
        std::weak_ptr<MapRenderer> _childMapRenderer;
        std::weak_ptr<TouchHandler> _childTouchHandler;

        mutable std::recursive_mutex _sourceMutex;
    };

}

#endif
