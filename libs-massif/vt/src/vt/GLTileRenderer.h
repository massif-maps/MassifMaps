/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_GLTILERENDERER_H_
#define _CARTO_VT_GLTILERENDERER_H_

#include "Bitmap.h"
#include "Color.h"
#include "ViewState.h"
#include "Label.h"
#include "Styles.h"
#include "Tile.h"
#include "TileId.h"
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

namespace carto::vt {
    class LabelCuller;

    class GLTileRenderer final {
    public:
        // Shadow cascades: pages of one shadow texture, near first. The count is a uniform, not a
        // shader define, so a change does not recompile every terrain program - but the shader
        // declares this many matrices and varyings, so raising it means touching the shader too.
        static constexpr int MAX_SHADOW_CASCADES = 4;

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
            // Hash of everything the paint's appearance depends on, INCLUDING what only the
            // injected lighting shader sees (light direction, colours, method). The renderer
            // cannot derive it - it never sees those uniforms - and without it a parameter
            // change would leave every already-baked drape texture in place.
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
        // DENSELY: the ordinal feeds a constant-NDC pull, whose eye tolerance grows as distance^2,
        // and a fixed stride per renderer inflates the total into the range that saw through ridges
        // in rounds 45-56. Tangram's ordinals are dense because it has one style list.
        int getStyleLayerCount() const;
        // Cross-LOD edge stitching for the shared regular grid surfaces: on an edge shared with
        // a coarser neighbouring tile, the surface follows the neighbour's coarser lattice so
        // the two tiles agree along the edge instead of cracking open. No effect without the
        // regular grid mode (the adaptive per-tile surfaces tesselate their borders to match).
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
        // Clearance a draped LINE is drawn in front of the ground with, in world units, constant in
        // METRES at every distance (see applyDepthBias). A line chords over the relief between its
        // vertices by a fixed number of metres; a clip- or ndc-constant bias cannot pay for that
        // without being worth hundreds of metres at range, which is what leaks through ridges.
        void setTerrainLineClearance(float clearance);
        void setTerrainDrapeFills(bool enabled, bool includeLines);
        void setTerrainDrapeResolution(int resolution);
        void setTerrainLighting(const TerrainLighting& lighting);
        // Turns this renderer into a paint baker (see TerrainPaint): it draws the DEM-derived
        // paint for every draped tile and nothing else. Only effective under a cross-layer drape
        // target, which is where the layer order is resolved.
        void setTerrainPaint(const TerrainPaint& paint);
        // Draw the paint AS the ground (tangram's arrangement: the shading is a block on the
        // terrain draw, one draw per tile, at the bottom of the order) instead of as its layer's
        // own surface over the ground. Cheaper by one full-surface draw per tile, but it puts the
        // shading UNDER every ground-shaped fill, so it only looks right when nothing ground-shaped
        // is drawn below the paint's layer - or when those fills are translucent, which is what
        // tangram's 'translucent-polygons' earth style is for.
        void setTerrainPaintOnGround(bool enabled);
        // Texture fetches per terrain vertex: 16 = the lattice clamp (4 grid corners x a 4-tap
        // manual bilinear each), 4 = the manual bilinear alone, 1 = a single hardware-filtered
        // fetch, which is what tangram's terrain vertex does. Vertex texture fetch is expensive on
        // mobile GPUs, so this is the first thing to measure when the frame sits in the swap wait.
        void setTerrainDemTaps(int taps);
        // Keep drawing a per-tile background mesh per LAYER under a shared ground. Tangram has no
        // such thing - its map background is the framebuffer clear colour, once per frame - so this
        // is off by default and exists to measure what those draws cost.
        void setTerrainTileBackgrounds(bool enabled);
        // The stencil tile masks that clip content to its own tile's screen footprint:
        // -1 = automatic (the default: off in a terrain frame, where a mask is a full displaced
        // grid per tile per stencil reset, kept in 2D, where it is a two-triangle quad - and kept
        // in both whenever a layer composites through a comp-op, which has no other clip), 0 =
        // never, 1 = always. Tangram has no stencil at all.
        void setTileMasks(int mode);
        // The terrain tiles a paint covers when it draws itself (no drape to bake into). A paint
        // has no tile set, so the owner hands it the terrain's own cover.
        void setTerrainPaintTiles(const std::vector<TileId>& tileIds);
        // Distance fog over the whole 3D scene, in world units: the terrain surface, rasters,
        // 2D geometry and 3D extrusions all fade towards this colour between the two distances.
        // A transparent colour or a zero range turns it off, and the programs are then built
        // without it. The drape bake is orthographic and is never fogged - its content is fogged
        // once, as part of the terrain surface it is painted on.
        void setFog(const Color& color, float startDistance, float distance);
        // Directional shadows. The owner renders the caster pass (renderShadowCasters) into its
        // own framebuffer from the light, then hands the packed-depth texture and the same
        // light matrix back here so the draped surface can look itself up in it.
        void setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const std::array<float, MAX_SHADOW_CASCADES>& depthBiases, float strength, float softness, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs);
        // Light-space view-projection fitted to the given terrain tiles; false if the set is empty
        // or no elevation is loaded. minHeight/maxHeight bound the shadowed volume - a generous slab
        // is what makes a low sun pixelated, since the box is fitted around it. The box is snapped
        // to a world lattice of whole mapSize texels, so it repeats within one step. One call per
        // cascade, each covering its own slice of the view distance.
        bool calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float maxDistanceMeters, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        // The terrain's shadow resolved once per screen pixel, into a half-resolution mask the
        // surface draws then sample by screen position instead of computing it each time.
        void setTerrainShadowMask(GLuint texture, float invScreenWidth, float invScreenHeight);
        // Draws the mask for the given tiles into the bound framebuffer. Returns the draw count.
        int renderTerrainShadowMask(const std::vector<TileId>& tileIds);
        // Moves as the caster geometry does: 3D extrusions fade in by growing, so the sum of
        // their blend factors says how far the shadow map has drifted from what is on screen.
        float shadowCasterFadeSignature() const;
        // Draws this renderer's shadow casters for one terrain tile into the bound framebuffer.
        // Returns the number of draws issued.
        int renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);

        // Cross-layer drape: with an external target this renderer stops owning drape textures and
        // becomes a content baker - the owner collects the tiles, bakes every renderer into ONE
        // texture per tile in layer order, and draws the surface once.
        // Shared terrain ground (tangram's model, what replaces the RTT drape): every renderer gets
        // the SAME cover and the ground is drawn once per frame, so a renderer with a cover set
        // establishes no depth domain of its own and stamps no masks. Ground-shaped content goes on
        // the COVER tiles, not the layer's own - two tesselations of one height field z-fight.
        // proxyDepths, parallel to tileIds: levels coarser than asked for (0 = its own). Tangram
        // multiplies the term by 48 for the terrain raster; a coarse stand-in is a different height
        // field and pokes through the content drawn on the level above it.
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
        // Bakes this renderer's drapeable content for one target tile into the currently bound
        // framebuffer and viewport. Does not clear - the owner clears once per tile before the
        // first renderer bakes, so later layers composite over earlier ones. Returns the number
        // of primitives drawn, so the owner can tell "nothing to bake" from "bake did nothing".
        int bakeDrapeTile(const TileId& targetTileId);
        // Draws the terrain surface for one target tile, textured with an externally owned drape
        // texture. Writes depth: the surface is the only depth-writing terrain geometry.
        // Returns the number of surface draws issued, or a negative reason code when nothing
        // was drawn (-1 no texture, -2 shared grid inactive, -3 tile not registered).
        // uvOffset/uvScale select a sub-rect of the texture: identity when the tile draws its own
        // drape texture, and the tile's rectangle inside an ancestor's texture when its own is not
        // baked yet. Standing in on the ancestor is what keeps a budgeted bake from flashing.
        int renderDrapedSurface(const TileId& targetTileId, GLuint drapeTexture, float uvOffsetX = 0.0f, float uvOffsetY = 0.0f, float uvScale = 1.0f);
        // Draws the terrain surface for a tile whose drape texture has no content yet, in a flat
        // colour. Keeps the depth buffer complete while bakes are still catching up.
        int renderDrapedSurfaceFill(const TileId& targetTileId, const Color& color);
        // Copies a rectangle of one drape texture into the currently bound drape framebuffer,
        // flat and unblended. This is how a brand new tile is given the picture the cache
        // already holds for its ground - magnified from an ancestor, or assembled from the finer
        // tiles it replaces - so that it never has to be shown as an empty fill while its own
        // bake waits for a budget. dstOffset/dstScale place it in the target texture's [0,1]
        // square, uvOffset/uvScale select the part of the source.
        int blitDrapeTexture(GLuint srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale);
        void setTerrainDepthWrite(bool enabled);
        void setTerrainTextureProvider(TerrainTextureProvider provider);
        void setDebugWireframe(bool enabled);
        // Outline every tile this renderer draws, on the ground: colour per zoom level,
        // brightness alternating with the tile parity, half opacity for a tile standing in with
        // another tile's data. What it is for is seeing WHICH tiles a layer draws and where their
        // footprints overlap - a coarser tile set under a finer one, an overzoomed ancestor, or a
        // retained tile still owning pixels.
        void setDebugTileBorders(bool enabled);
        void setDebugSurfacePrefill(bool enabled);
        void setTerrainBackgroundColor(const Color& color);
        void setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)> provider);
        // Ask for labels to be re-anchored onto the terrain on the next frame. Re-anchoring
        // samples the elevation once per label vertex, so it is driven by what actually
        // changed: the tileIds overload only marks the labels whose geometry lies over one of
        // the given (elevation) tiles, the argumentless one marks every label and is for
        // whole-data-set changes (data source, exaggeration, change-log overflow).
        void invalidateLabelElevation();
        void invalidateLabelElevation(const std::vector<TileId>& tileIds);
        void setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)> occlusionTest);
        void setLayerBlendingSpeed(float speed);
        void setLabelBlendingSpeed(float speed);
        void setRasterFilterMode(RasterFilterMode filterMode);
        void setRendererLayerFilter(const std::optional<std::regex>& filter);
        // Render-time layer-index gate: when set, only tile layers whose layerIndex is in
        // [first, second) are drawn this frame. Unlike setRendererLayerFilter (build-time), this is
        // consulted every frame, so one renderer can draw disjoint style-layer ranges across frames.
        // nullopt (default) = draw all layers (no effect).
        void setRendererLayerIndexRange(const std::optional<std::pair<int, int>>& range);
        // Style layers matching this filter stay OUT of the terrain drape bake and are drawn live
        // in the 3D pass instead, at screen resolution. The drape resolves content at the drape
        // texture's resolution, which is what a slope then magnifies: fills and road casings
        // survive that, hairline content (contours) does not. nullopt (default) = drape everything
        // the geometry type allows.
        void setNoDrapeLayerFilter(const std::optional<std::regex>& filter);
        void setClickHandlerLayerFilter(const std::optional<std::regex>& filter);
        void setViewState(const ViewState& viewState);
        void setLineAntialiasScale(float scale);
        void setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles);
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

        void cullLabels(LabelCuller& culler);

        bool findBitmapIntersections(const std::vector<cglib::ray3<double>>& rays, std::vector<BitmapIntersectionInfo>& results) const;
        bool findGeometryIntersections(const std::vector<cglib::ray3<double>>& rays, float pointBuffer, float lineBuffer, bool geom2D, bool geom3D, std::vector<GeometryIntersectionInfo>& results) const;
        bool findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const;

    private:
        using GlobalIdLabelMap = std::unordered_map<long long, std::shared_ptr<Label>>;
        using BitmapLabelMap = std::unordered_map<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

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
            // unsigned -1 handed to glVertexAttribPointer is GL_INVALID_VALUE, not a no-op the way
            // uniform location -1 is. The shadow caster programs drop several attributes (their
            // fragment shader only writes depth), which is where this bites.
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
            GLuint geometryVAO;
            // The program whose ATTRIBUTE LOCATIONS the VAO's pointers were set up for, or 0.
            // A VAO records pointers per attribute INDEX, and the same geometry is drawn by more
            // than one program - the shadow caster pass draws the extrusions with 'polygon3dshadow'
            // rather than 'polygon3d' - whose locations need not match. Remembering only THAT it
            // was initialised replayed the first program's layout for every later one.
            mutable GLuint geometryVAOProgram;

            CompiledGeometry() : vertexGeometryVBO(0), indicesVBO(0), geometryVAO(0), geometryVAOProgram(0) { }
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

            LabelBatchParameters() : labelCount(0), parameterCount(0), scale(0), glyphRenderSize(64), labelMatrix(cglib::mat4x4<double>::identity()), colorTable(), widthTable(), strokeWidthTable() { }
        };

        // Frames between two sweeps of the compiled-resource maps for expired owners (see
        // endFrame). The sweep touches every entry the tile cache still holds - measured at
        // ~800 entries and 0.5-0.9 ms a frame - and all it buys is releasing a VBO a few
        // frames earlier.
        static constexpr int RESOURCE_SWEEP_INTERVAL_FRAMES = 8;
        // Stencil bit reserved for the single-blend pass of a translucent style layer. The lower
        // bits carry the per-tile mask values, so the pass only runs while a frame has fewer target
        // tiles than this.
        static constexpr int SINGLE_BLEND_STENCIL_BIT = 128;
        static constexpr float HALO_RADIUS_SCALE = 2.5f; // the scaling factor for halo radius
        // Screen pixels per halo unit. The halo used to be converted with the glyph's RENDER size,
        // which was one constant when every glyph was rastered at 27 texels; the raster ladder made
        // it 24, 36 or 48, so the same style drew a halo twice as wide on a small label as on a
        // large one and up to five times what it drew before (docs/rendering/06-labels.md). The
        // conversion belongs to the antialias ramp - one screen pixel - and this is the width the
        // single-raster build gave: 27 / (32 * GLYPH_SDF_UNIT_then * 23), with the spread of 4 it
        // had. Kept so no style has to be retouched.
        static constexpr float HALO_PIXELS_PER_UNIT = 0.589f;
        static constexpr float STROKE_UV_SCALE = 2.857f; // stroked line UV scale factor
        static constexpr float POLYGON3D_HEIGHT_SCALE = 10018754.17f; // scaling factor for zoom 0 heights
        static constexpr float TERRAIN_LAYER_DEPTH_DELTA = 1.0f / 524288.0f; // 2^-19: NDC depth separation per draped layer bias unit (GPU terrain draping mode)
        // The FLOOR of a proxy tile's depth, tangram's `1` in
        //     setProxyDepth(m_proxyCounter > 0 ? std::max(maxVisS - tileId.s, 1) : 0)
        // (core/src/tile/tileManager.cpp). The depth itself is how many levels coarser the drawn
        // tile is than the deepest level on screen; a tile that stands in at that level still
        // takes one.
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
        bool isEmptyBlendRequired(CompOp compOp) const;

        unsigned int fogFlag() const;
        // TERRAIN_SHADOW plus the cascade count the receiver lookup is compiled for.
        unsigned int shadowReceiverFlags() const;
        // Same, for the terrain surface: reads the screen-space mask, or produces it.
        unsigned int surfaceShadowFlags() const;
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
        void renderLabels(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap);
        // One batching pass over a label list. CALLOUT leader lines get a pass of their own before
        // the text, so that no line is drawn over another label's glyphs.
        void renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap, Label::DrawPass pass);

        float evaluateFloatFunc(const FloatFunction& func);
        Color evaluateColorFunc(const ColorFunction& func);

        void setCompOp(CompOp compOp);
        void blendScreenTexture(float opacity, GLuint texture);
        void updateTerrainSkirts();
        const std::pair<bool, TerrainTexture>& resolveTerrainTexture(const TileId& tileId) const;
        bool setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix, bool gridSurface = false);
        // The tile set the terrain SURFACES are drawn from this frame - which is not the
        // renderer's own visible tiles as soon as a cover is handed in from outside (the drape
        // cover, or a paint's terrain cover). Edge stitching has to follow the drawn cover, or it
        // stitches a set of tiles nothing is drawn from and the surfaces crack at LOD rings.
        const std::set<TileId>& terrainSurfaceTileIds() const;
        // The cover tiles making up the ground under one render tile: the leaves inside it, or
        // the tile itself where the cover is coarser there (a capped split).
        const std::vector<TileId>& collectGroundLeaves(const TileId& targetTileId) const;
        void updateTerrainCoverTiles();
        void buildTerrainEdgeCoarsening();
        void setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        void renderTileMask(const TileId& tileId);
        void renderStencilDebugOverlay();
        // Bakes the DEM-derived paint of one target tile into the currently bound drape
        // framebuffer. Returns the number of primitives drawn (0 when there is no elevation
        // data for the tile yet, so the owner can tell it apart from a finished bake).
        int renderTerrainPaint(const TileId& targetTileId);
        // Draws the paint as the terrain surface, one draw per covered tile. Returns the draws.
        // asGround: the paint IS the ground pass - it carries the ground colour as its base and
        // writes depth, so no separate fill draw is needed for the tiles it covers, and it sits at
        // the bottom of the stack's depth order instead of at its layer's place in it.
        int renderTerrainPaintSurfaces(bool asGround = false);
        // The zoom-dependent relief boost of the paint, matching the normal-map path.
        float calculateTerrainPaintReliefBoost(float metersPerTexel) const;
        void renderTileSurfaceFill(const TileId& tileId, const Color& color, bool lit = false);
        void renderDrapeTextures(const std::vector<RenderTile>& renderTiles);
        int renderTileSurfaceDrape(const TileId& tileId, float uvOffsetX, float uvOffsetY, float uvScale);
        GLuint ensureDrapeTexture(const TileId& tileId);
        void releaseDrapeTexture(GLuint texture);
        void deleteDrapeResources();
        bool isDrapeableGeometry(TileGeometry::Type type) const;
        bool hasDrapeableContent(const RenderTileLayer& renderLayer) const;
        bool tileCovers(const TileId& tileId, const TileId& targetTileId) const;
        bool isTileDraped(const TileId& targetTileId) const;
        cglib::mat4x4<float> calculateDrapeMVPMatrix(const TileId& sourceTileId, const TileId& targetTileId) const;
        std::size_t calculateDrapeFingerprint(const RenderTile& renderTile) const;
        void renderTileWireframe(const TileId& tileId);
        void renderTileBorder(const TileId& tileId, const TileId& sourceTileId);
        void renderTileBackground(const TileId& tileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileBackground>& background);
        void renderTileBitmap(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap);
        void renderTileGeometry(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileGeometry>& geometry);
        void renderLabelBatch(const LabelBatchParameters& labelBatchParams, const std::shared_ptr<const Bitmap>& bitmap);

        const CompiledBitmap& buildCompiledBitmap(const std::shared_ptr<const Bitmap>& bitmap, bool genMipmaps);
        const CompiledBitmap& buildCompiledTileBitmap(const std::shared_ptr<TileBitmap>& tileBitmap);
        const CompiledGeometry& buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry);
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
        const cglib::mat4x4<double>* _shadowCasterViewProj = nullptr; // set during the shadow caster pass
        const cglib::mat4x4<float>* _drapeMVPOverride = nullptr; // when set, renderTileGeometry draws flat into the drape FBO
        bool _debugWireframe = false;
        bool _debugTileBorders = false;
        GLuint _tileBorderVBO = 0;               // the tile outline, in tile-local coordinates
        GLsizei _tileBorderVertexCount = 0;
        static constexpr int TILE_BORDER_SEGMENTS = 16; // per edge, so the line follows the terrain
        bool _debugSurfacePrefill = false;
        TerrainLighting _terrainLighting;
        TerrainPaint _terrainPaint;
        bool _terrainPaintOnGround = false;      // the paint replaces the ground fill (see setTerrainPaintOnGround)
        int _terrainDemTaps = 16;                // texture fetches per terrain vertex (see setTerrainDemTaps)
        bool _terrainTileBackgrounds = false;    // per-layer per-tile background meshes (see setTerrainTileBackgrounds)
        std::vector<TileId> _terrainPaintTiles; // what a paint covers when it draws itself
        GLuint _terrainShadowTexture = 0;
        int _terrainShadowMapSize = 0;
        int _terrainShadowCascades = 1;
        std::array<float, MAX_SHADOW_CASCADES> _terrainShadowBiases = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        GLuint _terrainShadowMaskTexture = 0;
        cglib::vec2<float> _terrainShadowMaskScale = cglib::vec2<float>(0.0f, 0.0f);
        bool _terrainShadowMaskPass = false; // the draw that produces the mask, not one that reads it
        float _terrainShadowStrength = 0.0f;
        float _terrainShadowSoftness = 1.0f;
        Color _fogColor;
        float _fogStartDistance = 0.0f;
        float _fogDistance = 0.0f;
        std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES> _terrainShadowViewProjs;
        Color _terrainBackgroundColor; // opaque terrain base fill + depth pre-pass color; transparent = depth-only
        std::vector<std::pair<TileId, GLint>> _debugOrderedTileMasks;
        TerrainTextureProvider _terrainTextureProvider;
        std::function<double(const cglib::vec3<double>&)> _labelElevationProvider;
        std::vector<TileId> _pendingLabelElevationTiles; // elevation tiles whose labels must be re-anchored
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
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _bitmapLabelMap; // for 'ground' labels and for 'billboard' labels
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _visibleBitmapLabelMap;  // for 'ground' labels and for 'billboard' labels
        std::vector<std::shared_ptr<Label>> _labels;
        int _resourceSweepCounter = 0;
        std::map<int, GlobalIdLabelMap> _layerLabelMap;
        std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceMap;
        GLuint _lastUsedProgram = 0; // currently bound program, 0 = unknown (see useProgram)
        std::map<std::string, ShaderProgram> _shaderProgramMap;
        // Allocation-free front cache for the above. Building the string key (id + flags +
        // lighting + filter) is several heap allocations and a string-keyed map lookup, and it
        // runs once per DRAW CALL - measured at 170-510 geometry draws per frame for an
        // ordinary style, which made it a per-frame cost of its own. The key is the call
        // site's literal POINTER plus the flags, so a hit costs a hash of four words; a miss
        // falls through to the string path, which still de-duplicates the programs themselves.
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

        // Memo for the style colour/width/offset functions. They take only the view state, fixed
        // for the frame, yet every draw re-evaluated up to MAX_PARAMETERS of each: 16.5 us of a
        // 44 us draw on an Adreno 610 with a 21-layer style. Cleared in setViewState, the only place
        // the argument changes. The entry holds a reference to the function object it is keyed on,
        // or a function that died mid-frame could have its address reused and answer wrongly.
        std::unordered_map<const void*, std::pair<FloatFunction::Function, float>> _floatFuncCache;
        std::unordered_map<const void*, std::pair<ColorFunction::Function, Color>> _colorFuncCache;

        // Per-view-state tile matrix memo. The draw loop is style-layer-major, so the same
        // handful of tiles is transformed again for every style layer - 20-odd distinct
        // matrices recomputed several hundred times a frame, each a double 4x4 multiply on
        // top of the transformer call. Cleared alongside the style function memo.
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
        // The elevation texture a tile stands on, resolved once per frame. setupTerrainUniforms
        // runs per DRAW, so with a dozen style layers the same tile asked the provider (a
        // std::function into the SDK's elevation texture cache, two map lookups and a struct
        // fill) a dozen times for an answer that cannot change within the frame.
        mutable std::unordered_map<TileId, std::pair<bool, TerrainTexture>> _terrainTextureCache;

        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<double>, TileMatrixKeyHash> _tileMatrixCache;
        mutable std::unordered_map<TileMatrixKey, cglib::mat4x4<float>, TileMatrixKeyHash> _tileMVPMatrixCache;

        // Compiled tile geometry, keyed on the raw pointer. The other compiled-* maps below
        // are keyed by weak_ptr with owner_less, so a lookup builds a weak_ptr (two atomic
        // refcount ops) and walks a red-black tree of control blocks - measured at 4-6 us per
        // draw call for a ~100% hit rate, and this is the one looked up on EVERY draw. The
        // weak_ptr rides along in the value instead, which is all the liveness sweep in
        // endFrame needs.
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
