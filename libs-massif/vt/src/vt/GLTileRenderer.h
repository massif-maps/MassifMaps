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
            cglib::vec4<float> decode = cglib::vec4<float>(0, 0, 0, 0);     // texture sample -> meters
            float metersToInternal = 0.0f; // meters -> world z units at the equator (exaggeration included)
            float mercatorYScale = 0.0f;   // world y -> mercator angle (for the per-vertex 1/cos(latitude) factor)
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

        explicit GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, float scale);

        void setLightingShader2D(const std::optional<LightingShader>& lightingShader2D);
        void setLightingShader3D(const std::optional<LightingShader>& lightingShader3D);
        void setLightingShaderNormalMap(const std::optional<LightingShader>& lightingShaderNormalMap);
        
        void setInteractionMode(bool enabled);
        void setTerrainMode(bool enabled, float depthBias);
        void setTerrainRegularGrid(bool enabled, int resolution);
        void setTerrainPainterOrder(bool enabled);
        void setTerrainSlackScale(float slackScale);
        void setTerrainDrapeFills(bool enabled, bool includeLines);
        void setTerrainDrapeResolution(int resolution);
        void setTerrainLighting(const TerrainLighting& lighting);
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
        // Light-space view-projection fitted to the given terrain tiles. Returns false when the
        // tile set is empty or no elevation data is available yet.
        // minHeight/maxHeight bound the shadowed volume in world z units. A generous slab is
        // what makes a low sun pixelated: the light box is fitted around it, and at a grazing
        // angle a 10 km slab stretches the box to tens of kilometres across.
        // mapSize is the shadow map resolution: the box is snapped to a world-anchored lattice of
        // whole texels, so the matrix repeats exactly while the camera moves inside one step.
        // One call per cascade: cascade c of cascadeCount covers its own slice of the camera's
        // view distance, so the near slice gets a box small enough for its texels to be about the
        // size of a screen pixel, while the far slice - where a screen pixel is tens of metres of
        // ground anyway - keeps the coarse one.
        bool calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float maxDistanceMeters, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const;
        // Moves as the caster geometry does: 3D extrusions fade in by growing, so the sum of
        // their blend factors says how far the shadow map has drifted from what is on screen.
        float shadowCasterFadeSignature() const;
        // Draws this renderer's shadow casters for one terrain tile into the bound framebuffer.
        // Returns the number of draws issued.
        int renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround);

        // Cross-layer drape (S3). When an external drape target is set, this renderer stops
        // owning drape textures and becomes a pure content baker: the owner collects the target
        // tiles via collectDrapeTiles, bakes every participating renderer into ONE texture per
        // tile in layer order via bakeDrapeTile, and then draws the surface once per tile with
        // renderDrapedSurface. That is what lets a hillshade layer and a vector tile layer share
        // a single drape texture, a single surface draw and a single depth domain.
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
        void setDebugSurfacePrefill(bool enabled);
        void setTerrainBackgroundColor(const Color& color);
        void setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)> provider, unsigned int version);
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
        void setClickHandlerLayerFilter(const std::optional<std::regex>& filter);
        void setViewState(const ViewState& viewState);
        void setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles);
        void teleportVisibleTiles(int dx, int dy);

        void initializeRenderer();
        void resetRenderer();
        void resetTileSurfaces();
        void invalidateTileSurfaces(const std::vector<TileId>& tileIds);
        void deinitializeRenderer();

        bool startFrame(float dt);
        void renderGeometry(bool geom2D, bool geom3D);
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
            NORMALMAP
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
            mutable bool geometryVAOInitialized;

            CompiledGeometry() : vertexGeometryVBO(0), indicesVBO(0), geometryVAO(0), geometryVAOInitialized(false) { }
        };

        struct CompiledLabelBatch {
            GLuint verticesVBO;
            GLuint normalsVBO;
            GLuint texCoordsVBO;
            GLuint attribsVBO;
            GLuint indicesVBO;

            CompiledLabelBatch() : verticesVBO(0), normalsVBO(0), texCoordsVBO(0), attribsVBO(0), indicesVBO(0) { }
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

        static constexpr float HALO_RADIUS_SCALE = 2.5f; // the scaling factor for halo radius
        static constexpr float STROKE_UV_SCALE = 2.857f; // stroked line UV scale factor
        static constexpr float POLYGON3D_HEIGHT_SCALE = 10018754.17f; // scaling factor for zoom 0 heights
        static constexpr float TERRAIN_LAYER_DEPTH_DELTA = 1.0f / 524288.0f; // 2^-19: NDC depth separation per draped layer bias unit (GPU terrain draping mode)
        static constexpr float TERRAIN_PAINTER_SURFACE_BACK = 2.0f; // painter-order: clip-slack units the depth-writing surface is pushed BACK (same magnitude as the regular-grid geometry forward slack); geometry then draws at its real depth with the same twist clearance but no forward pull, so it can not leak in front of a near ridge
        static constexpr float TERRAIN_EXTRUSION_DEPTH_DELTAS = 24.0f; // 3D extrusions vs the terrain surface pre-pass in the 3D overlay. A wall stands ON the ground it is tested against, so the pair is only separable to the depth buffer's resolution, which in eye units grows like distance^2/near - the same law a constant-NDC bias follows, which is why the clearance is expressed here and not as clip slack. Measured on the demo: 1 delta eats most of a 40 m building at a few km, 24 restores it, 64 adds nothing (and every extra delta is a wider band in which an extrusion behind a crest can show through it)
        static constexpr float TERRAIN_DEPTH_CLIP_SLACK = 1.0e-3f; // clip-space depth shift per bias unit at the reference tile size, scaled by tile size (quadratic law, see setupTerrainUniforms) and |proj m22|
        static constexpr double TERRAIN_DEPTH_CLIP_REF_TILE_SIZE = 512.0; // zoom 11 tile size in internal units - the anchor of the quadratic slack law
        static constexpr float ALPHA_HIT_THRESHOLD = 0.05f; // threshold value for 'transparent' pixel alphas
        static constexpr std::size_t DRAPE_TEXTURE_POOL_SIZE = 32; // recycled drape textures kept alive between frames

        bool isTileVisible(const TileId& tileId) const;
        bool isEmptyBlendRequired(CompOp compOp) const;

        unsigned int fogFlag() const;
        void setupFogUniforms(const ShaderProgram& shaderProgram) const;
        cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat3x3<double> calculateTileMatrix2D(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat4x4<float> calculateTileMVPMatrix(const TileId& tileId, float coordScale = 1.0f) const;

        bool testLayerFilter(const std::string& layerName, const std::optional<std::regex>& filter) const;
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
        void renderGeometry3D(const std::vector<RenderTile>& renderTiles);
        void renderLabels(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap);

        void setCompOp(CompOp compOp);
        void blendScreenTexture(float opacity, GLuint texture);
        void updateTerrainSkirts();
        bool setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        void setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix);
        void renderTileMask(const TileId& tileId);
        void renderStencilDebugOverlay();
        void renderTileSurfaceFill(const TileId& tileId, const Color& color);
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
        void renderTileBackground(const TileId& tileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileBackground>& background);
        void renderTileBitmap(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap);
        void renderTileGeometry(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileGeometry>& geometry);
        void renderLabelBatch(const LabelBatchParameters& labelBatchParams, const std::shared_ptr<const Bitmap>& bitmap);

        const CompiledBitmap& buildCompiledBitmap(const std::shared_ptr<const Bitmap>& bitmap, bool genMipmaps);
        const CompiledBitmap& buildCompiledTileBitmap(const std::shared_ptr<TileBitmap>& tileBitmap);
        const CompiledGeometry& buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry);
        const ShaderProgram& buildShaderProgram(const std::string& id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags);
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
        int _screenWidth = 0;
        int _screenHeight = 0;
        cglib::vec3<double> _tileSurfaceBuilderOrigin = cglib::vec3<double>(0, 0, 0);
        std::set<TileId> _tileSurfaceBuilderOriginTileIds;

        bool _interactionMode = false;
        bool _terrainMode = false;
        bool _terrainDepthWrite = false;
        float _terrainDepthBias = 0.0f;
        float _terrainSlackScale = 1.0f;         // scales the clip-constant slack; ~(32/meshResolution)^2 - the chord error shrinks quadratically with the tesselation
        float _terrainDrawDepthBias = 0.0f;      // per-draw NDC (w-scaled) depth bias while rendering 2D layers (GPU draping mode)
        float _terrainDrawDepthClipUnits = 0.0f; // per-draw clip-constant slack units (distance-growing; see setupTerrainUniforms)
        bool _terrainSkirtsEnabled = false;
        bool _terrainRegularGrid = false;        // shared unit-grid surfaces instead of per-tile tesselated meshes (planar terrain)
        int _terrainRegularGridResolution = 0;   // resolution of the currently built shared grid
        std::vector<std::shared_ptr<TileSurface>> _terrainGridSurfaces;
        std::vector<std::shared_ptr<TileSurface>> _terrainFlatSurfaces; // 1x1 grid for the flat drape bake // the single shared unit-grid surface, drawn per tile
        bool _terrainPainterOrder = false;       // tangram painter-order depth model (no surface occluder / no slack); implies regular grid
        float _terrainDrawLayerOffset = 0.0f;    // painter-order per-draw (proxy - layer) offset
        bool _terrainDrapeFills = false;         // maplibre-style: bake polygon fills flat to a per-tile texture, sampled on the surface
        bool _terrainDrapeLines = false;         // also bake vt tile lines into the drape texture (softer, but zero leak/hug error)
        int _drapeTextureSize = 512;             // per-tile drape texture resolution
        GLuint _drapeFBO = 0;                    // shared offscreen FBO for baking drape textures
        std::map<TileId, GLuint> _drapeTextures; // per-target-tile baked drape textures
        std::map<TileId, std::size_t> _drapeFingerprints; // what each cached texture was baked from; a change means it is stale
        std::vector<GLuint> _drapeTexturePool;   // recycled textures, so panning does not churn GL allocations
        std::vector<GLuint> _drapeStaleTextures; // wrong-size textures awaiting deletion on the GL thread
        bool _externalDrapeTarget = false;       // drape textures are owned by the caller (cross-layer stacks)
        std::set<TileId> _drapeTilesThisFrame;   // target tiles that have a valid drape texture this frame
        std::vector<TileId> _externalDrapeTiles; // terrain tiles the owner drapes this frame
        const cglib::mat4x4<double>* _shadowCasterViewProj = nullptr; // set during the shadow caster pass
        const cglib::mat4x4<float>* _drapeMVPOverride = nullptr; // when set, renderTileGeometry draws flat into the drape FBO
        bool _debugWireframe = false;
        bool _debugSurfacePrefill = false;
        TerrainLighting _terrainLighting;
        GLuint _terrainShadowTexture = 0;
        int _terrainShadowMapSize = 0;
        int _terrainShadowCascades = 1;
        std::array<float, MAX_SHADOW_CASCADES> _terrainShadowBiases = { { 0.0f, 0.0f, 0.0f, 0.0f } };
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
        unsigned int _labelElevationVersion = 0;
        unsigned int _appliedLabelElevationVersion = 0;
        std::function<bool(const cglib::vec3<double>&)> _labelOcclusionTest;
        float _layerBlendingSpeed = 1.0f;
        float _labelBlendingSpeed = 1.0f;
        RasterFilterMode _rasterFilterMode = RasterFilterMode::BILINEAR;
        std::optional<std::regex> _rendererLayerFilter;
        std::optional<std::pair<int, int>> _rendererLayerIndexRange;
        std::optional<std::regex> _clickHandlerLayerFilter;

        std::shared_ptr<std::vector<RenderTile>> _renderTiles;
        std::shared_ptr<std::vector<RenderTile>> _visibleRenderTiles;
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _bitmapLabelMap; // for 'ground' labels and for 'billboard' labels
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _visibleBitmapLabelMap;  // for 'ground' labels and for 'billboard' labels
        std::vector<std::shared_ptr<Label>> _labels;
        std::map<int, GlobalIdLabelMap> _layerLabelMap;
        std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceMap;
        std::map<std::string, ShaderProgram> _shaderProgramMap;
        std::map<std::weak_ptr<const Bitmap>, CompiledBitmap, std::owner_less<std::weak_ptr<const Bitmap>>> _compiledBitmapMap;
        std::map<std::weak_ptr<const TileBitmap>, CompiledBitmap, std::owner_less<std::weak_ptr<const TileBitmap>>> _compiledTileBitmapMap;
        std::map<std::weak_ptr<const TileGeometry>, CompiledGeometry, std::owner_less<std::weak_ptr<const TileGeometry>>> _compiledTileGeometryMap;
        std::map<std::weak_ptr<const TileSurface>, CompiledSurface, std::owner_less<std::weak_ptr<const TileSurface>>> _compiledTileSurfaceMap;
        std::map<int, CompiledLabelBatch> _compiledLabelBatches;
        int _labelBatchCounter = 0;

        VertexArray<cglib::vec3<float>> _labelVertices;
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
