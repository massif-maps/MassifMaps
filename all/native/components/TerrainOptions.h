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
         */
        int getMeshResolution() const;
        /**
         * Sets the terrain mesh resolution. Higher values give more detailed terrain
         * at the cost of memory and CPU. The effective resolution is also limited by
         * the resolution of the elevation tiles.
         * @param meshResolution The new mesh resolution (clamped to 2..256).
         */
        void setMeshResolution(int meshResolution);

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
         * Returns the camera terrain clearance: the minimum height the camera is kept
         * above the terrain surface, in meters.
         * @return The camera clearance in meters. The default is 60. 0 disables camera terrain-following.
         */
        float getCameraClearance() const;
        /**
         * Sets the camera terrain clearance: the minimum height the camera is kept above
         * the terrain surface, in meters. When the camera would dive below this, it is
         * corrected by zooming out through the normal camera event path.
         * @param clearance The new clearance in meters. 0 disables camera terrain-following.
         */
        void setCameraClearance(float clearance);

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
         * @return The relative depth tolerance. The default is 0.
         */
        float getBillboardOcclusionTolerance() const;
        /**
         * Sets how far behind the terrain a billboard or label anchor may sit and still count
         * as visible, as a fraction of its distance from the camera. 0, the default, hides a
         * label the moment its anchor goes behind the relief. Larger values
         * deliberately let partly hidden features label - a summit just behind a nearer ridge
         * still shows its name, which is what a peak-finder view wants.
         * @param tolerance The new relative tolerance (clamped to 0..1).
         */
        void setBillboardOcclusionTolerance(float tolerance);

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
        std::atomic<bool> _tileEdgeStitchingEnabled;
        std::atomic<bool> _drapeFillsEnabled;
        std::atomic<bool> _drapeLinesEnabled;
        std::atomic<int> _drapeResolution;
        std::atomic<int> _minZoom;
        std::atomic<int> _maxTileZoomOffset;
        std::atomic<int> _backgroundColorARGB;
        std::atomic<bool> _backgroundBitmapEnabled;
        std::atomic<float> _depthBias;
        std::atomic<float> _cameraClearance;
        std::atomic<float> _cameraClampDuration;
        std::atomic<bool> _billboardOcclusionEnabled;
        std::atomic<float> _billboardOcclusionTolerance;
        std::atomic<float> _textOcclusionOpacity;
        std::atomic<float> _viewDistanceFactor;
        std::atomic<float> _viewDistance;
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
