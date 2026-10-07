/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_GLTILERENDERER_H_
#define _MASSIF_VT_GLTILERENDERER_H_

#include "Bitmap.h"
#include "Color.h"
#include "ViewState.h"
#include "Label.h"
#include "LabelFade.h"
#include "Styles.h"
#include "Tile.h"
#include "TileId.h"
#include "SpanResolver.h"
#include "DrawOnceOrder.h"
#include "TileTransformer.h"
#include "TileBitmap.h"
#include "TileBackground.h"
#include "TileBitmap.h"
#include "TileSurface.h"
#include "TileSurfaceBuilder.h"
#include "ExtrusionOccluder.h"
#include "GLExtensions.h"

#include <memory>
#include <functional>
#include <tuple>
#include <optional>
#include <array>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <set>
#include <utility>
#include <regex>
#include <atomic>
#include <mutex>

#include <cglib/ray.h>

namespace massif::vt {
    class LabelCuller;

    class GLTileRenderer final {
    public:
        // Shadow cascades, near first. The count is a uniform, but the shader declares this many
        // matrices and varyings, so raising it means touching the shader too.
        static constexpr int MAX_SHADOW_CASCADES = 4;
        // Shadow reach as a multiple of the camera-to-focus distance: mapbox's shadow_renderer.ts
        // cameraToCenterDistance * 1.5 * 3.0.
        static constexpr double SHADOW_CUTOUT_DISTANCE_FACTOR = 4.5;
        // Fraction of that reach the outer cascade starts fading at (mapbox's u_shadow_fade_range).
        static constexpr double SHADOW_FADE_START_FRACTION = 0.75;

        struct LightingShader {
            bool perVertex;
            std::string shader;
            std::function<void(GLuint, const ViewState&)> setupFunc;

            explicit LightingShader(bool perVertex, std::string shader, std::function<void(GLuint, const ViewState&)> setupFunc) : perVertex(perVertex), shader(std::move(shader)), setupFunc(std::move(setupFunc)) { }
        };

        struct GeometryIntersectionInfo {
            TileId tileId;
            int layerIndex;
            long long featureId;
            int geoPointIndex; // used only for MultiPoint Point Geometry
            std::size_t rayIndex;
            double rayT;

            explicit GeometryIntersectionInfo(const TileId& tileId, int layerIndex, long long featureId, int geoPointIndex, std::size_t rayIndex, double rayT) : tileId(tileId), layerIndex(layerIndex), featureId(featureId), rayIndex(rayIndex), geoPointIndex(geoPointIndex), rayT(rayT) { }
        };

        struct BitmapIntersectionInfo {
            TileId tileId;
            int layerIndex;
            std::shared_ptr<const TileBitmap> bitmap;
            cglib::vec2<float> uv;
            std::size_t rayIndex;
            double rayT;

            explicit BitmapIntersectionInfo(const TileId& tileId, int layerIndex, std::shared_ptr<const TileBitmap> bitmap, const cglib::vec2<float>& uv, std::size_t rayIndex, double rayT) : tileId(tileId), layerIndex(layerIndex), bitmap(bitmap), uv(uv), rayIndex(rayIndex), rayT(rayT) { }
        };

        /**
         * A tile's elevation texture for GPU draping (vertex texture fetch); may cover an ancestor. The
         * internal bounds map a world rectangle to uv [0,1]^2, v growing north.
         * Height in metres = dot(normalized RGBA sample, decode) + decodeOffset.
         */
        struct TerrainTexture {
            GLuint textureId = 0;
            cglib::vec2<int> textureSize = cglib::vec2<int>(0, 0);          // texture dimensions in texels (for the shader-side bilinear filter)
            cglib::vec2<double> internalOrigin = cglib::vec2<double>(0, 0); // world position of uv (0,0)
            cglib::vec2<double> internalSize = cglib::vec2<double>(0, 0);   // world size covered by uv [0,1]
            int borderTexels = 1;          // neighbour texels around the raster on each side, inside textureSize
            cglib::vec4<float> decode = cglib::vec4<float>(0, 0, 0, 0);     // texture sample -> meters (linear part)
            float decodeOffset = 0.0f;                                      // ... plus this constant
            float metersToInternal = 0.0f; // meters -> world z units at the equator (exaggeration included)
            float mercatorYScale = 0.0f;   // world y -> mercator angle (for the per-vertex 1/cos(latitude) factor)
            float metersPerTexel = 0.0f;   // ground meters per texel at the equator (the 1/cos(latitude) stretch is per fragment)
            GLuint gradientTextureId = 0;  // RG16F forward differences in meters, same texels (ElevationGradient); 0 = flat light
            float minHeight = 1.0f;        // meters over the DEM raster, for culling; min > max = unknown
            float maxHeight = 0.0f;
            // The DEM box-filtered to one texel per mesh node, which the vertex stage displaces from.
            // 0 = none: the vertex stage samples the full texture and aliases sub-cell relief.
            GLuint nodeTextureId = 0;
            cglib::vec2<int> nodeTextureSize = cglib::vec2<int>(0, 0);
            cglib::vec2<double> nodeOrigin = cglib::vec2<double>(0, 0); // world position of node uv (0,0)
            cglib::vec2<double> nodeSize = cglib::vec2<double>(0, 0);   // world size covered by node uv [0,1]
        };

        using TerrainTextureProvider = std::function<bool(const TileId&, TerrainTexture&)>;

        /**
         * Directional lighting of the draped surface, replacing per-style lighting and the hillshade
         * raster. sunDir is a unit vector in the tile frame: x east, y north, z up.
         */
        struct TerrainLighting {
            bool enabled = false;
            cglib::vec3<float> sunDir = cglib::vec3<float>(0, 0, 1);
            cglib::vec3<float> sunColor = cglib::vec3<float>(1, 1, 1);
            cglib::vec3<float> ambientColor = cglib::vec3<float>(1, 1, 1);
            float sunIntensity = 1.0f;
            float ambientIntensity = 0.35f;
        };

        /**
         * DEM-derived paint drawn from the shared elevation texture, with no tile set of its own. It is
         * baked into the shared drape at its own layer position, so the style's hillshade order holds.
         */
        struct TerrainPaint {
            bool enabled = false;
            float heightScale = 1.0f;       // relief scale, as HillshadeRasterTileLayer defines it
            bool exaggerateHeightScale = true; // MapLibre's low-zoom relief boost
            bool legacyHeightScale = false;    // pre-MapLibre-parity formula
            float contrast = 0.5f;          // MapLibre 'hillshade-exaggeration', fed to the lighting shader
            float opacity = 1.0f;
            // Hash of the paint's whole appearance, incl. the injected lighting shader's inputs; the
            // renderer cannot derive it, and without it a change leaves stale drapes in place.
            std::size_t fingerprint = 0;
        };

        explicit GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, float scale);

        void setLightingShader2D(const std::optional<LightingShader>& lightingShader2D);
        void setLightingShader3D(const std::optional<LightingShader>& lightingShader3D);
        void setLightingShaderNormalMap(const std::optional<LightingShader>& lightingShaderNormalMap);
        
        void setInteractionMode(bool enabled);
        void setTerrainMode(bool enabled, float depthBias);
        void setTerrainRegularGrid(bool enabled, int resolution);
        // First stack-wide depth ordinal of this renderer's style layers, or every renderer claims 0.
        void setTerrainLayerOrdinalBase(int base);
        // Style layers drawn last frame, so the owner numbers the stack densely: the ordinal's eye
        // tolerance grows as distance^2, and gaps push it into the range that sees through ridges.
        int getStyleLayerCount() const;
        // Regular grid only: an edge shared with a coarser neighbour follows its lattice, so it does not crack.
        void setTerrainEdgeStitching(bool enabled);
        void setTerrainSlackScale(float slackScale);
        /**
         * Tangram's constant clip-space pull of content towards the viewer (polygon.vs depth_shift). It
         * falls off as 1/w, clearing near chords without a see-through band at range. 0 (default) = none.
         */
        void setTerrainContentDepthShift(float depthShift);
        // Metre-constant clearance of a draped line over the ground (see applyDepthBias); a clip- or
        // NDC-constant bias cannot pay for the chord without leaking through ridges.
        void setTerrainLineClearance(float clearance);
        void setTerrainDrapeFills(bool enabled, bool includeLines);
        // Whether bridges and tunnels stand on their chord. Off by default: spans drape like the
        // ground and cost only the empty-records test.
        void setSpansEnabled(bool enabled);
        void setTerrainDrapeResolution(int resolution);
        void setTerrainLighting(const TerrainLighting& lighting);
        // Extrusions' contact shadow (POLYGON3DGROUND): darkness at the wall, falloff to the skirt's edge.
        void setGroundAO(float intensity, float attenuation);
        /**
         * scale: mapbox's fill-extrusion-vertical-scale. growOnAppear raises walls with the tile fade-in.
         * fadeOnAppear (off by default) fades their colour, which shows the full-strength shadow through them.
         */
        void setBuildingHeight(float scale, float viewScale, bool growOnAppear, bool fadeOnAppear);
        // 0 below the minimum zoom, ramping to 1 one level above it.
        static float groundAOZoomFade(float zoom);
        // Whether the contact shadows would draw anything at all this frame (intensity and zoom).
        bool isGroundAOActive() const;
        // The same, for the drape bake, which applies no zoom fade.
        bool isGroundAOBakeable() const;
        // Draws the visible contact-shadow quads under MIN blending into one mask; returns the draw count.
        int renderGroundAOMask();
        // Default opacity an occluded label keeps, 1 = no occlusion; TileLabel::Style::occlusionOpacity wins.
        void setLabelOcclusionOpacity(float occludedOpacity);
        // Whether the visible tiles draw a background, a bitmap or a geometry, rather than labels alone.
        bool hasGroundContent() const;
        // Whether fully blended, opaque tile backgrounds paint all the flat ground the frustum sees.
        bool coversGround(const cglib::frustum3<double>& frustum) const;
        // The contact-shadow mask in one drape tile's frame, so it follows the terrain. Changes no GL state.
        int bakeGroundAOMask(const TileId& targetTileId);
        // Turns this renderer into a paint baker (see TerrainPaint); only under a cross-layer drape target.
        void setTerrainPaint(const TerrainPaint& paint);
        // Draw the paint as the ground (tangram): one surface draw per tile cheaper, but the shading
        // then lies under every ground-shaped fill.
        void setTerrainPaintOnGround(bool enabled);
        // Texture fetches per terrain vertex: 16 lattice clamp, 4 manual bilinear, 1 hardware-filtered (tangram).
        void setTerrainDemTaps(int taps);
        // Per-layer per-tile background meshes under a shared ground; off by default, tangram just clears.
        void setTerrainTileBackgrounds(bool enabled);
        // Stencil masks clipping content to its tile: -1 auto (off in terrain, where a mask is a full
        // displaced grid; on in 2D and for comp-op layers), 0 never, 1 always.
        void setTileMasks(int mode);
        // The terrain cover a paint draws itself over when there is no drape; it has no tile set of its own.
        void setTerrainPaintTiles(const std::vector<TileId>& tileIds);
        // Distance fog; a transparent colour or zero range disables it. rangeScale: world units per
        // range unit; horizonBlend: the angular term the sky shares.
        void setFog(const Color& color, float startDistance, float distance, float rangeScale, float horizonBlend);
        // Mapbox high-color / space-color, for custom fog shaders; the built-in blend ignores them.
        void setFogColors(const Color& highColor, const Color& spaceColor);
        // Mapbox vertical-range: fog fades out between two altitudes in metres; equal values disable it.
        void setFogVertical(float startMeters, float endMeters, float metersPerUnit, float cameraHeightMeters);
        // rayVec = uFogRay * vec3(gl_FragCoord.xy, 1) (FogShader::rayBasis); set every frame.
        void setFogRayBasis(const cglib::mat3x3<float>& rayBasis);
        // Replaces the whole fog block with GLSL defining applyFog() and skyFog(). Rebuilds every
        // program, so it acts only on a real change.
        void setFogShaderSource(const std::string& shaderSource);

        // True once an ESSL 3.00 program fell back to 1.00. Sticky, so the owner logs it once.
        bool hasShaderVersionFallback() const { return _essl3Failed; }
        // Caster tiles skipped for lack of elevation (else a sea-level plane): tells that apart from a
        // light-box clip. Reading clears it.
        int consumeShadowCastersMissingElevation() { int n = _shadowCastersMissingElevation; _shadowCastersMissingElevation = 0; return n; }
        // Scene light on a flat upward surface, sRGB (mapbox's ground radiance); scales colours with emissive < 1.
        void setRadiance(const cglib::vec3<float>& radiance) { _radiance = radiance; }
        // Emitted (unlit) fraction of the map background's colour; a Map setting, one value per style.
        void setBackgroundEmissive(float emissive) { _backgroundEmissive = emissive; }
        // The Map block's building-emissive, for extrusions whose rule sets none; kept so a draw can
        // restore u_emissive after a rule overrode it.
        void setBuildingEmissive(float emissive) { _buildingEmissive = emissive; }
        // The projection's metres-to-internal factor, so shadows do not need a DEM to be fitted.
        void setMetersToInternal(double metersToInternal) { _metersToInternal = metersToInternal; _spanResolver.setMetersToInternal(metersToInternal); }
        // The owner renders the casters from the light into its own framebuffer and hands the texture back here.
        void setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, MAX_SHADOW_CASCADES>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs);
        // Light view-projection fitted to the tiles, one call per cascade; false if empty or no elevation.
        // minHeight/maxHeight bound the shadowed volume: a generous slab pixelates a low sun.
        bool calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const std::vector<std::pair<double, double> >& casterHeights, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        // Terrain shadow resolved into a half-resolution screen mask that surface draws sample.
        void setTerrainShadowMask(GLuint texture, float invScreenWidth, float invScreenHeight);
        // Draws the mask for the given tiles into the bound framebuffer. Returns the draw count.
        int renderTerrainShadowMask(const std::vector<TileId>& tileIds);
        // Sum of the casters' fade-in blends: how far the shadow map drifted from growing extrusions.
        float shadowCasterFadeSignature(const std::vector<TileId>* coveredBy) const;
        // Draws this renderer's shadow casters for the given tiles into the bound framebuffer; returns the draws.
        int renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);

        // Shared terrain ground (tangram's model): every renderer gets the same cover, drawn once per
        // frame; ground-shaped content goes on the cover tiles, not the renderer's own.

        // proxyDepths, parallel to tileIds: levels coarser than asked for (0 = its own), since a coarse
        // stand-in is a different height field that pokes through content.
        void setTerrainGroundTiles(const std::vector<TileId>& tileIds, const std::vector<int>& proxyDepths);
        // Per drawn cover tile, metres a skirt hangs under its west/east/south/north edge, where the neighbour
        // there is drawn from other height data (a hole otherwise). Absent tiles hang none.
        void setTerrainSkirtDrops(const std::map<TileId, cglib::vec4<float>>& drops);
        // Draws the shared ground per cover tile at its true depth: the frame's only depth-writing
        // terrain geometry. Returns the draws.
        int renderTerrainGround(const Color& color);
        void setExternalDrapeTarget(bool enabled);
        // Terrain tiles the owner drapes this frame; their content is baked and must not be redrawn in 3D.
        void setExternalDrapeTiles(const std::vector<TileId>& tileIds);
        // Tiles this renderer would drape this frame, each with a fingerprint to detect a stale texture.
        void collectDrapeTiles(std::map<TileId, std::size_t>& drapeTiles) const;
        // Bakes one tile's drapeable content into the bound framebuffer, which the owner clears once per
        // tile. Returns the primitives drawn.
        int bakeDrapeTile(const TileId& targetTileId);
        // The deck's drape: only this tile's span content, so a bridge's road lands on its deck.
        // Exact complement of bakeDrapeTile.
        int bakeSpanDrapeTile(const TileId& targetTileId);
        // Tiles carrying a bridge or tunnel, with a bake fingerprint; empty without spans.
        void collectSpanDrapeTiles(std::map<TileId, std::size_t>& spanTiles) const;
        // Cut ends of chordless span pieces, stepped past the cut: the owner fetches the coarser tile
        // there so the piece resolves, or a bridge stays draped until its abutments are in view.
        void collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const;
        // The owner's baked span drape per tile; empty = no bridge in view.
        void setSpanDrapeTextures(const std::map<TileId, GLuint>& textures);
        // The ground drape and sub-rect drawn on each drape tile, for a deck's roof past its portals;
        // _drapeTextures are out of play when the owner composites.
        struct GroundDrape {
            GLuint texture = 0;
            float uvOffsetX = 0.0f, uvOffsetY = 0.0f, uvScale = 1.0f;
        };
        void setGroundDrapeTextures(const std::map<TileId, GroundDrape>& drapes);
        // Style layers with drapeable content, in draw order, flagged draped or live; the owner's
        // concatenated stack decides each live layer's occlusion mask.
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        // Bakes the accumulated alpha of the draped layers from fromStyleLayerIdx on, as bakeDrapeTile
        // does colour: the mask a live layer below the cut is drawn through.
        int bakeDrapeCoverage(const TileId& targetTileId, int fromStyleLayerIdx);
        // maskTextures[k]: mask k per drape tile; styleLayerMasks: live style layer -> mask index.
        // A layer absent from it draws unmasked.
        void setDrapeCoverageMasks(const std::vector<std::map<TileId, GLuint> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        // Draws one tile's surface with an external drape, writing depth; returns the draws or -1 no
        // texture, -2 shared grid inactive, -3 tile not registered. uvOffset/uvScale pick a sub-rect
        // (an ancestor's while the own bake waits), which keeps a budgeted bake from flashing.
        int renderDrapedSurface(const TileId& targetTileId, GLuint drapeTexture, float uvOffsetX = 0.0f, float uvOffsetY = 0.0f, float uvScale = 1.0f);
        // Flat-colour surface for a tile whose drape is not baked yet, keeping the depth buffer complete.
        int renderDrapedSurfaceFill(const TileId& targetTileId, const Color& color);
        // Copies a drape texture rect into the bound drape framebuffer, unblended, so a new tile shows
        // the cached ground while its own bake waits.
        int blitDrapeTexture(GLuint srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        void setTerrainDepthWrite(bool enabled);
        void setTerrainTextureProvider(TerrainTextureProvider provider);
        void setDebugWireframe(bool enabled);
        // Outlines every drawn tile: colour per zoom, brightness by tile parity, half opacity for a stand-in.
        void setDebugTileBorders(bool enabled);
        void setDebugSurfacePrefill(bool enabled);
        void setTerrainBackgroundColor(const Color& color);
        void setLabelElevationProvider(std::function<double(const cglib::vec3<double>&, int)> provider);
        /** Label anchors (internal space, WORLD_SIZE wide) -> vt's normalized coordinates, for span chords. */
        void setLabelPositionScale(double scale) { _labelPositionScale = scale; }
        // debug.massif.labelanchor: false anchors labels in the frame, true samples new ones on the cull thread.
        void setLabelAnchorOnCull(bool enabled) { _labelAnchorOnCull = enabled; }
        // Re-anchor labels onto the terrain next frame; the tileIds overload marks only labels over those tiles.
        void invalidateLabelElevation();
        void invalidateLabelElevation(const std::vector<TileId>& tileIds);
        // Same for extrusion bases, exact per tile as a building's centroid is in its own tile.
        // Spans stay global: a chord samples portals in other tiles.
        void invalidateExtrusionBases();
        void invalidateExtrusionBases(const std::vector<TileId>& tileIds);
        // Ground under an extrusion, internal z; reports missing data, as a guessed 0 buries a baked base.
        void setExtrusionElevationProvider(std::function<bool(const cglib::vec3<double>&, int, bool, double&)> provider);
        void setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)> occlusionTest);
        void setLayerBlendingSpeed(float speed);
        void setLabelBlendingSpeed(float speed);
        void setRasterFilterMode(RasterFilterMode filterMode);
        void setRendererLayerFilter(const std::optional<std::regex>& filter);
        // Per-frame gate: only layers with layerIndex in [first, second) draw, so one renderer can draw
        // disjoint ranges. nullopt (default) = all.
        void setRendererLayerIndexRange(const std::optional<std::pair<int, int>>& range);
        // Matching layers skip the drape and draw live at screen resolution, as a slope magnifies the
        // bake. nullopt (default) = drape everything the geometry type allows.
        void setNoDrapeLayerFilter(const std::optional<std::regex>& filter);
        void setClickHandlerLayerFilter(const std::optional<std::regex>& filter);
        void setViewState(const ViewState& viewState);
        void setLineAntialiasScale(float scale);
        // labelOnlyTiles: off-frustum tiles whose labels are placed (so they arrive opaque), never drawn.
        // spanReferenceTiles: fetched unseen for stranded bridge chords (collectUnresolvedSpanEnds);
        // they only join the span unions, so one overlapping the view does not double geometry.
        // shadowCasterTiles: off-frustum render tiles whose extrusions only cast; no surface, no labels.
        void setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::map<TileId, std::shared_ptr<const Tile>>& labelOnlyTiles = {}, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles = {}, const std::map<TileId, std::shared_ptr<const Tile>>& shadowCasterTiles = {});
        void teleportVisibleTiles(int dx, int dy);

        void initializeRenderer();
        void resetRenderer();
        // Keeps the uploaded tiles and their blend state: a new renderer fades every tile in from nothing.
        void setTransformer(std::shared_ptr<const TileTransformer> transformer);
        void resetTileSurfaces();
        void invalidateTileSurfaces(const std::vector<TileId>& tileIds);
        void deinitializeRenderer();

        bool startFrame(float dt);
        void renderGeometry(bool geom2D, bool geom3D, bool inline3D = false);
        void renderLabels(bool labels2D, bool labels3D);
        bool endFrame();

        /** Returns false when the culler's slice ran out before this layer's labels did; `empty`: there were none. */
        bool cullLabels(LabelCuller& culler, bool& empty);
        void restartLabelPlacement();
        /** Whether labels were moved onto newly arrived terrain since the last call. */
        bool consumeLabelsReanchored() { return _labelsReanchored.exchange(false); }
        void snapLabelTransition();

        bool findBitmapIntersections(const std::vector<cglib::ray3<double>>& rays, std::vector<BitmapIntersectionInfo>& results) const;
        bool findGeometryIntersections(const std::vector<cglib::ray3<double>>& rays, float pointBuffer, float lineBuffer, bool geom2D, bool geom3D, std::vector<GeometryIntersectionInfo>& results) const;
        bool findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const;

    private:
        using GlobalIdLabelMap = std::unordered_map<long long, std::shared_ptr<Label>>;
        // One list per pass, in draw order: grouping by atlas left cross-atlas order to a pointer hash,
        // putting labels under their icons.
        using PassLabels = std::vector<std::shared_ptr<Label>>;

        enum class LightingMode {
            NONE,
            GEOMETRY2D,
            GEOMETRY3D,
            NORMALMAP,
            TERRAINPAINT // the normal-map lighting shader, fed from the terrain DEM instead of a normal map raster
        };

        struct RenderTileLayer {
            TileId targetTileId = TileId(-1, -1, -1);
            TileId sourceTileId = TileId(-1, -1, -1);
            std::shared_ptr<const TileLayer> layer;
            float tileSize = 0.0f;
            bool active = false;
            float blend = 0.0f;
        };

        struct RenderTile {
            TileId targetTileId = TileId(-1, -1, -1);
            std::shared_ptr<const Tile> tile;
            std::multimap<int, RenderTileLayer> renderLayers;
            bool visible = false;
            bool current = false; // built from a tile in the new set, not carried over for uncovered ground
        };

        struct FrameBuffer {
            GLuint colorTexture;
            std::vector<GLuint> depthStencilRBs;
            std::vector<GLenum> depthStencilAttachments;
            GLuint fbo;

            FrameBuffer() : colorTexture(0), depthStencilRBs(), depthStencilAttachments(), fbo(0) { }
        };

        struct ShaderProgram {
            GLuint program;
            std::vector<GLuint> uniforms;
            // Signed: a dropped attribute is -1, which unsigned is GL_INVALID_VALUE in glVertexAttribPointer.
            std::vector<GLint> attribs;

            ShaderProgram() : program(0), uniforms(), attribs() { }
        };

        struct CompiledBitmap {
            GLuint texture;

            CompiledBitmap() : texture(0) { }
        };

        struct CompiledQuad {
            GLuint vbo;

            CompiledQuad() : vbo(0) { }
        };

        struct CompiledSurface {
            GLuint vertexGeometryVBO;
            GLuint indicesVBO;
            GLuint wireframeIndicesVBO;
            GLsizei wireframeIndicesCount;

            CompiledSurface() : vertexGeometryVBO(0), indicesVBO(0), wireframeIndicesVBO(0), wireframeIndicesCount(0) { }
        };

        struct CompiledGeometry {
            GLuint vertexGeometryVBO;
            GLuint indicesVBO;
            // One VAO per program, built on first use: programs need not share attribute locations, and
            // re-specifying one VAO for a second program draws garbage on Adreno.
            mutable std::vector<std::pair<GLuint, GLuint>> geometryVAOs; // program -> VAO

            CompiledGeometry() : vertexGeometryVBO(0), indicesVBO(0) { }
        };

        struct CompiledLabelBatch {
            GLuint verticesVBO;
            GLuint offsetsVBO;
            GLuint normalsVBO;
            GLuint texCoordsVBO;
            GLuint attribsVBO;
            GLuint indicesVBO;

            CompiledLabelBatch() : verticesVBO(0), offsetsVBO(0), normalsVBO(0), texCoordsVBO(0), attribsVBO(0), indicesVBO(0) { }
        };

        struct LabelBatchParameters {
            static constexpr int MAX_PARAMETERS = 16;

            int labelCount;
            int parameterCount;
            float scale;
            int glyphRenderSize;
            // The tile owning this batch's anchors, whose elevation uniforms lift them on the GPU; (-1,-1,-1) = none.
            TileId tileId { -1, -1, -1 };
            cglib::mat4x4<double> labelMatrix;
            std::array<cglib::vec4<float>, MAX_PARAMETERS> colorTable;
            std::array<float, MAX_PARAMETERS> widthTable;
            std::array<float, MAX_PARAMETERS> strokeWidthTable;

            LabelBatchParameters() : labelCount(0), parameterCount(0), scale(0), glyphRenderSize(64), labelMatrix(cglib::mat4x4<double>::identity()), colorTable(), widthTable(), strokeWidthTable() { }
        };

        // Frames between sweeps for expired owners: a sweep walks every cached entry to free a VBO sooner.
        static constexpr int RESOURCE_SWEEP_INTERVAL_FRAMES = 8;
        // Widest halo the encoded glyph field can describe, in screen pixels (the old GLYPH_RENDER_SPREAD).
        static constexpr float MAX_HALO_PIXELS = 4.7f;
        // An icon's padded field allows a wider halo; the encoding runs out at ~8 texels.
        static constexpr float MAX_ICON_HALO_PIXELS = 8.0f;
        static constexpr float STROKE_UV_SCALE = 2.857f; // stroked line UV scale factor
        static constexpr float TERRAIN_LAYER_DEPTH_DELTA = 1.0f / 524288.0f; // 2^-19: NDC depth separation per draped layer bias unit (GPU terrain draping mode)
        // Floor of a proxy tile's depth (levels coarser than the deepest on screen); tangram setProxyDepth.
        static constexpr float TERRAIN_PROXY_DEPTH_UNITS = 1.0f;
        // tangram res/scenes/terrain-3d.yaml, TANGRAM_RASTER_STYLE branch: `proxy *= 48.0`.
        static constexpr float TERRAIN_RASTER_PROXY_SCALE = 48.0f;
        static constexpr float TERRAIN_PAINTER_SURFACE_BACK = 2.0f; // clip-slack units the surface is pushed back, so content at real depth cannot leak past a near ridge
        static constexpr float TERRAIN_EXTRUSION_DEPTH_DELTAS = 24.0f; // extrusion vs surface pre-pass; more widens the band where one shows through a crest
        static constexpr float TERRAIN_DEPTH_CLIP_SLACK = 1.0e-3f; // clip-space depth shift per bias unit at the reference tile size, scaled by tile size (quadratic law, see setupTerrainUniforms) and |proj m22|
        static constexpr double TERRAIN_DEPTH_CLIP_REF_TILE_SIZE = 512.0; // zoom 11 tile size in internal units - the anchor of the quadratic slack law
        static constexpr float ALPHA_HIT_THRESHOLD = 0.05f; // threshold value for 'transparent' pixel alphas
        static constexpr std::size_t DRAPE_TEXTURE_POOL_SIZE = 32; // recycled drape textures kept alive between frames

        bool isTileVisible(const TileId& tileId) const;
        /**
         * Culling-only headroom above a tile's ground, internal units: maplibre's ASSUMED_MAX_FEATURE_HEIGHT_METERS,
         * grown as the frustum's bottom nears the horizon, so a building outlives its ground leaving the frustum.
         */
        double tileCullingHeadroom() const;
        bool isEmptyBlendRequired(CompOp compOp) const;

        unsigned int fogFlag() const;
        // COVERAGE while a mask is being baked: the fragment stage writes alpha, not colour.
        unsigned int coverageFlag() const;
        // DRAPE_MASK while a live no-drape layer is drawn under a mask resolved for its tile.
        unsigned int drapeMaskFlag() const;
        // TERRAIN_SHADOW plus the cascade count the receiver lookup is compiled for.
        unsigned int shadowReceiverFlags() const;
        // Same, for the terrain surface: reads the screen-space mask, or produces it.
        unsigned int surfaceShadowFlags() const;
        cglib::vec4<float> calculateShadowNormalOffsets(const cglib::mat4x4<double>& tileFrame) const;
        void setupShadowNormalOffsetUniforms(const ShaderProgram& shaderProgram, const cglib::mat4x4<double>& tileFrame) const;
        void setupShadowFadeRangeUniform(const ShaderProgram& shaderProgram) const;
        // Whether an extrusion layer is solid enough to cast - mapbox's noShadowCutoff.
        bool extrusionCastsShadow(const RenderTileLayer& renderLayer) const;
        void setupSurfaceShadowUniforms(const ShaderProgram& shaderProgram, const cglib::mat4x4<double>& surfaceFrame, bool hasElevation);
        // Binds the program only when it is not the one already bound (see the definition).
        void useProgram(const ShaderProgram& shaderProgram);
        // Forgets which program is bound. Must be called wherever another renderer may have
        // bound one of its own since the last draw.
        void resetProgramState();
        void setupFogUniforms(const ShaderProgram& shaderProgram) const;
        cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat3x3<double> calculateTileMatrix2D(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat4x4<float> calculateTileMVPMatrix(const TileId& tileId, float coordScale = 1.0f) const;

        bool testLayerFilter(const std::string& layerName, const std::optional<std::regex>& filter) const;
        bool isLayerDraped(const std::shared_ptr<const TileLayer>& layer) const;
        bool hasSpanContent(const RenderTileLayer& renderLayer) const;
        bool resolveSpanDrape(const TileId& targetTileId, GLuint& texture, cglib::vec4<float>& uvTransform) const;
        cglib::vec3<float> spanDrapeLight() const;
        std::map<TileId, GLuint> _spanDrapeTextures;
        // Bake extent per span drape tile in drape uv (u0, v0, u1, v1): the deck's own, so a narrow deck
        // gets the texture's full width.
        mutable std::map<TileId, cglib::vec4<float>> _spanDrapeBounds;
        bool _labelAnchorOnCull = true;
        GLuint _pendingSpanDrape = 0;
        std::map<TileId, GroundDrape> _groundDrapes;
        bool resolveGroundDrape(const TileId& targetTileId, GLuint& texture, cglib::vec4<float>& uvTransform) const;
        GLuint _pendingGroundDrape = 0; // the target tile's ground drape, for the roof past the portals
        static constexpr double SPAN_GROUND_TOLERANCE_METRES = 1.5;
        cglib::vec4<float> _pendingGroundDrapeTransform = cglib::vec4<float>(0, 0, 1, 1);
        cglib::vec4<float> _pendingSpanDrapeTransform = cglib::vec4<float>(0, 0, 1, 1);
        // Shared body of bakeDrapeTile/bakeDrapeCoverage from fromStyleLayerIdx on; caller holds _mutex.
        // clipZoom premultiplies the bake matrix: the span drape scales the deck's bounds to the texture.
        int bakeDrapeUnits(const TileId& targetTileId, int fromStyleLayerIdx, bool spanOnly = false, const cglib::mat4x4<float>* clipZoom = nullptr);
        // A tile's baked layers that pass `wanted`, in draw-once order once one carries a group: the bound
        // drape FBO then borrows _drapeStencilRB for the bake (see renderGeometry2D for the passes).
        void bakeLayersDrawOnce(const RenderTile& renderTile, const std::function<bool(const RenderTileLayer&)>& wanted, const std::function<void(const RenderTileLayer&)>& draw);
        // A live layer's occlusion mask over a tile and the target -> mask uv transform. False without a
        // mask, or when the drape tile is finer than the target: one draw cannot sample several masks.
        bool resolveDrapeCoverageMask(const TileId& targetTileId, int styleLayerIdx, GLuint& texture, cglib::vec4<float>& uvTransform) const;
        bool testIntersectionOpacity(const std::shared_ptr<const BitmapPattern>& pattern, const cglib::vec2<float>& uvp, const cglib::vec2<float>& uv0, const cglib::vec2<float>& uv1) const;

        void buildTileSurfaces(const std::set<TileId>& tileIds);

        void buildRenderTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles);
        void initializeRenderTile(TileId targetTileId, RenderTile& renderTile, const std::shared_ptr<const Tile>& tile, const std::vector<RenderTile>& existingRenderTiles) const;
        void mergeExistingRenderTile(TileId targetTileId, const RenderTile& existingRenderTile, std::vector<RenderTile>& renderTiles, int depth) const;
        bool updateRenderTile(RenderTile& renderTile, float dBlend) const;

        static long long calculateLabelGeometryHash(const Tile* tile, long long localId);
        /** A label map rebuild prepared off _mutex by prepareLabelMaps, committed under it by commitLabelMaps. */
        struct LabelMapBuild {
            long long signature = 0;
            bool unchanged = false;        // the tile set did not move; nothing to do
            unsigned int generation = 0;   // of the maps oldLabelMap was taken from
            std::map<int, GlobalIdLabelMap> oldLabelMap;
            std::optional<std::regex> layerFilter;
            std::shared_ptr<const TileTransformer> transformer; // setTransformer may swap it while the prepare runs
            std::map<int, std::unordered_map<long long, std::pair<long long, int>>> signatures;
            std::map<int, GlobalIdLabelMap> labelMap;
            std::map<int, std::unordered_set<long long>> reusedIds;
        };
        static long long calculateLabelTilesSignature(const std::vector<std::shared_ptr<const Tile>>& labelTiles);
        void prepareLabelMaps(const std::vector<std::shared_ptr<const Tile>>& labelTiles, const std::map<int, GlobalIdLabelMap>& oldLayerLabelMap, const std::optional<std::regex>& layerFilter, LabelMapBuild& build) const;
        void commitLabelMaps(LabelMapBuild& build);
        bool updateLabel(const std::shared_ptr<Label>& label, float dOpacity) const;

        void findTileGeometryIntersections(const TileId& tileId, const std::shared_ptr<const TileGeometry>& geometry, const std::vector<cglib::ray3<double>>& rays, float tileSize, float pointBuffer, float lineBuffer, float heightScale, std::vector<GeometryIntersectionInfo>& results) const;
        void findLabelIntersections(const std::shared_ptr<Label>& label, const std::vector<cglib::ray3<double>>& rays, float buffer, std::vector<GeometryIntersectionInfo>& results) const;
        void findTileBitmapIntersections(const TileId& tileId, const std::shared_ptr<const TileBitmap>& bitmap, const std::shared_ptr<const TileSurface>& tileSurface, const std::vector<cglib::ray3<double>>& rays, float tileSize, std::vector<BitmapIntersectionInfo>& results) const;

        void renderGeometry2D(const std::vector<RenderTile>& renderTiles, GLint stencilBits);
        void renderGeometry3D(const std::vector<RenderTile>& renderTiles, bool allowInline);
        // What begin3DPass set up, and what end3DPass has to undo. One layer's worth.
        struct Pass3DState {
            bool useOverlay = false;
            bool terrainOccluders = false;
            GLint previousFBO = 0;
            float layerOpacity = 1.0f;
            float geometryOpacity = 1.0f;
            CompOp layerCompOp = CompOp::SRC_OVER;
            // Translucent extrusions draw in two passes so only the nearest surface blends.
            bool translucentExtrusions = false;
        };
        // Opens one style layer's 3D pass (overlay FBO, occluder pre-pass, depth/blend state); draws up to
        // end3DPass resolve against the ground as extrusions do.
        Pass3DState begin3DPass(const std::vector<const RenderTileLayer*>& renderLayers, const std::vector<RenderTile>& renderTiles, bool allowInline);
        void end3DPass(const Pass3DState& state);
        // POLYGON3D geometries in draw order, optionally only over coveredBy and including the shadow caster
        // tiles past the view; the callback returns false to skip the rest of that layer.
        template <typename Func>
        void forEachVisibleExtrusion(const std::vector<TileId>* coveredBy, bool offscreen, Func&& func) const;
        void renderLabels(const std::vector<std::shared_ptr<Label>>& labels);
        // One batching pass in list order; CALLOUT leader lines get their own pass first, under all glyphs.
        void renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, Label::DrawPass pass);

        float evaluateFloatFunc(const FloatFunction& func);
        Color evaluateColorFunc(const ColorFunction& func);

        void setCompOp(CompOp compOp);
        void blendScreenTexture(float opacity, GLuint texture);
        void updateTerrainSkirts();
        const std::pair<bool, TerrainTexture>& resolveTerrainTexture(const TileId& tileId) const;
        bool terrainGridSurfaces() const;
        double sphereWorldRadius() const;
        cglib::vec2<double> sphereFrameMercator(const cglib::mat4x4<double>& vertexFrameMatrix) const;
        void setupSphericalUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        bool setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix, bool gridSurface = false);
        // Tiles the surfaces are drawn from: the handed-in cover, else the visible tiles. Edge stitching
        // must follow it, or the surfaces crack at LOD rings.
        const std::set<TileId>& terrainSurfaceTileIds() const;
        // Cover tiles under a render tile: the leaves inside it, or itself where the cover is coarser.
        const std::vector<TileId>& collectGroundLeaves(const TileId& targetTileId) const;
        void updateTerrainCoverTiles();
        void buildTerrainEdgeCoarsening();
        void setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        /**
         * Patches a tile-independent CPU ground into an extrusion's vertices; a per-tile shader sample
         * tore footprints spanning two tiles. False while elevation is missing: the geometry still draws,
         * with UNRESOLVED_BASE (polygon3DVsh samples the ground per vertex).
         */
        bool resolveExtrusionBases(const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry) const;
        void buildExtrusionBaseFootprints(const std::shared_ptr<TileGeometry>& geometry, const TileGeometry::VertexGeometryLayoutParameters& params, const VertexArray<std::uint8_t>& vertexGeometry, std::size_t vertexCount) const;
        void markPendingLabelsDirty();
        std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> labelAnchorFunc() const;
        // Flags labels anchored on a span deck, whose CPU height labelVsh must keep; one chord test per
        // label, since the height func cannot report which source it used.
        void markDeckAnchoredLabels(const std::vector<std::shared_ptr<Label>>& labels) const;
        // A label's batch: its anchor tile when the GPU supplies its height, (-1,-1,-1) when already anchored.
        TileId labelBatchTileId(const std::shared_ptr<Label>& label) const;
        bool anchorDirtyLabels();
        // Deck height at a point on a resolved span, for ground anchors belonging to the bridge; false off a span.
        bool spanHeightAt(const cglib::vec2<double>& pos, double& height) const;
        void renderTileMask(const TileId& tileId);
        void renderStencilDebugOverlay();
        // Bakes one tile's DEM paint into the bound drape framebuffer; returns primitives drawn, 0 without elevation.
        int renderTerrainPaint(const TileId& targetTileId);
        // Draws the paint as the surface per covered tile. asGround: it is the ground pass (ground colour
        // base, writes depth, bottom of the stack).
        int renderTerrainPaintSurfaces(bool asGround = false);
        // The zoom-dependent relief boost of the paint, matching the normal-map path.
        float calculateTerrainPaintReliefBoost(float metersPerTexel) const;
        void renderTileSurfaceFill(const TileId& tileId, const Color& color, bool lit = false);
        void renderDrapeTextures(const std::vector<RenderTile>& renderTiles);
        int renderTileSurfaceDrape(const TileId& tileId, float uvOffsetX, float uvOffsetY, float uvScale);
        // Draws the surface, skipping the shared grid's blocks off screen when gridSurface; returns the indices drawn.
        GLsizei drawSurfaceElements(const TileId& tileId, const TileSurface& surface, bool gridSurface) const;
        // The skirts of a grid surface draw, with its program and uniforms already set up.
        GLsizei drawTerrainSkirts(const TileId& tileId, const ShaderProgram& shaderProgram);
        // (first index, count) runs of the grid's blocks that can be on screen; all of it when unknown or not culled.
        std::vector<std::pair<GLsizei, GLsizei>> visibleGridIndexRuns(const TileId& tileId, const TileSurface& gridSurface, bool culled) const;
        GLuint ensureDrapeTexture(const TileId& tileId);
        void releaseDrapeTexture(GLuint texture);
        void deleteDrapeResources();
        bool isDrapeableGeometry(const std::shared_ptr<TileGeometry>& geometry) const;
        // Builds the lit raster program before the zoom-out needing it, so the compile misses the gesture.
        void warmTerrainRasterShader();
        bool hasDrapeableContent(const RenderTileLayer& renderLayer) const;
        // Contact shadows this layer would bake into the drape (see calculateDrapeFingerprint).
        bool hasGroundAOContent(const RenderTileLayer& renderLayer) const;
        bool hasGroundAOTiles(float zoomFade) const;
        void refreshGroundAOBakeable(); // caller holds _mutex; see isGroundAOBakeable
        // Element opacity a draped layer is baked with: the style's layer opacity, or 1 when the
        // layer has a comp-op (which the bake can not reproduce).
        float calculateDrapeOpacity(const RenderTileLayer& renderLayer) const;
        bool tileCovers(const TileId& tileId, const TileId& targetTileId) const;
        bool isTileDraped(const TileId& targetTileId) const;
        cglib::mat4x4<float> calculateDrapeMVPMatrix(const TileId& sourceTileId, const TileId& targetTileId) const;
        std::size_t calculateDrapeFingerprint(const RenderTile& renderTile) const;
        void renderTileWireframe(const TileId& tileId);
        void renderTileBorder(const TileId& tileId, const TileId& sourceTileId);
        void renderTileBackground(const TileId& tileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileBackground>& background);
        void renderTileBitmap(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap);
        void renderTileGeometry(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileGeometry>& geometry);
        // Which optional blocks a geometry draw runs with. Decided once by renderTileGeometry and
        // handed to the uniform setup, so the program flags and the uniforms cannot disagree.
        struct GeometryDrawMode {
            bool flatDrape = false;
            bool sphericalDrape = false; // a bake on a sphere: the shader places the vertex in the tile
            bool terrainVTF = false;
            bool shadowReceiver = false;
            bool terrainLit = false;
            unsigned int terrainFlag = 0;
        };
        // Everything a geometry draw needs that does not depend on its type: the MVP, the terrain
        // depth bias and elevation, the shadow cascades and the style translation.
        void setupGeometryCommonUniforms(const ShaderProgram& shaderProgram, const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry, const GeometryDrawMode& mode);
        // The vertex attribute layout of one compiled geometry. Bound as a VAO where the geometry
        // has one, attribute by attribute otherwise - which is also what the unbind undoes.
        static GLuint findGeometryVAO(const CompiledGeometry& compiledGeometry, GLuint program);
        void bindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry);
        void unbindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry);
        void renderLabelBatch(const LabelBatchParameters& labelBatchParams, const std::shared_ptr<const Bitmap>& bitmap);

        const CompiledBitmap& buildCompiledBitmap(const std::shared_ptr<const Bitmap>& bitmap, bool genMipmaps);
        const CompiledBitmap& buildCompiledTileBitmap(const std::shared_ptr<TileBitmap>& tileBitmap);
        const CompiledGeometry* buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry);
        void bindSurfaceSkirtAttrib(const ShaderProgram& shaderProgram, const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams);
        // id must be a string literal: its address keys _shaderProgramCache.
        const ShaderProgram& buildShaderProgram(const char* id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags);
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledTerrainGridSurfaces();
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledTerrainGridSkirtSurfaces();
        // The shadow caster and mask pass grid: both sides of the depth compare, so coarser only costs detail.
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledTerrainShadowGridSurfaces();
        // Two triangles per tile: the flat orthographic drape bake gains nothing from the displaced grid.
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledFlatSurfaces();
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledTileSurfaces(const TileId& tileId);

        void createShaderProgram(ShaderProgram& shaderProgram, const std::string& vsh, const std::string& fsh, const std::set<std::string>& defs, const std::map<std::string, int>& uniformMap, const std::map<std::string, int>& attribMap);
        void deleteShaderProgram(ShaderProgram& shaderProgram);
        void createFrameBuffer(FrameBuffer& frameBuffer, bool useColor, bool useDepth, bool useStencil);
        void deleteFrameBuffer(FrameBuffer& frameBuffer);
        void createCompiledBitmap(CompiledBitmap& compiledBitmap);
        void deleteCompiledBitmap(CompiledBitmap& compiledBitmap);
        void createCompiledQuad(CompiledQuad& compiledQuad);
        void deleteCompiledQuad(CompiledQuad& compiledQuad);
        void createCompiledSurface(CompiledSurface& compiledSurface);
        void deleteCompiledSurface(CompiledSurface& compiledSurface);
        void createCompiledGeometry(CompiledGeometry& compiledGeometry);
        void deleteCompiledGeometry(CompiledGeometry& compiledGeometry);
        void createCompiledLabelBatch(CompiledLabelBatch& compiledLabelBatch);
        void deleteCompiledLabelBatch(CompiledLabelBatch& compiledLabelBatch);

        std::optional<LightingShader> _lightingShader2D;
        std::optional<LightingShader> _lightingShader3D;
        std::optional<LightingShader> _lightingShaderNormalMap;
        TileSurfaceBuilder _tileSurfaceBuilder;

        FrameBuffer _overlayBuffer2D;
        FrameBuffer _overlayBuffer3D;
        CompiledQuad _screenQuad;

        ViewState _viewState;
        cglib::mat4x4<double> _cameraProjMatrix = cglib::mat4x4<double>::identity();
        float _fullResolution = 0;
        float _halfResolution = 0;
        float _lineAntialiasScale = 1.0f; // device pixels per line-width unit (see lineFsh)
        int _screenWidth = 0;
        int _screenHeight = 0;
        cglib::vec3<double> _tileSurfaceBuilderOrigin = cglib::vec3<double>(0, 0, 0);
        std::set<TileId> _tileSurfaceBuilderOriginTileIds;

        bool _interactionMode = false;
        bool _terrainMode = false;
        bool _terrainDepthWrite = false;
        float _terrainDepthBias = 0.0f;
        float _terrainContentDepthShift = 0.0f; // tangram-style constant-clip pull of content towards the viewer
        float _terrainSlackScale = 1.0f;         // scales the clip-constant slack; ~(32/meshResolution)^2 - the chord error shrinks quadratically with the tesselation
        float _terrainDrawDepthBias = 0.0f;      // per-draw NDC (w-scaled) depth bias while rendering 2D layers (GPU draping mode)
        float _terrainDrawDepthClipUnits = 0.0f; // per-draw clip-constant slack units (distance-growing; see setupTerrainUniforms)
        bool _terrainSkirtsEnabled = false;
        bool _terrainRegularGrid = false;        // tangram's model: one shared grid surface per tile + painter-order depth (no occluder, no slack)
        int _terrainRegularGridResolution = 0;   // resolution of the currently built shared grid
        bool _terrainEdgeStitching = false;      // snap grid surface edges to a coarser neighbour's lattice
        std::set<TileId> _visibleTileIds;        // this renderer's own visible tiles (surface cover when no external one is set)
        std::set<TileId> _terrainCoverTileIds;   // the cover the surfaces are actually drawn from (drape cover / paint cover)
        std::map<TileId, cglib::vec4<float>> _terrainEdgeCoarseningMap; // per drawn cover tile: lattice cell scale (2^k) on the west/east/south/north edge
        std::vector<std::shared_ptr<TileSurface>> _terrainGridSurfaces;
        std::vector<std::shared_ptr<TileSurface>> _terrainGridSkirtSurfaces;
        std::map<TileId, cglib::vec4<float>> _terrainSkirtDropMap; // see setTerrainSkirtDrops
        std::vector<std::shared_ptr<TileSurface>> _terrainShadowGridSurfaces; // at most SHADOW_GRID_MAX_RESOLUTION
        int _terrainShadowGridResolution = 0;
        std::vector<std::shared_ptr<TileSurface>> _terrainFlatSurfaces; // 1x1 grid for the flat drape bake
        float _terrainDrawLayerOffset = 0.0f;    // painter-order per-draw (proxy - layer) offset
        float _terrainLineClearance = 0.0f;      // world units a draped line clears the ground by, constant in metres at any range
        float _terrainDrawClearance = 0.0f;      // per-draw METRE-constant clearance in world units (applyDepthBias); non-zero only for content that chords over the ground
        DrawOncePass _drawOncePass = DrawOncePass::NONE; // the draw-once pass the 2D layer being drawn is in
        static constexpr GLuint DRAW_ONCE_STENCIL_BIT = 0x80; // tile mask values stay below it
        int _terrainLayerOrdinalBase = 0;        // first style-layer ordinal of this renderer in the stack
        std::set<int> _terrainStyleLayerIndices; // every style layer index this renderer has drawn - the stable order list
        int _terrainStyleLayersDrawn = 0;        // size of the order list above (the owner's dense numbering)
        bool _terrainDrapeFills = false;         // maplibre-style: bake polygon fills flat to a per-tile texture, sampled on the surface
        bool _terrainDrapeLines = false;         // also bake vt tile lines into the drape texture (softer, but zero leak/hug error)
        SpanResolver _spanResolver;              // 3D bridges: unions, chords, bases (setSpansEnabled)
        int _drapeTextureSize = 512;             // per-tile drape texture resolution
        GLuint _drapeFBO = 0;                    // shared offscreen FBO for baking drape textures
        GLuint _drapeStencilRB = 0;              // draw-once stencil for a bake, made on the first group
        cglib::vec2<int> _drapeStencilSize = cglib::vec2<int>(0, 0);
        std::map<TileId, GLuint> _drapeTextures; // per-target-tile baked drape textures
        std::map<TileId, std::size_t> _drapeFingerprints; // what each cached texture was baked from; a change means it is stale
        std::vector<GLuint> _drapeTexturePool;   // recycled textures, so panning does not churn GL allocations
        std::vector<GLuint> _drapeStaleTextures; // wrong-size textures awaiting deletion on the GL thread
        int _tileMasks = -1;                     // -1 automatic, 0 never, 1 always (see setTileMasks)
        bool _terrainSharedGround = false;       // the owner draws one ground pass for the whole layer stack
        Color _terrainGroundColor;               // what the ground pass painted; a background repeating it is skipped
        std::vector<TileId> _terrainGroundTiles; // the shared ground cover, in the owner's order
        std::vector<int> _terrainGroundProxyDepths; // per ground tile: levels coarser than asked for
        mutable std::unordered_map<TileId, std::vector<TileId>> _groundLeafCache; // render tile -> its cover leaves
        bool _externalDrapeTarget = false;       // drape textures are owned by the caller (cross-layer stacks)
        std::set<TileId> _drapeTilesThisFrame;   // target tiles that have a valid drape texture this frame
        std::vector<TileId> _externalDrapeTiles; // terrain tiles the owner drapes this frame
        bool _drapeCoveragePass = false;         // set only while a coverage mask is being baked
        int _drapeCoverageFromLayerIdx = 0;      // ... and the style layer that bake starts at
        std::vector<std::map<TileId, GLuint> > _drapeCoverageMasks; // per mask index, per drape tile
        std::map<int, int> _drapeCoverageLayerMasks;                // live style layer -> mask index
        GLuint _drapeMaskTexture = 0;            // the mask the geometry being drawn is occluded by
        cglib::vec4<float> _drapeMaskUVTransform; // ... and target-tile units -> that mask's units
        const cglib::mat4x4<double>* _shadowCasterViewProj = nullptr; // set during the shadow caster pass
        const cglib::mat4x4<float>* _drapeMVPOverride = nullptr; // when set, renderTileGeometry draws flat into the drape FBO
        std::array<GLfloat, 9> _sphereLightingFrame = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; // world -> the view's east/north/up
        bool _debugWireframe = false;
        bool _debugTileBorders = false;
        GLuint _tileBorderVBO = 0;               // the tile outline, in tile-local coordinates
        GLsizei _tileBorderVertexCount = 0;
        static constexpr int TILE_BORDER_SEGMENTS = 16; // per edge, so the line follows the terrain
        bool _debugSurfacePrefill = false;
        TerrainLighting _terrainLighting;
        // Below this zoom a contact shadow is a sub-pixel rim; groundAOZoomFade ramps it over one level.
        static constexpr float GROUND_AO_MIN_ZOOM = 16.0f;
        // Style scale x fade-in when growing on appear. The sun caster skips the view scale but keeps the
        // style's (no building, no shadow); a span deck takes neither.
        float buildingHeightScale(float blend, bool span = false) const {
            if (span) {
                return 1.0f;
            }
            return _shadowCasterSun ? casterHeightScale(blend) : casterHeightScale(blend) * _buildingHeightViewScale;
        }
        // The height the shadow MAP holds, which is what a receiver must look its own depth up at.
        float casterHeightScale(float blend, bool span = false) const {
            if (span) {
                return 1.0f;
            }
            return _buildingHeightScale * (_buildingGrowOnAppear && !_shadowCasterViewProj ? blend : 1.0f);
        }

        float _buildingHeightScale = 1.0f;
        float _buildingHeightViewScale = 1.0f;
        bool _shadowCasterSun = false; // set only while the sun's shadow map is being baked
        bool _buildingGrowOnAppear = false;
        bool _buildingFadeOnAppear = false;
        float _groundAOIntensity = 0.0f;
        // Read lock-free, per drape layer per frame.
        std::atomic<bool> _groundAOBakeable { false };
        float _groundAOAttenuation = 0.69f;
        bool _groundAOMaskPass = false; // set only while the mask is being drawn
        // Anchor depths meet the half-resolution buffer within rounding: mapbox's offset, and a ramp to
        // fade rather than switch.
        // Whole-label fade behind extrusions, ray-tested on the CPU against their meshes (06-labels.mdx).
        float calculateLabelVisibility(const Label& label);
        struct FrameOccluder {
            const ExtrusionOccluder* occluder;
            const TileGeometry* geometry;
            cglib::vec3<double> origin; // tile frame: world = origin + (x, y) * scale, z world
            double scale;
            double heightScale;         // world z per height unit, with the building's grow-in
            cglib::bbox3<double> bounds;
        };
        static constexpr float LABEL_OCCLUSION_SIZE_PIXELS = 30.0f; // the square sampled around an anchor
        // Where a ray stops short of its target: a label on a roof is not hidden by that roof.
        static constexpr float LABEL_OCCLUSION_MARGIN_METERS = 1.0f;
        // Past this tilt (90 = straight down) no building stands between the eye and a label: no rays cast.
        static constexpr float LABEL_OCCLUSION_MAX_TILT = 80.0f;
        float _labelOcclusionOpacity = 1.0f;  // what an occluded label keeps; 1 = no occlusion
        std::vector<FrameOccluder> _frameOccluders;
        bool _frameOccludersValid = false;
        // What a z-elevated label stands on (mapbox symbol-z-elevate): every loaded roof at full growth.
        struct RoofSurface {
            std::shared_ptr<const TileGeometry> geometry;
            cglib::vec3<double> origin;
            double scale;
            double heightScale;
            cglib::bbox3<double> bounds;
        };
        std::vector<RoofSurface> _roofSurfaces;
        std::size_t _roofSignature = 0;
        // Dirties the z-elevated labels when the roofs moved; false when nothing changed.
        bool refreshRoofSurfaces();
        // Under the lock only: a roof reads the extrusion bases the render thread resolves.
        std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> roofAnchorFunc(std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> anchorFunc) const;
        std::optional<double> roofHeightAt(const cglib::vec3<double>& pos) const;
        bool _groundAOBakePass = false; // set only while the ground AO mask is a drape bake
        TerrainPaint _terrainPaint;
        bool _terrainPaintOnGround = false;      // the paint replaces the ground fill (see setTerrainPaintOnGround)
        int _terrainDemTaps = 16;                // texture fetches per terrain vertex (see setTerrainDemTaps)
        bool _terrainTileBackgrounds = false;    // per-layer per-tile background meshes (see setTerrainTileBackgrounds)
        std::vector<TileId> _terrainPaintTiles; // what a paint covers when it draws itself
        GLuint _terrainShadowTexture = 0;
        int _terrainShadowMapSize = 0;
        // Metres -> world z units from the projection, so a map without elevation still gets shadows.
        double _metersToInternal = 0;
        cglib::vec3<float> _radiance = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
        float _backgroundEmissive = 1.0f;
        float _buildingEmissive = 0.0f; // the Map block's; a rule may override it per draw
        int _terrainShadowCascades = 1;
        // mapbox's u_shadow_bias: constant, slope scale, slope cap - normalised depth, all cascades.
        cglib::vec3<float> _terrainShadowBias = cglib::vec3<float>(0.0f, 0.0f, 0.0f);
        // 1 / each cascade's light-box depth in metres: the bias above is metric, the shader compares
        // in normalised depth, and every box normalises its own.
        std::array<float, MAX_SHADOW_CASCADES> _terrainShadowDepthScales = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        GLuint _terrainShadowMaskTexture = 0;
        cglib::vec2<float> _terrainShadowMaskScale = cglib::vec2<float>(0.0f, 0.0f);
        bool _terrainShadowMaskPass = false; // the draw that produces the mask, not one that reads it
        float _terrainShadowStrength = 0.0f;
        float _terrainShadowSoftness = 1.0f;
        bool _terrainShadowDepthTexture = false; // the map is the depth buffer, not a packed copy
        bool _terrainShadowHardwarePCF = false;  // ... and it is sampled through a comparison sampler
        int _shadowCastersMissingElevation = 0;
        mutable bool _essl3Failed = false;       // an ESSL 3.00 program did not build; see hasShaderVersionFallback
        float _terrainShadowNormalOffset = 3.0f; // in shadow-map texels; mapbox's default
        // View depth the outermost cascade fades out over, in internal units. Zero = no fade.
        cglib::vec2<float> _terrainShadowFadeRange = cglib::vec2<float>(0.0f, 0.0f);
        cglib::vec3<float> _terrainShadowSunDir = cglib::vec3<float>(0.0f, 0.0f, 1.0f);
        unsigned int _warmedRasterShaderFlags = 0; // flag set warmTerrainRasterShader last built for (0 = none)
        Color _fogColor;
        Color _fogHighColor;
        Color _fogSpaceColor;
        float _fogStartDistance = 0.0f; // range units, i.e. multiples of _fogRangeScale
        float _fogDistance = 0.0f;
        float _fogRangeScale = 1.0f;    // world units per range unit
        float _fogHorizonBlend = 0.0005f;
        float _fogVerticalStart = 0.0f; // metres
        float _fogVerticalEnd = 0.0f;
        float _fogMetersPerUnit = 1.0f;
        float _fogCameraHeight = 0.0f;  // metres
        cglib::mat3x3<float> _fogRayBasis = cglib::mat3x3<float>::identity();
        std::string _fogShaderSource;
        std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES> _terrainShadowViewProjs;
        Color _terrainBackgroundColor; // opaque terrain base fill + depth pre-pass color; transparent = depth-only
        std::vector<std::pair<TileId, GLint>> _debugOrderedTileMasks;
        TerrainTextureProvider _terrainTextureProvider;
        std::function<double(const cglib::vec3<double>&, int)> _labelElevationProvider;
        std::function<bool(const cglib::vec3<double>&, int, bool, double&)> _extrusionElevationProvider;
        std::atomic<unsigned int> _extrusionBaseVersion { 1 }; // bumped by invalidateExtrusionBases
        std::vector<TileId> _pendingLabelElevationTiles; // elevation tiles whose labels must be re-anchored
        double _labelPositionScale = 1.0; // label anchors are in internal coordinates, not vt's
        std::vector<TileId> _pendingExtrusionBaseTiles;  // ...and whose extrusion bases must be re-resolved
        bool _pendingLabelElevationAll = false;
        unsigned int _labelElevationGeneration = 1; // bumped by each whole-set invalidation; see Label::getElevationGeneration
        std::function<bool(const cglib::vec3<double>&)> _labelOcclusionTest;
        float _layerBlendingSpeed = 1.0f;
        float _labelBlendingSpeed = DEFAULT_LABEL_BLENDING_SPEED;
        RasterFilterMode _rasterFilterMode = RasterFilterMode::BILINEAR;
        std::optional<std::regex> _rendererLayerFilter;
        std::optional<std::regex> _noDrapeLayerFilter;
        mutable std::unordered_map<std::string, bool> _noDrapeLayerCache; // regex_match per call was a frame cost (performance-log.md, 32)
        std::optional<std::pair<int, int>> _rendererLayerIndexRange;
        std::optional<std::regex> _clickHandlerLayerFilter;

        std::shared_ptr<std::vector<RenderTile>> _renderTiles;
        std::shared_ptr<std::vector<RenderTile>> _visibleRenderTiles;
        std::array<std::shared_ptr<PassLabels>, 2> _passLabels; // for 'ground' labels and for 'billboard' labels
        std::array<std::shared_ptr<PassLabels>, 2> _visiblePassLabels;  // for 'ground' labels and for 'billboard' labels
        std::vector<std::shared_ptr<Label>> _labels;
        std::atomic<bool> _snapLabelTransition = false; // commit the next placement without a fade
        std::size_t _labelCullCursor = 0; // how far the current placement cycle got through _labels
        std::atomic<bool> _labelsReanchored { false }; // see consumeLabelsReanchored
        int _resourceSweepCounter = 0;
        std::map<int, GlobalIdLabelMap> _layerLabelMap;
        // The label tile set the maps were last built from; prepareLabelMaps depends on nothing else.
        std::optional<long long> _labelTilesSignature; // none = rebuild: 0 is the empty set's signature
        unsigned int _labelMapGeneration = 0; // bumped when the live maps are dropped under us
        mutable std::size_t _lastDrapeGlobalTerm = 0; // diagnostic only: see collectDrapeTiles
        std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceMap;
        GLuint _lastUsedProgram = 0; // currently bound program, 0 = unknown (see useProgram)
        std::map<std::string, ShaderProgram> _shaderProgramMap;
        // Allocation-free per-draw front cache for the above, keyed on the call site's literal pointer and flags.
        struct ShaderProgramKey {
            const char* id = nullptr;
            unsigned int flags = 0;
            int lightingMode = 0;
            int filterMode = 0;

            bool operator == (const ShaderProgramKey& other) const {
                return id == other.id && flags == other.flags && lightingMode == other.lightingMode && filterMode == other.filterMode;
            }
        };
        struct ShaderProgramKeyHash {
            std::size_t operator () (const ShaderProgramKey& key) const {
                std::size_t hash = std::hash<const void*>()(key.id);
                hash ^= key.flags + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= static_cast<unsigned int>(key.lightingMode * 31 + key.filterMode) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };
        std::unordered_map<ShaderProgramKey, const ShaderProgram*, ShaderProgramKeyHash> _shaderProgramCache;

        // Memo for view-state-only style functions; the entry holds its function, or a dead one's address
        // could be reused.
        std::unordered_map<const void*, std::pair<FloatFunction::Function, float>> _floatFuncCache;
        std::unordered_map<const void*, std::pair<ColorFunction::Function, Color>> _colorFuncCache;

        // Per-view-state tile matrix memo, as the style-layer-major draw loop repeats each tile per layer.
        // Cleared with the style function memo.
        struct TileMatrixKey {
            TileId tileId { 0, 0, 0 };
            float coordScale = 0;

            bool operator == (const TileMatrixKey& other) const {
                return tileId == other.tileId && coordScale == other.coordScale;
            }
        };
        struct TileMatrixKeyHash {
            std::size_t operator () (const TileMatrixKey& key) const {
                std::size_t hash = std::hash<TileId>()(key.tileId);
                hash ^= std::hash<float>()(key.coordScale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };
        // Elevation texture per tile, resolved once per frame; setupTerrainUniforms runs per draw.
        mutable std::unordered_map<TileId, std::pair<bool, TerrainTexture>> _terrainTextureCache;

        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<double>, TileMatrixKeyHash> _tileMatrixCache;
        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<float>, TileMatrixKeyHash> _tileMVPMatrixCache;

        // Keyed on the raw pointer: a weak_ptr/owner_less lookup costs atomics and a tree walk on every
        // draw. The weak_ptr rides in the value.
        struct OwnedCompiledGeometry {
            std::weak_ptr<const TileGeometry> owner;
            CompiledGeometry geometry;
        };
        std::unordered_map<const TileGeometry*, OwnedCompiledGeometry> _compiledTileGeometryMap;
        std::map<std::weak_ptr<const Bitmap>, CompiledBitmap, std::owner_less<std::weak_ptr<const Bitmap>>> _compiledBitmapMap;
        std::map<std::weak_ptr<const TileBitmap>, CompiledBitmap, std::owner_less<std::weak_ptr<const TileBitmap>>> _compiledTileBitmapMap;
        std::map<std::weak_ptr<const TileSurface>, CompiledSurface, std::owner_less<std::weak_ptr<const TileSurface>>> _compiledTileSurfaceMap;
        std::map<int, CompiledLabelBatch> _compiledLabelBatches;
        int _labelBatchCounter = 0;

        VertexArray<cglib::vec3<float>> _labelVertices;
        VertexArray<cglib::vec3<float>> _labelOffsets;
        VertexArray<cglib::vec3<float>> _labelNormals;
        VertexArray<cglib::vec2<std::int16_t>> _labelTexCoords;
        VertexArray<cglib::vec4<std::int8_t>> _labelAttribs;
        VertexArray<std::uint16_t> _labelIndices;

        const std::shared_ptr<GLExtensions> _glExtensions;
        std::shared_ptr<const TileTransformer> _transformer;
        const float _scale;

        mutable std::mutex _mutex;
    };
}

#endif
