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
             * Rendering only: the terrain passes, the drape and the elevation fetches are dropped,
             * but the tiles keep the terrain subdivision they were decoded with. Switching costs
             * nothing and is instant, and a flat map still carries a 3D map's triangles.
             */
            TERRAIN_FLATTEN_MODE_RENDER,
            /**
             * The whole way: a flat map decodes, culls and draws as if no terrain were configured.
             * The price is a re-decode at each switch, paid while the map is already flat.
             */
            TERRAIN_FLATTEN_MODE_FULL
        };
    }

    /**
     * 3D terrain configuration, attached to the map via Options::setTerrainOptions.
     * The elevation data source can be shared with a HillshadeRasterTileLayer, in which case
     * both features use the same tiles (ideally the data source should be wrapped in a
     * MemoryCacheTileDataSource to avoid duplicate loads).
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
         * Switches the map between flat and 3D terrain, without detaching the elevation data the way
         * setEnabled does. Auto-flattening writes the same state, so an app driving this itself
         * normally turns auto off (setAutoFlattenParallax(0) and setAutoFlattenTilt(0)). What the
         * switch costs, and whether a flat map goes on paying for 3D, is setFlattenMode.
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
         * Sets how far a flattened terrain goes back towards a plain 2D map. RENDER is the cheap
         * switch: the terrain passes stop, but the tiles keep the subdivision 3D needed, so a flat
         * map still draws a 3D map's triangles. FULL drops that too - a flat map decodes, culls and
         * draws as if no terrain were configured - at the price of re-decoding the visible tiles at
         * every switch.
         *
         * That re-decode is not visible: it is made while the map is already flat, where the two
         * densities draw the same picture, and the tiles being replaced stay on screen until their
         * replacement arrives. Going back to 3D waits for the tiles it needs before it starts to
         * rise, so the wait shows as 3D arriving late rather than as a half-built map.
         * @param mode The new flatten mode.
         */
        void setFlattenMode(TerrainFlattenMode::TerrainFlattenMode mode);

        /**
         * Returns how far the terrain is flattened right now, 0 (full 3D) to 1 (flat).
         * @return The flatten ratio.
         */
        float getFlattenRatio() const;
        /**
         * Drives the 2D/3D switch by hand, off the app's own clock: 0 is full 3D, 1 is flat. Writing
         * this takes the ratio away from setFlattened's animation, which is what an app does to make
         * the terrain match a camera flight EXACTLY - feed it the flight's own progress rather than
         * hope two timers agree. Auto-flattening is suspended while the app drives, and STAYS
         * suspended until setFlattened hands the ratio back - so an app that drives an animation
         * writes setFlattened once at the end of it, or a later tilt gesture does nothing.
         *
         * Rising is still gated on the tiles 3D needs: a ratio below 1 asks for them and the ground
         * is HELD flat until they arrive, because unsubdivided geometry displaced over relief is a
         * road in the sky. isSwitching() is that hold - wait on it before starting the animation.
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
         * Sets the terrain parallax, in SCREEN PIXELS, below which 3D stops being worth its cost and
         * the map renders flat. The parallax is how far the highest ground in view moves on screen
         * because it is displaced:
         *
         *     parallax = halfScreenDiagonal * heightRange * exaggeration / cameraDistance
         *
         * so it falls with the camera's height and rises with how mountainous the data is - a fixed
         * zoom threshold is wrong for one of the two. The rule writes the same state setFlattened
         * does, without touching setEnabled; how far flattening then goes is setFlattenMode.
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
         * Sets how long the terrain takes to sink flat, and - unless setAutoFlattenRiseDuration
         * overrides it - to rise again. The ramp scales the heights on the GPU, so it costs no tile
         * re-decode; only when it reaches flat are the terrain passes themselves dropped, by which
         * point the two render identically. Does not cover the wait for the tiles 3D needs (see
         * setFlattenMode) - that is not the animation.
         * @param duration The new duration in seconds. 0 switches instantly.
         */
        void setAutoFlattenDuration(float duration);

        /**
         * Returns how long the terrain takes to rise back into 3D.
         * @return The duration in seconds, or a negative value to follow getAutoFlattenDuration.
         */
        float getAutoFlattenRiseDuration() const;
        /**
         * Sets how long the terrain takes to RISE, separately from how long it takes to sink. The
         * two are rarely worth the same: the rise is the one an app matches to a camera flight, and
         * the one that waited for its tiles first. For an exact match to a flight, drive
         * setFlattenRatio instead - a duration is a second timer, not the same clock.
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
         * Enables or disables seamless tile edge handling. When enabled, the 1-texel border of
         * every elevation texture is taken from the neighbouring elevation tiles - same-level
         * neighbours texel-exactly, coarser (ancestor) neighbours by sampling their height field.
         * Adjacent terrain tiles then agree on the height along their shared edge instead of
         * showing a ridge of up to one DEM texel of relief. Costs no IO, only a small amount of
         * CPU when an elevation texture is built. Disable if the elevation tiles already match
         * exactly across tile borders.
         * @param enabled True to fill elevation texture borders from neighbouring tiles.
         */
        void setSeamlessTileEdgesEnabled(bool enabled);

        /**
         * Returns whether elevation tile prefetching is enabled.
         * @return True if visible tiles and their neighbours are requested from the elevation data source. The default is true.
         */
        bool isElevationPrefetchEnabled() const;
        /**
         * Enables or disables elevation tile prefetching. When enabled, every visible terrain tile
         * asynchronously requests its own elevation tile and the 8 surrounding ones, so neighbouring
         * terrain tiles are displaced by the same DEM level and border texels have real neighbour
         * data. When disabled, elevation tiles are only loaded as a side effect of map tile fetches,
         * which leaves cached map tiles (and the tiles around the viewport) on coarser ancestor
         * elevation data. This is the costly option: it adds elevation tile requests, decoding and
         * cache pressure. Disable to keep elevation traffic at a minimum, or if the elevation
         * tileset is fully local.
         * @param enabled True to prefetch elevation tiles for visible tiles and their neighbours.
         */
        void setElevationPrefetchEnabled(bool enabled);

        /**
         * Returns the terrain mesh resolution.
         * @return The maximum number of grid cells per tile edge used for terrain geometry. The default is 64.
         *         The effective detail is capped by the ELEVATION SOURCE's zoom as well: a z12 DEM
         *         has no more to give a 512 grid than a 256 one.
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
         * Cuts and meshes the terrain the way geo-three's webapp does (LODFrustum and
         * MaterialHeightShader.getGeometry), so the two can render the same picture:
         *
         *  - a tile at zoom z is subdivided while the camera is closer to its centre than
         *    distance * 2^(20 - z) Mercator metres - geo-three's desktop value is 70 - with no tile
         *    budget. setMaxZoom is its maximum level: the DEM's maximum plus its overzoom of 2.
         *  - a tile's mesh is MeshResolution cells per edge up to zoom 12, halved per level above
         *    it and never under 16, without the usual cap of 96.
         *  - the heights are read bilinearly off the DEM, not averaged over a box of cells.
         * @param distance Mercator metres at zoom 20, or 0 for the SDK's own rule.
         */
        void setSubdivideDistance(float distance);

        /**
         * Returns the resolution the elevation node field is built at.
         * @return Node field cells per tile edge, or 0 to follow MeshResolution. The default is 0.
         */
        int getSurfaceNodeResolution() const;
        /**
         * Sets the resolution of the height field the terrain is displaced from, independently of
         * the mesh drawn over it.
         *
         * These are two different things and only one of them carries detail. The vertex stage does
         * not sample the DEM: it samples the NODE FIELD, the DEM box-filtered to this many cells per
         * tile edge (ElevationNodeField), sized per DEM tile. The mesh is a lattice per RENDER tile,
         * and once the camera is overzoomed past the elevation source's maximum zoom the render tile
         * covers a fraction of a DEM tile - at z16 on a z12 source, 32 texels - so even a 96-cell
         * lattice is finer than the data and the node field is what the relief is limited by.
         *
         * Tied together, asking for detail meant paying for a lattice that was never the constraint:
         * 256 cells per render tile is 7x the vertices of 96 for relief that comes from the field.
         * Measured on a Crosscall, mesh 256 panned visibly slower than 96 at the same picture.
         *
         * The cost of this one is memory, not frames: the field is (resolution x tiles-per-edge + 1)^2
         * texels per cached DEM grid, so 256 over a 512-texel source is ~790 KB a grid against
         * ~150 KB at 96. 0 follows MeshResolution, which is what every caller got before.
         * @param resolution Node cells per tile edge (clamped to 2..512), or 0 to follow MeshResolution.
         */
        void setSurfaceNodeResolution(int resolution);

        /**
         * Returns the downscale factor of the packed depth/normal texture post-process effects read.
         * @return The divisor applied to the screen size for that buffer. The default is 2.
         */
        int getPostProcessDownscale() const;
        /**
         * Sets the downscale factor of the packed depth/normal texture that post-process effects
         * read (PostProcessEffect::setTerrainDepthRequired / setTerrainNormalsRequired).
         *
         * 2 - the default - is a map's setting: the buffer is a quarter of the pixels, and an effect
         * that SHADES it cannot tell. An effect that DIFFERENTIATES it can: a peak finder drawing
         * ridge lines from the normals magnifies that buffer's texels at close range into visible
         * blocks, stair-stepped crests and comb streaking down steep faces. 1 removes them, at four
         * times the fill for a pass already measured at 9.5 ms of a 19.3 ms frame on an Adreno 610 -
         * so it is worth it for a panorama and not for a map.
         *
         * The occlusion read-back keeps its own half-resolution buffer either way: it samples depth
         * at points, and a full-resolution glReadPixels is a stall, not a detail.
         * @param downscale The new downscale factor (clamped to 1..4).
         */
        void setPostProcessDownscale(int downscale);

        /**
         * Returns whether cross-LOD tile edge stitching is enabled.
         * @return True if grid surface edges follow a coarser neighbour's lattice. The default is true.
         */
        bool isTileEdgeStitchingEnabled() const;
        /**
         * Enables or disables cross-LOD tile edge stitching. Neighbouring terrain tiles at
         * different zoom levels interpolate the elevation between differently spaced grid
         * vertices along their shared edge, which opens a thin crack. When enabled, the finer
         * tile chords across the coarser neighbour's grid nodes on that edge, so both tiles
         * describe the same edge. Needs an even MeshResolution, and only takes effect in GPU
         * draping mode. Costs one uniform per tile - no extra geometry.
         * @param enabled True to snap grid surface edges to a coarser neighbour's grid.
         */
        void setTileEdgeStitchingEnabled(bool enabled);

        /**
         * Returns whether polygon fills are draped as a render-to-texture surface.
         * @return True if fills are baked to a per-tile texture and sampled on the surface. The default is false.
         */
        /**
         * Returns how many terrain surface meshes may be cached.
         * @return The cache size in meshes, or 0 for the built-in rule. The default is 0.
         */
        int getMeshCacheSize() const;
        /**
         * Sets how many terrain surface meshes TerrainRenderer may keep, and with it how many tiles
         * the visible cut may hold (half this, since one frame walks the same cut two to four times
         * at different mesh resolutions).
         *
         * 0 keeps the built-in 160/80, which is a map's working set. A PANORAMA's is several times
         * that: the cut reaches a hundred kilometres, and looking around changes each tile's LOD
         * stitching mask, which is part of the mesh cache key - so panning mints new keys faster than
         * the cache holds them. Measured on an Adreno 610 at 160: `RenderStats terrainMesh`
         * builds=72 evictions=72 every second while panning, each rebuild re-baking the surface
         * normals for 4225 vertices, for prelude spikes of 200-290 ms.
         *
         * Raising it also steadies the LOD: the cut coarsens a whole zoom level whenever it overflows
         * its half of this, so a small cache means the panorama keeps dropping and regaining detail.
         * @param meshes The number of meshes to cache, or 0 for the built-in rule.
         */
        void setMeshCacheSize(int meshes);

        /**
         * Returns whether the shared ground pass draws the terrain a second time.
         * @return True if the ground is drawn under the layers. The default is true.
         */
        bool isSharedGroundEnabled() const;
        /**
         * Enables or disables the shared-ground draw in the no-drape (DrapeFillsEnabled false)
         * arrangement.
         *
         * That path normally draws the terrain cover once in a flat colour before any layer, so the
         * layers have a ground to composite onto. A SURFACE SHADER already painted the terrain, with
         * depth, in the same frame - so when the only thing on the map is a surface shader plus
         * label/billboard layers (the peak finder), the ground pass draws the whole mesh a second
         * time for nothing. It measured 10.6 ms of a 22 ms panorama frame on an Adreno 610, the
         * single largest item, with 94 flat fills per frame and zero of them carrying content.
         *
         * Leave it TRUE for any map with fills or lines: without a ground they composite onto
         * whatever the surface shader left, which is not the same picture.
         * @param enabled True to draw the shared ground, false to leave the surface shader's output.
         */
        void setSharedGroundEnabled(bool enabled);

        bool isDrapeFillsEnabled() const;
        /**
         * Enables or disables maplibre-style render-to-texture fill draping (experimental, spike). When
         * enabled, polygon fills are rendered FLAT into a per-tile offscreen texture and then sampled as
         * the color of the terrain surface mesh, instead of being drawn as displaced geometry. Because the
         * fills become the surface's texture they follow the terrain exactly - no chord sag, so no holes,
         * no see-through, and no depth slack - at flat-render (2D) fill cost. Lines/contours and labels are
         * unaffected (still drawn as sharp geometry on top). Only native (non-overzoomed) fills are draped.
         * Requires GPU draping mode (vertex texture fetch, planar projection).
         * @param enabled True to drape fills as a texture, false to draw them as geometry.
         */
        void setDrapeFillsEnabled(bool enabled);

        /**
         * Returns whether vt tile lines are also draped (in addition to fills).
         * @return True if tile lines are baked into the drape texture. The default is true.
         */
        bool isDrapeLinesEnabled() const;
        /**
         * Enables or disables draping of vt tile lines in addition to fills (needs DrapeFillsEnabled).
         * Draped lines are baked into the per-tile texture: they follow the terrain exactly and cost
         * no per-frame geometry (a city pan runs at twice the frame rate), but they resolve at the
         * drape resolution rather than the screen's. Layers matching NoDrapeLayerFilter stay sharp
         * either way. See docs/internals/rendering/04-terrain.md.
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
         * Enables or disables 3D bridges: a feature styled `line-elevation-mode: span` (or the
         * polygon/building variants) is laid straight between its two portals instead of draped
         * over the terrain, and a span deck stands as an extrusion carrying its road. Off, every
         * such feature drapes like the ground and none of the span machinery runs - no chord
         * resolution, no deck drape bakes, no reference tile fetches. Needs terrain.
         * See docs/internals/rendering/04-terrain.md, "Bridges and tunnels: spans".
         * @param enabled True to lift spans onto their chord, false to drape them.
         */
        void setBridges3DEnabled(bool enabled);

        /**
         * Returns the style layers that are kept out of the terrain drape bake.
         * @return A regular expression matched against vt style layer names. The default is
         *         "^contour.*"; an empty string drapes everything the geometry type allows.
         */
        std::string getNoDrapeLayerFilter() const;
        /**
         * Sets which style layers must NOT be baked into the drape texture, as a regular expression
         * over the vt layer name (which comes from the style's own rule names). They are drawn live
         * in the 3D pass at screen resolution instead. Hairline content is what the drape resolution
         * costs, hence contours by default. They still take the terrain's sun and shadow, so they
         * shade like the ground they lie on.
         * @param filter The regular expression, or an empty string to drape everything.
         */
        void setNoDrapeLayerFilter(const std::string& filter);

        /**
         * Returns the per-tile drape texture resolution, 0 when it follows the screen.
         * @return The drape texture resolution in pixels, 0 for automatic.
         */
        int getDrapeResolution() const;
        /**
         * Sets the per-tile drape texture resolution. Draped content is rasterized into a texture
         * of this size and resampled onto the terrain surface, so this trades sharpness of thin
         * content (lines, outlines) against video memory: cost is resolution^2 * 4 bytes per
         * visible tile. maplibre uses twice the tile size (1024 for 512px tiles) for this reason.
         * 0 (the default) takes it from the SCREEN instead: the tile LOD refines a tile until it
         * covers at most a 2x2 block of nominal tiles, so 2 * tileDrawSize * pixelScale texels is
         * one texel per screen pixel at that bound - a fixed resolution is either coarser than the
         * screen (draped fill edges stair-step as you zoom in) or finer than it can show.
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
         * Caps the tile zoom the terrain mesh is cut at, AND which elevation tiles are SELECTED to
         * feed it (ElevationManager::setMaxDataZoomCap). 0, the default, leaves both to the
         * distance-based rule, the tile budget and the data source maximum.
         *
         * Selection only. It deliberately does not reach getDetailZoomLimit (the terrain LOD floor)
         * or getMaxDataZoom (the projection surface's subdivision): both decide TESSELLATION, and
         * capping them drove the tile surfaces to 3654 draws in one frame.
         *
         * The terrain LOD is distance based, so a camera near the ground subdivides what is in front
         * of it as deep as the data allows, and keeps subdividing as finer elevation tiles arrive.
         * Three things follow from that, and all three are worse in a view along the ground than in
         * a map looked down at:
         *
         *  - the SURFACE keeps changing height under everything anchored to it. Labels are
         *    re-anchored as their ground moves (vt::Label::updateElevation), so a placement that
         *    settled is re-decided, and a camera held above the terrain is dragged with it - the
         *    whole view rises and sinks while tiles stream in.
         *  - neighbouring tiles resolve to DIFFERENT elevation grids while that is happening, since
         *    each takes the finest one resident for it. Their shared edge is then built from two
         *    height fields that disagree.
         *  - it is unbounded work: the cut grows until the budget stops it.
         *
         * Pinning the zoom makes the height field settle once and stay settled, which is what a
         * panorama wants - peakfinder.com resolves its DEM for the viewpoint once and never refines
         * it. The cost is detail near the camera, which a view reaching a hundred kilometres can
         * mostly afford.
         *
         * MEASURED: pinning the mesh cut alone is not enough. With the cut pinned, RenderStats
         * showed tileRecalc 0 and a label set that stopped growing - and the elevation grid cache
         * still sat at capacity with 2-6 reinserts a second, forever, because the DATA zoom is
         * capped by the source rather than by the cut. Each reload bumps the elevation version, so
         * the labels kept re-anchoring and the ground kept moving. The cap has to reach the data,
         * which is why it does.
         * @param maxZoom The new maximum zoom level (clamped to 0..24), or 0 for no cap.
         */
        void setMaxZoom(int maxZoom);

        /**
         * Returns the factor applied to the view distance.
         * @return The view distance factor. The default is 1, which is exactly tangram's rule.
         */
        float getViewDistanceFactor() const;
        /**
         * Sets how far from the camera the map is drawn and where the far plane sits, as a factor
         * on tangram's own rule (core/src/view/view.cpp):
         *     far = 2 * cameraHeight / cos(pitch + fovy/2), capped by
         *     maxTileDistance = worldTileSize(zoom) * (2^(MAX_LOD+1) - 1), with MAX_LOD 6.
         * A factor of 1 is that rule verbatim; smaller ends the view closer, larger extends it.
         * This is what makes a near-horizontal view affordable: taken from the visible ground
         * instead, the view reaches the horizon - hundreds of tiles, most of them a few pixels
         * tall, each carrying its own labels. Pair a small factor with fog so the ground fades
         * out instead of ending.
         * It also decides the depth budget: tangram's model is calibrated on a far/near ratio of a
         * few hundred, and a deeper far spends the NDC precision the per-layer depth separation
         * needs.
         * A style may pin an absolute distance instead, in meters, with
         * "terrain-max-visible-distance".
         * @param factor The new view distance factor. The default is 1.
         */
        void setViewDistanceFactor(float factor);

        /**
         * Returns the minimum view distance, in meters.
         * @return The view distance in meters. 0 (the default) leaves the factor rule alone.
         */
        float getViewDistance() const;
        /**
         * Sets a MINIMUM distance the map is drawn to, in METERS, whatever the camera's height or
         * pitch. Tangram's rule is proportional to the camera's height above the ground, so
         * approaching the terrain shortens the view - which is right for a map seen from above and
         * wrong for a view along the ground, where the same landscape should stay visible as the
         * camera descends into it. An absolute distance keeps the ground reaching at least this far
         * at any elevation and any tilt. The far plane follows it, which spends depth precision
         * (see setViewDistanceFactor), so this is an explicit trade - pair it with fog so the
         * ground fades out instead of ending.
         * It only ever EXTENDS the factor rule: metres are zoom-independent while the rule scales
         * with the camera's height, so a distance that reaches the horizon up close would end the
         * ground in a disc well inside a zoomed-out screen.
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
         * Sets a MAXIMUM distance the map is drawn to, in METERS - the counterpart of
         * setViewDistance, and the only metric way to make the map reach LESS far than the factor
         * rule. The rule is proportional to the camera's height over the cosine of the angle to the
         * horizon, so a view along the ground reaches tens of kilometres from a hillside and past a
         * hundred from a summit: ground that is fetched, meshed, draped and drawn to be a few pixels
         * of haze. A ceiling caps the cull envelope (CullWorker), the tile walk (TileLayer) and the
         * far plane together, so it is a real saving rather than clipped work.
         * NOT derived from the fog, on purpose. The fog's range says where the ground has gone
         * white, and a summit standing above the haze is both further than that and the whole point
         * of FogOptions' vertical range - so the two are set independently and this one is the app's
         * own trade between how far the view reaches and what it costs.
         * Applied after setViewDistance, which it therefore also caps: asking for at least 150 km
         * and at most 30 gives 30.
         * 0 (the default) is no ceiling, leaving the factor rule and the tile walk cap to bound the
         * view as before.
         * @param distance The new maximum view distance in meters, or 0 for no ceiling.
         */
        void setViewDistanceMax(float distance);

        /**
         * Returns the drape cache budget in megabytes.
         * @return The drape cache budget in megabytes. The default is 96.
         */
        int getDrapeCacheSize() const;
        /**
         * Sets how much video memory the cached drape textures may take, in megabytes. The cache
         * has to hold the LIVE cover AND the generation it just replaced: a leaf whose own bake has
         * not landed stands in on the cached tiles under it, so a budget that fits only one cover
         * evicts the previous generation on every frame of a zoom and those leaves are painted in
         * the flat background colour instead - the ground blinking during a fast zoom.
         * It also decides the automatic drape resolution (see DrapeResolution and DrapeWorkingSet),
         * since the two have to agree.
         * @param megabytes The new budget in megabytes, or 0 for the default of 96.
         */
        void setDrapeCacheSize(int megabytes);

        /**
         * Returns the elevation grid cache budget in megabytes, 0 for the SDK's own rule.
         * @return The elevation cache budget in megabytes. The default is 0.
         */
        int getElevationCacheSize() const;
        /**
         * Sets how much memory the decoded elevation GRIDS may take, in megabytes.
         *
         * The default rule sizes this by grid COUNT (ElevationManager's MIN_CACHED_GRIDS, 192), which
         * assumes a map's working set: the ground around one viewpoint at one zoom. A panorama breaks
         * that assumption - it sees a hundred kilometres at once, so its working set is several times
         * 192 grids, the cache sits permanently full, and every grid evicted is immediately asked for
         * again. That costs far more than memory: each ElevationManager runs PREFETCH_THREADS (3)
         * decoding threads, and a cache that never holds its working set keeps all of them busy for
         * as long as the mode is open. Device-measured on a Crosscall: six such threads burned
         * ~34,600 CPU ticks against the render thread's 3,094, so the mode was starved of CPU while
         * the renderer itself was idle.
         *
         * Raise it for a wide-view mode; leave it alone for a map. A grid is `getDataSize()` bytes -
         * 1796 KB for a 512-texel DEM - so the budget divided by that is the number of grids held.
         * @param megabytes The new budget in megabytes, or 0 for the grid-count rule.
         */
        void setElevationCacheSize(int megabytes);

        /**
         * Returns how many drape tiles the automatic resolution assumes are cached at once.
         * @return The assumed working set in tiles. The default is 64.
         */
        int getDrapeWorkingSet() const;
        /**
         * Sets how many drape tiles the automatic resolution (DrapeResolution 0) assumes have to
         * fit the budget at once: the live cover plus the generation a zoom or pan is about to need
         * back. The resolution is halved until that many fit DrapeCacheSize.
         * Lower values buy sharpness at the price of a cache that thrashes; a real cover was
         * measured at 15-34 leaves, so a working set below that cannot hold even the live cover.
         * @param tiles The assumed working set in tiles. The default is 64.
         */
        void setDrapeWorkingSet(int tiles);

        /**
         * Returns how many zoom levels below the camera a tile may coarsen to.
         * @return The maximum tile zoom coarsening. The default is 3.
         */
        int getMaxTileZoomCoarsening() const;
        /**
         * Sets how far BELOW the camera's zoom the tile LOD may take a tile in terrain mode
         * (Options::TileLODFactor decides the rest). The tile surface is the depth OCCLUDER and its
         * tesselation is proportional to the tile size, so a tile that coarsens freely has its
         * ridge crests chopped flat and content drawn over a finer tile of another layer - a road,
         * a contour - shows through the ridge in front of it. The DEM level follows the tile zoom
         * as well (one elevation texture per tile), so the same tiles also shade as blocky
         * hillshade.
         * Larger values give the LOD more room - fewer tiles at a tilt, at the price of both;
         * 0 pins every tile to the camera's own zoom.
         * @param levels The new maximum tile zoom coarsening. The default is 3.
         */
        void setMaxTileZoomCoarsening(int levels);

        /**
         * Returns the terrain background color.
         * @return The terrain background color. The default is transparent (no background).
         */
        Color getBackgroundColor() const;
        /**
         * Sets the terrain background color: an opaque base fill of the terrain surface
         * drawn under all layers. It keeps the terrain shape visible (and its depth valid
         * for vector element and billboard occlusion) even without any raster or vector
         * tile layer content - without it the terrain is transparent wherever no layer
         * paints. Transparent (the default) disables the fill.
         * @param color The new terrain background color.
         */
        void setBackgroundColor(const Color& color);

        /**
         * Returns the terrain background bitmap state.
         * @return True if the map background bitmap is draped over the terrain as the base fill. The default is false.
         */
        bool isBackgroundBitmapEnabled() const;
        /**
         * Sets the terrain background bitmap state. When enabled, the map background bitmap
         * (Options::getBackgroundBitmap, the repeating pattern flat maps show below the tiles)
         * is draped over the terrain surface as the base fill drawn under all layers,
         * instead of the solid background color. Like the background color fill, it keeps
         * the terrain shape visible (and its depth valid for occlusion) where no layer
         * paints, and shows through translucent tile layer content.
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
         * Sets a fragment shader that paints the terrain surface itself. When set, it replaces
         * the background bitmap and the background color as the terrain base fill: the surface
         * is drawn as an opaque pass under all layers, so a map with no tile layer at all still
         * shows shaded relief. The source must define
         *
         *     vec4 surfaceColor();
         *
         * returning the non-premultiplied surface colour. These are available to it:
         *
         *     varying vec3  v_normal;      // unit surface normal, world space (x east, y north, z up)
         *     varying vec3  v_worldPos;    // surface position in internal map units
         *     varying float v_elevation;   // surface elevation in metres (before exaggeration)
         *     varying float v_dist;        // distance from the camera in metres
         *     uniform vec3  u_sunDir;      // unit vector towards the sun, world space
         *     uniform vec4  u_sunColor;    // sun colour, rgba 0..1
         *     uniform float u_sunIntensity;
         *     uniform float u_ambientIntensity;
         *     uniform float u_time;        // seconds since the map view was created
         *     uniform float u_zoom;        // current fractional map zoom
         *     uniform vec2  u_resolution;  // viewport size in pixels
         *
         * The surface must NOT fog itself: the SDK applies the same fog the rest of the frame gets
         * to whatever this returns. The fog uniforms and helpers documented on
         * FogOptions::setShaderSource are declared here too, and must not be redeclared.
         *
         * plus every parameter set with setSurfaceParameter (float) and setSurfaceColorParameter
         * (vec4, rgba 0..1) as a uniform of that name. Redeclaring any of the above is a compile
         * error, and a shader that fails to compile is dropped (the background bitmap/color is
         * used instead) with the error logged.
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
         * Sets the maximum visible tile zoom offset, relative to the camera zoom level.
         * Terrain level-of-detail is distance based: tiles close to the camera (and mountain
         * faces rising towards it) are shown at higher tile zoom levels than flat rendering
         * would ever use at the same camera zoom. If the map style renders differently at
         * different tile zoom levels, these LOD rings become visible as patches with hard
         * boundaries. Offset 0 caps tile detail at the level flat rendering would show at
         * the current camera zoom; positive values allow that many extra levels of detail
         * near the camera. Values of 100 or more disable the cap.
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
         * Sets the camera terrain clearance floor, in meters. The camera is always kept a
         * height above the terrain under it that scales with the zoom, as in mapbox: a
         * sixteenth of its distance to sea level, so zooming in is never blocked by the
         * clearance alone. This floor is added under that rule for apps that want a fixed
         * minimum. A zoom in stops at the clearance; a camera pushed under it by a pan or
         * by arriving elevation is lifted at a constant zoom, by reducing the tilt.
         * @param clearance The new clearance floor in meters. 0 (the default) applies the zoom-relative rule alone.
         */
        void setCameraClearance(float clearance);

        /**
         * Returns the share of the camera's altitude that the terrain clearance takes.
         * @return The clearance fraction. The default is 1/16.
         */
        float getCameraClearanceFraction() const;
        /**
         * Sets the share of the camera's altitude the clearance takes, replacing the 1/16 above.
         *
         * That rule models an ORBITING map camera, where the altitude and the viewing distance are
         * the same number - so the higher the ground, the further off it the camera is held. A
         * FIRST-PERSON view is the case it gets wrong: on a 4800 m summit it insists on 320 m of
         * clearance, and the eye floats a third of a kilometre above the peak it is standing on.
         *
         * Set it to 0 and the clearance becomes CameraClearance alone - a fixed height above the
         * ground, which is what "stand here" means. Leave it alone for a map.
         * @param fraction The new fraction, clamped to [0, 1). 1/16 is the default.
         */
        void setCameraClearanceFraction(float fraction);

        /**
         * Returns the height the viewpoint is lifted above the ground-following focus, in meters.
         * @return The focus lift in meters. The default is 0.
         */
        float getFocusLift() const;
        /**
         * Sets the height the viewpoint is lifted above the ground under it, in meters.
         *
         * The focus sits ON the terrain (mapbox's transform._centerAltitude) and the renderer owns
         * its height, so an application cannot place the eye above the ground by writing a focus
         * position with a z in it - the next frame recomputes it. This is the lift that survives:
         * it is ADDED to whatever the ground-following rule decides, so the clearance shell and the
         * follow band keep working underneath it and the eye ends up this far above the ground it
         * stands over, at any zoom and any tilt.
         *
         * What it is for is a first-person viewpoint: a peak finder standing a few hundred meters
         * up, seeing over the ridge in front of it. A camera looking at the horizon has almost no
         * orbit height to raise it with, so the zoom cannot buy that view.
         *
         * Meters, like CameraClearance: the Mercator stretch and the display scale are applied
         * here, not by the caller.
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
         * Sets how far behind the terrain a billboard or label anchor may sit and still count
         * as visible, as a fraction of its distance from the camera. 0 hides a label the moment its
         * anchor goes behind the relief, which is too tight over real terrain - the default 0.2 was
         * measured at Grenoble, where 0 dropped POIs on slopes facing the camera. Larger values
         * deliberately let partly hidden features label - a summit just behind a nearer ridge
         * still shows its name, which is what a peak-finder view wants.
         * @param tolerance The new relative tolerance (clamped to 0..1).
         */
        void setBillboardOcclusionTolerance(float tolerance);

        /**
         * Returns the ground distance the surface normals are measured over, in meters.
         * @return The normal sample distance, or 0 while the mesh's own spacing is used.
         */
        float getNormalSampleDistance() const;
        /**
         * Sets the ground distance the per-vertex surface normals are measured over, in meters.
         *
         * The default 0 takes the gradient from the MESH - the neighbouring grid nodes of the tile
         * the vertex belongs to. A tile carries the same number of cells whatever ground it covers,
         * so that step halves with every zoom level, and two tiles meeting at an LOD boundary smooth
         * the same hillside by different amounts. Their normals then disagree along the whole shared
         * edge. For shading nobody notices; for anything that DIFFERENTIATES the normals - a ridge
         * line drawn from the normal buffer (PostProcessEffect::setTerrainNormalsRequired) - every
         * tile boundary in the view becomes a drawn line, and no amount of care at the edge fixes
         * it, because the heights agree and the SCALE does not.
         *
         * A fixed distance makes the normal a property of the DEM instead: both sides of a boundary
         * sample the same two points and return the same value, so there is no edge to draw. What
         * survives is the real relief at that scale - 60-150 m reads ridges without turning every
         * DEM step into one.
         *
         * Costs four cached elevation lookups per mesh vertex, once per mesh build rather than per
         * frame. Planar surfaces only: on the globe the sample offsets are not the local frame's.
         * @param distance The new sample distance in meters, or 0 for the mesh's own spacing.
         */
        void setNormalSampleDistance(float distance);

        /**
         * Returns the opacity a label keeps while its anchor is behind 3D content.
         * @return The opacity of an occluded label. The default is 1, i.e. no occlusion.
         */
        float getTextOcclusionOpacity() const;
        /**
         * Sets the opacity a label keeps while the point it is anchored at is hidden by 3D
         * content - buildings, not the terrain, which occludes labels regardless (see
         * BillboardOcclusionTolerance). 0 hides such a label completely; 1, the default, draws it
         * as if nothing were in front of it.
         *
         * The test is per LABEL, not per fragment: a building crossing part of a word does not cut
         * it, the whole label fades by how much of a small square around its anchor is covered.
         *
         * Below 1 this costs one extra pass over the visible extrusions per frame (measured at
         * ~0.85 ms on an Adreno 610 at a city camera); at 1 the pass does not run at all. The
         * style's 'text-occlusion-opacity' wins over this value where it sets one.
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
         * @return The cache capacity in bytes. The default is 32MB.
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
         * Applies the switch's own ratio, 0 to 1. Scales the heights the elevation manager hands
         * out, leaving the app's own exaggeration alone. Does NOT notify option listeners or clear
         * the manual flag: it is driven per frame by the renderer, which asks for its own redraws.
         * Internal method.
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
        // Whether the renderer's 2D/3D switch has taken over. Until it has, setFlattened is the
        // whole state - see the comment there.
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

        // Contours are the one thing the drape's resolution visibly costs: they are hairline, and a
        // slope magnifies the texture, so they smear where fills and road casings survive.
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
