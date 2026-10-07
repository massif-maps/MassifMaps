/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINOPTIONS_H_
#define _MASSIF_TERRAINOPTIONS_H_

#include "core/MapPos.h"
#include "graphics/Color.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace massif {
    class TileDataSource;
    class ElevationDecoder;
    class ElevationManager;

    namespace TerrainFlattenMode {
        /**
         * How far a flattened terrain goes back towards a plain 2D map.
         */
        enum TerrainFlattenMode {
            /**
             * Rendering only: the terrain passes, the drape and the elevation fetches are dropped, but the
             * tiles keep their terrain subdivision. Switching is instant, but a flat map carries 3D triangles.
             */
            TERRAIN_FLATTEN_MODE_RENDER,
            /**
             * The whole way: a flat map decodes, culls and draws as if no terrain were configured.
             * The price is a re-decode at a switch, paid while the map is already flat; switching back at
             * the same view reuses the last decode, kept in memory.
             */
            TERRAIN_FLATTEN_MODE_FULL
        };
    }

    /**
     * 3D terrain configuration, attached to the map via Options::setTerrainOptions. The elevation data source
     * can be shared with a HillshadeRasterTileLayer (wrap it in a MemoryCacheTileDataSource to avoid duplicate loads).
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class TerrainOptions {
    public:
        /**
         * Interface for monitoring terrain option change events. Internal.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }

            /**
             * Listener method that gets called when a terrain option has changed.
             * @param optionName The name of the option that has changed.
             */
            virtual void onTerrainOptionChanged(const std::string& optionName) = 0;
        };

        /**
         * Constructs a TerrainOptions object from an elevation data source.
         * The elevation decoder is resolved from the data source "encoding" setting
         * ("mapbox" or "terrarium"), defaulting to the MapBox encoding.
         * @param dataSource The data source with RGB-encoded elevation tiles.
         */
        explicit TerrainOptions(const std::shared_ptr<TileDataSource>& dataSource);
        /**
         * Constructs a TerrainOptions object from an elevation data source and an explicit decoder.
         * @param dataSource The data source with RGB-encoded elevation tiles.
         * @param elevationDecoder The decoder for the elevation tile encoding.
         */
        TerrainOptions(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        virtual ~TerrainOptions();

        /**
         * Returns the elevation data source.
         * @return The elevation data source.
         */
        std::shared_ptr<TileDataSource> getDataSource() const;
        /**
         * Returns the elevation decoder used as the source-level default. Each tile resolves its
         * own decoder from its "dem_encoding" meta data, so two data sources of different
         * encodings can be combined behind one OrderedTileDataSource.
         * @return The default elevation decoder.
         */
        std::shared_ptr<ElevationDecoder> getElevationDecoder() const;

        /**
         * Returns the enabled state of the terrain.
         * @return True if 3D terrain rendering is enabled. The default is true.
         */
        bool isEnabled() const;
        /**
         * Sets the enabled state of the terrain. If disabled, the map renders flat,
         * but the elevation data source stays attached.
         * @param enabled The new enabled state.
         */
        void setEnabled(bool enabled);

        /**
         * Returns whether the map is asked to render flat. This is the 2D/3D state, whether it was
         * set by the app or by auto-flattening; the switch itself is animated, so for a moment after
         * a change the map is still on its way there.
         * @return True if the map is flat, or on its way to flat. The default is false.
         */
        bool isFlattened() const;
        /**
         * Switches the map between flat and 3D terrain without detaching the elevation data (unlike setEnabled).
         * Auto-flattening writes the same state, so an app driving this normally sets both AutoFlatten thresholds to 0.
         * An app that starts in 2D sets this before it adds its layers, so nothing decodes for 3D.
         * @param flattened True to render flat.
         */
        void setFlattened(bool flattened);

        /**
         * Returns how far a flattened terrain goes back towards a plain 2D map.
         * @return The flatten mode. The default is TERRAIN_FLATTEN_MODE_RENDER.
         */
        TerrainFlattenMode::TerrainFlattenMode getFlattenMode() const;
        /**
         * Sets how far a flattened terrain goes back towards a plain 2D map: RENDER only stops the terrain passes;
         * FULL also drops the 3D subdivision, re-decoding the visible tiles at each switch (invisibly, while flat).
         * Going back to 3D waits for the tiles it needs before it starts to rise.
         * @param mode The new flatten mode.
         */
        void setFlattenMode(TerrainFlattenMode::TerrainFlattenMode mode);

        /**
         * Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat).
         * @return The flatten ratio.
         */
        float getFlattenRatio() const;
        /**
         * Drives the 2D/3D switch by hand (0 full 3D, 1 flat), e.g. from a camera flight's own progress. Auto-flattening
         * stays suspended until setFlattened hands the ratio back, so call it once at the end. Below 1 the ground is
         * held flat until the 3D tiles arrive; wait on isSwitching() before starting the animation.
         * @param ratio The new flatten ratio, 0 to 1.
         */
        void setFlattenRatio(float ratio);

        /**
         * Returns whether the switch is holding the ground flat while the tiles 3D needs load.
         * @return True while the switch is waiting for tiles.
         */
        bool isSwitching() const;

        /**
         * Returns the screen parallax below which the terrain renders flat.
         * @return The parallax in screen pixels. The default is 2. 0 never flattens.
         */
        float getAutoFlattenParallax() const;
        /**
         * Sets the parallax in screen pixels (halfScreenDiagonal * heightRange * exaggeration / cameraDistance)
         * below which the map renders flat; it writes the same state setFlattened does.
         * Restores at 1.5x this value, so a camera sitting on the boundary does not oscillate.
         * @param pixels The new parallax threshold in screen pixels, or 0 to never flatten. The default is 2.
         */
        void setAutoFlattenParallax(float pixels);

        /**
         * Returns the tilt at or above which the terrain renders flat.
         * @return The tilt in degrees. The default is 88. 0 never flattens.
         */
        float getAutoFlattenTilt() const;
        /**
         * Sets the tilt, in degrees, at or above which the map renders flat whatever the parallax.
         * 90 is straight down in this SDK, where the displacement is there but shows nothing worth
         * its cost. Restores 2 degrees below the threshold, so a tilt gesture does not oscillate.
         * @param tilt The new tilt threshold in degrees, or 0 to never flatten. The default is 88.
         */
        void setAutoFlattenTilt(float tilt);

        /**
         * Returns how long the terrain takes to sink flat.
         * @return The duration in seconds. The default is 0.3.
         */
        float getAutoFlattenDuration() const;
        /**
         * Sets how long the terrain takes to sink flat, and to rise unless setAutoFlattenRiseDuration overrides it.
         * The ramp scales heights on the GPU (no re-decode); the wait for the tiles 3D needs is not included.
         * @param duration The new duration in seconds. 0 switches instantly.
         */
        void setAutoFlattenDuration(float duration);

        /**
         * Returns how long the terrain takes to rise back into 3D.
         * @return The duration in seconds, or a negative value to follow getAutoFlattenDuration.
         */
        float getAutoFlattenRiseDuration() const;
        /**
         * Sets how long the terrain takes to rise, separately from sinking. To match a camera flight
         * exactly, drive setFlattenRatio instead: a duration is a second timer.
         * @param duration The new duration in seconds, or a negative value to use setAutoFlattenDuration.
         */
        void setAutoFlattenRiseDuration(float duration);

        /**
         * Returns the terrain height exaggeration factor.
         * @return The exaggeration factor. The default is 1.0.
         */
        float getExaggeration() const;
        /**
         * Sets the terrain height exaggeration factor. 1.0 means true-to-scale heights.
         * Note: changing the exaggeration triggers a re-tesselation of loaded tiles, which is a relatively expensive operation.
         * @param exaggeration The new exaggeration factor.
         */
        void setExaggeration(float exaggeration);

        /**
         * Returns whether seamless tile edge handling is enabled.
         * @return True if elevation textures take their border texels from neighbouring DEM tiles at any level. The default is true.
         */
        bool isSeamlessTileEdgesEnabled() const;
        /**
         * Enables or disables seamless tile edges: each elevation texture's 1-texel border is taken from the
         * neighbouring DEM tiles (coarser ones sampled), so adjacent tiles agree on edge height. No IO, a little CPU.
         * Disable if the elevation tiles already match exactly across tile borders.
         * @param enabled True to fill elevation texture borders from neighbouring tiles.
         */
        void setSeamlessTileEdgesEnabled(bool enabled);

        /**
         * Returns whether elevation tile prefetching is enabled.
         * @return True if visible tiles and their neighbours are requested from the elevation data source. The default is true.
         */
        bool isElevationPrefetchEnabled() const;
        /**
         * Enables or disables elevation prefetching: each visible terrain tile requests its own elevation tile and the
         * 8 around it, so neighbours share a DEM level; off, cached map tiles stay on coarser ancestor data.
         * Costs requests, decoding and cache pressure; disable for minimal traffic or a fully local tileset.
         * @param enabled True to prefetch elevation tiles for visible tiles and their neighbours.
         */
        void setElevationPrefetchEnabled(bool enabled);

        /**
         * Returns the terrain mesh resolution.
         * @return The maximum number of grid cells per tile edge used for terrain geometry. The default is 64.
         */
        int getMeshResolution() const;
        /**
         * Sets the terrain mesh resolution. Higher values give more detailed terrain
         * at the cost of memory and CPU. The effective resolution is also limited by
         * the resolution of the elevation tiles.
         * @param meshResolution The new mesh resolution (clamped to 2..1024).
         */
        void setMeshResolution(int meshResolution);

        /**
         * Returns the distance geo-three's terrain LOD subdivides at.
         * @return Mercator metres at zoom 20, or 0 while the SDK's own rule is in use. The default is 0.
         */
        float getSubdivideDistance() const;
        /**
         * Cuts and meshes the terrain the way geo-three's webapp does, so the two render the same picture:
         * a tile at zoom z subdivides while the camera is closer than distance * 2^(20 - z) Mercator
         * metres (geo-three's desktop value is 70), with no tile budget and bilinear DEM heights.
         * @param distance Mercator metres at zoom 20, or 0 for the SDK's own rule.
         */
        void setSubdivideDistance(float distance);

        /**
         * Returns the resolution the elevation node field is built at.
         * @return Node field cells per tile edge, or 0 to follow MeshResolution. The default is 0.
         */
        int getSurfaceNodeResolution() const;
        /**
         * Sets the resolution of the height field the terrain is displaced from, per DEM tile edge,
         * independently of the mesh drawn over it. Overzoomed, this field limits the relief, not the mesh.
         * Costs memory per cached DEM grid, not frames. The default 0 follows MeshResolution.
         * @param resolution Node cells per tile edge (clamped to 2..512), or 0 to follow MeshResolution.
         */
        void setSurfaceNodeResolution(int resolution);

        /**
         * Returns the downscale factor of the packed depth/normal texture post-process effects read.
         * @return The divisor applied to the screen size for that buffer. The default is 2.
         */
        int getPostProcessDownscale() const;
        /**
         * Sets the downscale factor of the packed depth/normal texture that post-process effects read.
         * The default 2 suits shading; an effect that differentiates the normals (ridge lines) shows
         * blocky texels, which 1 removes at four times the fill. The occlusion read-back is unaffected.
         * @param downscale The new downscale factor (clamped to 1..4).
         */
        void setPostProcessDownscale(int downscale);

        /**
         * Returns whether cross-LOD tile edge stitching is enabled.
         * @return True if grid surface edges follow a coarser neighbour's lattice. The default is true.
         */
        bool isTileEdgeStitchingEnabled() const;
        /**
         * Enables or disables cross-LOD edge stitching: the finer tile chords across a coarser neighbour's grid
         * nodes, closing the crack between them. Needs an even MeshResolution and GPU draping mode;
         * costs one uniform per tile.
         * @param enabled True to snap grid surface edges to a coarser neighbour's grid.
         */
        void setTileEdgeStitchingEnabled(bool enabled);

        /**
         * Returns how many terrain surface meshes may be cached.
         * @return The cache size in meshes, or 0 for the built-in rule. The default is 0.
         */
        int getMeshCacheSize() const;
        /**
         * Sets how many terrain surface meshes may be cached; the visible cut holds half as many tiles.
         * The default 0 keeps the built-in 160/80, a map's working set. Raise it for a panorama, whose
         * wider cut otherwise rebuilds meshes while turning and keeps dropping a zoom level.
         * @param meshes The number of meshes to cache, or 0 for the built-in rule.
         */
        void setMeshCacheSize(int meshes);

        /**
         * Returns whether the shared ground pass draws the terrain a second time.
         * @return True if the ground is drawn under the layers. The default is true.
         */
        bool isSharedGroundEnabled() const;
        /**
         * Enables or disables the flat shared-ground draw when DrapeFillsEnabled is false. The default is true.
         * Disable it only when a surface shader plus labels/billboards is all the map draws; fills and
         * lines need that ground to composite onto.
         * @param enabled True to draw the shared ground, false to leave the surface shader's output.
         */
        void setSharedGroundEnabled(bool enabled);

        /**
         * Returns whether polygon fills are draped as a render-to-texture surface.
         * @return True if fills are baked to a per-tile texture and sampled on the surface. The default is true.
         */
        bool isDrapeFillsEnabled() const;
        /**
         * Enables or disables maplibre-style fill draping (experimental): fills render flat into a per-tile texture
         * sampled by the terrain surface, so they follow it exactly at 2D fill cost. Only native (non-overzoomed)
         * fills drape; lines need DrapeLinesEnabled. Requires GPU draping mode (vertex texture fetch, planar).
         * @param enabled True to drape fills as a texture, false to draw them as geometry.
         */
        void setDrapeFillsEnabled(bool enabled);

        /**
         * Returns whether vt tile lines are also draped (in addition to fills).
         * @return True if tile lines are baked into the drape texture. The default is true.
         */
        bool isDrapeLinesEnabled() const;
        /**
         * Enables or disables draping of tile lines too (needs DrapeFillsEnabled): they follow the terrain exactly
         * at no per-frame geometry cost, but resolve at the drape resolution. Layers matching NoDrapeLayerFilter
         * stay sharp. See docs/internals/rendering/04-terrain.md.
         * @param enabled True to drape tile lines too, false to keep them as sharp geometry.
         */
        void setDrapeLinesEnabled(bool enabled);

        /**
         * Returns whether bridges and tunnels stand on their own chord (3D bridges).
         * @return True if a `span` feature is lifted onto the chord between its portals and a
         * span deck is drawn as an extrusion. The default is false.
         */
        bool isBridges3DEnabled() const;
        /**
         * Enables or disables 3D bridges: a `line-elevation-mode: span` feature (or polygon/building variant) is laid
         * straight between its portals and a span deck stands as an extrusion; off, spans drape and no span work runs.
         * Needs terrain. See docs/internals/rendering/04-terrain.md, "Bridges and tunnels: spans".
         * @param enabled True to lift spans onto their chord, false to drape them.
         */
        void setBridges3DEnabled(bool enabled);

        /**
         * Returns the style layers that are kept out of the terrain drape bake.
         * @return A regular expression matched against vt style layer names. The default is
         *         "^contour|maneuver.*"; an empty string drapes everything the geometry type allows.
         */
        std::string getNoDrapeLayerFilter() const;
        /**
         * Sets which style layers are not baked into the drape texture, as a regular expression over the vt layer
         * name; they are drawn live at screen resolution, still sun and shadow shaded. Meant for hairlines such as contours.
         * @param filter The regular expression, or an empty string to drape everything.
         */
        void setNoDrapeLayerFilter(const std::string& filter);

        /**
         * Returns the per-tile drape texture resolution, 0 when it follows the screen.
         * @return The drape texture resolution in pixels, 0 for automatic.
         */
        int getDrapeResolution() const;
        /**
         * Sets the per-tile drape texture resolution, trading thin-content sharpness against video memory
         * (resolution^2 * 4 bytes per visible tile). 0 (the default) follows the screen: 2 * tileDrawSize * pixelScale,
         * one texel per pixel at the tile LOD bound, halved until DrapeWorkingSet tiles fit DrapeCacheSize.
         * @param resolution The new drape texture resolution, clamped to [128, 2048], or 0 to follow the screen.
         */
        void setDrapeResolution(int resolution);

        /**
         * Returns the minimum tile zoom level with 3D terrain.
         * @return The minimum zoom level. The default is 5.
         */
        int getMinZoom() const;
        /**
         * Sets the minimum tile zoom level with 3D terrain. Tiles below this zoom level render flat
         * and do not fetch elevation data. Terrain displacement is invisible at low zoom levels anyway,
         * so this limits the number of elevation tiles fetched and processed for far-away/zoomed-out views.
         * @param minZoom The new minimum zoom level (clamped to 0..24).
         */
        void setMinZoom(int minZoom);

        /**
         * Returns the maximum tile zoom level the terrain mesh is cut at.
         * @return The maximum zoom level, or 0 while the cut is unbounded.
         */
        int getMaxZoom() const;
        /**
         * Caps the tile zoom the terrain mesh is cut at and the elevation tiles selected to feed it,
         * so the ground settles once instead of refining under labels and camera (a panorama). The
         * default 0 leaves both to the distance rule, the tile budget and the source maximum.
         * @param maxZoom The new maximum zoom level (clamped to 0..24), or 0 for no cap.
         */
        void setMaxZoom(int maxZoom);

        /**
         * Returns the factor applied to the view distance.
         * @return The view distance factor. The default is 1, which is exactly tangram's rule.
         */
        float getViewDistanceFactor() const;
        /**
         * Sets the view distance and far plane as a factor on tangram's rule (far = 2 * cameraHeight / cos(pitch + fovy/2),
         * capped at worldTileSize(zoom) * 127); smaller ends the view closer (pair with fog), larger costs depth precision.
         * A style may pin an absolute distance instead, in meters, with "terrain-max-visible-distance".
         * @param factor The new view distance factor. The default is 1.
         */
        void setViewDistanceFactor(float factor);

        /**
         * Returns the minimum view distance, in meters.
         * @return The view distance in meters. 0 (the default) leaves the factor rule alone.
         */
        float getViewDistance() const;
        /**
         * Sets a minimum distance the map is drawn to, in meters, at any camera height or tilt, for views along the
         * ground. It only ever extends the factor rule and spends depth precision, so pair it with fog.
         * 0 (the default) leaves the factor rule alone.
         * @param distance The new minimum view distance in meters, or 0 for the factor rule alone.
         */
        void setViewDistance(float distance);

        /**
         * Returns the maximum view distance, in meters.
         * @return The view distance ceiling in meters. 0 (the default) is no ceiling.
         */
        float getViewDistanceMax() const;
        /**
         * Sets a maximum distance the map is drawn to, in meters, capping culling, the tile walk and
         * the far plane; it also caps setViewDistance. Independent of the fog, which a summit may
         * stand above. 0 (the default) is no ceiling.
         * @param distance The new maximum view distance in meters, or 0 for no ceiling.
         */
        void setViewDistanceMax(float distance);

        /**
         * Returns the drape cache budget in megabytes.
         * @return The drape cache budget in megabytes, or 0 for the built-in 192. The default is 0.
         */
        int getDrapeCacheSize() const;
        /**
         * Sets how much video memory the cached drape textures may take, in megabytes. It must hold the live cover
         * and the generation it replaced, or the ground blinks during a fast zoom. It also bounds the automatic
         * drape resolution (see DrapeResolution and DrapeWorkingSet).
         * @param megabytes The new budget in megabytes, or 0 for the default of 192.
         */
        void setDrapeCacheSize(int megabytes);

        /**
         * Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule.
         * @return The elevation cache budget in megabytes. The default is 0.
         */
        int getElevationCacheSize() const;
        /**
         * Sets how much memory the decoded elevation grids may take, in megabytes (a 512-texel grid is
         * ~1.8 MB). The default 0 keeps 192 grids, a map's working set; raise it for a wide view, where a
         * full cache refetches every eviction and keeps the decode threads busy.
         * @param megabytes The new budget in megabytes, or 0 for the grid-count rule.
         */
        void setElevationCacheSize(int megabytes);

        /**
         * Returns how many drape tiles the automatic resolution assumes are cached at once.
         * @return The assumed working set in tiles, or 0 for the built-in 64. The default is 0.
         */
        int getDrapeWorkingSet() const;
        /**
         * Sets how many drape tiles the automatic resolution (DrapeResolution 0) assumes must fit DrapeCacheSize at
         * once (live cover plus the next generation). Lower is sharper but thrashes below a real cover's 15-34 leaves.
         * @param tiles The assumed working set in tiles, or 0 for the built-in 64.
         */
        void setDrapeWorkingSet(int tiles);

        /**
         * Returns how many zoom levels below the camera a tile may coarsen to.
         * @return The maximum tile zoom coarsening. The default is 3.
         */
        int getMaxTileZoomCoarsening() const;
        /**
         * Sets how far below the camera's zoom the tile LOD may take a tile in terrain mode. The surface is the depth
         * occluder and its DEM level follows the tile, so coarse tiles chop ridge crests (content shows through) and
         * shade blocky; larger values mean fewer tiles at a tilt. 0 pins every tile to the camera's own zoom.
         * @param levels The new maximum tile zoom coarsening. The default is 3.
         */
        void setMaxTileZoomCoarsening(int levels);

        /**
         * Returns the terrain background color.
         * @return The terrain background color. The default is transparent (no background).
         */
        Color getBackgroundColor() const;
        /**
         * Sets the terrain background color: an opaque base fill under all layers that keeps the terrain shape
         * (and its depth, for occlusion) where no layer paints. Transparent (the default) disables the fill.
         * @param color The new terrain background color.
         */
        void setBackgroundColor(const Color& color);

        /**
         * Returns the terrain background bitmap state.
         * @return True if the map background bitmap is draped over the terrain as the base fill. The default is false.
         */
        bool isBackgroundBitmapEnabled() const;
        /**
         * Sets the terrain background bitmap state. When enabled, the map background bitmap (Options::getBackgroundBitmap)
         * is draped as the base fill under all layers instead of the background color.
         * @param enabled The new background bitmap state.
         */
        void setBackgroundBitmapEnabled(bool enabled);

        /**
         * Returns the custom terrain surface fragment shader source, or an empty string if
         * no shaded surface is drawn.
         * @return The custom surface shader source.
         */
        std::string getSurfaceShaderSource() const;
        /**
         * Sets a fragment shader defining "vec4 surfaceColor()" (non-premultiplied, not fogged) that paints the terrain
         * surface as the base fill, replacing the background bitmap/color. On compile error it is dropped and logged.
         * Varyings, uniforms and parameters: docs/internals/rendering/04-terrain.md, "The surface shader".
         * @param shaderSource The GLSL source, or an empty string for no shaded surface.
         */
        void setSurfaceShaderSource(const std::string& shaderSource);

        /**
         * Returns the value of a terrain surface shader float parameter.
         * @param name The name of the parameter.
         * @return The value of the parameter, or 0 if not set.
         */
        float getSurfaceParameter(const std::string& name) const;
        /**
         * Sets a terrain surface shader float parameter, exposed to the shader as a uniform.
         * @param name The name of the parameter (must be a valid GLSL identifier).
         * @param value The new value for the parameter.
         */
        void setSurfaceParameter(const std::string& name, float value);

        /**
         * Returns the value of a terrain surface shader color parameter.
         * @param name The name of the parameter.
         * @return The value of the parameter, or transparent black if not set.
         */
        Color getSurfaceColorParameter(const std::string& name) const;
        /**
         * Sets a terrain surface shader color parameter, exposed to the shader as a vec4
         * uniform with components in the 0..1 range.
         * @param name The name of the parameter (must be a valid GLSL identifier).
         * @param color The new value for the parameter.
         */
        void setSurfaceColorParameter(const std::string& name, const Color& color);

        /**
         * Returns all terrain surface shader float parameters. Internal method.
         * @return The map of all float parameters.
         */
        std::map<std::string, float> getSurfaceParameters() const;
        /**
         * Returns all terrain surface shader color parameters. Internal method.
         * @return The map of all color parameters.
         */
        std::map<std::string, Color> getSurfaceColorParameters() const;

        /**
         * Returns the maximum visible tile zoom offset, relative to the camera zoom level.
         * @return The maximum tile zoom offset. The default is 100 (no cap).
         */
        int getMaxTileZoomOffset() const;
        /**
         * Sets the maximum visible tile zoom offset, relative to the camera zoom level. Distance-based terrain LOD shows
         * near tiles finer than flat rendering would, exposing zoom-dependent styling as hard-edged rings; 0 caps at the
         * flat level, positive values allow that many extra levels. Values of 100 or more disable the cap.
         * @param offset The new maximum tile zoom offset (values >= 100 disable the cap).
         */
        void setMaxTileZoomOffset(int offset);

        /**
         * Returns the camera terrain clearance floor: an explicit minimum height the camera
         * is kept above the terrain surface, in meters.
         * @return The camera clearance floor in meters. The default is 0.
         */
        float getCameraClearance() const;
        /**
         * Sets the camera terrain clearance floor, in meters, added under the zoom-relative rule (as in mapbox, a share of
         * the camera's altitude, see CameraClearanceFraction). A zoom in stops at the clearance; a camera pushed under it
         * by a pan or arriving elevation is lifted at a constant zoom, by reducing the tilt.
         * @param clearance The new clearance floor in meters. 0 (the default) applies the zoom-relative rule alone.
         */
        void setCameraClearance(float clearance);

        /**
         * Returns the share of the camera's altitude that the terrain clearance takes.
         * @return The clearance fraction. The default is 1/16.
         */
        float getCameraClearanceFraction() const;
        /**
         * Sets the share of the camera's altitude the clearance takes (default 1/16).
         * 0 leaves CameraClearance alone as a fixed height above the ground, as a first-person view needs.
         * @param fraction The new fraction, clamped to [0, 1). 1/16 is the default.
         */
        void setCameraClearanceFraction(float fraction);

        /**
         * Returns the height the viewpoint is lifted above the ground-following focus, in meters.
         * @return The focus lift in meters. The default is 0.
         */
        float getFocusLift() const;
        /**
         * Sets the height the viewpoint is lifted above the ground under it, in meters, added to the
         * ground-following rule (a focus z is recomputed every frame). For a first-person viewpoint
         * standing above the ridge in front of it.
         * @param lift The new lift in meters. 0 (the default) leaves the focus on the ground.
         */
        void setFocusLift(float lift);

        /**
         * Returns the duration of the camera terrain-following correction animation.
         * @return The correction duration in seconds. The default is 0 (instant correction).
         */
        float getCameraClampDuration() const;
        /**
         * Sets the duration of the camera terrain-following correction animation.
         * @param duration The new duration in seconds. 0 applies corrections instantly.
         */
        void setCameraClampDuration(float duration);

        /**
         * Returns the clip-space depth bias used when depth-testing draped 2D geometry against the terrain.
         * @return The depth bias. The default is 0.0002.
         */
        float getDepthBias() const;
        /**
         * Sets the clip-space depth bias used when depth-testing draped 2D geometry against the terrain.
         * Larger values prevent draped layers from being clipped by the terrain surface itself,
         * at the cost of geometry slightly behind terrain ridges 'shining through' near silhouettes.
         * @param depthBias The new depth bias (clamped to 0..0.01).
         */
        void setDepthBias(float depthBias);

        /**
         * Returns the billboard/label terrain occlusion tolerance.
         * @return The relative depth tolerance. The default is 0.2.
         */
        float getBillboardOcclusionTolerance() const;
        /**
         * Sets how far behind the terrain a billboard or label anchor may sit and still count as visible, as a fraction
         * of its distance from the camera. The default is 0.2; 0 hides a label the moment its anchor goes behind the
         * relief; larger values let a summit just behind a nearer ridge keep its name.
         * @param tolerance The new relative tolerance (clamped to 0..1).
         */
        void setBillboardOcclusionTolerance(float tolerance);

        /**
         * Returns the ground distance the surface normals are measured over, in meters.
         * @return The normal sample distance, or 0 while the mesh's own spacing is used.
         */
        float getNormalSampleDistance() const;
        /**
         * Sets the ground distance the per-vertex surface normals are measured over, in meters. The default
         * 0 uses the mesh spacing, which differs across LOD boundaries and draws them as lines in a normal-
         * differentiating effect; 60-150 m reads ridges. Planar surfaces only.
         * @param distance The new sample distance in meters, or 0 for the mesh's own spacing.
         */
        void setNormalSampleDistance(float distance);

        /**
         * Returns the opacity a label keeps while its anchor is behind 3D content.
         * @return The opacity of an occluded label. The default is 1, i.e. no occlusion.
         */
        float getTextOcclusionOpacity() const;
        /**
         * Sets the opacity a label keeps while its anchor is hidden by buildings (terrain occludes regardless), faded
         * per label by how much of a small square around the anchor is covered. 1, the default, disables it; below 1
         * costs one extra pass over visible extrusions. The style's 'text-occlusion-opacity' wins where it sets one.
         * @param opacity The opacity of an occluded label (clamped to 0..1).
         */
        void setTextOcclusionOpacity(float opacity);

        /**
         * Returns the billboard/label terrain occlusion state.
         * @return True if billboards and labels hidden behind terrain are faded out. The default is true.
         */
        bool isBillboardOcclusionEnabled() const;
        /**
         * Sets the billboard/label terrain occlusion state.
         * @param enabled The new occlusion state.
         */
        void setBillboardOcclusionEnabled(bool enabled);

        /**
         * Returns the capacity of the decoded elevation tile cache in bytes.
         * @return The cache capacity in bytes. The default is 64MB, grown to hold 192 grids unless set explicitly.
         */
        std::size_t getElevationCacheCapacity() const;
        /**
         * Sets the capacity of the decoded elevation tile cache in bytes.
         * @param capacityInBytes The new cache capacity in bytes.
         */
        void setElevationCacheCapacity(std::size_t capacityInBytes);

        /**
         * Returns the terrain elevation in meters at the given position.
         * The position is expected to be in WGS84 coordinates.
         * Note: this method may block on network/IO if the elevation tile is not cached.
         * @param pos The position to query.
         * @return The elevation in meters, or -1000000 if no elevation data is available.
         */
        double getElevation(const MapPos& pos) const;
        /**
         * Returns terrain elevations in meters at the given positions (WGS84).
         * One value is returned for every input position, in the input order.
         * Note: this method may block on network/IO if the elevation tiles are not cached.
         * @param poses The positions to query.
         * @return The elevations in meters (-1000000 where no data is available).
         */
        std::vector<double> getElevations(const std::vector<MapPos>& poses) const;

        /**
         * Returns whether 3D terrain is being rendered right now: enabled by the app AND not
         * flattened away. Every renderer and culler asks this; what the TILES were decoded for is
         * isDecodeActive(), which lags this by a switch. Internal method.
         * @return True if the terrain is rendering in 3D.
         */
        bool isActive() const;

        /**
         * Returns whether tiles are being decoded for 3D terrain - subdivided, so that displacing
         * them follows the ground. Only ever changed while the map is flat, where both densities
         * draw the same picture. In RENDER mode this is isEnabled(). Internal method.
         * @return True if tiles carry the terrain subdivision.
         */
        bool isDecodeActive() const;
        /**
         * Sets whether tiles are decoded for 3D terrain. Driven by the renderer's 2D/3D switch, and
         * only while the map is flat. Internal method.
         * @param active True to decode tiles with the terrain subdivision.
         */
        void setDecodeActive(bool active);

        /**
         * Applies the switch's own ratio, 0 to 1, scaling the heights handed out without touching the app's exaggeration.
         * Does not notify listeners or clear the manual flag: the renderer drives it per frame. Internal method.
         * @param ratio The new flatten ratio.
         */
        void applyFlattenRatio(float ratio);

        /**
         * Returns whether the app is driving the ratio itself (setFlattenRatio), which suspends
         * both the switch's animation and auto-flattening. Internal method.
         * @return True if the app owns the ratio.
         */
        bool isManualFlatten() const;
        /**
         * Returns the ratio the app last asked for with setFlattenRatio. Internal method.
         * @return The requested flatten ratio.
         */
        float getManualFlattenRatio() const;
        /**
         * Records whether the switch is holding the ground flat while tiles load, for isSwitching().
         * Internal method.
         * @param switching True while the switch is waiting for tiles.
         */
        void setSwitching(bool switching);

        /**
         * Returns the elevation manager. Internal method.
         * @return The elevation manager.
         */
        std::shared_ptr<ElevationManager> getElevationManager() const;

        /**
         * Registers listener for terrain option change events. Internal method.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        /**
         * Unregisters listener from terrain option change events. Internal method.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

    private:
        void notifyOptionChanged(const std::string& optionName);
        void writeFlattenRatio(float ratio);
        void markSwitchingIfRising(float askedRatio);

        const std::shared_ptr<TileDataSource> _dataSource;
        const std::shared_ptr<ElevationManager> _elevationManager;

        std::atomic<bool> _enabled;
        // The app's own exaggeration. What the elevation manager holds is this scaled by the flatten
        // ramp, so flattening never overwrites what the app asked for.
        std::atomic<float> _exaggeration;
        std::atomic<bool> _flattened;
        std::atomic<TerrainFlattenMode::TerrainFlattenMode> _flattenMode;
        std::atomic<bool> _decodeActive;
        // Whether the renderer's 2D/3D switch has taken over; until then setFlattened is the whole state.
        std::atomic<bool> _flattenSwitchStarted;
        std::atomic<bool> _flattenManual;
        std::atomic<float> _flattenManualRatio;
        std::atomic<bool> _switching;
        std::atomic<float> _flattenRatio;
        std::atomic<float> _autoFlattenParallax;
        std::atomic<float> _autoFlattenTilt;
        std::atomic<float> _autoFlattenDuration;
        std::atomic<float> _autoFlattenRiseDuration;
        std::atomic<int> _meshResolution;
        std::atomic<float> _subdivideDistance;
        std::atomic<int> _surfaceNodeResolution;
        std::atomic<int> _postProcessDownscale;
        std::atomic<bool> _tileEdgeStitchingEnabled;
        std::atomic<int> _meshCacheSize;
        std::atomic<bool> _sharedGroundEnabled;
        std::atomic<bool> _drapeFillsEnabled;
        std::atomic<bool> _drapeLinesEnabled;
        std::atomic<bool> _bridges3DEnabled;
        std::atomic<int> _drapeResolution;
        std::atomic<int> _minZoom;
        std::atomic<int> _maxZoom;
        std::atomic<int> _maxTileZoomOffset;
        std::atomic<int> _backgroundColorARGB;
        std::atomic<bool> _backgroundBitmapEnabled;
        std::atomic<float> _depthBias;
        std::atomic<float> _cameraClearance;
        std::atomic<float> _cameraClearanceFraction;
        std::atomic<float> _focusLift;
        std::atomic<float> _cameraClampDuration;
        std::atomic<bool> _billboardOcclusionEnabled;
        std::atomic<float> _billboardOcclusionTolerance;
        std::atomic<float> _normalSampleDistance;
        std::atomic<float> _textOcclusionOpacity;
        std::atomic<float> _viewDistanceFactor;
        std::atomic<float> _viewDistance;
        std::atomic<float> _viewDistanceMax;
        std::atomic<int> _drapeCacheSize;
        std::atomic<int> _elevationCacheSize;
        std::atomic<int> _drapeWorkingSet;
        std::atomic<int> _maxTileZoomCoarsening;

        // Hairlines (contours) smear in the drape, where a slope magnifies the texture.
        static const std::string DEFAULT_NO_DRAPE_LAYER_FILTER;

        std::string _noDrapeLayerFilter;
        mutable std::mutex _noDrapeMutex;

        std::string _surfaceShaderSource;
        std::map<std::string, float> _surfaceParameters;
        std::map<std::string, Color> _surfaceColorParameters;
        mutable std::mutex _surfaceMutex;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;
    };
}

#endif
