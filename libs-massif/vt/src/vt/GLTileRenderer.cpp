#include "GLTileRenderer.h"
#include "GLTileRendererShaders.h"
#include "Color.h"
#include "TileGeometryIterator.h"
#include "TileSurfaceBuilder.h"
#include "BitmapManager.h"
#include "LabelCuller.h"

#include <cassert>
#include <algorithm>

namespace {
    bool isEmptyBlendRequired(carto::vt::CompOp compOp) {
        using carto::vt::CompOp;

        switch (compOp) {
        case CompOp::SRC:
        case CompOp::SRC_OVER:
        case CompOp::DST_OVER:
        case CompOp::DST_ATOP:
        case CompOp::PLUS:
        case CompOp::MINUS:
        case CompOp::LIGHTEN:
            return false;
        default:
            return true;
        }
    }

    void setGLBlendState(carto::vt::CompOp compOp) {
        using carto::vt::CompOp;
        
        struct GLBlendState {
            GLenum blendEquation;
            GLenum blendFuncSrc;
            GLenum blendFuncDst;
        };
        
        static const std::map<CompOp, GLBlendState> compOpBlendStates = {
            { CompOp::SRC,      { GL_FUNC_ADD, GL_ONE, GL_ZERO } },
            { CompOp::SRC_OVER, { GL_FUNC_ADD, GL_ONE, GL_ONE_MINUS_SRC_ALPHA } },
            { CompOp::SRC_IN,   { GL_FUNC_ADD, GL_DST_ALPHA, GL_ZERO } },
            { CompOp::SRC_ATOP, { GL_FUNC_ADD, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA } },
            { CompOp::DST,      { GL_FUNC_ADD, GL_ZERO, GL_ONE } },
            { CompOp::DST_OVER, { GL_FUNC_ADD, GL_ONE_MINUS_DST_ALPHA, GL_ONE } },
            { CompOp::DST_IN,   { GL_FUNC_ADD, GL_ZERO, GL_SRC_ALPHA } },
            { CompOp::DST_ATOP, { GL_FUNC_ADD, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA } },
            { CompOp::ZERO,     { GL_FUNC_ADD, GL_ZERO, GL_ZERO } },
            { CompOp::PLUS,     { GL_FUNC_ADD, GL_ONE, GL_ONE } },
            { CompOp::MINUS,    { GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE } },
            { CompOp::MULTIPLY, { GL_FUNC_ADD, GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA } },
            { CompOp::SCREEN,   { GL_FUNC_ADD, GL_ONE, GL_ONE_MINUS_SRC_COLOR } },
            { CompOp::DARKEN,   { GL_MIN_EXT,  GL_ONE, GL_ONE } },
            { CompOp::LIGHTEN,  { GL_MAX_EXT,  GL_ONE, GL_ONE } }
        };
        
        auto it = compOpBlendStates.find(compOp);
        if (it != compOpBlendStates.end()) {
            glBlendFunc(it->second.blendFuncSrc, it->second.blendFuncDst);
            glBlendEquation(it->second.blendEquation);
        }
    }

    void checkGLError() {
        for (GLenum error = glGetError(); error != GL_NONE; error = glGetError()) {
            assert(error != GL_NONE);
        }
    }
}

namespace carto { namespace vt {
    GLTileRenderer::GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, const boost::optional<LightingShader>& lightingShader2D, const boost::optional<LightingShader>& lightingShader3D, float scale) :
        _lightingShader2D(lightingShader2D), _lightingShader3D(lightingShader3D), _tileSurfaceBuilder(transformer), _cameraProjMatrix(cglib::mat4x4<double>::identity()), _glExtensions(std::move(glExtensions)), _transformer(std::move(transformer)), _scale(scale), _mutex()
    {
        _blendNodes = std::make_shared<std::vector<std::shared_ptr<BlendNode>>>();
        _bitmapLabelMap[0] = _bitmapLabelMap[1] = std::make_shared<BitmapLabelMap>();
    }

    void GLTileRenderer::setInteractionMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _interactionMode = enabled;
    }

    void GLTileRenderer::setSubTileBlending(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _subTileBlending = enabled;
    }
    
    void GLTileRenderer::setViewState(const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        _cameraProjMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
        _fullResolution = viewState.resolution;
        _halfResolution = viewState.resolution * 0.5f;
        _viewState = viewState;
        _viewState.zoomScale *= _scale;
    }
    
    void GLTileRenderer::setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, bool blend) {
        using TilePair = std::pair<TileId, std::shared_ptr<const Tile>>;

        // Clear the 'visible' label list for now (used only for culling)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _labels.clear();
        }

        // Build visible tile list for labels. Also build tile surfaces.
        std::set<TileId> tileIds;
        std::vector<std::shared_ptr<const Tile>> labelTiles;
        for (const TilePair& tilePair : tiles) {
            tileIds.insert(tilePair.first);
            
            if (tilePair.second) {
                // Keep only unique tiles and order them by tile zoom level.
                // This will fix flickering when multiple tiles from different zoom levels redefine same label.
                auto it = std::lower_bound(labelTiles.begin(), labelTiles.end(), tilePair.second, [](const std::shared_ptr<const Tile>& tile1, const std::shared_ptr<const Tile>& tile2) {
                    return std::make_pair(tile2->getTileId(), tile2) < std::make_pair(tile1->getTileId(), tile1);
                });
                if (it == labelTiles.end() || *it != tilePair.second) {
                    labelTiles.insert(it, tilePair.second);
                }
            }
        }

        // All other operations must be synchronized
        std::lock_guard<std::mutex> lock(_mutex);

        // Update tile surface builder tile list (needed to avoid T-vertices in tesselation). Reset origin only if all tiles change.
        bool updateOrigin = true;
        for (const TileId& oldTileId : _tileSurfaceBuilderOriginTileIds) {
            if (tileIds.find(oldTileId) != tileIds.end()) {
                updateOrigin = false;
                break;
            }
        }
        if (updateOrigin) {
            cglib::vec3<double> origin(0, 0, 0);
            for (const TileId& tileId : tileIds) {
                origin += _transformer->calculateTileBBox(tileId).center() * (1.0 / tileIds.size());
            }
            _tileSurfaceBuilderOrigin = origin;
            _tileSurfaceBuilderOriginTileIds = tileIds;
            _tileSurfaceBuilder.setOrigin(origin);
        }
        _tileSurfaceBuilder.setVisibleTiles(tileIds);

        // Reset surface caches. Note that this does not mean that the surfaces are not cached.
        _tileSurfaceMap.clear();

        // Create label list, merge geometries
        std::unordered_map<std::pair<int, long long>, std::shared_ptr<Label>, LabelHash> newLabelMap;
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(tile->getTileId(), 1.0f);
            std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tile->getTileId());
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    std::pair<int, long long> labelId(layer->getLayerIndex(), tileLabel->getGlobalId());
                    auto label = std::make_shared<Label>(*tileLabel, tileMatrix, transformer);
                    auto newLabelIt = newLabelMap.find(labelId);
                    if (newLabelIt != newLabelMap.end()) {
                        newLabelIt->second->mergeGeometries(*label);
                        continue;
                    }
                    newLabelMap[labelId] = label;
                }
            }
        }

        // Release old labels
        for (auto labelIt = _labelMap.begin(); labelIt != _labelMap.end();) {
            if (labelIt->second->getOpacity() <= 0) {
                labelIt = _labelMap.erase(labelIt);
            } else {
                labelIt->second->setActive(false);
                labelIt++;
            }
        }
        
        // Copy existing label placements
        for (auto newLabelIt = newLabelMap.begin(); newLabelIt != newLabelMap.end(); newLabelIt++) {
            auto oldLabelIt = _labelMap.find(newLabelIt->first);
            if (oldLabelIt != _labelMap.end()) {
                newLabelIt->second->setVisible(oldLabelIt->second->isVisible());
                newLabelIt->second->setOpacity(oldLabelIt->second->getOpacity());
                newLabelIt->second->snapPlacement(*oldLabelIt->second);
            } else {
                newLabelIt->second->setVisible(false);
                newLabelIt->second->setOpacity(0);
            }
            newLabelIt->second->setActive(true);
            _labelMap[newLabelIt->first] = newLabelIt->second;
        }
        
        // Build final label list, group labels by font bitmaps
        std::vector<std::shared_ptr<Label>> labels;
        labels.reserve(_labelMap.size());
        std::array<std::shared_ptr<BitmapLabelMap>, 2> bitmapLabelMap;
        bitmapLabelMap[0] = std::make_shared<BitmapLabelMap>();
        bitmapLabelMap[1] = std::make_shared<BitmapLabelMap>();
        for (auto labelIt = _labelMap.begin(); labelIt != _labelMap.end(); labelIt++) {
            const std::shared_ptr<Label>& label = labelIt->second;
            int pass = (label->getStyle()->orientation == LabelOrientation::BILLBOARD_3D ? 1 : 0);
            (*bitmapLabelMap[pass])[label->getStyle()->glyphMap->getBitmapPattern()->bitmap].push_back(label);
            labels.push_back(label);
        }
        _labels = std::move(labels);
        _bitmapLabelMap = std::move(bitmapLabelMap);

        // Sort labels by priority. This is a hack implementation and is needed only for labels that 'are allowed' to overlap each other.
        // The current implementation only does proper ordering within single bitmap atlas, but usually this is enough
        for (int pass = 0; pass < 2; pass++) {
            for (auto it = _bitmapLabelMap[pass]->begin(); it != _bitmapLabelMap[pass]->end(); it++) {
                std::stable_sort(it->second.begin(), it->second.end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
                    return label1->getPriority() < label2->getPriority();
                });
            }
        }
        
        // Build blend nodes for tiles
        auto blendNodes = std::make_shared<std::vector<std::shared_ptr<BlendNode>>>();
        blendNodes->reserve(tiles.size());
        for (const TilePair& tilePair : tiles) {
            auto blendNode = std::make_shared<BlendNode>(tilePair.first, tilePair.second, blend ? 0.0f : 1.0f);
            for (std::shared_ptr<BlendNode>& oldBlendNode : *_blendNodes) {
                if (blendNode->tileId == oldBlendNode->tileId && blendNode->tile == oldBlendNode->tile) {
                    blendNode = oldBlendNode;
                    break;
                }
                if (blend && blendNode->tileId.intersects(oldBlendNode->tileId)) {
                    bool subTileBlending = _subTileBlending;

                    // Disable subtile blending if alpha channel is used on the tile
                    for (const std::shared_ptr<TileLayer>& layer : blendNode->tile->getLayers()) {
                        for (const std::shared_ptr<TileBitmap>& bitmap : layer->getBitmaps()) {
                            if (bitmap->getFormat() == TileBitmap::Format::RGBA) {
                                subTileBlending = false;
                            }
                        }
                    }

                    if (subTileBlending) {
                        blendNode->childNodes.push_back(oldBlendNode);
                        oldBlendNode->blend = calculateBlendNodeOpacity(*oldBlendNode, 1.0f); // this is an optimization, to reduce extensive blending subtrees
                        oldBlendNode->childNodes.clear();
                    } else {
                        blendNode->blend = std::max(blendNode->blend, oldBlendNode->blend);
                    }
                }
            }
            blendNodes->push_back(std::move(blendNode));
        }
        _blendNodes = std::move(blendNodes);
    }

    void GLTileRenderer::initializeRenderer() {
        const std::map<std::string, std::pair<std::string, std::string>> shaderMap = {
            { "blend", { blendVsh, blendFsh } },
            { "background", { backgroundVsh, backgroundFsh } },
            { "bitmap", { bitmapVsh, bitmapFsh } },
            { "label", { labelVsh, labelFsh } },
            { "point", { pointVsh, pointFsh } },
            { "line", { lineVsh, lineFsh } },
            { "polygon", { polygonVsh, polygonFsh } },
            { "polygon3d", { polygon3DVsh, polygon3DFsh } }
        };

        std::lock_guard<std::mutex> lock(_mutex);

        // Register shaders
        for (auto it = shaderMap.begin(); it != shaderMap.end(); it++) {
            std::string vsh = commonVsh;
            std::string fsh = commonFsh;
            if (it->first == "polygon3d") {
                if (_lightingShader3D) {
                    if (_lightingShader3D->perVertex) {
                        vsh += _lightingShader3D->shader;
                    } else {
                        fsh += _lightingShader3D->shader;
                    }
                }
            } else if (it->first != "blend") {
                if (_lightingShader2D) {
                    if (_lightingShader2D->perVertex) {
                        vsh += _lightingShader2D->shader;
                    } else {
                        fsh += _lightingShader2D->shader;
                    }
                }
            }
            vsh += it->second.first;
            fsh += it->second.second;
            
            _shaderManager.registerShaders(it->first, vsh, fsh);
        }
        
        // Create shader contexts
        _defaultContext = std::make_shared<std::set<std::string>>();

        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                auto defs = std::make_shared<std::set<std::string>>();
                if (i != 0) {
                    defs->insert("PATTERN");
                }
                if (j != 0) {
                    defs->insert("TRANSFORM");
                }
                if (_lightingShader2D) {
                    defs->insert(_lightingShader2D->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                }
                _patternTransformLighting2DContext[i][j] = defs;
            }
        }

        for (int i = 0; i < 2; i++) {
            auto defs = std::make_shared<std::set<std::string>>();
            if (i != 0) {
                defs->insert("TRANSFORM");
            }
            if (_lightingShader3D) {
                defs->insert(_lightingShader3D->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
            }
            _transformLighting3DContext[i] = defs;
        }

        for (int i = 0; i < 2; i++) {
            auto defs = std::make_shared<std::set<std::string>>();
            if (i != 0) {
                defs->insert("DERIVATIVES");
            }
            if (_lightingShader2D) {
                defs->insert(_lightingShader2D->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
            }
            _derivativesLighting2DContext[i] = defs;
        }
    }
    
    void GLTileRenderer::resetRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Drop all caches with texture references/FBO/VBOs
        _compiledBitmapMap.clear();
        _compiledTileBitmapMap.clear();
        _compiledTileGeometryMap.clear();
        _compiledLabelBatches.clear();
        _layerBuffers.clear();
        _overlayBuffer = FrameBuffer();
        _screenQuad = CompiledQuad();

        // Reset shader programs
        _shaderManager.resetPrograms();
    }
        
    void GLTileRenderer::deinitializeRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Release compiled bitmaps (textures)
        for (auto it = _compiledBitmapMap.begin(); it != _compiledBitmapMap.end(); it++) {
            deleteCompiledBitmap(it->second);
        }
        _compiledBitmapMap.clear();

        // Release compiled tile bitmaps (textures)
        for (auto it = _compiledTileBitmapMap.begin(); it != _compiledTileBitmapMap.end(); it++) {
            deleteCompiledBitmap(it->second);
        }
        _compiledTileBitmapMap.clear();

        // Release compiled surfaces (VBOs)
        for (auto it = _compiledTileSurfaceMap.begin(); it != _compiledTileSurfaceMap.end(); it++) {
            deleteCompiledSurface(it->second);
        }
        _compiledTileSurfaceMap.clear();

        // Release compiled geometry (VBOs)
        for (auto it = _compiledTileGeometryMap.begin(); it != _compiledTileGeometryMap.end(); it++) {
            deleteCompiledGeometry(it->second);
        }
        _compiledTileGeometryMap.clear();

        // Release compiled label batches (VBOs)
        for (auto it = _compiledLabelBatches.begin(); it != _compiledLabelBatches.end(); it++) {
            deleteCompiledLabelBatch(it->second);
        }
        _compiledLabelBatches.clear();
        
        // Release FBOs
        for (auto it = _layerBuffers.begin(); it != _layerBuffers.end(); it++) {
            deleteFrameBuffer(*it);
        }
        _layerBuffers.clear();
        
        // Release screen and overlay FBOs
        deleteFrameBuffer(_overlayBuffer);

        // Release tile and screen VBOs
        deleteCompiledQuad(_screenQuad);

        // Release shader programs
        _shaderManager.deletePrograms();
        
        _blendNodes.reset();
        _renderBlendNodes.reset();
        _bitmapLabelMap[0].reset();
        _bitmapLabelMap[1].reset();
        _renderBitmapLabelMap[0].reset();
        _renderBitmapLabelMap[1].reset();
        _labels.clear();
        _labelMap.clear();
    }
    
    void GLTileRenderer::startFrame(float dt) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Update geometry blending state
        _renderBlendNodes = _blendNodes;
        for (std::shared_ptr<BlendNode>& blendNode : *_renderBlendNodes) {
            updateBlendNode(*blendNode, dt);
        }
        
        // Update labels
        _renderBitmapLabelMap = _bitmapLabelMap;
        for (int pass = 0; pass < 2; pass++) {
            for (const std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>& bitmapLabels : *_renderBitmapLabelMap[pass]) {
                updateLabels(bitmapLabels.second, dt);
            }
        }
        
        // Load viewport dimensions, update dependent values
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] != _screenWidth || viewport[3] != _screenHeight) {
            _screenWidth = viewport[2];
            _screenHeight = viewport[3];

            // Release layer FBOs
            for (auto it = _layerBuffers.begin(); it != _layerBuffers.end(); it++) {
                deleteFrameBuffer(*it);
            }
            _layerBuffers.clear();

            // Release screen/overlay FBOs
            deleteFrameBuffer(_overlayBuffer);
        }

        // Reset label batch counter
        _labelBatchCounter = 0;
    }
    
    bool GLTileRenderer::renderGeometry2D() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Update GL state
        int stencilBits = 0;
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // 2D geometry pass
        if (stencilBits > 0) {
            glEnable(GL_STENCIL_TEST);
            glStencilMask(255);
        }
        bool update = renderBlendNodes2D(*_renderBlendNodes, stencilBits);
        
        // Restore GL state
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(255);
        
        return update;
    }
    
    bool GLTileRenderer::renderGeometry3D() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Update GL state
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // 3D polygon pass
        bool update = renderBlendNodes3D(*_renderBlendNodes);
        
        // Restore GL state
        glEnable(GL_BLEND);
        glStencilMask(255);
        
        return update;
    }
    
    bool GLTileRenderer::renderLabels(bool labels2D, bool labels3D) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Update GL state
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // Label pass
        bool update = false;
        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && labels2D) || (pass == 1 && labels3D)) {
                for (const std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>& bitmapLabels : *_renderBitmapLabelMap[pass]) {
                    update = renderLabels(bitmapLabels.second, bitmapLabels.first) || update;
                }
            }
        }
        
        // Restore GL state
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glStencilMask(255);
        
        return update;
    }
    
    void GLTileRenderer::endFrame() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Release unused textures
        for (auto it = _compiledBitmapMap.begin(); it != _compiledBitmapMap.end();) {
            if (it->first.expired()) {
                deleteCompiledBitmap(it->second);
                it = _compiledBitmapMap.erase(it);
            } else {
                it++;
            }
        }
        
        // Release unused tile textures
        for (auto it = _compiledTileBitmapMap.begin(); it != _compiledTileBitmapMap.end();) {
            if (it->first.expired()) {
                deleteCompiledBitmap(it->second);
                it = _compiledTileBitmapMap.erase(it);
            } else {
                it++;
            }
        }

        // Release unused tile surface VBOs
        for (auto it = _compiledTileSurfaceMap.begin(); it != _compiledTileSurfaceMap.end();) {
            if (it->first.expired()) {
                deleteCompiledSurface(it->second);
                it = _compiledTileSurfaceMap.erase(it);
            } else {
                it++;
            }
        }

        // Release unused tile geometry VBOs
        for (auto it = _compiledTileGeometryMap.begin(); it != _compiledTileGeometryMap.end();) {
            if (it->first.expired()) {
                deleteCompiledGeometry(it->second);
                it = _compiledTileGeometryMap.erase(it);
            } else {
                it++;
            }
        }

        // Note: we do not release unused label batches. These are unlinkely very big and can be reused later
    }

    void GLTileRenderer::cullLabels(LabelCuller& culler) {
        std::vector<std::shared_ptr<Label>> labels;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            labels = _labels;
        }

        culler.process(labels, _mutex);
    }
    
    bool GLTileRenderer::findGeometryIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, long long>>& results, float radius, bool geom2D, bool geom3D) const {
        std::lock_guard<std::mutex> lock(_mutex);

        // Loop over all blending/rendering nodes
        std::size_t initialResultCount = results.size();
        for (const std::shared_ptr<BlendNode>& blendNode : *_blendNodes) {
            std::multimap<int, RenderNode> renderNodeMap;
            if (!buildRenderNodes(*blendNode, 1.0f, renderNodeMap)) {
                continue;
            }

            for (auto it = renderNodeMap.begin(); it != renderNodeMap.end(); it++) {
                const RenderNode& renderNode = it->second;

                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderNode.tileId.zoom > blendNode->tileId.zoom ? renderNode.tileId : blendNode->tileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderNode.tileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderNode.tileId);

                // Test all geometry batches for intersections
                cglib::ray3<double> rayTile = cglib::transform_ray(ray, invTileMatrix);
                for (const std::shared_ptr<TileGeometry>& geometry : renderNode.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        if (!geom3D) {
                            continue;
                        }
                    } else {
                        if (!geom2D || !cglib::intersect_bbox(tileBBox, ray)) {
                            continue;
                        }
                    }

                    std::vector<std::pair<double, long long>> resultsTile;
                    findTileGeometryIntersections(renderNode.tileId, blendNode->tile, geometry, rayTile, radius, blendNode->blend, resultsTile);

                    for (std::pair<double, long long> resultTile : resultsTile) {
                        long long id = resultTile.second;
                        cglib::vec3<float> posTile = cglib::vec3<float>::convert(rayTile(resultTile.first));
                        cglib::vec2<float> tilePos = tileTransformer->calculateTilePosition(posTile);

                        // Check that the hit position is inside the tile and normal is facing toward the ray
                        cglib::vec2<float> clipPos = tilePos;
                        if (blendNode->tileId.zoom > renderNode.tileId.zoom) {
                            cglib::mat3x3<double> clipTransform = cglib::inverse(calculateTileMatrix2D(blendNode->tileId)) * calculateTileMatrix2D(renderNode.tileId);
                            clipPos = cglib::transform_point(tilePos, cglib::mat3x3<float>::convert(clipTransform));
                        }
                        if (clipPos(0) < 0 || clipPos(1) < 0 || clipPos(0) > 1 || clipPos(1) > 1) {
                            continue;
                        }
                        cglib::vec3<float> normal = tileTransformer->calculateNormal(tilePos);
                        if (cglib::dot_product(normal, cglib::vec3<float>::convert(ray.direction)) >= 0) {
                            continue;
                        }

                        cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(posTile), tileMatrix);
                        results.emplace_back(renderNode.tileId, cglib::dot_product(pos - ray.origin, ray.direction) / cglib::dot_product(ray.direction, ray.direction), id);
                    }
                }
            }
        }

        return results.size() > initialResultCount;
    }
    
    bool GLTileRenderer::findLabelIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, long long>>& results, float radius, bool labels2D, bool labels3D) const {
        std::lock_guard<std::mutex> lock(_mutex);

        // Test for label intersections. The ordering may be mixed compared to actual rendering order, but this is non-issue if the labels are non-overlapping.
        std::size_t initialResultCount = results.size();
        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && !labels2D) || (pass == 1 && !labels3D)) {
                continue;
            }
            
            for (const std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>& bitmapLabels : *_renderBitmapLabelMap[pass]) {
                for (const std::shared_ptr<Label>& label : bitmapLabels.second) {
                    if (!label->isValid() || !label->isVisible() || !label->isActive() || label->getOpacity() <= 0) {
                        continue;
                    }

                    double result = 0;
                    if (findLabelIntersection(label, ray, radius, result)) {
                        results.emplace_back(label->getTileId(), result, label->getLocalId());
                    }
                }
            }
        }

        return results.size() > initialResultCount;
    }

    bool GLTileRenderer::findBitmapIntersections(const cglib::ray3<double>& ray, std::vector<std::tuple<TileId, double, TileBitmap, cglib::vec2<float>>>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        // First find the intersecting tile. NOTE: we ignore building height information
        std::size_t initialResults = results.size();
        for (const std::shared_ptr<BlendNode>& blendNode : *_blendNodes) {
            std::multimap<int, RenderNode> renderNodeMap;
            if (!buildRenderNodes(*blendNode, 1.0f, renderNodeMap)) {
                continue;
            }

            for (auto it = renderNodeMap.begin(); it != renderNodeMap.end(); it++) {
                const RenderNode& renderNode = it->second;
                
                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderNode.tileId.zoom > blendNode->tileId.zoom ? renderNode.tileId : blendNode->tileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderNode.tileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderNode.tileId);

                // Do intersection with the tile bbox first
                if (!cglib::intersect_bbox(tileBBox, ray)) {
                    continue;
                }
                
                // Store all bitmaps
                cglib::ray3<double> rayTile = cglib::transform_ray(ray, invTileMatrix);
                for (const std::shared_ptr<TileBitmap>& bitmap : renderNode.layer->getBitmaps()) {
                    auto it = _tileSurfaceMap.find(renderNode.tileId);
                    if (it == _tileSurfaceMap.end()) {
                        continue;
                    }

                    std::vector<std::pair<double, cglib::vec2<float>>> resultsTile;
                    for (const std::shared_ptr<const TileSurface>& tileSurface : it->second) {
                        findTileSurfaceIntersections(renderNode.tileId, blendNode->tile, tileSurface, rayTile, resultsTile);
                    }

                    for (std::pair<double, cglib::vec2<float>> resultTile : resultsTile) {
                        cglib::vec3<float> posTile = cglib::vec3<float>::convert(rayTile(resultTile.first));
                        cglib::vec2<float> tilePos = resultTile.second;

                        // Check that the hit position is inside the tile and normal is facing toward the ray
                        cglib::vec2<float> clipPos = tilePos;
                        if (blendNode->tileId.zoom > renderNode.tileId.zoom) {
                            cglib::mat3x3<double> clipTransform = cglib::inverse(calculateTileMatrix2D(blendNode->tileId)) * calculateTileMatrix2D(renderNode.tileId);
                            clipPos = cglib::transform_point(tilePos, cglib::mat3x3<float>::convert(clipTransform));
                        }
                        if (clipPos(0) < 0 || clipPos(1) < 0 || clipPos(0) > 1 || clipPos(1) > 1) {
                            continue;
                        }

                        cglib::vec3<float> normal = tileTransformer->calculateNormal(tilePos);
                        if (cglib::dot_product(normal, cglib::vec3<float>::convert(ray.direction)) >= 0) {
                            continue;
                        }

                        cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(posTile), tileMatrix);
                        results.emplace_back(renderNode.tileId, cglib::dot_product(pos - ray.origin, ray.direction) / cglib::dot_product(ray.direction, ray.direction), *bitmap, tilePos);
                    }
                }
            }
        }

        return results.size() > initialResults;
    }
    
    bool GLTileRenderer::isTileVisible(const TileId& tileId) const {
        cglib::bbox3<double> bbox = _transformer->calculateTileBBox(tileId);
        return _viewState.frustum.inside(bbox);
    }

    cglib::mat4x4<double> GLTileRenderer::calculateTileMatrix(const TileId& tileId, float coordScale) const {
        return _transformer->calculateTileMatrix(tileId, coordScale);
    }
    
    cglib::mat3x3<double> GLTileRenderer::calculateTileMatrix2D(const TileId& tileId, float coordScale) const {
        double z = 1.0 / (1 << tileId.zoom);
        cglib::mat3x3<double> m = cglib::mat3x3<double>::zero();
        m(0, 0) = z * coordScale;
        m(1, 1) = -z * coordScale;
        m(2, 2) = 1;
        m(0, 2) = tileId.x * z - 0.5;
        m(1, 2) = ((1 << tileId.zoom) - tileId.y) * z - 0.5;
        return m;
    }

    cglib::mat4x4<float> GLTileRenderer::calculateTileMVPMatrix(const TileId& tileId, float coordScale) const {
        return cglib::mat4x4<float>::convert(_cameraProjMatrix * calculateTileMatrix(tileId, coordScale));
    }
    
    float GLTileRenderer::calculateBlendNodeOpacity(const BlendNode& blendNode, float blend) const {
        float opacity = blend * blendNode.blend;
        for (const std::shared_ptr<BlendNode>& childBlendNode : blendNode.childNodes) {
            opacity += calculateBlendNodeOpacity(*childBlendNode, blend * (1 - blendNode.blend));
        }
        return std::min(opacity, 1.0f);
    }

    void GLTileRenderer::updateBlendNode(BlendNode& blendNode, float dBlend) const {
        if (!isTileVisible(blendNode.tileId)) {
            blendNode.blend = 1.0f;
            return;
        }
        
        blendNode.blend += dBlend;
        if (blendNode.blend >= 1.0f) {
            blendNode.blend = 1.0f;
            blendNode.childNodes.clear();
        }
        
        for (std::shared_ptr<BlendNode>& childBlendNode : blendNode.childNodes) {
            updateBlendNode(*childBlendNode, dBlend);
        }
    }
    
    bool GLTileRenderer::buildRenderNodes(const BlendNode& blendNode, float blend, std::multimap<int, RenderNode>& renderNodeMap) const {
        if (!isTileVisible(blendNode.tileId)) {
            return false;
        }
        
        bool exists = false;
        if (blendNode.tile) {
            // Use original source tile id for render node, but apply tile shift from target tile
            TileId rootTileId = blendNode.tileId;
            while (rootTileId.zoom > 0) {
                rootTileId = rootTileId.getParent();
            }
            TileId tileId = blendNode.tile->getTileId();
            tileId.x += rootTileId.x * (1 << tileId.zoom);
            tileId.y += rootTileId.y * (1 << tileId.zoom);
            
            // Add render nodes for each layer
            for (const std::shared_ptr<TileLayer>& layer : blendNode.tile->getLayers()) {
                // Special case for raster layers - ignore global blend factor
                RenderNode renderNode(tileId, blendNode.tile, layer, (layer->getGeometries().empty() ? blendNode.blend : blend * blendNode.blend));
                addRenderNode(renderNode, renderNodeMap);
            }
            exists = true;
        }
        
        for (const std::shared_ptr<BlendNode>& childBlendNode : blendNode.childNodes) {
            if (buildRenderNodes(*childBlendNode, blend * (1 - blendNode.blend), renderNodeMap)) {
                exists = true;
            }
        }
        return exists;
    }
    
    void GLTileRenderer::addRenderNode(RenderNode renderNode, std::multimap<int, RenderNode>& renderNodeMap) const {
        const std::shared_ptr<const TileLayer>& layer = renderNode.layer;
        auto range = renderNodeMap.equal_range(layer->getLayerIndex());
        for (auto it = range.first; it != range.second; ) {
            RenderNode& baseRenderNode = (it++)->second;
            if (!renderNode.tileId.intersects(baseRenderNode.tileId)) {
                continue;
            }

            // Check if the layer should be combined with base layer
            bool combine = false;
            if (!layer->getGeometries().empty() && !baseRenderNode.layer->getGeometries().empty()) {
                TileGeometry::Type type = layer->getGeometries().front()->getType();
                TileGeometry::Type baseType = baseRenderNode.layer->getGeometries().front()->getType();
                combine = (type == baseType);
            }
            
            // Combine layers, if possible
            if (combine) {
                if (baseRenderNode.tileId.zoom > renderNode.tileId.zoom) {
                    renderNode.blend = std::max(renderNode.blend, std::min(renderNode.initialBlend + baseRenderNode.blend, 1.0f));
                    it = renderNodeMap.erase(--it);
                } else {
                    baseRenderNode.blend = std::max(baseRenderNode.blend, std::min(baseRenderNode.initialBlend + renderNode.blend, 1.0f));
                    return;
                }
            }
        }
        
        // New/non-intersecting layer. Add it to render node map.
        auto it = renderNodeMap.lower_bound(layer->getLayerIndex());
        renderNodeMap.insert(it, { layer->getLayerIndex(), renderNode });
    }
    
    void GLTileRenderer::updateLabels(const std::vector<std::shared_ptr<Label>>& labels, float dOpacity) const {
        for (const std::shared_ptr<Label>& label : labels) {
            if (!label->isValid()) {
                continue;
            }
            
            float sign = (label->isVisible() && label->isActive() ? 1.0f : -1.0f);
            float step = (label->getOpacity() <= 0.0f || label->getOpacity() >= 1.0f ? 0.01f : dOpacity); // important if dOpacity is highly variable - if fully hidden/visible, take small first step
            label->setOpacity(std::max(0.0f, std::min(1.0f, label->getOpacity() + sign * step)));
        }
    }
    
    void GLTileRenderer::findTileGeometryIntersections(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileGeometry>& geometry, const cglib::ray3<double>& ray, float radius, float heightScale, std::vector<std::pair<double, long long>>& results) const {
        float scale = geometry->getGeometryScale() / tile->getTileSize();
        for (TileGeometryIterator it(tileId, tile, geometry, _transformer, _viewState, radius, scale, heightScale); it; ++it) {
            TileGeometryIterator::Triangle triangle = it.triangle();

            double t = 0;
            if (cglib::intersect_triangle(cglib::vec3<double>::convert(triangle[0]), cglib::vec3<double>::convert(triangle[1]), cglib::vec3<double>::convert(triangle[2]), ray, &t)) {
                results.emplace_back(t, it.id());
            }
        }
    }

    void GLTileRenderer::findTileSurfaceIntersections(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileSurface>& tileSurface, const cglib::ray3<double>& ray, std::vector<std::pair<double, cglib::vec2<float>>>& results) const {
        cglib::mat4x4<double> surfaceToTileTransform = cglib::inverse(calculateTileMatrix(tileId)) * cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
        for (std::size_t index = 0; index + 2 < tileSurface->getIndices().size(); index += 3) {
            std::array<cglib::vec3<double>, 3> triangle;
            for (int i = 0; i < 3; i++) {
                std::size_t coordOffset = tileSurface->getIndices()[index + i] * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.coordOffset;
                const std::int16_t* coordPtr = reinterpret_cast<const std::int16_t*>(&tileSurface->getVertexGeometry()[coordOffset]);
                triangle[i] = cglib::transform_point(cglib::vec3<double>(coordPtr[0], coordPtr[1], coordPtr[2]), surfaceToTileTransform);
            }
                
            double t = 0;
            if (cglib::intersect_triangle(triangle[0], triangle[1], triangle[2], ray, &t)) {
                std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tileId);
                results.emplace_back(t, transformer->calculateTilePosition(cglib::vec3<float>::convert(ray(t))));
            }
        }
    }

    bool GLTileRenderer::findLabelIntersection(const std::shared_ptr<Label>& label, const cglib::ray3<double>& ray, float radius, double& result) const {
        float size = label->getStyle()->sizeFunc(_viewState);
        if (size <= 0) {
            return false;
        }

        std::array<cglib::vec3<float>, 4> envelope;
        if (!label->calculateEnvelope(size, radius, _viewState, envelope)) {
            return false;
        }
        if (cglib::dot_product(label->getNormal(), cglib::vec3<float>::convert(ray.direction)) >= 0) {
            return false;
        }

        std::array<cglib::vec3<double>, 4> quad;
        if (!label->getStyle()->translate) {
            for (int i = 0; i < 4; i++) {
                quad[i] = _viewState.origin + cglib::vec3<double>::convert(envelope[i]);
            }
        } else {
            float zoomScale = std::pow(2.0f, label->getTileId().zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*label->getStyle()->translate) * zoomScale;
            cglib::mat4x4<double> translateMatrix = cglib::mat4x4<double>::convert(_transformer->calculateTileTransform(label->getTileId(), translate, 1.0f));
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(label->getTileId(), 1);
            cglib::mat4x4<double> labelMatrix = tileMatrix * translateMatrix * cglib::inverse(tileMatrix) * cglib::translate4_matrix(_viewState.origin);
            for (int i = 0; i < 4; i++) {
                quad[i] = cglib::transform_point(cglib::vec3<double>::convert(envelope[i]), labelMatrix);
            }
        }

        return cglib::intersect_triangle(quad[0], quad[1], quad[2], ray, &result) || cglib::intersect_triangle(quad[0], quad[2], quad[3], ray, &result);
    }

    bool GLTileRenderer::renderBlendNodes2D(const std::vector<std::shared_ptr<BlendNode>>& blendNodes, int stencilBits) {
        int stencilNum = (1 << stencilBits) - 1; // forces initial stencil clear
        boost::optional<GLenum> activeStencilMode;
        boost::optional<int> activeStencilNum;
        boost::optional<CompOp> activeCompOp;

        auto setupStencil = [&](bool enable) {
            GLenum stencilMode = enable ? GL_EQUAL : GL_ALWAYS;
            if (!(stencilMode == activeStencilMode && activeStencilNum == stencilNum) && stencilBits > 0) {
                glStencilFunc(stencilMode, stencilNum, 255);
                activeStencilMode = stencilMode;
                activeStencilNum = stencilNum;
            }
        };

        auto setupBlendMode = [&, this](CompOp compOp) {
            if (compOp != activeCompOp) {
                setGLBlendState(compOp);
                activeCompOp = compOp;
            }
        };

        TileId activeTileMaskId(-1, -1, -1); // invalid mask
        bool update = false;
        for (const std::shared_ptr<BlendNode>& blendNode : blendNodes) {
            std::multimap<int, RenderNode> renderNodeMap;
            if (!buildRenderNodes(*blendNode, 1.0f, renderNodeMap)) {
                continue;
            }
            
            float backgroundOpacity = calculateBlendNodeOpacity(*blendNode, 1.0f);
            if (backgroundOpacity > 0) {
                setupStencil(false);
                setupBlendMode(CompOp::SRC_OVER);
                renderTileBackground(blendNode->tileId, blendNode->tile->getBackground(), blendNode->tile->getTileSize(), backgroundOpacity);
            }
            update = backgroundOpacity < 1.0f || update;
            
            std::unordered_map<int, std::size_t> layerBufferMap;
            for (auto it = renderNodeMap.begin(); it != renderNodeMap.end(); it++) {
                const RenderNode& renderNode = it->second;
                
                float blendOpacity = 1.0f;
                float geometryOpacity = 1.0f;
                float opacity = (renderNode.layer->getOpacityFunc())(_viewState);
                if (renderNode.layer->getCompOp()) { // a 'useful' hack - we use real layer opacity only if comp-op is explicitly defined; otherwise we translate it into element opacity, which is in many cases close enough
                    blendOpacity = opacity;
                } else {
                    geometryOpacity = opacity;
                }

                GLint currentFBO = 0;
                bool blendGeometry = false;

                auto setupFrameBuffer = [&]() {
                    if (renderNode.layer->getCompOp() && !blendGeometry) {
                        blendGeometry = true;

                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                        auto it = layerBufferMap.find(renderNode.layer->getLayerIndex());
                        if (it == layerBufferMap.end()) {
                            std::size_t bufferIndex = layerBufferMap.size();
                            if (bufferIndex >= _layerBuffers.size()) {
                                _layerBuffers.emplace_back();
                                createFrameBuffer(_layerBuffers.back(), true, false, stencilBits > 0);
                            }
                            it = layerBufferMap.emplace(renderNode.layer->getLayerIndex(), bufferIndex).first;
                        }
                        glBindFramebuffer(GL_FRAMEBUFFER, _layerBuffers[it->second].fbo);
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

                        activeTileMaskId = TileId(-1, -1, -1); // force stencil mask update
                    }
                };

                if (renderNode.layer->getCompOp()) {
                    if (isEmptyBlendRequired(renderNode.layer->getCompOp().get())) {
                        setupFrameBuffer();
                    }
                }

                for (const std::shared_ptr<TileBitmap>& bitmap : renderNode.layer->getBitmaps()) {
                    setupFrameBuffer();
                    
                    setupStencil(false);
                    setupBlendMode(CompOp::SRC_OVER);
                    renderTileBitmap(renderNode.tileId, blendNode->tileId, renderNode.blend, geometryOpacity, bitmap);
                }
                
                for (const std::shared_ptr<TileGeometry>& geometry : renderNode.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        continue;
                    }

                    setupFrameBuffer();

                    TileId tileMaskId = renderNode.tileId.zoom > blendNode->tileId.zoom ? renderNode.tileId : blendNode->tileId;
                    if (activeTileMaskId != tileMaskId && stencilBits > 0) {
                        if (++stencilNum == (1 << stencilBits)) { // do initial clear, or clear after each N (usually 256) updates
                            glClearStencil(0);
                            glClear(GL_STENCIL_BUFFER_BIT);
                            stencilNum = 1;
                        }

                        glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
                        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

                        setupStencil(false);
                        renderTileMask(tileMaskId);

                        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

                        activeTileMaskId = tileMaskId;
                    }

                    setupStencil(true);
                    setupBlendMode(geometry->getStyleParameters().compOp);
                    renderTileGeometry(renderNode.tileId, blendNode->tileId, renderNode.blend, geometryOpacity, renderNode.tile, geometry);
                }
                update = renderNode.initialBlend < 1.0f || update;

                if (blendGeometry) {
                    const FrameBuffer& layerBuffer = _layerBuffers[layerBufferMap[renderNode.layer->getLayerIndex()]];

                    if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !layerBuffer.depthStencilAttachments.empty()) {
                        _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(layerBuffer.depthStencilAttachments.size()), layerBuffer.depthStencilAttachments.data());
                    }

                    glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

                    setupStencil(false);
                    setupBlendMode(renderNode.layer->getCompOp().get());
                    blendTileTexture(renderNode.tileId, blendOpacity, layerBuffer.colorTexture);
                }
            }
        }
        
        return update;
    }
    
    bool GLTileRenderer::renderBlendNodes3D(const std::vector<std::shared_ptr<BlendNode>>& blendNodes) {
        bool update = false;
        GLint currentFBO = 0;
        bool blendGeometry = false;
        for (const std::shared_ptr<BlendNode>& blendNode : blendNodes) {
            std::multimap<int, RenderNode> renderNodeMap;
            if (!buildRenderNodes(*blendNode, 1.0f, renderNodeMap)) {
                continue;
            }
            
            for (auto it = renderNodeMap.begin(); it != renderNodeMap.end(); it++) {
                const RenderNode& renderNode = it->second;

                float opacity = 1.0f;
                if (renderNode.layer->getCompOp()) {
                    opacity = (renderNode.layer->getOpacityFunc())(_viewState);
                }

                for (const std::shared_ptr<TileGeometry>& geometry : renderNode.layer->getGeometries()) {
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        continue;
                    }
                    
                    // Bind screen FBO lazily, only when needed
                    if (!blendGeometry) {
                        blendGeometry = true;
                        
                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                        if (_overlayBuffer.fbo == 0) {
                            createFrameBuffer(_overlayBuffer, true, true, false);
                        }

                        glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer.fbo);
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    }
                    
                    renderTileGeometry(renderNode.tileId, blendNode->tileId, renderNode.blend, opacity, renderNode.tile, geometry);
                }
                update = renderNode.initialBlend < 1.0f || update;
            }
        }
        
        // If any 3D geometry, blend rendered screen FBO
        if (blendGeometry) {
            if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !_overlayBuffer.depthStencilAttachments.empty()) {
                _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer.depthStencilAttachments.size()), _overlayBuffer.depthStencilAttachments.data());
            }

            glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);

            blendScreenTexture(1.0f, _overlayBuffer.colorTexture);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }

        return update;
    }
    
    bool GLTileRenderer::renderLabels(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap) {
        bool update = false;
        
        LabelBatchParameters labelBatchParams;
        std::shared_ptr<const TileLabel::Style> lastLabelStyle;
        int styleIndex = -1;
        int haloStyleIndex = -1;
        for (const std::shared_ptr<Label>& label : labels) {
            if (!label->isValid()) {
                continue;
            }
            if (label->getOpacity() <= 0.0f) {
                continue;
            }
            const std::shared_ptr<const TileLabel::Style>& labelStyle = label->getStyle();

            if (lastLabelStyle != labelStyle) {
                cglib::vec4<float> color = (labelStyle->colorFunc)(_viewState).rgba();
                float size = (labelStyle->sizeFunc)(_viewState);
                cglib::vec4<float> haloColor = (labelStyle->haloColorFunc)(_viewState).rgba();
                float haloRadius = (labelStyle->haloRadiusFunc)(_viewState) * HALO_RADIUS_SCALE;

                if (labelStyle->translate || (lastLabelStyle && lastLabelStyle->translate) || labelBatchParams.scale != labelStyle->scale || labelBatchParams.parameterCount + 2 > LabelBatchParameters::MAX_PARAMETERS) {
                    renderLabelBatch(labelBatchParams, bitmap);
                    labelBatchParams.labelCount = 0;
                    labelBatchParams.parameterCount = 0;
                    labelBatchParams.scale = labelStyle->scale;
                    if (labelStyle->translate) {
                        float zoomScale = std::pow(2.0f, label->getTileId().zoom - _viewState.zoom);
                        cglib::vec2<float> translate = (*labelStyle->translate) * zoomScale;
                        cglib::mat4x4<double> translateMatrix = cglib::mat4x4<double>::convert(_transformer->calculateTileTransform(label->getTileId(), translate, 1.0f));
                        cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(label->getTileId(), 1);
                        labelBatchParams.labelMatrix = _viewState.cameraMatrix * tileMatrix * translateMatrix * cglib::inverse(tileMatrix) * cglib::translate4_matrix(_viewState.origin);
                    } else {
                        labelBatchParams.labelMatrix = _viewState.cameraMatrix * cglib::translate4_matrix(_viewState.origin);
                    }

                    styleIndex = -1;
                    haloStyleIndex = -1;
                } else {
                    for (styleIndex = labelBatchParams.parameterCount; --styleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[styleIndex] == color && labelBatchParams.widthTable[styleIndex] == size && labelBatchParams.strokeWidthTable[styleIndex] == 0) {
                            break;
                        }
                    }
                    for (haloStyleIndex = haloRadius > 0 ? labelBatchParams.parameterCount : 0; --haloStyleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[haloStyleIndex] == haloColor && labelBatchParams.widthTable[haloStyleIndex] == size && labelBatchParams.strokeWidthTable[haloStyleIndex] == haloRadius) {
                            break;
                        }
                    }
                }
                
                if (styleIndex < 0) {
                    styleIndex = labelBatchParams.parameterCount++;
                    labelBatchParams.colorTable[styleIndex] = color;
                    labelBatchParams.widthTable[styleIndex] = size;
                    labelBatchParams.strokeWidthTable[styleIndex] = 0;
                }
                if (haloRadius > 0 && haloStyleIndex < 0) {
                    haloStyleIndex = labelBatchParams.parameterCount++;
                    labelBatchParams.colorTable[haloStyleIndex] = haloColor;
                    labelBatchParams.widthTable[haloStyleIndex] = size;
                    labelBatchParams.strokeWidthTable[haloStyleIndex] = haloRadius;
                }

                lastLabelStyle = labelStyle;
            }

            label->calculateVertexData(labelBatchParams.widthTable[styleIndex], _viewState, styleIndex, haloStyleIndex, _labelVertices, _labelNormals, _labelTexCoords, _labelAttribs, _labelIndices);

            labelBatchParams.labelCount++;

            if (_labelVertices.size() >= 32768) { // flush the batch if largest vertex index is getting 'close' to 64k limit
                renderLabelBatch(labelBatchParams, bitmap);
            }
            
            update = label->getOpacity() < 1.0f || update;
        }

        renderLabelBatch(labelBatchParams, bitmap);

        return update;
    }
    
    void GLTileRenderer::blendScreenTexture(float opacity, GLuint texture) {
        if (opacity <= 0) {
            return;
        }

        GLuint shaderProgram = _shaderManager.createProgram("blend", _defaultContext);
        GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
        glUseProgram(shaderProgram);
        checkGLError();
        
        if (_screenQuad.vbo == 0) {
            createCompiledQuad(_screenQuad);
        }
        glBindBuffer(GL_ARRAY_BUFFER, _screenQuad.vbo);
        glVertexAttribPointer(positionLocation, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(positionLocation);
        
        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::identity();
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "uTexture"), 0);
        Color color(opacity, opacity, opacity, opacity);
        glUniform4fv(glGetUniformLocation(shaderProgram, "uColor"), 1, color.rgba().data());
        glUniform2f(glGetUniformLocation(shaderProgram, "uInvScreenSize"), 1.0f / _screenWidth, 1.0f / _screenHeight);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        glDisableVertexAttribArray(positionLocation);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void GLTileRenderer::blendTileTexture(const TileId& tileId, float opacity, GLuint texture) {
        if (opacity <= 0) {
            return;
        }
        
        for (const std::shared_ptr<const TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            GLuint shaderProgram = _shaderManager.createProgram("blend", _defaultContext);
            GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
            glUseProgram(shaderProgram);
            checkGLError();

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform1i(glGetUniformLocation(shaderProgram, "uTexture"), 0);
            Color color(opacity, opacity, opacity, opacity);
            glUniform4fv(glGetUniformLocation(shaderProgram, "uColor"), 1, color.rgba().data());
            glUniform2f(glGetUniformLocation(shaderProgram, "uInvScreenSize"), 1.0f / _screenWidth, 1.0f / _screenHeight);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            glBindTexture(GL_TEXTURE_2D, 0);

            glDisableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
    
    void GLTileRenderer::renderTileMask(const TileId& tileId) {
        for (const std::shared_ptr<const TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            GLuint shaderProgram = _shaderManager.createProgram("background", _defaultContext);
            GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
            glUseProgram(shaderProgram);
            checkGLError();

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());

            Color color(0, 0, 0, 0);
            glUniform4fv(glGetUniformLocation(shaderProgram, "uColor"), 1, color.rgba().data());
            glUniform1f(glGetUniformLocation(shaderProgram, "uOpacity"), 0);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            glDisableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
    
    void GLTileRenderer::renderTileBackground(const TileId& tileId, const std::shared_ptr<TileBackground>& background, float tileSize, float opacity) {
        if (opacity <= 0) {
            return;
        }
        if (!background->getPattern() && !background->getColor().value()) {
            return;
        }

        for (const std::shared_ptr<const TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            GLuint shaderProgram = _shaderManager.createProgram("background", _patternTransformLighting2DContext[background->getPattern() ? 1 : 0][0]);
            GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
            GLint normalLocation = _lightingShader2D ? glGetAttribLocation(shaderProgram, "aVertexNormal") : -1;
            GLint uvLocation = background->getPattern() ? glGetAttribLocation(shaderProgram, "aVertexUV") : -1;
            glUseProgram(shaderProgram);
            checkGLError();

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(positionLocation);
            if (background->getPattern()) {
                glVertexAttribPointer(uvLocation, 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.texCoordOffset));
                glEnableVertexAttribArray(uvLocation);
            }
            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(normalLocation, 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(normalLocation);
                } else {
                    glVertexAttrib3f(normalLocation, 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());

            if (auto pattern = background->getPattern()) {
                CompiledBitmap compiledBitmap;
                auto it = _compiledBitmapMap.find(pattern->bitmap);
                if (it == _compiledBitmapMap.end()) {
                    createCompiledBitmap(compiledBitmap);

                    std::shared_ptr<const Bitmap> scaledPatternBitmap = BitmapManager::scaleToPOT(pattern->bitmap);
                    glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    if (scaledPatternBitmap) {
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scaledPatternBitmap->width, scaledPatternBitmap->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaledPatternBitmap->data.data());
                    }
                    glGenerateMipmap(GL_TEXTURE_2D);

                    _compiledBitmapMap[pattern->bitmap] = compiledBitmap;
                } else {
                    compiledBitmap = it->second;
                }

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
                glUniform1i(glGetUniformLocation(shaderProgram, "uPattern"), 0);

                if (pattern->bitmap) {
                    cglib::vec2<float> uvScale(tileSize / pattern->bitmap->width, tileSize / pattern->bitmap->height);
                    glUniform2f(glGetUniformLocation(shaderProgram, "uUVScale"), uvScale(0), uvScale(1));
                }
            }

            glUniform4fv(glGetUniformLocation(shaderProgram, "uColor"), 1, background->getColor().rgba().data());
            glUniform1f(glGetUniformLocation(shaderProgram, "uOpacity"), opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(normalLocation);
                }
            }
            if (background->getPattern()) {
                glBindTexture(GL_TEXTURE_2D, 0);

                glDisableVertexAttribArray(uvLocation);
            }
            glDisableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }

    void GLTileRenderer::renderTileBitmap(const TileId& tileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap) {
        if (blend * opacity <= 0) {
            return;
        }

        for (const std::shared_ptr<const TileSurface>& tileSurface : buildCompiledTileSurfaces(targetTileId.zoom > tileId.zoom ? targetTileId : tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            GLuint shaderProgram = _shaderManager.createProgram("bitmap", _patternTransformLighting2DContext[1][0]);
            GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
            GLint normalLocation = _lightingShader2D ? glGetAttribLocation(shaderProgram, "aVertexNormal") : -1;
            GLint uvLocation = glGetAttribLocation(shaderProgram, "aVertexUV");
            glUseProgram(shaderProgram);
            checkGLError();

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(positionLocation);
            glVertexAttribPointer(uvLocation, 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.texCoordOffset));
            glEnableVertexAttribArray(uvLocation);
            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(normalLocation, 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(normalLocation);
                } else {
                    glVertexAttrib3f(normalLocation, 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());

            CompiledBitmap compiledTileBitmap;
            auto it = _compiledTileBitmapMap.find(bitmap);
            if (it == _compiledTileBitmapMap.end()) {
                createCompiledBitmap(compiledTileBitmap);

                // Use a different strategy if the bitmap is not of POT dimensions, simply do not create the mipmaps
                bool genMipmaps = (bitmap->getWidth() & (bitmap->getWidth() - 1)) == 0 && (bitmap->getHeight() & (bitmap->getHeight() - 1)) == 0;
                glBindTexture(GL_TEXTURE_2D, compiledTileBitmap.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                GLenum format = GL_NONE;
                switch (bitmap->getFormat()) {
                case TileBitmap::Format::GRAYSCALE:
                    format = GL_LUMINANCE;
                    break;
                case TileBitmap::Format::RGB:
                    format = GL_RGB;
                    break;
                case TileBitmap::Format::RGBA:
                    format = GL_RGBA;
                    break;
                }
                glTexImage2D(GL_TEXTURE_2D, 0, format, bitmap->getWidth(), bitmap->getHeight(), 0, format, GL_UNSIGNED_BYTE, bitmap->getData().empty() ? NULL : bitmap->getData().data());
                if (genMipmaps) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }

                if (!_interactionMode) {
                    bitmap->releaseBitmap(); // if interaction is enabled, keep the original bitmap
                }

                _compiledTileBitmapMap[bitmap] = compiledTileBitmap;
            } else {
                compiledTileBitmap = it->second;
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledTileBitmap.texture);
            glUniform1i(glGetUniformLocation(shaderProgram, "uPattern"), 0);

            cglib::mat3x3<float> uvMatrix = cglib::mat3x3<float>::identity();
            if (targetTileId.zoom > tileId.zoom) {
                uvMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(tileId)) * calculateTileMatrix2D(targetTileId));
            }
            uvMatrix = cglib::mat3x3<float> { { 1.0, 0.0, 0.0 }, { 0.0, -1.0, 1.0 }, { 0.0, 0.0, 1.0 } } * uvMatrix;
            glUniformMatrix3fv(glGetUniformLocation(shaderProgram, "uUVMatrix"), 1, GL_FALSE, uvMatrix.data());

            glUniform1f(glGetUniformLocation(shaderProgram, "uOpacity"), blend * opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            glBindTexture(GL_TEXTURE_2D, 0);

            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(normalLocation);
                }
            }
            glDisableVertexAttribArray(uvLocation);
            glDisableVertexAttribArray(positionLocation);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }

    void GLTileRenderer::renderTileGeometry(const TileId& tileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<TileGeometry>& geometry) {
        const TileGeometry::StyleParameters& styleParams = geometry->getStyleParameters();
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        
        if (blend * opacity <= 0) {
            return;
        }
        
        GLuint shaderProgram = 0;
        switch (geometry->getType()) {
            case TileGeometry::Type::POINT:
                shaderProgram = _shaderManager.createProgram("point", _patternTransformLighting2DContext[styleParams.pattern ? 1 : 0][styleParams.translate ? 1 : 0]);
                break;
            case TileGeometry::Type::LINE:
                shaderProgram = _shaderManager.createProgram("line", _patternTransformLighting2DContext[styleParams.pattern ? 1 : 0][styleParams.translate ? 1 : 0]);
                break;
            case TileGeometry::Type::POLYGON:
                shaderProgram = _shaderManager.createProgram("polygon", _patternTransformLighting2DContext[styleParams.pattern ? 1 : 0][styleParams.translate ? 1 : 0]);
                break;
            case TileGeometry::Type::POLYGON3D:
                shaderProgram = _shaderManager.createProgram("polygon3d", _transformLighting3DContext[styleParams.translate ? 1 : 0]);
                break;
            default:
                return;
        }
        GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
        GLint uvLocation = vertexGeomLayoutParams.texCoordOffset >= 0 ? glGetAttribLocation(shaderProgram, "aVertexUV") : -1;
        GLint normalLocation = _lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D ? glGetAttribLocation(shaderProgram, "aVertexNormal") : -1;
        GLint binormalLocation = vertexGeomLayoutParams.binormalOffset >= 0 ? glGetAttribLocation(shaderProgram, "aVertexBinormal") : -1;
        GLint heightLocation = vertexGeomLayoutParams.heightOffset >= 0 ? glGetAttribLocation(shaderProgram, "aVertexHeight") : -1;
        GLint attribsLocation = glGetAttribLocation(shaderProgram, "aVertexAttribs");
        glUseProgram(shaderProgram);
        checkGLError();

        cglib::mat4x4<float> mvpMatrix = calculateTileMVPMatrix(tileId, 1.0f / vertexGeomLayoutParams.coordScale);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());
        
        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, tileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            cglib::mat4x4<float> transformMatrix = _transformer->calculateTileTransform(tileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uTransformMatrix"), 1, GL_FALSE, transformMatrix.data());
        }

        std::array<cglib::vec4<float>, TileGeometry::StyleParameters::MAX_PARAMETERS> colors;
        for (int i = 0; i < styleParams.parameterCount; i++) {
            Color color = Color::fromColorOpacity((styleParams.colorFuncs[i])(_viewState) * blend, opacity);
            colors[i] = color.rgba();
        }
        
        if (geometry->getType() == TileGeometry::Type::POINT) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths, strokeWidths;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                float width = std::max(0.0f, (styleParams.widthFuncs[i])(_viewState)) * geometry->getGeometryScale() / tile->getTileSize();
                if (width <= 0) {
                    colors[i] = cglib::vec4<float>(0, 0, 0, 0);
                }
                widths[i] = width;

                float strokeWidth = (styleParams.strokeWidthFuncs[i])(_viewState) * HALO_RADIUS_SCALE;
                strokeWidths[i] = strokeWidth;
            }

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(strokeWidths.begin(), strokeWidths.begin() + styleParams.parameterCount, [](float strokeWidth) { return strokeWidth == 0; })) {
                    return;
                }
            }
            
            glUniform1f(glGetUniformLocation(shaderProgram, "uBinormalScale"), vertexGeomLayoutParams.coordScale / vertexGeomLayoutParams.binormalScale / std::pow(2.0f, _viewState.zoom - tileId.zoom));
            glUniform1f(glGetUniformLocation(shaderProgram, "uSDFScale"), GLYPH_RENDER_SIZE / _fullResolution / BITMAP_SDF_SCALE);
            glUniform1fv(glGetUniformLocation(shaderProgram, "uWidthTable"), styleParams.parameterCount, widths.data());
            glUniform1fv(glGetUniformLocation(shaderProgram, "uStrokeWidthTable"), styleParams.parameterCount, strokeWidths.data());
        } else if (geometry->getType() == TileGeometry::Type::LINE) {
            constexpr float gamma = 0.5f;

            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                if (styleParams.widthFuncs[i] == FloatFunction(0)) {
                    widths[i] = 0;
                    continue;
                }
                
                float width = 0.5f * std::abs((styleParams.widthFuncs[i])(_viewState)) * geometry->getGeometryScale() / tile->getTileSize();
                float pixelWidth = _fullResolution * width;
                if (pixelWidth < 1.0f) {
                    colors[i] = colors[i] * pixelWidth; // should do gamma correction here, but simple implementation gives closer results to Mapnik
                    width = (pixelWidth > 0.0f ? 1.0f / _fullResolution : 0.0f); // normalize width to pixelWidth = 1
                }
                widths[i] = width;
            }

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(styleParams.widthFuncs.begin(), styleParams.widthFuncs.begin() + styleParams.parameterCount, [](const FloatFunction& func) { return func != FloatFunction(0); })) { // check that all are proper lines, not polygons
                    return;
                }
            }

            glUniform1f(glGetUniformLocation(shaderProgram, "uBinormalScale"), vertexGeomLayoutParams.coordScale / (_halfResolution * vertexGeomLayoutParams.binormalScale * std::pow(2.0f, _viewState.zoom - tileId.zoom)));
            glUniform1fv(glGetUniformLocation(shaderProgram, "uWidthTable"), styleParams.parameterCount, widths.data());
            glUniform1f(glGetUniformLocation(shaderProgram, "uHalfResolution"), _halfResolution);
            glUniform1f(glGetUniformLocation(shaderProgram, "uGamma"), gamma);
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
            float tileHeightScale = static_cast<float>(cglib::length(cglib::transform_vector(cglib::vec3<double>(0, 0, 1), calculateTileMatrix(tileId))));
            glUniform1f(glGetUniformLocation(shaderProgram, "uUVScale"), 1.0f / vertexGeomLayoutParams.texCoordScale);
            glUniform1f(glGetUniformLocation(shaderProgram, "uHeightScale"), blend / vertexGeomLayoutParams.heightScale * vertexGeomLayoutParams.coordScale);
            glUniform1f(glGetUniformLocation(shaderProgram, "uAbsHeightScale"), blend / vertexGeomLayoutParams.heightScale * POLYGON3D_HEIGHT_SCALE * tileHeightScale);
            cglib::mat3x3<float> tileMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(targetTileId)) * calculateTileMatrix2D(tileId));
            if (styleParams.translate) {
                float zoomScale = std::pow(2.0f, tileId.zoom - _viewState.zoom);
                cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
                tileMatrix = tileMatrix * cglib::translate3_matrix(cglib::vec3<float>(translate(0), translate(1), 1));
            }
            glUniformMatrix3fv(glGetUniformLocation(shaderProgram, "uTileMatrix"), 1, GL_FALSE, tileMatrix.data());
        }

        if (std::all_of(colors.begin(), colors.begin() + styleParams.parameterCount, [](const cglib::vec4<float>& color) {
            return std::all_of(color.cbegin(), color.cend(), [](float val) { return val < 1.0f / 256.0f; });
        })) {
            return;
        }

        glUniform4fv(glGetUniformLocation(shaderProgram, "uColorTable"), styleParams.parameterCount, colors[0].data());
        
        CompiledGeometry compiledGeometry;
        auto itGeom = _compiledTileGeometryMap.find(geometry);
        if (itGeom == _compiledTileGeometryMap.end()) {
            createCompiledGeometry(compiledGeometry);

            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);
            glBufferData(GL_ARRAY_BUFFER, geometry->getVertexGeometry().size() * sizeof(std::uint8_t), geometry->getVertexGeometry().data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, geometry->getIndices().size() * sizeof(std::uint16_t), geometry->getIndices().data(), GL_STATIC_DRAW);

            if (!_interactionMode) {
                geometry->releaseVertexArrays(); // if interaction is enabled, we must keep the vertex arrays. Otherwise optimize for lower memory usage
            }

            _compiledTileGeometryMap[geometry] = compiledGeometry;
        } else {
            compiledGeometry = itGeom->second;
        }

        if (styleParams.pattern) {
            float zoomScale = std::pow(2.0f, std::floor(_viewState.zoom) - tileId.zoom);
            float coordScale = 1.0f / (vertexGeomLayoutParams.texCoordScale * styleParams.pattern->widthScale);
            cglib::vec2<float> uvScale(coordScale, coordScale);
            if (geometry->getType() == TileGeometry::Type::LINE) {
                uvScale(0) *= zoomScale;
            } else if (geometry->getType() == TileGeometry::Type::POLYGON) {
                uvScale *= zoomScale;
            }
            glUniform2f(glGetUniformLocation(shaderProgram, "uUVScale"), uvScale(0), uvScale(1));

            CompiledBitmap compiledBitmap;
            auto itBitmap = _compiledBitmapMap.find(styleParams.pattern->bitmap);
            if (itBitmap == _compiledBitmapMap.end()) {
                createCompiledBitmap(compiledBitmap);

                // Skip mipmap generation for line patterns as it simply smears the pattern
                bool genMipmaps = geometry->getType() != TileGeometry::Type::LINE;
                std::shared_ptr<const Bitmap> patternBitmap = BitmapManager::scaleToPOT(styleParams.pattern->bitmap);
                glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, patternBitmap->width, patternBitmap->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, patternBitmap->data.data());
                if (genMipmaps) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }

                _compiledBitmapMap[styleParams.pattern->bitmap] = compiledBitmap;
            } else {
                compiledBitmap = itBitmap->second;
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glUniform1i(glGetUniformLocation(shaderProgram, "uPattern"), 0);
        }

        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(compiledGeometry.geometryVAO);
        }
        if (compiledGeometry.geometryVAO == 0 || itGeom == _compiledTileGeometryMap.end()) {
            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);
            glVertexAttribPointer(positionLocation, vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(positionLocation);

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                glVertexAttribPointer(attribsLocation, 4, GL_BYTE, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.attribsOffset));
                glEnableVertexAttribArray(attribsLocation);
            } else {
                glVertexAttrib4f(attribsLocation, 0, 0, 0, 0);
            }
            
            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                glVertexAttribPointer(uvLocation, 2, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.texCoordOffset));
                glEnableVertexAttribArray(uvLocation);
            }
            
            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(normalLocation, vertexGeomLayoutParams.dimensions, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(normalLocation);
                } else {
                    glVertexAttrib3f(normalLocation, 0, 0, 1);
                }
            }

            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                glVertexAttribPointer(binormalLocation, vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.binormalOffset));
                glEnableVertexAttribArray(binormalLocation);
            }
            
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                glVertexAttribPointer(heightLocation, 1, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, reinterpret_cast<const GLvoid*>(vertexGeomLayoutParams.heightOffset));
                glEnableVertexAttribArray(heightLocation);
            }
            
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
        }

        if (geometry->getType() != TileGeometry::Type::POLYGON3D && _lightingShader2D) {
            _lightingShader2D->setupFunc(shaderProgram, _viewState);
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D && _lightingShader3D) {
            _lightingShader3D->setupFunc(shaderProgram, _viewState);
        }
        
        glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(0);
        } else {
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                glDisableVertexAttribArray(heightLocation);
            }
            
            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                glDisableVertexAttribArray(binormalLocation);
            }

            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(normalLocation);
                }
            }
            
            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                glDisableVertexAttribArray(uvLocation);
            }

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                glDisableVertexAttribArray(attribsLocation);
            }
            
            glDisableVertexAttribArray(positionLocation);
        }
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void GLTileRenderer::renderLabelBatch(const LabelBatchParameters& labelBatchParams, const std::shared_ptr<const Bitmap>& bitmap) {
        if (_labelIndices.empty()) {
            return;
        }

        CompiledLabelBatch compiledLabelBatch;
        auto itBatch = _compiledLabelBatches.find(_labelBatchCounter);
        if (itBatch == _compiledLabelBatches.end()) {
            createCompiledLabelBatch(compiledLabelBatch);
            _compiledLabelBatches[_labelBatchCounter] = compiledLabelBatch;
        } else {
            compiledLabelBatch = itBatch->second;
        }
        _labelBatchCounter++;

        CompiledBitmap compiledBitmap;
        auto itBitmap = _compiledBitmapMap.find(bitmap);
        if (itBitmap == _compiledBitmapMap.end()) {
            createCompiledBitmap(compiledBitmap);

            // Turn off mipmap creation as most labels are SDF texts
            bool genMipMaps = false;
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMipMaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bitmap->width, bitmap->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bitmap->data.data());
            if (genMipMaps) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            _compiledBitmapMap[bitmap] = compiledBitmap;
        } else {
            compiledBitmap = itBitmap->second;
        }

        bool useDerivatives = _glExtensions->GL_OES_standard_derivatives_supported();
        GLuint shaderProgram = _shaderManager.createProgram("label", _derivativesLighting2DContext[useDerivatives ? 1 : 0]);
        GLint positionLocation = glGetAttribLocation(shaderProgram, "aVertexPosition");
        GLint normalLocation = _lightingShader2D ? glGetAttribLocation(shaderProgram, "aVertexNormal") : -1;
        GLint uvLocation = glGetAttribLocation(shaderProgram, "aVertexUV");
        GLint attribsLocation = glGetAttribLocation(shaderProgram, "aVertexAttribs");
        glUseProgram(shaderProgram);
        checkGLError();

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_viewState.projectionMatrix * labelBatchParams.labelMatrix);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uMVPMatrix"), 1, GL_FALSE, mvpMatrix.data());

        glUniform1f(glGetUniformLocation(shaderProgram, "uSDFScale"), GLYPH_RENDER_SIZE / labelBatchParams.scale / _fullResolution / BITMAP_SDF_SCALE);
        if (useDerivatives) {
            float scale = 1.0f / labelBatchParams.scale / _fullResolution / BITMAP_SDF_SCALE;
            glUniform2f(glGetUniformLocation(shaderProgram, "uDerivScale"), bitmap->width * scale, bitmap->height * scale);
        }
        glUniform4fv(glGetUniformLocation(shaderProgram, "uColorTable"), labelBatchParams.parameterCount, labelBatchParams.colorTable[0].data());
        glUniform1fv(glGetUniformLocation(shaderProgram, "uWidthTable"), labelBatchParams.parameterCount, labelBatchParams.widthTable.data());
        glUniform1fv(glGetUniformLocation(shaderProgram, "uStrokeWidthTable"), labelBatchParams.parameterCount, labelBatchParams.strokeWidthTable.data());
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.verticesVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelVertices.size() * 3 * sizeof(float), _labelVertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(positionLocation);

        if (_lightingShader2D) {
            glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.normalsVBO);
            glBufferData(GL_ARRAY_BUFFER, _labelNormals.size() * 3 * sizeof(float), _labelNormals.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(normalLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(normalLocation);

            _lightingShader2D->setupFunc(shaderProgram, _viewState);
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.texCoordsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelTexCoords.size() * 2 * sizeof(std::int16_t), _labelTexCoords.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(uvLocation, 2, GL_SHORT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(uvLocation);

        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.attribsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelAttribs.size() * 4 * sizeof(std::int8_t), _labelAttribs.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(attribsLocation, 4, GL_BYTE, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(attribsLocation);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledLabelBatch.indicesVBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, _labelIndices.size() * sizeof(std::uint16_t), _labelIndices.data(), GL_DYNAMIC_DRAW);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "uBitmap"), 0);
        glUniform2f(glGetUniformLocation(shaderProgram, "uUVScale"), 1.0f / bitmap->width, 1.0f / bitmap->height);

        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(_labelIndices.size()), GL_UNSIGNED_SHORT, 0);

        glDisableVertexAttribArray(attribsLocation);
        
        glDisableVertexAttribArray(uvLocation);

        if (_lightingShader2D) {
            glDisableVertexAttribArray(normalLocation);
        }
        
        glDisableVertexAttribArray(positionLocation);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        _labelVertices.clear();
        _labelNormals.clear();
        _labelTexCoords.clear();
        _labelAttribs.clear();
        _labelIndices.clear();
    }

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledTileSurfaces(const TileId& tileId) {
        auto it = _tileSurfaceMap.find(tileId);
        if (it == _tileSurfaceMap.end()) {
            it = _tileSurfaceMap.emplace(tileId, _tileSurfaceBuilder.buildTileSurface(tileId)).first;
        }
        for (const std::shared_ptr<TileSurface>& tileSurface : it->second) {
            CompiledSurface& compiledSurface = _compiledTileSurfaceMap[tileSurface];
            if (compiledSurface.indicesVBO == 0) {
                createCompiledSurface(compiledSurface);

                glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
                glBufferData(GL_ARRAY_BUFFER, tileSurface->getVertexGeometry().size() * sizeof(std::uint8_t), tileSurface->getVertexGeometry().data(), GL_STATIC_DRAW);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileSurface->getIndices().size() * sizeof(std::uint16_t), tileSurface->getIndices().data(), GL_STATIC_DRAW);
            }
        }
        return it->second;
    }

    void GLTileRenderer::createFrameBuffer(FrameBuffer& frameBuffer, bool useColor, bool useDepth, bool useStencil) {
        glGenFramebuffers(1, &frameBuffer.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.fbo);

        if (useDepth && useStencil && _glExtensions->GL_OES_packed_depth_stencil_supported()) {
            GLuint depthStencilRB = 0;
            glGenRenderbuffers(1, &depthStencilRB);
            glBindRenderbuffer(GL_RENDERBUFFER, depthStencilRB);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, _screenWidth, _screenHeight);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthStencilRB);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencilRB);
            frameBuffer.depthStencilAttachments.push_back(GL_DEPTH_ATTACHMENT);
            frameBuffer.depthStencilAttachments.push_back(GL_STENCIL_ATTACHMENT);
            frameBuffer.depthStencilRBs.push_back(depthStencilRB);
        } else {
            if (useDepth) {
                GLuint depthRB = 0;
                glGenRenderbuffers(1, &depthRB);
                glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, _screenWidth, _screenHeight);
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRB);
                frameBuffer.depthStencilAttachments.push_back(GL_DEPTH_ATTACHMENT);
                frameBuffer.depthStencilRBs.push_back(depthRB);
            }
            if (useStencil) {
                GLuint stencilRB = 0;
                glGenRenderbuffers(1, &stencilRB);
                glBindRenderbuffer(GL_RENDERBUFFER, stencilRB);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, _screenWidth, _screenHeight);
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencilRB);
                frameBuffer.depthStencilAttachments.push_back(GL_STENCIL_ATTACHMENT);
                frameBuffer.depthStencilRBs.push_back(stencilRB);
            }
        }

        if (useColor) {
            glGenTextures(1, &frameBuffer.colorTexture);
            glBindTexture(GL_TEXTURE_2D, frameBuffer.colorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _screenWidth, _screenHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameBuffer.colorTexture, 0);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            checkGLError();
        }
    }

    void GLTileRenderer::deleteFrameBuffer(FrameBuffer& frameBuffer) {
        if (frameBuffer.fbo != 0) {
            glDeleteFramebuffers(1, &frameBuffer.fbo);
            frameBuffer.fbo = 0;
        }
        if (!frameBuffer.depthStencilRBs.empty()) {
            glDeleteRenderbuffers(static_cast<GLsizei>(frameBuffer.depthStencilRBs.size()), frameBuffer.depthStencilRBs.data());
            frameBuffer.depthStencilRBs.clear();
        }
        if (frameBuffer.colorTexture != 0) {
            glDeleteTextures(1, &frameBuffer.colorTexture);
            frameBuffer.colorTexture = 0;
        }
    }

    void GLTileRenderer::createCompiledBitmap(CompiledBitmap& compiledBitmap) {
        glGenTextures(1, &compiledBitmap.texture);
    }

    void GLTileRenderer::deleteCompiledBitmap(CompiledBitmap& compiledBitmap) {
        if (compiledBitmap.texture != 0) {
            glDeleteTextures(1, &compiledBitmap.texture);
            compiledBitmap.texture = 0;
        }
    }

    void GLTileRenderer::createCompiledQuad(CompiledQuad& compiledQuad) {
        static const float vertices[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };

        glGenBuffers(1, &compiledQuad.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, compiledQuad.vbo);
        glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(float), vertices, GL_STATIC_DRAW);
    }

    void GLTileRenderer::deleteCompiledQuad(CompiledQuad& compiledQuad) {
        if (compiledQuad.vbo != 0) {
            glDeleteBuffers(1, &compiledQuad.vbo);
            compiledQuad.vbo = 0;
        }
    }

    void GLTileRenderer::createCompiledSurface(CompiledSurface& compiledSurface) {
        glGenBuffers(1, &compiledSurface.vertexGeometryVBO);
        glGenBuffers(1, &compiledSurface.indicesVBO);
    }

    void GLTileRenderer::deleteCompiledSurface(CompiledSurface& compiledSurface) {
        if (compiledSurface.vertexGeometryVBO != 0) {
            glDeleteBuffers(1, &compiledSurface.vertexGeometryVBO);
            compiledSurface.vertexGeometryVBO = 0;
        }
        if (compiledSurface.indicesVBO != 0) {
            glDeleteBuffers(1, &compiledSurface.indicesVBO);
            compiledSurface.indicesVBO = 0;
        }
    }

    void GLTileRenderer::createCompiledGeometry(CompiledGeometry& compiledGeometry) {
        if (_glExtensions->GL_OES_vertex_array_object_supported()) {
            _glExtensions->glGenVertexArraysOES(1, &compiledGeometry.geometryVAO);
        }
        glGenBuffers(1, &compiledGeometry.vertexGeometryVBO);
        glGenBuffers(1, &compiledGeometry.indicesVBO);
    }
    
    void GLTileRenderer::deleteCompiledGeometry(CompiledGeometry& compiledGeometry) {
        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glDeleteVertexArraysOES(1, &compiledGeometry.geometryVAO);
            compiledGeometry.geometryVAO = 0;
        }
        if (compiledGeometry.vertexGeometryVBO != 0) {
            glDeleteBuffers(1, &compiledGeometry.vertexGeometryVBO);
            compiledGeometry.vertexGeometryVBO = 0;
        }
        if (compiledGeometry.indicesVBO != 0) {
            glDeleteBuffers(1, &compiledGeometry.indicesVBO);
            compiledGeometry.indicesVBO = 0;
        }
    }

    void GLTileRenderer::createCompiledLabelBatch(CompiledLabelBatch& compiledLabelBatch) {
        glGenBuffers(1, &compiledLabelBatch.verticesVBO);
        glGenBuffers(1, &compiledLabelBatch.normalsVBO);
        glGenBuffers(1, &compiledLabelBatch.texCoordsVBO);
        glGenBuffers(1, &compiledLabelBatch.attribsVBO);
        glGenBuffers(1, &compiledLabelBatch.indicesVBO);
    }

    void GLTileRenderer::deleteCompiledLabelBatch(CompiledLabelBatch& compiledLabelBatch) {
        if (compiledLabelBatch.verticesVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.verticesVBO);
            compiledLabelBatch.verticesVBO = 0;
        }
        if (compiledLabelBatch.normalsVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.normalsVBO);
            compiledLabelBatch.normalsVBO = 0;
        }
        if (compiledLabelBatch.texCoordsVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.texCoordsVBO);
            compiledLabelBatch.texCoordsVBO = 0;
        }
        if (compiledLabelBatch.attribsVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.attribsVBO);
            compiledLabelBatch.attribsVBO = 0;
        }
        if (compiledLabelBatch.indicesVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.indicesVBO);
            compiledLabelBatch.indicesVBO = 0;
        }
    }
} }
