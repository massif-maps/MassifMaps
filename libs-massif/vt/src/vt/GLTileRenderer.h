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
#include "Styles.h"
#include "Tile.h"
#include "TileId.h"
#include "SpanResolver.h"
#include "TileTransformer.h"
#include "TileBitmap.h"
#include "TileBackground.h"
#include "TileBitmap.h"
#include "TileSurface.h"
#include "TileSurfaceBuilder.h"
#include "GLExtensions.h"

#include <memory>
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
#include <mutex>

#include <cglib/ray.h>

namespace massif::vt {
    class LabelCuller;

    class GLTileRenderer final {
    public:
        // Shadow cascades: pages of one shadow texture, near first. The count is a uniform, not a
        // shader define, so a change does not recompile every terrain program - but the shader
        // declares this many matrices and varyings, so raising it means touching the shader too.
        static constexpr int MAX_SHADOW_CASCADES = 4;
        // How far shadows reach, as a multiple of the camera-to-focus distance. mapbox's model
        // verbatim (3d-style/render/shadow_renderer.ts: cameraToCenterDistance * 1.5 * 3.0). Public
        // because the outer cascade's fade range is derived from the same number.
        static constexpr double SHADOW_CUTOUT_DISTANCE_FACTOR = 4.5;
        // Fraction of that distance the outer cascade starts fading at - mapbox's
        // u_shadow_fade_range = [far * 0.75, far].
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
         * A GL elevation texture covering a tile, for GPU terrain draping: in terrain mode
         * every draped vertex replaces its z with the height sampled from this texture in
         * the vertex shader (requires vertex texture fetch support). The texture may cover
         * an ancestor tile (overzoom); the internal bounds define the world rectangle
         * mapped to the [0,1]x[0,1] uv range, with v growing towards north (up).
         * Height in meters = dot(RGBA texture sample (normalized to [0,1]), decode).
         */
        struct TerrainTexture {
            GLuint textureId = 0;
            cglib::vec2<int> textureSize = cglib::vec2<int>(0, 0);          // texture dimensions in texels (for the shader-side bilinear filter)
            cglib::vec2<double> internalOrigin = cglib::vec2<double>(0, 0); // world position of uv (0,0)
            cglib::vec2<double> internalSize = cglib::vec2<double>(0, 0);   // world size covered by uv [0,1]
            cglib::vec4<float> decode = cglib::vec4<float>(0, 0, 0, 0);     // texture sample -> meters (linear part)
            float decodeOffset = 0.0f;                                      // ... plus this constant
            float metersToInternal = 0.0f; // meters -> world z units at the equator (exaggeration included)
            float mercatorYScale = 0.0f;   // world y -> mercator angle (for the per-vertex 1/cos(latitude) factor)
            float metersPerTexel = 0.0f;   // ground meters per texel at the equator (the 1/cos(latitude) stretch is per fragment)
            // The NODE texture: the same DEM box-filtered to the surface lattice, one texel per mesh
            // node, which is what the VERTEX stage displaces from. 0 when the provider has none, and
            // the vertex stage then samples the full texture, aliasing relief finer than a cell.
            GLuint nodeTextureId = 0;
            cglib::vec2<int> nodeTextureSize = cglib::vec2<int>(0, 0);
            cglib::vec2<double> nodeOrigin = cglib::vec2<double>(0, 0); // world position of node uv (0,0)
            cglib::vec2<double> nodeSize = cglib::vec2<double>(0, 0);   // world size covered by node uv [0,1]
        };

        using TerrainTextureProvider = std::function<bool(const TileId&, TerrainTexture&)>;

        /**
         * Directional lighting applied to the draped terrain surface. The surface is the only
         * lit ground geometry in the scene once every 2D layer is baked into the drape texture,
         * so this one struct replaces per-style lighting and the pre-baked hillshade raster.
         * sunDir is a unit vector in the tile frame: x east, y north, z up.
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
         * DEM-derived paint drawn from the shared terrain elevation texture instead of from a
         * tile set of its own: one quad per draped tile, no tiles fetched, decoded or uploaded,
         * no normal map and no surface pass. A renderer in paint mode holds no tiles at all -
         * it bakes the paint into the shared drape texture at its own position in the layer
         * order, so the style's placement of the hillshade is preserved exactly.
         */
        struct TerrainPaint {
            bool enabled = false;
            float heightScale = 1.0f;       // relief scale, as HillshadeRasterTileLayer defines it
            bool exaggerateHeightScale = true; // MapLibre's low-zoom relief boost
            bool legacyHeightScale = false;    // pre-MapLibre-parity formula
            float contrast = 0.5f;          // MapLibre 'hillshade-exaggeration', fed to the lighting shader
            float opacity = 1.0f;
            // Hash of everything the paint's appearance depends on, INCLUDING what only the injected
            // lighting shader sees. The renderer cannot derive it, and without it a parameter change
            // leaves every already-baked drape texture in place.
            std::size_t fingerprint = 0;
        };

        explicit GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, float scale);

        void setLightingShader2D(const std::optional<LightingShader>& lightingShader2D);
        void setLightingShader3D(const std::optional<LightingShader>& lightingShader3D);
        void setLightingShaderNormalMap(const std::optional<LightingShader>& lightingShaderNormalMap);
        
        void setInteractionMode(bool enabled);
        void setTerrainMode(bool enabled, float depthBias);
        void setTerrainRegularGrid(bool enabled, int resolution);
        // Where this renderer's style layers start in the stack's depth ordering (see
        // renderGeometry2D). Every tile layer has its own renderer and its own style layers, so
        // without a base they would all claim ordinal 0 and fight each other once content writes.
        void setTerrainLayerOrdinalBase(int base);
        // How many style layers this renderer drew last frame, so the owner can number the stack
        // DENSELY: the ordinal feeds a constant-NDC pull whose eye tolerance grows as distance^2, and
        // a fixed stride per renderer inflates the total into the range that sees through ridges.
        int getStyleLayerCount() const;
        // Cross-LOD edge stitching for the shared regular grid surfaces: on an edge shared with a
        // coarser neighbour the surface follows that neighbour's lattice, so the two agree instead of
        // cracking open. No effect without the regular grid mode.
        void setTerrainEdgeStitching(bool enabled);
        void setTerrainSlackScale(float slackScale);
        /**
         * Tangram's content depth shift: a CONSTANT clip-space pull of drawn content towards the
         * viewer (tangram-ng polygon.vs, 'depth_shift = -0.02*u_proj[2][3]'). Unlike a constant-NDC
         * bias its effect falls off as 1/w, so it separates content from the surface near the
         * camera - where an un-subdivided segment chords furthest below it - without opening a
         * see-through band at range. 0 (the default) keeps content exactly on the surface, which
         * is only correct while geometry is subdivided to follow it.
         */
        void setTerrainContentDepthShift(float depthShift);
        // Clearance a draped LINE is drawn in front of the ground with, constant in METRES at every
        // distance (see applyDepthBias): a line chords over the relief by a fixed number of metres,
        // which a clip- or ndc-constant bias cannot pay for without leaking through ridges.
        void setTerrainLineClearance(float clearance);
        void setTerrainDrapeFills(bool enabled, bool includeLines);
        // Whether span features (bridges, tunnels) stand on their chord. Off by default: a span is
        // draped like the ground, no deck is drawn, and nothing below runs - a map that never turns it
        // on pays only the empty-records test every geometry already made.
        void setSpansEnabled(bool enabled);
        void setTerrainDrapeResolution(int resolution);
        void setTerrainLighting(const TerrainLighting& lighting);
        // The contact shadow extrusions cast on the ground (POLYGON3DGROUND): how dark it goes
        // against the wall, and the falloff curve out to the skirt's edge.
        void setGroundAO(float intensity, float attenuation);
        /**
         * Every extrusion's height, multiplied, and whether a tile's fade-in also RAISES it.
         *
         * `scale` is the style's own (mapbox's fill-extrusion-vertical-scale). `growOnAppear` is
         * the renderer's: the walls used to be scaled by the tile blend, so every building rose out
         * of the ground each time its tile faded in - which no source style asks for, and which is
         * a timed animation rather than the zoom ramp a style would write.
         *
         * `fadeOnAppear` is the same question for the COLOUR, and is OFF by default: an extrusion
         * that fades in is transparent for the length of the fade, and its own shadow - cast at
         * full strength from the first frame - is then plainly visible THROUGH its walls. gl-js has
         * no timed fade for an extrusion either. A style that wants one turns it back on.
         */
        void setBuildingHeight(float scale, float viewScale, bool growOnAppear, bool fadeOnAppear);
        // 0 below the minimum zoom, ramping to 1 one level above it.
        static float groundAOZoomFade(float zoom);
        // Whether the contact shadows would draw anything at all this frame (intensity and zoom).
        bool isGroundAOActive() const;
        // The same, for the drape bake, which applies no zoom fade - see the body.
        bool isGroundAOBakeable() const;
        // Draws every visible contact-shadow quad into the bound framebuffer under MIN blending,
        // resolving their overlaps into one mask. Returns the number of geometries drawn.
        int renderGroundAOMask();
        // Per-label occlusion against a screen depth texture of the 3D occluders, from the same camera
        // (mapbox's model: the whole label fades, never clipped). Texture 0 turns it off; occluderSize
        // is the square sampled around the anchor, in screen pixels.
        void setLabelOcclusionDepth(unsigned int depthTexture, float occluderSize);
        // What an occluded label keeps, this layer's own default. A style LAYER may set its own
        // (TileLabel::Style::occlusionOpacity), which wins for its labels. 1 = no occlusion.
        void setLabelOcclusionOpacity(float occludedOpacity);
        // True when some style layer asks for occlusion even though the default does not.
        bool hasStyledLabelOcclusion() const;
        // Draws every visible extrusion into the bound depth target from the camera. The ground is
        // NOT drawn: labels are already tested against the terrain on the CPU, per label
        // (TileRenderer::setLabelOcclusionTest). Returns the number of geometries drawn.
        int renderLabelOcclusionDepth();

        // The same capsules resolved in ONE DRAPE TILE's frame, into the bound target. Baked into
        // the ground, the shadow follows the terrain exactly. Changes no GL state - see the body.
        int bakeGroundAOMask(const TileId& targetTileId);
        // Turns this renderer into a paint baker (see TerrainPaint): it draws the DEM-derived
        // paint for every draped tile and nothing else. Only effective under a cross-layer drape
        // target, which is where the layer order is resolved.
        void setTerrainPaint(const TerrainPaint& paint);
        // Draw the paint AS the ground (tangram's arrangement) instead of as its layer's own surface
        // over it: cheaper by one full-surface draw per tile, but it puts the shading UNDER every
        // ground-shaped fill, so it only looks right when nothing ground-shaped is below it.
        void setTerrainPaintOnGround(bool enabled);
        // Texture fetches per terrain vertex: 16 = the lattice clamp, 4 = the manual bilinear alone,
        // 1 = a single hardware-filtered fetch, as tangram's terrain vertex does. The first thing to
        // measure when the frame sits in the swap wait.
        void setTerrainDemTaps(int taps);
        // Keep drawing a per-tile background mesh per LAYER under a shared ground. Tangram has no
        // such thing - its map background is the framebuffer clear colour, once per frame - so this
        // is off by default and exists to measure what those draws cost.
        void setTerrainTileBackgrounds(bool enabled);
        // The stencil tile masks that clip content to its own tile's footprint: -1 = automatic (off in
        // a terrain frame, where a mask is a full displaced grid per tile; kept in 2D and for a comp-op
        // layer, which has no other clip), 0 = never, 1 = always. Tangram has no stencil at all.
        void setTileMasks(int mode);
        // The terrain tiles a paint covers when it draws itself (no drape to bake into). A paint
        // has no tile set, so the owner hands it the terrain's own cover.
        void setTerrainPaintTiles(const std::vector<TileId>& tileIds);
        // Distance fog over the whole scene: everything fades towards this colour between the two
        // distances, and a transparent colour or a zero range turns it off. rangeScale is how many
        // world units one range unit is; horizonBlend is the angular term the SKY takes too.
        void setFog(const Color& color, float startDistance, float distance, float rangeScale, float horizonBlend);
        // The atmosphere colours a custom fog shader can reach (Mapbox high-color / space-color).
        // The built-in blend ignores them; they are here so one shader source works everywhere.
        void setFogColors(const Color& highColor, const Color& spaceColor);
        // Mapbox vertical-range: the fog fades out between two altitudes in metres, so a summit
        // stands clear of a haze filling the valley. Equal values disable the fade.
        void setFogVertical(float startMeters, float endMeters, float metersPerUnit, float cameraHeightMeters);
        // The view ray basis (FogShader::rayBasis): rayVec = uFogRay * vec3(gl_FragCoord.xy, 1).
        // Changes with the camera, so the owner sets it every frame.
        void setFogRayBasis(const cglib::mat3x3<float>& rayBasis);
        // Replaces the WHOLE fog block with application GLSL defining applyFog() and skyFog().
        // Rebuilds every program, so it acts only on a real change - callers pass the current source
        // every frame.
        void setFogShaderSource(const std::string& shaderSource);
        // Directional shadows: the owner renders the caster pass into its own framebuffer from the
        // light, then hands the packed-depth texture and the same light matrix back here.

        // True once an ESSL 3.00 program failed to build and its 1.00 fallback was used. Sticky, so
        // the owner logs it once rather than every frame.
        bool hasShaderVersionFallback() const { return _essl3Failed; }
        // Caster tiles skipped because their elevation was not loaded - they would otherwise be
        // drawn as a flat sea-level plane. Tells a missing shadow caused by a tile that was never
        // asked for apart from one clipped by the light box. Reading it clears it.
        int consumeShadowCastersMissingElevation() { int n = _shadowCastersMissingElevation; _shadowCastersMissingElevation = 0; return n; }
        // What the scene light does to a flat, upward-facing surface, in sRGB - mapbox's ground
        // radiance. Only a colour whose emissive is below 1 is multiplied by it.
        void setRadiance(const cglib::vec3<float>& radiance) { _radiance = radiance; }
        // How much of the map background's colour is emitted rather than lit. It is a Map setting,
        // so it is one value for the whole style rather than a per-geometry function.
        void setBackgroundEmissive(float emissive) { _backgroundEmissive = emissive; }
        // The Map block's building-emissive, which an EXTRUSION takes unless its own rule states one.
        // Held here so the draw can fall back to it: a rule that overrides u_emissive has to put the
        // map's value back for the next draw that does not.
        void setBuildingEmissive(float emissive) { _buildingEmissive = emissive; }
        // The projection's metres-to-internal factor, so shadows do not need a DEM to be fitted.
        void setMetersToInternal(double metersToInternal) { _metersToInternal = metersToInternal; _spanResolver.setMetersToInternal(metersToInternal); }
        void setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, MAX_SHADOW_CASCADES>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs);
        // Light-space view-projection fitted to the given terrain tiles; false if the set is empty or no
        // elevation is loaded. minHeight/maxHeight bound the shadowed volume - a generous slab is what
        // makes a low sun pixelated. One call per cascade, each covering its own distance slice.
        bool calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        // The terrain's shadow resolved once per screen pixel, into a half-resolution mask the
        // surface draws then sample by screen position instead of computing it each time.
        void setTerrainShadowMask(GLuint texture, float invScreenWidth, float invScreenHeight);
        // Draws the mask for the given tiles into the bound framebuffer. Returns the draw count.
        int renderTerrainShadowMask(const std::vector<TileId>& tileIds);
        // Moves as the caster geometry does: 3D extrusions fade in by growing, so the sum of
        // their blend factors says how far the shadow map has drifted from what is on screen.
        float shadowCasterFadeSignature(const std::vector<TileId>* coveredBy) const;
        // Draws this renderer's shadow casters for one terrain tile into the bound framebuffer.
        // Returns the number of draws issued.
        int renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);

        // Shared terrain ground (tangram's model, what replaces the RTT drape): every renderer gets the
        // SAME cover and the ground is drawn once per frame, so a renderer with a cover set establishes
        // no depth domain of its own. Ground-shaped content goes on the COVER tiles, not its own.

        // proxyDepths, parallel to tileIds: levels coarser than asked for (0 = its own). A coarse
        // stand-in is a different height field and pokes through the content drawn above it, so
        // tangram multiplies the term by 48 for the terrain raster.
        void setTerrainGroundTiles(const std::vector<TileId>& tileIds, const std::vector<int>& proxyDepths);
        // Draws the shared ground for the cover: the displaced grid surface per tile in the given
        // colour, writing depth at its TRUE depth. The only depth-writing terrain geometry in the
        // frame - everything drawn afterwards tests against it and never writes. Returns the draws.
        int renderTerrainGround(const Color& color);
        void setExternalDrapeTarget(bool enabled);
        // The terrain tiles the owner drapes and draws this frame. Content covered by them must
        // NOT be drawn again as displaced 3D geometry - it is already in the drape texture, and
        // redrawing it brings back the depth-writing tile background that hides the fills.
        void setExternalDrapeTiles(const std::vector<TileId>& tileIds);
        // Target tiles this renderer would drape this frame, each with a fingerprint of the
        // content that would be baked (so the owner can detect a stale texture).
        void collectDrapeTiles(std::map<TileId, std::size_t>& drapeTiles) const;
        // Bakes this renderer's drapeable content for one target tile into the bound framebuffer. Does
        // not clear - the owner clears once per tile, so later layers composite over earlier ones.
        // Returns the primitives drawn, so "nothing to bake" is distinguishable from "bake did nothing".
        int bakeDrapeTile(const TileId& targetTileId);
        // The DECK's own drape: the span content of this tile and nothing else, so a bridge's road
        // lands on the deck carrying it instead of on the valley floor beside it. Exact complement
        // of bakeDrapeTile, which excludes spans by construction.
        int bakeSpanDrapeTile(const TileId& targetTileId);
        // The tiles that carry a bridge or a tunnel, with a fingerprint of what would be baked.
        // Empty for a map with no spans, which is what keeps this free for everyone else.
        void collectSpanDrapeTiles(std::map<TileId, std::size_t>& spanTiles) const;
        // The cut ends of the span pieces that could not be given a chord, each stepped just past the
        // cut. The owner fetches the coarser tile each point lands in, unseen, so its piece can resolve
        // and lend its chord - otherwise a bridge stays draped until its abutments scroll into view.
        void collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const;
        // The baked span drape per tile, handed back by the owner after baking. Empty = no bridge
        // in view, which is the only cost a map without spans pays.
        void setSpanDrapeTextures(const std::map<TileId, GLuint>& textures);
        // The GROUND drape drawn on each drape tile this frame, with the sub-rect it is drawn through,
        // for the roof of a deck past its road's portals - which is ground. Handed over like the span
        // drapes: the renderer's own _drapeTextures are out of play when the owner composites.
        struct GroundDrape {
            GLuint texture = 0;
            float uvOffsetX = 0.0f, uvOffsetY = 0.0f, uvScale = 1.0f;
        };
        void setGroundDrapeTextures(const std::map<TileId, GroundDrape>& drapes);
        // This renderer's style layers with drapeable content in the visible set, in draw order, each
        // flagged draped or live. The owner concatenates these into one ordered stack, which is where a
        // live layer's position - and so its occlusion mask - is decided.
        void collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const;
        // Bakes the COVERAGE - accumulated alpha, not colour - of the draped style layers at or after
        // fromStyleLayerIdx. Same geometry, transforms and tile selection as bakeDrapeTile; only the
        // fragment output differs. This is the mask a live layer below the cut is drawn through.
        int bakeDrapeCoverage(const TileId& targetTileId, int fromStyleLayerIdx);
        // maskTextures[k] is the k-th mask's texture per drape tile; styleLayerMasks maps a live style
        // layer to the mask index it is occluded by. A layer absent from the map draws unmasked, which
        // is correct when nothing draped comes after it.
        void setDrapeCoverageMasks(const std::vector<std::map<TileId, GLuint> >& maskTextures, const std::map<int, int>& styleLayerMasks);
        // Draws the terrain surface for one target tile, textured with an externally owned drape
        // texture, writing depth. Returns the surface draws issued, or a negative reason code
        // (-1 no texture, -2 shared grid inactive, -3 tile not registered).

        // uvOffset/uvScale select a sub-rect of the texture: identity for the tile's own drape, and its
        // rectangle inside an ancestor's when its own is not baked yet - which is what keeps a budgeted
        // bake from flashing.
        int renderDrapedSurface(const TileId& targetTileId, GLuint drapeTexture, float uvOffsetX = 0.0f, float uvOffsetY = 0.0f, float uvScale = 1.0f);
        // Draws the terrain surface for a tile whose drape texture has no content yet, in a flat
        // colour. Keeps the depth buffer complete while bakes are still catching up.
        int renderDrapedSurfaceFill(const TileId& targetTileId, const Color& color);
        // Copies a rectangle of one drape texture into the bound drape framebuffer, flat and unblended:
        // how a new tile is given the picture the cache already holds for its ground, so it is never an
        // empty fill while its own bake waits for a budget.
        int blitDrapeTexture(GLuint srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        void setTerrainDepthWrite(bool enabled);
        void setTerrainTextureProvider(TerrainTextureProvider provider);
        void setDebugWireframe(bool enabled);
        // Outline every tile this renderer draws, on the ground: colour per zoom level, brightness
        // alternating with the tile parity, half opacity for a stand-in. For seeing WHICH tiles a layer
        // draws and where their footprints overlap.
        void setDebugTileBorders(bool enabled);
        void setDebugSurfacePrefill(bool enabled);
        void setTerrainBackgroundColor(const Color& color);
        void setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)> provider);
        /**
         * What multiplies a LABEL anchor position to get vt's normalized map coordinates. Label
         * geometry arrives in the SDK's internal space (WORLD_SIZE wide), unlike everything else
         * here, so the span chords cannot be compared against it without this.
         */
        void setLabelPositionScale(double scale) { _labelPositionScale = scale; }
        // Measurement switch (TileRenderer reads debug.massif.labelanchor): 0 anchors every label
        // in the frame as before, 1 samples a new tile set's labels on the cull thread.
        void setLabelAnchorOnCull(bool enabled) { _labelAnchorOnCull = enabled; }
        // Ask for labels to be re-anchored onto the terrain next frame. Re-anchoring samples the
        // elevation once per label vertex, so the tileIds overload marks only the labels over those
        // tiles; the argumentless one is for whole-data-set changes.
        void invalidateLabelElevation();
        void invalidateLabelElevation(const std::vector<TileId>& tileIds);
        // The same for the extrusion bases: the tileIds overload re-resolves only the extrusions
        // standing OVER those elevation tiles, which is exact because a building's centroid is inside
        // its own tile. Spans keep the global path - a chord samples PORTALS in other tiles.
        void invalidateExtrusionBases();
        void invalidateExtrusionBases(const std::vector<TileId>& tileIds);
        // The ground under an extrusion, in internal z units. Unlike the label provider this one
        // REPORTS whether there was data: a base is baked into the vertices, so guessing 0 where
        // the ground is 215 m puts the whole prism under the terrain.
        void setExtrusionElevationProvider(std::function<bool(const cglib::vec3<double>&, int, bool, double&)> provider);
        void setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)> occlusionTest);
        void setLayerBlendingSpeed(float speed);
        void setLabelBlendingSpeed(float speed);
        void setRasterFilterMode(RasterFilterMode filterMode);
        void setRendererLayerFilter(const std::optional<std::regex>& filter);
        // Render-time layer-index gate: only tile layers whose layerIndex is in [first, second) are
        // drawn. Unlike the build-time setRendererLayerFilter this is consulted every frame, so one
        // renderer can draw disjoint style-layer ranges. nullopt (default) = all layers.
        void setRendererLayerIndexRange(const std::optional<std::pair<int, int>>& range);
        // Style layers matching this filter stay OUT of the drape bake and are drawn live in the 3D
        // pass, at screen resolution: the bake resolves content at the drape texture's resolution, which
        // a slope magnifies. nullopt (default) = drape everything the geometry type allows.
        void setNoDrapeLayerFilter(const std::optional<std::regex>& filter);
        void setClickHandlerLayerFilter(const std::optional<std::regex>& filter);
        void setViewState(const ViewState& viewState);
        void setLineAntialiasScale(float scale);
        // `spanReferenceTiles`: tiles fetched UNSEEN for the chord of a stranded bridge piece
        // (collectUnresolvedSpanEnds). They join the span unions and nothing else - not drawn,
        // no labels - so a reference that overlaps the view does not double its geometry.
        void setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles = {});
        void teleportVisibleTiles(int dx, int dy);

        void initializeRenderer();
        void resetRenderer();
        void resetTileSurfaces();
        void invalidateTileSurfaces(const std::vector<TileId>& tileIds);
        void deinitializeRenderer();

        bool startFrame(float dt);
        void renderGeometry(bool geom2D, bool geom3D, bool inline3D = false);
        void renderLabels(bool labels2D, bool labels3D);
        bool endFrame();

        /** Returns false when the culler's slice ran out before this layer's labels did. */
        bool cullLabels(LabelCuller& culler);

        bool findBitmapIntersections(const std::vector<cglib::ray3<double>>& rays, std::vector<BitmapIntersectionInfo>& results) const;
        bool findGeometryIntersections(const std::vector<cglib::ray3<double>>& rays, float pointBuffer, float lineBuffer, bool geom2D, bool geom3D, std::vector<GeometryIntersectionInfo>& results) const;
        bool findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const;

    private:
        using GlobalIdLabelMap = std::unordered_map<long long, std::shared_ptr<Label>>;
        // One list per pass, in DRAW order. Grouping by glyph atlas instead put the order of two
        // labels in different atlases at the mercy of a pointer hash, so a small label could land
        // under the icon it is meant to sit on; the batch changes atlas mid-list instead.
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
            // SIGNED: glGetAttribLocation returns -1 for an attribute the linker dropped, and an
            // unsigned -1 handed to glVertexAttribPointer is GL_INVALID_VALUE, not the no-op uniform
            // location -1 is. The depth-only shadow caster programs drop several attributes.
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
            // ONE VAO PER PROGRAM, built on first use. A VAO records pointers per attribute INDEX,
            // and the same geometry is drawn by more than one program whose locations need not
            // match - re-specifying a single VAO for a second program draws garbage on Adreno.
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
            cglib::mat4x4<double> labelMatrix;
            std::array<cglib::vec4<float>, MAX_PARAMETERS> colorTable;
            std::array<float, MAX_PARAMETERS> widthTable;
            std::array<float, MAX_PARAMETERS> strokeWidthTable;
            // What an occluded label keeps (see setLabelOcclusionOpacity). One value per BATCH rather
            // than a per-style slot: it is a property of the style layer, so consecutive labels share
            // it, and a slot would have to join the colour/size key.
            float occlusionOpacity;

            LabelBatchParameters() : labelCount(0), parameterCount(0), scale(0), glyphRenderSize(64), labelMatrix(cglib::mat4x4<double>::identity()), colorTable(), widthTable(), strokeWidthTable(), occlusionOpacity(1.0f) { }
        };

        // Frames between two sweeps of the compiled-resource maps for expired owners. The sweep touches
        // every entry the tile cache holds - ~800 entries, 0.5-0.9 ms a frame - and all it buys is
        // releasing a VBO a few frames earlier.
        static constexpr int RESOURCE_SWEEP_INTERVAL_FRAMES = 8;
        // Widest halo the encoded field can describe, in screen pixels. Past this the field has
        // run out and the halo stops growing whatever the style asks - it is the old
        // GLYPH_RENDER_SPREAD cap, carried over unchanged.
        static constexpr float MAX_HALO_PIXELS = 4.7f;
        // An ICON carries a padded field, so its halo can run further than a font glyph's - mapbox's
        // icon-halo-width 3 is 7.8 device pixels on a 2.6x screen. Not much further: the encoding runs
        // out at ~8 texels, past which the quad's own edge reads as inside.
        static constexpr float MAX_ICON_HALO_PIXELS = 8.0f;
        static constexpr float STROKE_UV_SCALE = 2.857f; // stroked line UV scale factor
        static constexpr float TERRAIN_LAYER_DEPTH_DELTA = 1.0f / 524288.0f; // 2^-19: NDC depth separation per draped layer bias unit (GPU terrain draping mode)
        // The FLOOR of a proxy tile's depth, tangram's `1` in tileManager.cpp setProxyDepth. The depth
        // itself is how many levels coarser the drawn tile is than the deepest on screen; a tile
        // standing in at that level still takes one.
        static constexpr float TERRAIN_PROXY_DEPTH_UNITS = 1.0f;
        // tangram res/scenes/terrain-3d.yaml, TANGRAM_RASTER_STYLE branch: `proxy *= 48.0`.
        static constexpr float TERRAIN_RASTER_PROXY_SCALE = 48.0f;
        static constexpr float TERRAIN_PAINTER_SURFACE_BACK = 2.0f; // painter-order: clip-slack units the depth-writing surface is pushed BACK (same magnitude as the regular-grid geometry forward slack); geometry then draws at its real depth with the same twist clearance but no forward pull, so it can not leak in front of a near ridge
        static constexpr float TERRAIN_EXTRUSION_DEPTH_DELTAS = 24.0f; // 3D extrusions vs the terrain surface pre-pass in the 3D overlay. A wall stands ON the ground it is tested against, so the pair is only separable to the depth buffer's resolution, which in eye units grows like distance^2/near - the same law a constant-NDC bias follows, which is why the clearance is expressed here and not as clip slack. Measured on the demo: 1 delta eats most of a 40 m building at a few km, 24 restores it, 64 adds nothing (and every extra delta is a wider band in which an extrusion behind a crest can show through it)
        static constexpr float TERRAIN_DEPTH_CLIP_SLACK = 1.0e-3f; // clip-space depth shift per bias unit at the reference tile size, scaled by tile size (quadratic law, see setupTerrainUniforms) and |proj m22|
        static constexpr double TERRAIN_DEPTH_CLIP_REF_TILE_SIZE = 512.0; // zoom 11 tile size in internal units - the anchor of the quadratic slack law
        static constexpr float ALPHA_HIT_THRESHOLD = 0.05f; // threshold value for 'transparent' pixel alphas
        static constexpr std::size_t DRAPE_TEXTURE_POOL_SIZE = 32; // recycled drape textures kept alive between frames

        bool isTileVisible(const TileId& tileId) const;
        /**
         * How far above its ground a tile's content may stand, in internal units, for CULLING only.
         * maplibre's ASSUMED_MAX_FEATURE_HEIGHT_METERS, grown as the frustum's bottom edge nears
         * the horizon (covering_tiles.ts). Without it a building vanishes the moment the ground it
         * stands on leaves the frustum, which is well before the building does.
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
        // The part of each span drape tile the bake covers, in drape uv (u0, v0, u1, v1): the deck's
        // own extent rather than the whole tile, so a narrow deck gets the texture's full width
        // across itself. Collected with the tiles, read by the bake and by the sampling transform.
        mutable std::map<TileId, cglib::vec4<float>> _spanDrapeBounds;
        bool _labelAnchorOnCull = true;
        GLuint _pendingSpanDrape = 0;
        std::map<TileId, GroundDrape> _groundDrapes;
        bool resolveGroundDrape(const TileId& targetTileId, GLuint& texture, cglib::vec4<float>& uvTransform) const;
        GLuint _pendingGroundDrape = 0; // the target tile's ground drape, for the roof past the portals
        static constexpr double SPAN_GROUND_TOLERANCE_METRES = 1.5;
        cglib::vec4<float> _pendingGroundDrapeTransform = cglib::vec4<float>(0, 0, 1, 1);
        cglib::vec4<float> _pendingSpanDrapeTransform = cglib::vec4<float>(0, 0, 1, 1);
        // The body shared by bakeDrapeTile and bakeDrapeCoverage, restricted to the style layers at or
        // after fromStyleLayerIdx; the caller holds the mutex. clipZoom is premultiplied onto the bake
        // matrix - the span drape bakes only the deck's bounds, scaled up to the whole texture.
        int bakeDrapeUnits(const TileId& targetTileId, int fromStyleLayerIdx, bool spanOnly = false, const cglib::mat4x4<float>* clipZoom = nullptr);
        // The mask a live style layer is occluded by over one target tile, and the transform taking
        // target-tile units to that mask tile's. False when there is no mask, or when the drape tile is
        // FINER than the target - one draw cannot sample several masks.
        bool resolveDrapeCoverageMask(const TileId& targetTileId, int styleLayerIdx, GLuint& texture, cglib::vec4<float>& uvTransform) const;
        bool testIntersectionOpacity(const std::shared_ptr<const BitmapPattern>& pattern, const cglib::vec2<float>& uvp, const cglib::vec2<float>& uv0, const cglib::vec2<float>& uv1) const;

        void buildTileSurfaces(const std::set<TileId>& tileIds);

        void buildRenderTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles);
        void initializeRenderTile(TileId targetTileId, RenderTile& renderTile, const std::shared_ptr<const Tile>& tile, const std::vector<RenderTile>& existingRenderTiles) const;
        void mergeExistingRenderTile(TileId targetTileId, const RenderTile& existingRenderTile, std::vector<RenderTile>& renderTiles, int depth) const;
        bool updateRenderTile(RenderTile& renderTile, float dBlend) const;

        static long long calculateLabelGeometryHash(const Tile* tile, long long localId);
        void buildLabelMaps(const std::vector<std::shared_ptr<const Tile>>& labelTiles);
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
            // An extrusion whose own colour is translucent (fill opacity below 1) is drawn in two
            // passes, so only its nearest surface blends - see renderGeometry3D.
            bool translucentExtrusions = false;
        };
        // Opens the 3D pass for one style layer: the overlay framebuffer when the layer needs one,
        // the terrain occluder pre-pass, and the depth/blend state the draws run under. Anything
        // drawn between the two calls is resolved against the ground exactly as an extrusion is.
        Pass3DState begin3DPass(const std::vector<const RenderTileLayer*>& renderLayers, const std::vector<RenderTile>& renderTiles, bool allowInline);
        void end3DPass(const Pass3DState& state);
        // Every POLYGON3D geometry of every visible tile, in draw order, optionally restricted to
        // the tiles covered by `coveredBy`. The callback returns false to skip the rest of that
        // layer's geometries.
        template <typename Func>
        void forEachVisibleExtrusion(const std::vector<TileId>* coveredBy, Func&& func) const;
        void renderLabels(const std::vector<std::shared_ptr<Label>>& labels);
        // One batching pass over a label list, in list order. CALLOUT leader lines get a pass of
        // their own before the text, so that no line is drawn over another label's glyphs.
        void renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, Label::DrawPass pass);

        float evaluateFloatFunc(const FloatFunction& func);
        Color evaluateColorFunc(const ColorFunction& func);

        void setCompOp(CompOp compOp);
        void blendScreenTexture(float opacity, GLuint texture);
        void updateTerrainSkirts();
        const std::pair<bool, TerrainTexture>& resolveTerrainTexture(const TileId& tileId) const;
        bool setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix, bool gridSurface = false);
        // The tile set the terrain SURFACES are drawn from this frame, which is not the renderer's own
        // visible tiles as soon as a cover is handed in. Edge stitching has to follow the DRAWN cover,
        // or it stitches a set nothing is drawn from and the surfaces crack at LOD rings.
        const std::set<TileId>& terrainSurfaceTileIds() const;
        // The cover tiles making up the ground under one render tile: the leaves inside it, or
        // the tile itself where the cover is coarser there (a capped split).
        const std::vector<TileId>& collectGroundLeaves(const TileId& targetTileId) const;
        void updateTerrainCoverTiles();
        void buildTerrainEdgeCoarsening();
        void setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        /**
         * Resolves an extrusion's ground on the CPU and patches it into the vertex data.
         *
         * The base has to be the SAME for every vertex of a building, and a vertex-shader sample
         * cannot promise that: the elevation texture bound is the one the TILE BEING DRAWN carries,
         * so a footprint spanning two tiles was sampled through two textures, got two bases, and
         * tore open along the tile line. One CPU query against the global ElevationManager is
         * tile-independent by construction. mapbox-gl-js reaches the same place from the other end
         * (fill_extrusion_bucket's centroid buffer) but needs updateBorders to reconcile the two
         * halves, because their lookup is per tile and each half carries its own clipped footprint.
         *
         * Returns false while the elevation for this tile has not loaded. The geometry is still
         * DRAWN in that case - its slots keep TileGeometry::UNRESOLVED_BASE and polygon3DVsh falls
         * back to the ground under each vertex, which is the pre-CPU behaviour. Skipping the draw
         * instead loses the building outright, and any wrong-but-plausible base buries it.
         */
        bool resolveExtrusionBases(const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry) const;
        void buildExtrusionBaseFootprints(const std::shared_ptr<TileGeometry>& geometry, const TileGeometry::VertexGeometryLayoutParameters& params, const VertexArray<std::uint8_t>& vertexGeometry, std::size_t vertexCount) const;
        /**
         * The DECK height over a point standing on a span, for anything anchored to the ground
         * that belongs to the bridge rather than to the terrain under it - a road name, a POI, a
         * one-way arrow. Without it they sit on the ground the bridge flies over.
         *
         * @return False when the point is not on a resolved span.
         */
        void markPendingLabelsDirty();
        std::function<double(const cglib::vec3<double>&)> labelHeightFunc() const;
        bool anchorDirtyLabels();
        bool spanHeightAt(const cglib::vec2<double>& pos, double& height) const;
        void renderTileMask(const TileId& tileId);
        void renderStencilDebugOverlay();
        // Bakes the DEM-derived paint of one target tile into the currently bound drape
        // framebuffer. Returns the number of primitives drawn (0 when there is no elevation
        // data for the tile yet, so the owner can tell it apart from a finished bake).
        int renderTerrainPaint(const TileId& targetTileId);
        // Draws the paint as the terrain surface, one draw per covered tile. asGround: the paint IS the
        // ground pass - it carries the ground colour as its base and writes depth, so it needs no
        // separate fill draw and sits at the bottom of the stack's depth order.
        int renderTerrainPaintSurfaces(bool asGround = false);
        // The zoom-dependent relief boost of the paint, matching the normal-map path.
        float calculateTerrainPaintReliefBoost(float metersPerTexel) const;
        void renderTileSurfaceFill(const TileId& tileId, const Color& color, bool lit = false);
        void renderDrapeTextures(const std::vector<RenderTile>& renderTiles);
        int renderTileSurfaceDrape(const TileId& tileId, float uvOffsetX, float uvOffsetY, float uvScale);
        GLuint ensureDrapeTexture(const TileId& tileId);
        void releaseDrapeTexture(GLuint texture);
        void deleteDrapeResources();
        bool isDrapeableGeometry(const std::shared_ptr<TileGeometry>& geometry) const;
        // Builds the lit raster program ahead of the zoom-out that first needs it, so its compile
        // does not land inside the gesture.
        void warmTerrainRasterShader();
        bool hasDrapeableContent(const RenderTileLayer& renderLayer) const;
        // Contact shadows this layer would bake into the drape (see calculateDrapeFingerprint).
        bool hasGroundAOContent(const RenderTileLayer& renderLayer) const;
        bool hasGroundAOTiles(float zoomFade) const;
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
        // id must be a string LITERAL: its address is the identity in the front cache below.
        const ShaderProgram& buildShaderProgram(const char* id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags);
        const std::vector<std::shared_ptr<TileSurface>>& buildCompiledTerrainGridSurfaces();
        // Two triangles covering the tile square. The drape bake is flat and orthographic, so the
        // displaced grid's thousands of triangles buy nothing there and cost everything.
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
        std::vector<std::shared_ptr<TileSurface>> _terrainFlatSurfaces; // 1x1 grid for the flat drape bake // the single shared unit-grid surface, drawn per tile
        float _terrainDrawLayerOffset = 0.0f;    // painter-order per-draw (proxy - layer) offset
        float _terrainLineClearance = 0.0f;      // world units a draped line clears the ground by, constant in metres at any range
        float _terrainDrawClearance = 0.0f;      // per-draw METRE-constant clearance in world units (applyDepthBias); non-zero only for content that chords over the ground
        int _terrainLayerOrdinalBase = 0;        // first style-layer ordinal of this renderer in the stack
        std::set<int> _terrainStyleLayerIndices; // every style layer index this renderer has drawn - the stable order list
        int _terrainStyleLayersDrawn = 0;        // size of the order list above (the owner's dense numbering)
        bool _terrainDrapeFills = false;         // maplibre-style: bake polygon fills flat to a per-tile texture, sampled on the surface
        bool _terrainDrapeLines = false;         // also bake vt tile lines into the drape texture (softer, but zero leak/hug error)
        SpanResolver _spanResolver;              // 3D bridges: unions, chords, bases (setSpansEnabled)
        int _drapeTextureSize = 512;             // per-tile drape texture resolution
        GLuint _drapeFBO = 0;                    // shared offscreen FBO for baking drape textures
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
        bool _debugWireframe = false;
        bool _debugTileBorders = false;
        GLuint _tileBorderVBO = 0;               // the tile outline, in tile-local coordinates
        GLsizei _tileBorderVertexCount = 0;
        static constexpr int TILE_BORDER_SEGMENTS = 16; // per edge, so the line follows the terrain
        bool _debugSurfacePrefill = false;
        TerrainLighting _terrainLighting;
        // Below this zoom a contact shadow is a sub-pixel rim; groundAOZoomFade ramps it over one level.
        static constexpr float GROUND_AO_MIN_ZOOM = 16.0f;
        // The style's height multiplier, times the tile's fade-in when buildings grow as they appear.
        // The sun CASTER leaves the VIEW scale out - those buildings are still there - but follows the
        // style's own scale: no building, no shadow. A span DECK takes none of it, being structure.
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
        float _groundAOAttenuation = 0.69f;
        bool _groundAOMaskPass = false; // set only while the mask is being drawn
        // A label's anchor sits ON the ground and the buffer it is compared against is half resolution
        // from the same camera, so the two depths meet within rounding. The offset is mapbox's own; the
        // ramp turns the comparison into a fade rather than a switch.
        static constexpr float LABEL_OCCLUSION_DEPTH_OFFSET = -0.0001f;
        static constexpr float LABEL_OCCLUSION_DEPTH_RAMP = 0.0033f;
        GLuint _labelOcclusionTexture = 0;    // 0 = labels are not occluded by 3D content
        float _labelOcclusionSize = 30.0f;    // screen pixels sampled around a label's anchor
        float _labelOcclusionOpacity = 1.0f;  // what an occluded label keeps; 1 = no occlusion
        bool _labelOcclusionStyled = false;   // ... or some style layer sets its own
        bool _groundAOBakePass = false; // ... and only while that mask is a DRAPE bake
        TerrainPaint _terrainPaint;
        bool _terrainPaintOnGround = false;      // the paint replaces the ground fill (see setTerrainPaintOnGround)
        int _terrainDemTaps = 16;                // texture fetches per terrain vertex (see setTerrainDemTaps)
        bool _terrainTileBackgrounds = false;    // per-layer per-tile background meshes (see setTerrainTileBackgrounds)
        std::vector<TileId> _terrainPaintTiles; // what a paint covers when it draws itself
        GLuint _terrainShadowTexture = 0;
        int _terrainShadowMapSize = 0;
        // Metres -> world z units, for a map with NO elevation at all. The fit reads this factor
        // off a decoded DEM tile, which is fine on terrain and is why a flat 2D map got no shadows
        // at all: the factor is a property of the PROJECTION, so the caller can state it outright.
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
        std::function<double(const cglib::vec3<double>&)> _labelElevationProvider;
        std::function<bool(const cglib::vec3<double>&, int, bool, double&)> _extrusionElevationProvider;
        std::atomic<unsigned int> _extrusionBaseVersion { 1 }; // bumped by invalidateExtrusionBases
        std::vector<TileId> _pendingLabelElevationTiles; // elevation tiles whose labels must be re-anchored
        double _labelPositionScale = 1.0; // label anchors are in internal coordinates, not vt's
        std::vector<TileId> _pendingExtrusionBaseTiles;  // ...and whose extrusion bases must be re-resolved
        bool _pendingLabelElevationAll = false;
        std::function<bool(const cglib::vec3<double>&)> _labelOcclusionTest;
        float _layerBlendingSpeed = 1.0f;
        float _labelBlendingSpeed = 1.0f;
        RasterFilterMode _rasterFilterMode = RasterFilterMode::BILINEAR;
        std::optional<std::regex> _rendererLayerFilter;
        std::optional<std::regex> _noDrapeLayerFilter;
        std::optional<std::pair<int, int>> _rendererLayerIndexRange;
        std::optional<std::regex> _clickHandlerLayerFilter;

        std::shared_ptr<std::vector<RenderTile>> _renderTiles;
        std::shared_ptr<std::vector<RenderTile>> _visibleRenderTiles;
        std::array<std::shared_ptr<PassLabels>, 2> _passLabels; // for 'ground' labels and for 'billboard' labels
        std::array<std::shared_ptr<PassLabels>, 2> _visiblePassLabels;  // for 'ground' labels and for 'billboard' labels
        std::vector<std::shared_ptr<Label>> _labels;
        std::size_t _labelCullCursor = 0; // how far the current placement cycle got through _labels
        int _resourceSweepCounter = 0;
        std::map<int, GlobalIdLabelMap> _layerLabelMap;
        std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceMap;
        GLuint _lastUsedProgram = 0; // currently bound program, 0 = unknown (see useProgram)
        std::map<std::string, ShaderProgram> _shaderProgramMap;
        // Allocation-free front cache for the above: building the string key is several allocations and
        // a string-keyed lookup, once per DRAW CALL at 170-510 draws a frame. Keyed on the call site's
        // literal POINTER plus the flags, so a hit costs a hash of four words.
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

        // Memo for the style colour/width/offset functions: they take only the view state, yet every
        // draw re-evaluated up to MAX_PARAMETERS of each - 16.5 us of a 44 us draw. The entry holds a
        // reference to the function it is keyed on, or a dead one's address could be reused.
        std::unordered_map<const void*, std::pair<FloatFunction::Function, float>> _floatFuncCache;
        std::unordered_map<const void*, std::pair<ColorFunction::Function, Color>> _colorFuncCache;

        // Per-view-state tile matrix memo: the draw loop is style-layer-major, so the same handful of
        // tiles is transformed again for every style layer - 20-odd matrices recomputed several hundred
        // times a frame. Cleared alongside the style function memo.
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
        // The elevation texture a tile stands on, resolved once per frame: setupTerrainUniforms runs
        // per DRAW, so with a dozen style layers the same tile asked the provider a dozen times for an
        // answer that cannot change within the frame.
        mutable std::unordered_map<TileId, std::pair<bool, TerrainTexture>> _terrainTextureCache;

        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<double>, TileMatrixKeyHash> _tileMatrixCache;
        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<float>, TileMatrixKeyHash> _tileMVPMatrixCache;

        // Compiled tile geometry, keyed on the RAW pointer: the other compiled-* maps use weak_ptr with
        // owner_less, whose lookup is two atomic refcount ops and a tree walk - 4-6 us per draw call,
        // and this is the one looked up on EVERY draw. The weak_ptr rides along in the value.
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
        const std::shared_ptr<const TileTransformer> _transformer;
        const float _scale;

        mutable std::mutex _mutex;
    };
}

#endif
