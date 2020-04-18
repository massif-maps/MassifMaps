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
#include <array>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <mutex>

#include <boost/optional.hpp>

#include <cglib/ray.h>

namespace carto { namespace vt {
    class LabelCuller;

    class GLTileRenderer final {
    public:
        struct LightingShader {
            bool perVertex;
            std::string shader;
            std::function<void(GLuint, const ViewState&)> setupFunc;

            explicit LightingShader(bool perVertex, std::string shader, std::function<void(GLuint, const ViewState&)> setupFunc) : perVertex(perVertex), shader(std::move(shader)), setupFunc(std::move(setupFunc)) { }
        };
        
        explicit GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, const boost::optional<LightingShader>& lightingShader2D, const boost::optional<LightingShader>& lightingShader3D, float scale);

        void setInteractionMode(bool enabled);
        void setSubTileBlending(bool enabled);
        void setViewState(const ViewState& viewState);
        void setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, bool blend);

        void initializeRenderer();
        void resetRenderer();
        void deinitializeRenderer();

        void startFrame(float dt);
        bool renderGeometry2D();
        bool renderGeometry3D();
        bool renderLabels(bool labels2D, bool labels3D);
        void endFrame();

        void cullLabels(LabelCuller& culler);

        bool findGeometryIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, long long>>& results, float radius, bool geom2D, bool geom3D) const;
        bool findLabelIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, long long>>& results, float radius, bool labels2D, bool labels3D) const;
        bool findBitmapIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, TileBitmap, cglib::vec2<float>>>& results) const;

    private:
        using BitmapLabelMap = std::unordered_map<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        struct BlendNode {
            TileId tileId;
            std::shared_ptr<const Tile> tile;
            float blend;
            std::vector<std::shared_ptr<BlendNode>> childNodes;

            explicit BlendNode(const TileId& tileId, std::shared_ptr<const Tile> tile, float blend) : tileId(tileId), tile(std::move(tile)), blend(blend), childNodes() { }
        };

        struct RenderNode {
            TileId tileId;
            std::shared_ptr<const Tile> tile;
            std::shared_ptr<const TileLayer> layer;
            float initialBlend;
            float blend;

            explicit RenderNode(const TileId& tileId, std::shared_ptr<const Tile> tile, std::shared_ptr<const TileLayer> layer, float blend) : tileId(tileId), tile(std::move(tile)), layer(std::move(layer)), initialBlend(blend), blend(blend) { }
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
            std::vector<GLuint> attribs;

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

            CompiledSurface() : vertexGeometryVBO(0), indicesVBO(0) { }
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
            constexpr static int MAX_PARAMETERS = 16;

            int labelCount;
            int parameterCount;
            float scale;
            cglib::mat4x4<double> labelMatrix;
            std::array<cglib::vec4<float>, MAX_PARAMETERS> colorTable;
            std::array<float, MAX_PARAMETERS> widthTable;
            std::array<float, MAX_PARAMETERS> strokeWidthTable;

            LabelBatchParameters() : labelCount(0), parameterCount(0), scale(0), labelMatrix(cglib::mat4x4<double>::identity()), colorTable(), widthTable(), strokeWidthTable() { }
        };

        struct LabelHash {
            std::size_t operator()(const std::pair<int, long long>& labelId) const {
                return labelId.first ^ static_cast<std::size_t>(labelId.second & 0xffffffff) ^ static_cast<std::size_t>(labelId.second >> 32);
            }
        };

        constexpr static float HALO_RADIUS_SCALE = 2.5f; // the scaling factor for halo radius
        constexpr static float POLYGON3D_HEIGHT_SCALE = 10018754.17f; // scaling factor for zoom 0 heights

        bool isTileVisible(const TileId& tileId) const;

        cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat3x3<double> calculateTileMatrix2D(const TileId& tileId, float coordScale = 1.0f) const;
        cglib::mat4x4<float> calculateTileMVPMatrix(const TileId& tileId, float coordScale = 1.0f) const;

        float calculateBlendNodeOpacity(const BlendNode& blendNode, float blend) const;
        
        void updateBlendNode(BlendNode& blendNode, float dBlend) const;
        bool buildRenderNodes(const BlendNode& blendNode, float blend, std::multimap<int, RenderNode>& renderNodeMap) const;
        void addRenderNode(RenderNode renderNode, std::multimap<int, RenderNode>& renderNodeMap) const;
        void updateLabels(const std::vector<std::shared_ptr<Label>>& labels, float dOpacity) const;

        void findTileGeometryIntersections(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileGeometry>& geometry, const cglib::ray3<double>& ray, float radius, float heightScale, std::vector<std::pair<double, long long>>& results) const;
        void findTileSurfaceIntersections(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileSurface>& tileSurface, const cglib::ray3<double>& ray, std::vector<std::pair<double, cglib::vec2<float>>>& results) const;
        bool findLabelIntersection(const std::shared_ptr<Label>& label, const cglib::ray3<double>& ray, float radius, double& result) const;

        bool renderBlendNodes2D(const std::vector<std::shared_ptr<BlendNode>>& blendNodes, int stencilBits);
        bool renderBlendNodes3D(const std::vector<std::shared_ptr<BlendNode>>& blendNodes);
        bool renderLabels(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap);

        void blendScreenTexture(float opacity, GLuint texture);
        void blendTileTexture(const TileId& tileId, float opacity, GLuint texture);
        void renderTileMask(const TileId& tileId);
        void renderTileBackground(const TileId& tileId, const std::shared_ptr<TileBackground>& background, float tileSize, float opacity);
        void renderTileBitmap(const TileId& tileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap);
        void renderTileGeometry(const TileId& tileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<TileGeometry>& geometry);
        void renderLabelBatch(const LabelBatchParameters& labelBatchParams, const std::shared_ptr<const Bitmap>& bitmap);

        const CompiledBitmap& buildCompiledBitmap(const std::shared_ptr<const Bitmap>& bitmap, bool genMipmaps);
        const CompiledBitmap& buildCompiledTileBitmap(const std::shared_ptr<TileBitmap>& tileBitmap);
        const CompiledGeometry& buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry);
        const ShaderProgram& buildShaderProgram(const std::string& id, const std::string& vsh, const std::string& fsh, bool pattern, bool translate, bool lighting2D, bool lighting3D, bool derivs);
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

        boost::optional<LightingShader> _lightingShader2D;
        boost::optional<LightingShader> _lightingShader3D;
        TileSurfaceBuilder _tileSurfaceBuilder;

        std::vector<FrameBuffer> _layerBuffers;
        FrameBuffer _overlayBuffer;
        CompiledQuad _screenQuad;

        ViewState _viewState;
        cglib::mat4x4<double> _cameraProjMatrix;
        float _fullResolution = 0;
        float _halfResolution = 0;
        int _screenWidth = 0;
        int _screenHeight = 0;
        cglib::vec3<double> _tileSurfaceBuilderOrigin;
        std::set<TileId> _tileSurfaceBuilderOriginTileIds;

        bool _subTileBlending = false;
        bool _interactionMode = false;

        std::shared_ptr<std::vector<std::shared_ptr<BlendNode>>> _blendNodes;
        std::shared_ptr<std::vector<std::shared_ptr<BlendNode>>> _renderBlendNodes;
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _bitmapLabelMap; // for 'ground' labels and for 'billboard' labels
        std::array<std::shared_ptr<BitmapLabelMap>, 2> _renderBitmapLabelMap;  // for 'ground' labels and for 'billboard' labels
        std::vector<std::shared_ptr<Label>> _labels;
        std::unordered_map<std::pair<int, long long>, std::shared_ptr<Label>, LabelHash> _labelMap;
        std::unordered_map<TileId, std::vector<std::shared_ptr<TileSurface>>> _tileSurfaceMap;
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
} }

#endif
