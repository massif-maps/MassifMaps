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
    const GLvoid* bufferGLOffset(int offset) {
#ifndef NDEBUG
        if (offset < 0) {
            throw std::runtime_error("Illegal buffer offset");
        }
#endif
        return reinterpret_cast<const GLvoid*>(static_cast<std::size_t>(offset));
    }

    void checkGLError() {
#ifndef NDEBUG
        std::string errorCodes;
        for (GLenum error = glGetError(); error != GL_NONE; error = glGetError()) {
            errorCodes += (errorCodes.empty() ? "" : ",");
        }
        if (!errorCodes.empty()) {
            throw std::runtime_error("Rendering failed: error codes" + errorCodes);
        }
#endif
    }
}

namespace carto { namespace vt {
    GLTileRenderer::GLTileRenderer(std::shared_ptr<GLExtensions> glExtensions, std::shared_ptr<const TileTransformer> transformer, float scale) :
        _tileSurfaceBuilder(transformer), _glExtensions(std::move(glExtensions)), _transformer(std::move(transformer)), _scale(scale)
    {
    }

    void GLTileRenderer::setLightingShader2D(const std::optional<LightingShader>& lightingShader2D) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        _lightingShader2D = lightingShader2D;
    }

    void GLTileRenderer::setLightingShader3D(const std::optional<LightingShader>& lightingShader3D) {
        std::lock_guard<std::mutex> lock(_mutex);

        _lightingShader3D = lightingShader3D;
    }

    void GLTileRenderer::setLightingShaderNormalMap(const std::optional<LightingShader>& lightingShaderNormalMap) {
        std::lock_guard<std::mutex> lock(_mutex);

        _lightingShaderNormalMap = lightingShaderNormalMap;
    }

    void GLTileRenderer::setInteractionMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _interactionMode = enabled;
    }

    void GLTileRenderer::setRasterFilterMode(RasterFilterMode filterMode) {
        std::lock_guard<std::mutex> lock(_mutex);

        _rasterFilterMode = filterMode;
    }
    
    void GLTileRenderer::setViewState(const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        _cameraProjMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
        _fullResolution = viewState.resolution;
        _halfResolution = viewState.resolution * 0.5f;
        _viewState = viewState;
        _viewState.zoomScale *= _scale;
    }
    
    void GLTileRenderer::setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles) {
        using TilePair = std::pair<TileId, std::shared_ptr<const Tile>>;

        // Clear the 'visible' label list for now (used only for culling)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _labels.clear();
        }

        // Build visible tile list for labels. Also build tile surfaces.
        std::set<TileId> tileIds;
        std::vector<std::shared_ptr<const Tile>> labelTiles;
        for (TilePair tilePair : tiles) {
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

        // Build label maps
        buildLabelMaps(labelTiles);
        
        // Build render tiles
        buildRenderTiles(tiles);
    }

    void GLTileRenderer::teleportVisibleTiles(int dx, int dy) {
        std::lock_guard<std::mutex> lock(_mutex);

        // Apply the requested shift to all target tiles
        std::vector<RenderTile> renderTiles;
        renderTiles.reserve(_renderTiles->size());
        for (RenderTile renderTile : *_renderTiles) {
            renderTile.targetTileId = renderTile.targetTileId.getTeleported(dx, dy);
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                RenderTileLayer& renderLayer = it->second;
                renderLayer.targetTileId = renderLayer.targetTileId.getTeleported(dx, dy);
            }
            renderTiles.push_back(std::move(renderTile));
        }
        _renderTiles = std::make_shared<std::vector<RenderTile>>(std::move(renderTiles));
    }

    void GLTileRenderer::initializeRenderer() {
        _renderTiles = std::make_shared<std::vector<RenderTile>>();
        for (int pass = 0; pass < 2; pass++) {
            _bitmapLabelMap[pass] = std::make_shared<BitmapLabelMap>();
        }
    }
    
    void GLTileRenderer::resetRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Drop all caches with shader/texture/FBO/VBO references
        _shaderProgramMap.clear();
        _compiledBitmapMap.clear();
        _compiledTileBitmapMap.clear();
        _compiledTileSurfaceMap.clear();
        _compiledTileGeometryMap.clear();
        _compiledLabelBatches.clear();
        _overlayBuffer2D = FrameBuffer();
        _overlayBuffer3D = FrameBuffer();
        _screenQuad = CompiledQuad();
    }
        
    void GLTileRenderer::deinitializeRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Release shaders
        for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); it++) {
            deleteShaderProgram(it->second);
        }
        _shaderProgramMap.clear();

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
        
        // Release screen and overlay FBOs
        deleteFrameBuffer(_overlayBuffer2D);
        deleteFrameBuffer(_overlayBuffer3D);

        // Release tile and screen VBOs
        deleteCompiledQuad(_screenQuad);
        
        _renderTiles.reset();
        _visibleRenderTiles.reset();
        for (int pass = 0; pass < 2; pass++) {
            _bitmapLabelMap[pass].reset();
            _visibleBitmapLabelMap[pass].reset();
        }
        _labels.clear();
        _layerLabelMap.clear();
    }
    
    bool GLTileRenderer::startFrame(float dt) {
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        std::lock_guard<std::mutex> lock(_mutex);

        bool refresh = false;

        // Load viewport dimensions, update dependent values
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] != _screenWidth || viewport[3] != _screenHeight) {
            _screenWidth = viewport[2];
            _screenHeight = viewport[3];

            // Release screen/overlay FBOs
            deleteFrameBuffer(_overlayBuffer2D);
            deleteFrameBuffer(_overlayBuffer3D);
        }

        // Update geometry blending state
        _visibleRenderTiles = _renderTiles;
        for (RenderTile& renderTile : *_visibleRenderTiles) {
            refresh = updateRenderTile(renderTile, dt) || refresh;
        }
        
        // Update labels
        _visibleBitmapLabelMap = _bitmapLabelMap;
        for (int pass = 0; pass < 2; pass++) {
            for (BitmapLabelsPair bitmapLabels : *_visibleBitmapLabelMap[pass]) {
                for (const std::shared_ptr<Label>& label : bitmapLabels.second) {
                    refresh = updateLabel(label, dt) || refresh;
                }
            }
        }
        
        // Reset label batch counter
        _labelBatchCounter = 0;

        return refresh;
    }
    
    void GLTileRenderer::renderGeometry2D() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_visibleRenderTiles) {
            return;
        }

        // Extract current stencil state
        GLint stencilBits = 0;
        GLint currentFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
        if (currentFBO != 0) {
            GLint stencilRB = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &stencilRB);
            if (stencilRB != 0) {
                GLint currentRB = 0;
                glGetIntegerv(GL_RENDERBUFFER_BINDING, &currentRB);
                glBindRenderbuffer(GL_RENDERBUFFER, stencilRB);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencilBits);
                glBindRenderbuffer(GL_RENDERBUFFER, currentRB);
            }
        } else {
            glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
        }

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

        // 2D geometry pass
        renderGeometry2D(*_visibleRenderTiles, stencilBits);
        
        // Restore GL state
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(255);
    }
    
    void GLTileRenderer::renderGeometry3D() {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_visibleRenderTiles) {
            return;
        }
        
        // Update GL state
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // 3D polygon pass
        renderGeometry3D(*_visibleRenderTiles);
        
        // Restore GL state
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glEnable(GL_BLEND);
        glStencilMask(255);
    }
    
    void GLTileRenderer::renderLabels(bool labels2D, bool labels3D) {
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        std::lock_guard<std::mutex> lock(_mutex);

        if (!_visibleBitmapLabelMap[0] || !_visibleBitmapLabelMap[1]) {
            return;
        }
        
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
        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && labels2D) || (pass == 1 && labels3D)) {
                for (BitmapLabelsPair bitmapLabels : *_visibleBitmapLabelMap[pass]) {
                    renderLabels(bitmapLabels.second, bitmapLabels.first);
                }
            }
        }
        
        // Restore GL state
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glStencilMask(255);
    }
    
    bool GLTileRenderer::endFrame() {
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
        return false;
    }

    void GLTileRenderer::cullLabels(LabelCuller& culler) {
        std::vector<std::shared_ptr<Label>> labels;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            labels = _labels;
        }

        culler.process(labels, _mutex);
    }
    
    bool GLTileRenderer::findBitmapIntersections(const std::vector<cglib::ray3<double>>& rays, std::vector<BitmapIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        // Scan each tile/each layer
        std::size_t initialResultCount = results.size();
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.active) {
                    continue;
                }

                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderLayer.targetTileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderLayer.sourceTileId);

                // Do intersection with the tile bbox first
                if (!std::any_of(rays.begin(), rays.end(), [&](const cglib::ray3<double>& ray) { return cglib::intersect_bbox(tileBBox, ray); })) {
                    continue;
                }
                
                // Store all bitmaps
                std::vector<cglib::ray3<double>> rayTiles;
                for (const cglib::ray3<double>& ray : rays) {
                    rayTiles.push_back(cglib::transform_ray(ray, invTileMatrix));
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                    auto it = _tileSurfaceMap.find(renderLayer.sourceTileId);
                    if (it == _tileSurfaceMap.end()) {
                        continue;
                    }

                    std::vector<BitmapIntersectionInfo> resultsTile;
                    for (const std::shared_ptr<TileSurface>& tileSurface : it->second) {
                        findTileBitmapIntersections(renderLayer.sourceTileId, bitmap, tileSurface, rayTiles, renderLayer.tileSize, resultsTile);
                    }

                    for (const BitmapIntersectionInfo& resultTile : resultsTile) {
                        const cglib::ray3<double>& ray = rays[resultTile.rayIndex];

                        cglib::vec3<float> posTile = cglib::vec3<float>::convert(rayTiles[resultTile.rayIndex](resultTile.rayT));
                        cglib::vec2<float> tilePos = resultTile.uv;

                        // Check that the hit position is inside the tile and normal is facing toward the ray
                        cglib::mat3x3<double> clipTransform = cglib::inverse(calculateTileMatrix2D(renderLayer.targetTileId)) * calculateTileMatrix2D(renderLayer.sourceTileId);
                        cglib::vec2<float> clipPos = cglib::transform_point(tilePos, cglib::mat3x3<float>::convert(clipTransform));
                        if (clipPos(0) < 0 || clipPos(1) < 0 || clipPos(0) > 1 || clipPos(1) > 1) {
                            continue;
                        }

                        cglib::vec3<float> normal = tileTransformer->calculateNormal(tilePos);
                        if (cglib::dot_product(normal, cglib::vec3<float>::convert(ray.direction)) >= 0) {
                            continue;
                        }

                        cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(posTile), tileMatrix);
                        double rayT = cglib::dot_product(pos - ray.origin, ray.direction) / cglib::dot_product(ray.direction, ray.direction);
                        results.emplace_back(resultTile.tileId, renderLayer.layer->getLayerIndex(), resultTile.bitmap, resultTile.uv, resultTile.rayIndex, rayT);
                    }
                }
            }
        }

        return results.size() > initialResultCount;
    }
    
    bool GLTileRenderer::findGeometryIntersections(const std::vector<cglib::ray3<double>>& rays, float pointBuffer, float lineBuffer, bool geom2D, bool geom3D, std::vector<GeometryIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Build render layer map for each layer
        std::size_t initialResultCount = results.size();
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.active) {
                    continue;
                }

                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderLayer.targetTileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderLayer.sourceTileId);

                // Test all geometry batches for intersections
                std::vector<cglib::ray3<double>> rayTiles;
                for (const cglib::ray3<double>& ray : rays) {
                    rayTiles.push_back(cglib::transform_ray(ray, invTileMatrix));
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        if (!geom3D) {
                            continue;
                        }
                    } else {
                        if (!geom2D || !std::any_of(rays.begin(), rays.end(), [&](const cglib::ray3<double>& ray) { return cglib::intersect_bbox(tileBBox, ray); })) {
                            continue;
                        }
                    }

                    std::vector<GeometryIntersectionInfo> resultsTile;
                    findTileGeometryIntersections(renderLayer.sourceTileId, geometry, rayTiles, renderLayer.tileSize, pointBuffer, lineBuffer, renderLayer.blend, resultsTile);
                    
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        std::stable_sort(resultsTile.begin(), resultsTile.end(), [](const GeometryIntersectionInfo& result1, const GeometryIntersectionInfo& result2) {
                            return result1.rayT > result2.rayT;
                        });
                    }

                    for (const GeometryIntersectionInfo& resultTile : resultsTile) {
                        const cglib::ray3<double>& ray = rays[resultTile.rayIndex];

                        cglib::vec3<float> posTile = cglib::vec3<float>::convert(rayTiles[resultTile.rayIndex](resultTile.rayT));
                        cglib::vec2<float> tilePos = tileTransformer->calculateTilePosition(posTile);

                        // Check that the hit position is inside the tile and normal is facing toward the ray
                        cglib::mat3x3<double> clipTransform = cglib::inverse(calculateTileMatrix2D(renderLayer.targetTileId)) * calculateTileMatrix2D(renderLayer.sourceTileId);
                        cglib::vec2<float> clipPos = cglib::transform_point(tilePos, cglib::mat3x3<float>::convert(clipTransform));
                        if (clipPos(0) < 0 || clipPos(1) < 0 || clipPos(0) > 1 || clipPos(1) > 1) {
                            continue;
                        }
                        cglib::vec3<float> normal = tileTransformer->calculateNormal(tilePos);
                        if (cglib::dot_product(normal, cglib::vec3<float>::convert(ray.direction)) >= 0) {
                            continue;
                        }

                        cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(posTile), tileMatrix);
                        double rayT = cglib::dot_product(pos - ray.origin, ray.direction) / cglib::dot_product(ray.direction, ray.direction);
                        results.emplace_back(resultTile.tileId, renderLayer.layer->getLayerIndex(), resultTile.featureId, resultTile.rayIndex, rayT);
                    }
                }
            }
        }

        return results.size() > initialResultCount;
    }
    
    bool GLTileRenderer::findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const {
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        std::lock_guard<std::mutex> lock(_mutex);

        // Test for label intersections. The ordering may be mixed compared to actual rendering order, but this is non-issue if the labels are non-overlapping.
        std::size_t initialResultCount = results.size();
        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && !labels2D) || (pass == 1 && !labels3D)) {
                continue;
            }

            for (BitmapLabelsPair bitmapLabels : *_bitmapLabelMap[pass]) {
                for (const std::shared_ptr<Label>& label : bitmapLabels.second) {
                    if (!label->isValid() || !label->isVisible() || !label->isActive() || label->getOpacity() <= 0) {
                        continue;
                    }

                    std::vector<GeometryIntersectionInfo> resultsLocal;
                    findLabelIntersections(label, rays, buffer, resultsLocal);
                    
                    for (const GeometryIntersectionInfo& result : resultsLocal) {
                        if (cglib::dot_product(label->getNormal(), cglib::vec3<float>::convert(rays[result.rayIndex].direction)) >= 0) {
                            continue;
                        }

                        results.emplace_back(result.tileId, label->getLayerIndex(), result.featureId, result.rayIndex, result.rayT);
                    }
                }
            }
        }

        return results.size() > initialResultCount;
    }

    bool GLTileRenderer::isTileVisible(const TileId& tileId) const {
        cglib::bbox3<double> bbox = _transformer->calculateTileBBox(tileId);
        return _viewState.frustum.inside(bbox);
    }

    bool GLTileRenderer::isEmptyBlendRequired(CompOp compOp) const {
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

    bool GLTileRenderer::testIntersectionOpacity(const std::shared_ptr<const BitmapPattern>& pattern, const cglib::vec2<float>& uvp, const cglib::vec2<float>& uv0, const cglib::vec2<float>& uv1) const {
        if (!pattern) {
            return false;
        }

        int xp = static_cast<int>(uvp(0) * pattern->bitmap->width);
        int yp = static_cast<int>(uvp(1) * pattern->bitmap->height);
        int x0 = static_cast<int>(uv0(0) * pattern->bitmap->width);
        int y0 = static_cast<int>(uv0(1) * pattern->bitmap->height);
        int x1 = static_cast<int>(uv1(0) * pattern->bitmap->width);
        int y1 = static_cast<int>(uv1(1) * pattern->bitmap->height);
        
        // Test that the hit point is surrounded by solid pixels in each direction
        int mask = 0;
        for (int x = x0, y = yp; x <= x1; x++) {
            if (x >= 0 && x < pattern->bitmap->width && y >= 0 && y < pattern->bitmap->height) {
                float alpha = Color(pattern->bitmap->data[y * pattern->bitmap->width + x])[3];
                if (alpha > ALPHA_HIT_THRESHOLD) {
                    mask |= (x >= xp ? 1 : 0);
                    mask |= (x <= xp ? 2 : 0);
                }
            }
        }
        for (int x = xp, y = y0; y <= y1; y++) {
            if (x >= 0 && x < pattern->bitmap->width && y >= 0 && y < pattern->bitmap->height) {
                float alpha = Color(pattern->bitmap->data[y * pattern->bitmap->width + x])[3];
                if (alpha > ALPHA_HIT_THRESHOLD) {
                    mask |= (y >= yp ? 4 : 0);
                    mask |= (y <= yp ? 8 : 0);
                }
            }
        }
        return mask == 15;
    }

    void GLTileRenderer::buildRenderTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles) {
        std::vector<RenderTile> renderTiles;
        renderTiles.reserve(tiles.size() + _renderTiles->size());

        // Build new render tiles
        for (auto it = tiles.begin(); it != tiles.end(); it++) {
            RenderTile& renderTile = renderTiles.emplace_back();
            initializeRenderTile(it->first, renderTile, it->second, *_renderTiles);
        }

        // Merge existing tiles not yet added
        for (auto it = _renderTiles->begin(); it != _renderTiles->end(); it++) {
            RenderTile existingRenderTile = *it;
            if (existingRenderTile.visible) {
                mergeExistingRenderTile(existingRenderTile.targetTileId, existingRenderTile, renderTiles, 1);
            }
        }

        // Update built tile list
        _renderTiles = std::make_shared<std::vector<RenderTile>>(std::move(renderTiles));
    }

    void GLTileRenderer::initializeRenderTile(TileId targetTileId, RenderTile& renderTile, const std::shared_ptr<const Tile>& tile, const std::vector<RenderTile>& existingRenderTiles) const {
        TileId rootTileId = targetTileId;
        while (rootTileId.zoom > 0) {
            rootTileId = rootTileId.getParent();
        }
        TileId sourceTileId = tile->getTileId().getTeleported(rootTileId.x, rootTileId.y);
        if (sourceTileId.zoom > targetTileId.zoom) {
            targetTileId = sourceTileId;
        }

        renderTile.targetTileId = targetTileId;
        renderTile.tile = tile;
        renderTile.visible = false;
        for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
            RenderTileLayer renderLayer;
            renderLayer.targetTileId = targetTileId;
            renderLayer.sourceTileId = sourceTileId;
            renderLayer.layer = layer;
            renderLayer.tileSize = tile->getTileSize();
            renderLayer.active = true;
            renderLayer.blend = (!layer->getBitmaps().empty() && layer->getBitmaps().front()->getFormat() == TileBitmap::Format::RGBA ? 1.0f : 0.0f);
            renderTile.renderLayers.insert({ layer->getLayerIndex(), std::move(renderLayer) });
        }

        std::multimap<int, RenderTileLayer> existingRenderLayers;
        for (const RenderTile& existingRenderTile : existingRenderTiles) {
            if (!renderTile.targetTileId.intersects(existingRenderTile.targetTileId)) {
                continue;
            }
            
            renderTile.visible = renderTile.visible || existingRenderTile.visible;
            for (auto it = existingRenderTile.renderLayers.begin(); it != existingRenderTile.renderLayers.end(); it++) {
                int layerIdx = it->first;
                RenderTileLayer existingRenderLayer = it->second;

                auto it2 = renderTile.renderLayers.find(layerIdx);
                if (it2 != renderTile.renderLayers.end()) {
                    RenderTileLayer& renderLayer = it2->second;
                    if (renderLayer.layer == existingRenderLayer.layer || renderLayer.layer->getBitmaps().empty()) {
                        renderLayer.blend = std::max(renderLayer.blend, existingRenderLayer.blend);
                        continue;
                    }
                }

                existingRenderLayer.targetTileId = (existingRenderLayer.targetTileId.zoom > targetTileId.zoom ? existingRenderLayer.targetTileId : targetTileId);
                existingRenderLayer.active = !existingRenderLayer.layer->getBitmaps().empty();
                existingRenderLayers.insert({ layerIdx, std::move(existingRenderLayer) });
            }
        }

        std::swap(renderTile.renderLayers, existingRenderLayers);
        for (auto it = existingRenderLayers.begin(); it != existingRenderLayers.end(); it++) {
            renderTile.renderLayers.insert({ it->first, it->second });
        }
    }

    void GLTileRenderer::mergeExistingRenderTile(TileId targetTileId, const RenderTile& existingRenderTile, std::vector<RenderTile>& renderTiles, int depth) const {
        if (depth < 0) {
            return;
        }

        for (const RenderTile& renderTile : renderTiles) {
            if (renderTile.targetTileId.covers(targetTileId)) {
                return;
            }
            if (targetTileId.covers(renderTile.targetTileId)) {
                for (int i = 0; i < 4; i++) {
                    mergeExistingRenderTile(targetTileId.getChild(i / 2, i % 2), existingRenderTile, renderTiles, depth - 1);
                }
                return;
            }
        }

        RenderTile renderTile = existingRenderTile;
        renderTile.targetTileId = targetTileId;
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            RenderTileLayer& renderLayer = it->second;
            renderLayer.targetTileId = (targetTileId.zoom > renderLayer.targetTileId.zoom ? targetTileId : renderLayer.targetTileId);
            renderLayer.active = !renderLayer.layer->getBitmaps().empty();
        }
        renderTiles.push_back(renderTile);
    }

    bool GLTileRenderer::updateRenderTile(RenderTile& renderTile, float dBlend) const {
        renderTile.visible = isTileVisible(renderTile.targetTileId);

        bool refresh = false;
        for (auto it = renderTile.renderLayers.end(); it != renderTile.renderLayers.begin(); ) {
            it--;
            RenderTileLayer& renderLayer = it->second;

            float delta = renderTile.visible ? dBlend : 1.0f;
            if (renderLayer.active) {
                renderLayer.blend = std::min(1.0f, renderLayer.blend + delta);
                refresh = (renderLayer.blend < 1.0f) || refresh;
                if (renderLayer.blend >= 1.0f) {
                    while (it != renderTile.renderLayers.begin()) {
                        auto it2 = it;
                        it2--;
                        if (it->first != it2->first || !it->second.targetTileId.covers(it2->second.targetTileId)) {
                            break;
                        }
                        it = renderTile.renderLayers.erase(it2);
                    }
                }
            }
            else {
                renderLayer.blend = std::max(0.0f, renderLayer.blend - delta);
                refresh = (renderLayer.blend > 0.0f) || refresh;
                if (renderLayer.blend <= 0.0f) {
                    it = renderTile.renderLayers.erase(it);
                }
            }
        }
        return refresh;
    }

    void GLTileRenderer::buildLabelMaps(const std::vector<std::shared_ptr<const Tile>>& labelTiles) {
        // Create label list, merge geometries
        std::map<int, GlobalIdLabelMap> newLayerLabelMap;
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(tile->getTileId(), 1.0f);
            std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tile->getTileId());
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                GlobalIdLabelMap& newLabelMap = newLayerLabelMap[layer->getLayerIndex()];
                if (newLabelMap.empty()) {
                    newLabelMap.reserve(_layerLabelMap[layer->getLayerIndex()].size() + 64);
                }
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    auto newLabel = std::make_shared<Label>(*tileLabel, tileMatrix, transformer);
                    std::shared_ptr<Label>& label = newLabelMap[tileLabel->getGlobalId()];
                    if (label) {
                        label->mergeGeometries(*newLabel);
                    }
                    else {
                        label = newLabel;
                    }
                }
            }
        }

        // Release old labels
        for (auto oldLayerLabelIt = _layerLabelMap.begin(); oldLayerLabelIt != _layerLabelMap.end(); oldLayerLabelIt++) {
            GlobalIdLabelMap& oldLabelMap = oldLayerLabelIt->second;
            for (auto oldLabelIt = oldLabelMap.begin(); oldLabelIt != oldLabelMap.end(); ) {
                const std::shared_ptr<Label>& oldLabel = oldLabelIt->second;
                if (oldLabel->getOpacity() <= 0) {
                    oldLabelIt = oldLabelMap.erase(oldLabelIt);
                }
                else {
                    oldLabel->setActive(false);
                    oldLabelIt++;
                }
            }
        }

        // Copy existing label placements
        for (auto newLayerLabelIt = newLayerLabelMap.begin(); newLayerLabelIt != newLayerLabelMap.end(); newLayerLabelIt++) {
            const GlobalIdLabelMap& newLabelMap = newLayerLabelIt->second;
            GlobalIdLabelMap& labelMap = _layerLabelMap[newLayerLabelIt->first];
            for (auto newLabelIt = newLabelMap.begin(); newLabelIt != newLabelMap.end(); newLabelIt++) {
                const std::shared_ptr<Label>& newLabel = newLabelIt->second;
                std::shared_ptr<Label>& label = labelMap[newLabelIt->first];
                if (label) {
                    newLabel->setVisible(label->isVisible());
                    newLabel->setOpacity(label->getOpacity());
                    newLabel->snapPlacement(*label);
                }
                else {
                    newLabel->setVisible(false);
                    newLabel->setOpacity(0);
                }
                newLabel->setActive(true);
                label = newLabel;
            }
        }

        // Build final label list, group labels by font bitmaps. Sort the groups to have stable render order.
        std::vector<std::shared_ptr<Label>> labels;
        labels.reserve(_labels.size() + 64);
        std::array<std::shared_ptr<BitmapLabelMap>, 2> bitmapLabelMap;
        for (int pass = 0; pass < 2; pass++) {
            bitmapLabelMap[pass] = std::make_shared<BitmapLabelMap>();
        }
        for (auto layerLabelIt = _layerLabelMap.begin(); layerLabelIt != _layerLabelMap.end(); layerLabelIt++) {
            const GlobalIdLabelMap& labelMap = layerLabelIt->second;
            for (auto labelIt = labelMap.begin(); labelIt != labelMap.end(); labelIt++) {
                const std::shared_ptr<Label>& label = labelIt->second;
                const std::shared_ptr<const Bitmap>& bitmap = label->getStyle()->glyphMap->getBitmapPattern()->bitmap;
                int pass = (label->getStyle()->orientation == LabelOrientation::BILLBOARD_3D ? 1 : 0);

                std::vector<std::shared_ptr<Label>>& bitmapLabels = (*bitmapLabelMap[pass])[bitmap];
                if (bitmapLabels.empty()) {
                    bitmapLabels.reserve((*_bitmapLabelMap[pass])[bitmap].size() + 64);
                }
                bitmapLabels.push_back(label);
                labels.push_back(label);
            }
        }
        for (int pass = 0; pass < 2; pass++) {
            for (auto it = bitmapLabelMap[pass]->begin(); it != bitmapLabelMap[pass]->end(); it++) {
                std::stable_sort(it->second.begin(), it->second.end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
                    if (label1->getPriority() != label2->getPriority()) {
                        return label1->getPriority() > label2->getPriority();
                    }
                    if (label1->getLayerIndex() != label2->getLayerIndex()) {
                        return label1->getLayerIndex() < label2->getLayerIndex();
                    }
                    return label1->getGlobalId() > label2->getGlobalId();
                });
            }
        }

        // Update built label lists and maps
        _labels = std::move(labels);
        _bitmapLabelMap = std::move(bitmapLabelMap);
    }

    bool GLTileRenderer::updateLabel(const std::shared_ptr<Label>& label, float dOpacity) const {
        bool refresh = false;
        if (label->isValid()) {
            if (label->isVisible() && label->isActive()) {
                float opacity = std::min(1.0f, label->getOpacity() + dOpacity);
                label->setOpacity(opacity);
                refresh = (opacity < 1.0f) || refresh;
            }
            else {
                float opacity = std::max(0.0f, label->getOpacity() - dOpacity);
                label->setOpacity(opacity);
                refresh = (opacity > 0.0f) || refresh;
            }
        }
        return refresh;
    }
    
    void GLTileRenderer::findTileGeometryIntersections(const TileId& tileId, const std::shared_ptr<const TileGeometry>& geometry, const std::vector<cglib::ray3<double>>& rays, float tileSize, float pointBuffer, float lineBuffer, float heightScale, std::vector<GeometryIntersectionInfo>& results) const {
        float scale = geometry->getGeometryScale() / tileSize / std::pow(2.0f, _viewState.zoom - tileId.zoom);
        for (TileGeometryIterator it(tileId, geometry, _transformer, _viewState, pointBuffer, lineBuffer, scale, heightScale); it; ++it) {
            long long featureId = it.id();
            TileGeometryIterator::TriangleCoords coords = it.triangleCoords();

            for (std::size_t i = 0; i < rays.size(); i++) {
                double t = 0;
                cglib::vec2<double> uv(0.0f, 0.0f);
                if (cglib::intersect_triangle(cglib::vec3<double>::convert(coords[0]), cglib::vec3<double>::convert(coords[1]), cglib::vec3<double>::convert(coords[2]), rays[i], &t, &uv)) {
                    if (geometry->getType() == TileGeometry::Type::POINT && it.attribs()[1] == 1) {
                        TileGeometryIterator::TriangleUVs triUVs = it.triangleUVs();
                        cglib::vec2<float> interpolatedUV = triUVs[0] + (triUVs[1] - triUVs[0]) * static_cast<float>(uv(0)) + (triUVs[2] - triUVs[0]) * static_cast<float>(uv(1));
                        float u0 = std::min(triUVs[0](0), std::min(triUVs[1](0), triUVs[2](0)));
                        float u1 = std::max(triUVs[0](0), std::max(triUVs[1](0), triUVs[2](0)));
                        float v0 = std::min(triUVs[0](1), std::min(triUVs[1](1), triUVs[2](1)));
                        float v1 = std::max(triUVs[0](1), std::max(triUVs[1](1), triUVs[2](1)));
                        if (!testIntersectionOpacity(geometry->getStyleParameters().pattern, interpolatedUV, cglib::vec2<float>(u0, v0), cglib::vec2<float>(u1, v1))) {
                            continue;
                        }
                    }
                    if (!results.empty()) {
                        const GeometryIntersectionInfo& result = results.back();
                        if (result.tileId == tileId && result.featureId == featureId) {
                            break;
                        }
                    }
                    results.emplace_back(tileId, -1, featureId, i, t);
                    break;
                }
            }
        }
    }

    void GLTileRenderer::findLabelIntersections(const std::shared_ptr<Label>& label, const std::vector<cglib::ray3<double>>& rays, float buffer, std::vector<GeometryIntersectionInfo>& results) const {
        float size = label->getStyle()->sizeFunc(_viewState);
        if (size <= 0) {
            return;
        }

        std::array<cglib::vec3<float>, 4> envelope;
        if (!label->calculateEnvelope(size, buffer, _viewState, envelope)) {
            return;
        }

        std::array<cglib::vec3<double>, 4> quad;
        if (!label->getStyle()->transform) {
            for (int i = 0; i < 4; i++) {
                quad[i] = _viewState.origin + cglib::vec3<double>::convert(envelope[i]);
            }
        } else {
            float zoomScale = std::pow(2.0f, label->getTileId().zoom - _viewState.zoom);
            cglib::vec2<float> translate = label->getStyle()->transform->translate() * zoomScale;
            cglib::mat4x4<double> translateMatrix = cglib::mat4x4<double>::convert(_transformer->calculateTileTransform(label->getTileId(), translate, 1.0f));
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(label->getTileId(), 1);
            cglib::mat4x4<double> labelMatrix = tileMatrix * translateMatrix * cglib::inverse(tileMatrix) * cglib::translate4_matrix(_viewState.origin);
            for (int i = 0; i < 4; i++) {
                quad[i] = cglib::transform_point(cglib::vec3<double>::convert(envelope[i]), labelMatrix);
            }
        }

        for (std::size_t i = 0; i < rays.size(); i++) {
            double t = 0;
            if (cglib::intersect_triangle(quad[0], quad[1], quad[2], rays[i], &t) || cglib::intersect_triangle(quad[0], quad[2], quad[3], rays[i], &t)) {
                results.emplace_back(label->getTileId(), -1, label->getLocalId(), i, t);
                break;
            }
        }
    }

    void GLTileRenderer::findTileBitmapIntersections(const TileId& tileId, const std::shared_ptr<const TileBitmap>& bitmap, const std::shared_ptr<const TileSurface>& tileSurface, const std::vector<cglib::ray3<double>>& rays, float tileSize, std::vector<BitmapIntersectionInfo>& results) const {
        cglib::mat4x4<double> surfaceToTileTransform = cglib::inverse(calculateTileMatrix(tileId)) * cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
        for (std::size_t index = 0; index + 2 < tileSurface->getIndices().size(); index += 3) {
            std::array<cglib::vec3<double>, 3> triangle;
            for (int i = 0; i < 3; i++) {
                std::size_t coordOffset = tileSurface->getIndices()[index + i] * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.coordOffset;
                const float* coordPtr = reinterpret_cast<const float*>(&tileSurface->getVertexGeometry()[coordOffset]);
                triangle[i] = cglib::transform_point(cglib::vec3<double>(coordPtr[0], coordPtr[1], coordPtr[2]), surfaceToTileTransform);
            }

            for (std::size_t i = 0; i < rays.size(); i++) {
                double t = 0;
                if (cglib::intersect_triangle(triangle[0], triangle[1], triangle[2], rays[i], &t)) {
                    std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tileId);
                    cglib::vec2<float> uv = transformer->calculateTilePosition(cglib::vec3<float>::convert(rays[i](t)));
                    if (!results.empty()) {
                        const BitmapIntersectionInfo& result = results.back();
                        if (result.tileId == tileId && result.bitmap == bitmap) {
                            break;
                        }
                    }
                    results.emplace_back(tileId, -1, bitmap, uv, i, t);
                    break;
                }
            }
        }
    }

    void GLTileRenderer::renderGeometry2D(const std::vector<RenderTile>& renderTiles, GLint stencilBits) {
        // Extract layer tiles for each layers
        std::map<int, std::vector<const RenderTileLayer*>> renderLayerMap;
        for (const RenderTile& renderTile : renderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const std::shared_ptr<const TileLayer>& layer = it->second.layer;

                bool contains2DGeometry = !layer->getBackgrounds().empty() || !layer->getBitmaps().empty();
                for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
                    contains2DGeometry = (geometry->getType() != TileGeometry::Type::POLYGON3D) || contains2DGeometry;
                }
                if (contains2DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    renderLayerMap[it->first].push_back(&it->second);
                }
            }
        }

        // Allocate stencil value for each target tile
        std::map<TileId, GLint> tileStencilMap;
        if (stencilBits > 0) {
            for (const RenderTile& renderTile : renderTiles) {
                if (!renderTile.visible || renderTile.renderLayers.empty()) {
                    continue;
                }
                auto it = renderTile.renderLayers.begin();
                TileId targetTileId = it->second.targetTileId;
                while (++it != renderTile.renderLayers.end()) {
                    if (it->second.targetTileId.zoom < targetTileId.zoom) {
                        targetTileId = it->second.targetTileId;
                    }
                }
                tileStencilMap[targetTileId] = static_cast<int>(tileStencilMap.size() + 1);
            }
            glEnable(GL_STENCIL_TEST);
        }
        
        // Render tile layers in correct order
        bool resetStencil = true;
        std::optional<CompOp> currentCompOp;
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            const std::vector<const RenderTileLayer*>& renderLayers = it->second;
            if (renderLayers.empty()) {
                continue;
            }
            const std::shared_ptr<const TileLayer>& layer = renderLayers.front()->layer;

            // Layer settings
            float layerOpacity = (layer->getOpacityFunc())(_viewState);
            float geometryOpacity = 1.0f;
            if (!layer->getCompOp()) { // a 'useful' hack - we use real layer opacity only if comp-op is explicitly defined; otherwise we translate it into element opacity, which is in many cases close enough
                std::swap(layerOpacity, geometryOpacity);
            }
            CompOp layerCompOp = (layer->getCompOp() ? *layer->getCompOp() : CompOp::SRC_OVER);

            // If compositing is enabled for this layer, prepare overlay rendering buffer.
            GLint currentFBO = 0;
            if (layer->getCompOp()) {
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                if (_overlayBuffer2D.fbo == 0) {
                    createFrameBuffer(_overlayBuffer2D, true, false, stencilBits > 0);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer2D.fbo);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                resetStencil = true;
            }

            // If needed, initialize the stencil buffer with target tile masks
            if (resetStencil && stencilBits > 0) {
                resetStencil = false;

                glStencilMask(255);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
                for (auto it = tileStencilMap.begin(); it != tileStencilMap.end(); it++) {
                    glStencilFunc(GL_ALWAYS, it->second, 255);
                    renderTileMask(it->first);
                }
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glStencilMask(0);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }

            // Render tile layers for this layer
            for (const RenderTileLayer* renderLayer : renderLayers) {
                if (stencilBits > 0) {
                    int stencilValue = 0;
                    for (TileId targetTileId = renderLayer->targetTileId; targetTileId.zoom >= 0; targetTileId = targetTileId.getParent()) {
                        auto stencilIt = tileStencilMap.find(targetTileId);
                        if (stencilIt != tileStencilMap.end()) {
                            stencilValue = stencilIt->second;
                            break;
                        }
                    }
                    glStencilFunc(GL_EQUAL, stencilValue, 255);
                }

                for (const std::shared_ptr<TileBackground>& background : renderLayer->layer->getBackgrounds()) {
                    CompOp backgroundCompOp = CompOp::SRC_OVER;
                    if (currentCompOp != backgroundCompOp) {
                        setCompOp(backgroundCompOp);
                        currentCompOp = backgroundCompOp;
                    }
                    renderTileBackground(renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, background);
                }

                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer->layer->getBitmaps()) {
                    CompOp bitmapCompOp = CompOp::SRC_OVER;
                    if (currentCompOp != bitmapCompOp) {
                        setCompOp(bitmapCompOp);
                        currentCompOp = bitmapCompOp;
                    }
                    renderTileBitmap(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, bitmap);
                }

                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        CompOp geometryCompOp = geometry->getStyleParameters().compOp;
                        if (currentCompOp != geometryCompOp) {
                            setCompOp(geometryCompOp);
                            currentCompOp = geometryCompOp;
                        }
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, geometry);
                    }
                }
            }

            // If compositing was enabled for this layer, blend the rendered layer with framebuffer
            if (layer->getCompOp()) {
                if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !_overlayBuffer2D.depthStencilAttachments.empty()) {
                    _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer2D.depthStencilAttachments.size()), _overlayBuffer2D.depthStencilAttachments.data());
                }

                glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

                if (stencilBits > 0) {
                    glDisable(GL_STENCIL_TEST);
                }
                if (currentCompOp != layerCompOp) {
                    setCompOp(layerCompOp);
                    currentCompOp = layerCompOp;
                }
                blendScreenTexture(layerOpacity, _overlayBuffer2D.colorTexture);
                if (stencilBits > 0) {
                    glEnable(GL_STENCIL_TEST);
                }
            }
        }
    }
    
    void GLTileRenderer::renderGeometry3D(const std::vector<RenderTile>& renderTiles) {
        // Extract layer tiles for each layers
        std::map<int, std::vector<const RenderTileLayer*>> renderLayerMap;
        for (const RenderTile& renderTile : renderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const std::shared_ptr<const TileLayer>& layer = it->second.layer;

                bool contains3DGeometry = false;
                for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
                    contains3DGeometry = (geometry->getType() == TileGeometry::Type::POLYGON3D) || contains3DGeometry;
                }
                if (contains3DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    renderLayerMap[it->first].push_back(&it->second);
                }
            }
        }

        // Render tile layers in correct order
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            const std::vector<const RenderTileLayer*>& renderLayers = it->second;
            if (renderLayers.empty()) {
                continue;
            }
            const std::shared_ptr<const TileLayer>& layer = renderLayers.front()->layer;

            // Layer settings
            float layerOpacity = (layer->getOpacityFunc())(_viewState);
            float geometryOpacity = 1.0f;
            if (!layer->getCompOp()) { // use the hack to conform with normal '2D' layers
                std::swap(layerOpacity, geometryOpacity);
            }
            CompOp layerCompOp = (layer->getCompOp() ? *layer->getCompOp() : CompOp::SRC_OVER);

            // Always use separate rendering overlay with Z buffer. Prepare the overlay buffer.
            GLint currentFBO = 0;
            if (true) {
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                if (_overlayBuffer3D.fbo == 0) {
                    createFrameBuffer(_overlayBuffer3D, true, true, false);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer3D.fbo);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            // Render tile layers for this layer
            for (const RenderTileLayer* renderLayer : renderLayers) {
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        // NOTE: geometry comp op is not supported for 3D polygons. Blending is disabled, setGLBlendState not needed
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, geometry);
                    }
                }
            }

            // Blend the rendered layer with framebuffer
            if (true) {
                if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !_overlayBuffer3D.depthStencilAttachments.empty()) {
                    _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer3D.depthStencilAttachments.size()), _overlayBuffer3D.depthStencilAttachments.data());
                }

                glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

                glEnable(GL_BLEND);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                setCompOp(layerCompOp);
                blendScreenTexture(layerOpacity, _overlayBuffer3D.colorTexture);
                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
            }
        }
    }
    
    void GLTileRenderer::renderLabels(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap) {
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

                if (labelStyle->transform || (lastLabelStyle && lastLabelStyle->transform) || labelBatchParams.scale != labelStyle->scale || labelBatchParams.parameterCount + 2 > LabelBatchParameters::MAX_PARAMETERS) {
                    renderLabelBatch(labelBatchParams, bitmap);
                    labelBatchParams.labelCount = 0;
                    labelBatchParams.parameterCount = 0;
                    labelBatchParams.scale = labelStyle->scale;
                    if (labelStyle->transform) {
                        float zoomScale = std::pow(2.0f, label->getTileId().zoom - _viewState.zoom);
                        cglib::vec2<float> translate = labelStyle->transform->translate() * zoomScale;
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
        }

        renderLabelBatch(labelBatchParams, bitmap);
    }
    
    void GLTileRenderer::setCompOp(CompOp compOp) {
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

    void GLTileRenderer::blendScreenTexture(float opacity, GLuint texture) {
        if (opacity <= 0) {
            return;
        }

        const ShaderProgram& shaderProgram = buildShaderProgram("blendscreen", blendVsh, blendFsh, LightingMode::NONE, RasterFilterMode::NONE, false, false, false);
        glUseProgram(shaderProgram.program);
        
        if (_screenQuad.vbo == 0) {
            createCompiledQuad(_screenQuad);
        }
        glBindBuffer(GL_ARRAY_BUFFER, _screenQuad.vbo);
        glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);
        
        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::identity();
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(shaderProgram.uniforms[U_TEXTURE], 0);
        Color color(opacity, opacity, opacity, opacity);
        glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
        glUniform2f(shaderProgram.uniforms[U_UVSCALE], 1.0f / _screenWidth, 1.0f / _screenHeight);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        checkGLError();
    }

    void GLTileRenderer::renderTileMask(const TileId& tileId) {
        for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            const ShaderProgram& shaderProgram = buildShaderProgram("tilemask", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, false, false, false);
            glUseProgram(shaderProgram.program);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            Color color(0, 0, 0, 0);
            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 0);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }
    
    void GLTileRenderer::renderTileBackground(const TileId& tileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileBackground>& background) {
        if (blend * opacity <= 0) {
            return;
        }
        if (!background->getPattern() && !background->getColor().value()) {
            return;
        }

        for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            const ShaderProgram& shaderProgram = buildShaderProgram("tilebackground", backgroundVsh, backgroundFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, background->getPattern() ? true : false, false, false);
            glUseProgram(shaderProgram.program);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);
            if (background->getPattern()) {
                glVertexAttribPointer(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
                glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            }
            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                } else {
                    glVertexAttrib3f(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            if (auto pattern = background->getPattern()) {
                const CompiledBitmap& compiledBitmap = buildCompiledBitmap(pattern->bitmap, true);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
                glUniform1i(shaderProgram.uniforms[U_PATTERN], 0);

                if (pattern->bitmap) {
                    glUniform2f(shaderProgram.uniforms[U_UVSCALE], tileSize / pattern->bitmap->width, tileSize / pattern->bitmap->height);
                }
            }

            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, background->getColor().rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], blend * opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            }
            if (background->getPattern()) {
                glBindTexture(GL_TEXTURE_2D, 0);

                glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            }
            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }

    void GLTileRenderer::renderTileBitmap(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, const std::shared_ptr<TileBitmap>& bitmap) {
        if (blend * opacity <= 0) {
            return;
        }
        if (_rasterFilterMode == RasterFilterMode::NONE) {
            return;
        }
        if (bitmap->getType() == TileBitmap::Type::NORMALMAP && !_lightingShaderNormalMap) {
            return;
        }

        for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTileSurfaces(targetTileId)) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            const ShaderProgram* shaderProgramPtr = nullptr;
            switch (bitmap->getType()) {
            case TileBitmap::Type::COLORMAP:
                shaderProgramPtr = &buildShaderProgram("tilecolormap", colormapVsh, colormapFsh, LightingMode::GEOMETRY2D, _rasterFilterMode, true, false, false);
                break;
            case TileBitmap::Type::NORMALMAP:
                shaderProgramPtr = &buildShaderProgram("tilenormalmap", normalmapVsh, normalmapFsh, LightingMode::NORMALMAP, _rasterFilterMode, true, false, false);
                break;
            default:
                return;
            }
            const ShaderProgram& shaderProgram = *shaderProgramPtr;
            glUseProgram(shaderProgram.program);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);
            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            if (bitmap->getType() == TileBitmap::Type::COLORMAP && _lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                } else {
                    glVertexAttrib3f(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
            } else if (bitmap->getType() == TileBitmap::Type::NORMALMAP && _lightingShaderNormalMap) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                } else {
                    glVertexAttrib3f(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                if (vertexGeomLayoutParams.binormalOffset >= 0) {
                    glVertexAttribPointer(shaderProgram.attribs[A_VERTEXBINORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.binormalOffset));
                    glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXBINORMAL]);
                } else {
                    glVertexAttrib3f(shaderProgram.attribs[A_VERTEXBINORMAL], 0, 1, 0);
                }
                _lightingShaderNormalMap->setupFunc(shaderProgram.program, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * cglib::translate4_matrix(_tileSurfaceBuilderOrigin));
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            const CompiledBitmap& compiledTileBitmap = buildCompiledTileBitmap(bitmap);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledTileBitmap.texture);
            glUniform1i(shaderProgram.uniforms[U_BITMAP], 0);
            glUniform4f(shaderProgram.uniforms[U_UVSCALE], bitmap->getWidth(), bitmap->getHeight(), 1.0f / bitmap->getWidth(), 1.0f / bitmap->getHeight());

            cglib::mat3x3<float> uvMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(sourceTileId)) * calculateTileMatrix2D(targetTileId));
            uvMatrix = cglib::mat3x3<float> { { 1.0, 0.0, 0.0 }, { 0.0, -1.0, 1.0 }, { 0.0, 0.0, 1.0 } } * uvMatrix;
            glUniformMatrix3fv(shaderProgram.uniforms[U_UVMATRIX], 1, GL_FALSE, uvMatrix.data());

            glUniform1f(shaderProgram.uniforms[U_OPACITY], blend * opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

            glBindTexture(GL_TEXTURE_2D, 0);

            if (bitmap->getType() == TileBitmap::Type::COLORMAP && _lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            } else if (bitmap->getType() == TileBitmap::Type::NORMALMAP && _lightingShaderNormalMap) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
                if (vertexGeomLayoutParams.binormalOffset >= 0) {
                    glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXBINORMAL]);
                }
            }
            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }

    void GLTileRenderer::renderTileGeometry(const TileId& sourceTileId, const TileId& targetTileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileGeometry>& geometry) {
        const TileGeometry::StyleParameters& styleParams = geometry->getStyleParameters();
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        
        if (blend * opacity <= 0) {
            return;
        }
        
        const ShaderProgram* shaderProgramPtr = nullptr;
        switch (geometry->getType()) {
        case TileGeometry::Type::POINT:
            shaderProgramPtr = &buildShaderProgram("point", pointVsh, pointFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, styleParams.pattern ? true : false, styleParams.translate ? true : false, false);
            break;
        case TileGeometry::Type::LINE:
            shaderProgramPtr = &buildShaderProgram("line", lineVsh, lineFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, styleParams.pattern ? true : false, styleParams.translate ? true : false, false);
            break;
        case TileGeometry::Type::POLYGON:
            shaderProgramPtr = &buildShaderProgram("polygon", polygonVsh, polygonFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, styleParams.pattern ? true : false, styleParams.translate ? true : false, false);
            break;
        case TileGeometry::Type::POLYGON3D:
            shaderProgramPtr = &buildShaderProgram("polygon3d", polygon3DVsh, polygon3DFsh, LightingMode::GEOMETRY3D, RasterFilterMode::NONE, styleParams.pattern ? true : false, styleParams.translate ? true : false, false);
            break;
        default:
            return;
        }
        const ShaderProgram& shaderProgram = *shaderProgramPtr;
        glUseProgram(shaderProgram.program);

        cglib::mat4x4<float> mvpMatrix = calculateTileMVPMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        
        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            cglib::mat4x4<float> transformMatrix = _transformer->calculateTileTransform(sourceTileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
            glUniformMatrix4fv(shaderProgram.uniforms[U_TRANSFORMMATRIX], 1, GL_FALSE, transformMatrix.data());
        }

        std::array<cglib::vec4<float>, TileGeometry::StyleParameters::MAX_PARAMETERS> colors;
        for (int i = 0; i < styleParams.parameterCount; i++) {
            Color color = Color::fromColorOpacity((styleParams.colorFuncs[i])(_viewState) * blend, opacity);
            colors[i] = color.rgba();
        }
        
        if (geometry->getType() == TileGeometry::Type::POINT) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths, strokeWidths;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                float width = std::max(0.0f, (styleParams.widthFuncs[i])(_viewState)) * geometry->getGeometryScale() / tileSize;
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
            
            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], vertexGeomLayoutParams.coordScale / vertexGeomLayoutParams.binormalScale / std::pow(2.0f, _viewState.zoom - sourceTileId.zoom));
            glUniform1f(shaderProgram.uniforms[U_SDFSCALE], GLYPH_RENDER_SIZE / _fullResolution / BITMAP_SDF_SCALE);
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, widths.data());
            glUniform1fv(shaderProgram.uniforms[U_STROKEWIDTHTABLE], styleParams.parameterCount, strokeWidths.data());
        } else if (geometry->getType() == TileGeometry::Type::LINE) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                // Check for 0-width function. This is used only for polygons.
                if (styleParams.widthFuncs[i] == FloatFunction(0)) {
                    widths[i] = -1;
                }
                else {
                    float width = 0.5f * _fullResolution * std::abs((styleParams.widthFuncs[i])(_viewState)) * geometry->getGeometryScale() / tileSize;
                    if (width < 1.0f) {
                        colors[i] = colors[i] * width; // should do gamma correction here, but simple implementation gives closer results to Mapnik
                        width = (width > 0 ? 1.0f : 0.0f); // normalize width
                    }
                    widths[i] = width * 0.5f;
                }
            }

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(styleParams.widthFuncs.begin(), styleParams.widthFuncs.begin() + styleParams.parameterCount, [](const FloatFunction& func) { return func != FloatFunction(0); })) { // check that all are proper lines, not polygons
                    return;
                }
            }

            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], vertexGeomLayoutParams.coordScale / (_halfResolution * vertexGeomLayoutParams.binormalScale * std::pow(2.0f, _viewState.zoom - sourceTileId.zoom)));
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, widths.data());

            if (styleParams.pattern) {
                std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> strokeScales;
                for (int i = 0; i < styleParams.parameterCount; i++) {
                    float strokeScale = (styleParams.strokeScales[i] > 0 ? STROKE_UV_SCALE / styleParams.pattern->bitmap->width / styleParams.strokeScales[i] / 127.0f / (_fullResolution / tileSize) : 0.0f);
                    strokeScales[i] = strokeScale * std::pow(2.0f, std::floor(_viewState.zoom) - _viewState.zoom);
                }
                glUniform1fv(shaderProgram.uniforms[U_STROKESCALETABLE], styleParams.parameterCount, strokeScales.data());
            }
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
            float tileHeightScale = static_cast<float>(cglib::length(cglib::transform_vector(cglib::vec3<double>(0, 0, 1), calculateTileMatrix(sourceTileId))));
            glUniform1f(shaderProgram.uniforms[U_UVSCALE], 1.0f / vertexGeomLayoutParams.texCoordScale);
            glUniform1f(shaderProgram.uniforms[U_HEIGHTSCALE], blend / vertexGeomLayoutParams.heightScale * vertexGeomLayoutParams.coordScale);
            glUniform1f(shaderProgram.uniforms[U_ABSHEIGHTSCALE], blend / vertexGeomLayoutParams.heightScale * POLYGON3D_HEIGHT_SCALE * tileHeightScale);
            cglib::mat3x3<float> tileMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(targetTileId)) * calculateTileMatrix2D(sourceTileId));
            if (styleParams.translate) {
                float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
                cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
                tileMatrix = tileMatrix * cglib::translate3_matrix(cglib::vec3<float>(translate(0), translate(1), 1));
            }
            glUniformMatrix3fv(shaderProgram.uniforms[U_TILEMATRIX], 1, GL_FALSE, tileMatrix.data());
        }

        if (std::all_of(colors.begin(), colors.begin() + styleParams.parameterCount, [](const cglib::vec4<float>& color) {
            return std::all_of(color.cbegin(), color.cend(), [](float val) { return val < 1.0f / 256.0f; });
        })) {
            return;
        }

        glUniform4fv(shaderProgram.uniforms[U_COLORTABLE], styleParams.parameterCount, colors[0].data());
        
        if (styleParams.pattern) {
            float zoomScale = std::pow(2.0f, std::floor(_viewState.zoom) - sourceTileId.zoom);
            float coordScale = 1.0f / (vertexGeomLayoutParams.texCoordScale * styleParams.pattern->widthScale);
            cglib::vec2<float> uvScale(coordScale, coordScale);
            if (geometry->getType() == TileGeometry::Type::LINE) {
                uvScale(0) *= zoomScale;
            } else if (geometry->getType() == TileGeometry::Type::POLYGON) {
                uvScale *= zoomScale;
            }
            glUniform2f(shaderProgram.uniforms[U_UVSCALE], uvScale(0), uvScale(1));

            const CompiledBitmap& compiledBitmap = buildCompiledBitmap(styleParams.pattern->bitmap, geometry->getType() != TileGeometry::Type::LINE);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glUniform1i(shaderProgram.uniforms[U_PATTERN], 0);
        }

        const CompiledGeometry& compiledGeometry = buildCompiledTileGeometry(geometry);
        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(compiledGeometry.geometryVAO);
        }
        if (compiledGeometry.geometryVAO == 0 || !compiledGeometry.geometryVAOInitialized) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);

            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                glVertexAttribPointer(shaderProgram.attribs[A_VERTEXATTRIBS], 4, GL_BYTE, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.attribsOffset));
                glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXATTRIBS]);
            }
            
            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                glVertexAttribPointer(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
                glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            }
            
            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glVertexAttribPointer(shaderProgram.attribs[A_VERTEXNORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                    glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            }

            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                glVertexAttribPointer(shaderProgram.attribs[A_VERTEXBINORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.binormalOffset));
                glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXBINORMAL]);
            }
            
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                glVertexAttribPointer(shaderProgram.attribs[A_VERTEXHEIGHT], 1, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.heightOffset));
                glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXHEIGHT]);
            }
        }

        if (!(vertexGeomLayoutParams.attribsOffset >= 0)) {
            glVertexAttrib4f(shaderProgram.attribs[A_VERTEXATTRIBS], 0, 0, 0, 0);
        }

        if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
            if (!(vertexGeomLayoutParams.normalOffset >= 0)) {
                glVertexAttrib3f(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
            }
        }

        if (geometry->getType() != TileGeometry::Type::POLYGON3D && _lightingShader2D) {
            _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D && _lightingShader3D) {
            _lightingShader3D->setupFunc(shaderProgram.program, _viewState);
        }

        glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);

        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(0);
        } else {
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXHEIGHT]);
            }
            
            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXBINORMAL]);
            }

            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            }
            
            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);
            }

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXATTRIBS]);
            }
            
            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);
        }

        if (compiledGeometry.geometryVAO == 0 || !compiledGeometry.geometryVAOInitialized) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            compiledGeometry.geometryVAOInitialized = compiledGeometry.geometryVAO != 0;
        }
        
        if (styleParams.pattern) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        checkGLError();
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

        bool useDerivatives = _glExtensions->GL_OES_standard_derivatives_supported();

        const CompiledBitmap& compiledBitmap = buildCompiledBitmap(bitmap, false);
        const ShaderProgram& shaderProgram = buildShaderProgram("labels", labelVsh, labelFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, false, false, useDerivatives);
        glUseProgram(shaderProgram.program);

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_viewState.projectionMatrix * labelBatchParams.labelMatrix);
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

        glUniform1f(shaderProgram.uniforms[U_SDFSCALE], GLYPH_RENDER_SIZE / labelBatchParams.scale / _fullResolution / BITMAP_SDF_SCALE);
        if (useDerivatives) {
            float scale = 1.0f / labelBatchParams.scale / _fullResolution / BITMAP_SDF_SCALE;
            glUniform2f(shaderProgram.uniforms[U_DERIVSCALE], bitmap->width * scale, bitmap->height * scale);
        }
        glUniform4fv(shaderProgram.uniforms[U_COLORTABLE], labelBatchParams.parameterCount, labelBatchParams.colorTable[0].data());
        glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], labelBatchParams.parameterCount, labelBatchParams.widthTable.data());
        glUniform1fv(shaderProgram.uniforms[U_STROKEWIDTHTABLE], labelBatchParams.parameterCount, labelBatchParams.strokeWidthTable.data());
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.verticesVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelVertices.size() * 3 * sizeof(float), _labelVertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

        if (_lightingShader2D) {
            glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.normalsVBO);
            glBufferData(GL_ARRAY_BUFFER, _labelNormals.size() * 3 * sizeof(float), _labelNormals.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);

            _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.texCoordsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelTexCoords.size() * 2 * sizeof(std::int16_t), _labelTexCoords.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);

        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.attribsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelAttribs.size() * 4 * sizeof(std::int8_t), _labelAttribs.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(shaderProgram.attribs[A_VERTEXATTRIBS], 4, GL_BYTE, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(shaderProgram.attribs[A_VERTEXATTRIBS]);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledLabelBatch.indicesVBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, _labelIndices.size() * sizeof(std::uint16_t), _labelIndices.data(), GL_DYNAMIC_DRAW);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
        glUniform1i(shaderProgram.uniforms[U_BITMAP], 0);
        glUniform2f(shaderProgram.uniforms[U_UVSCALE], 1.0f / bitmap->width, 1.0f / bitmap->height);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_labelIndices.size()), GL_UNSIGNED_SHORT, 0);

        glBindTexture(GL_TEXTURE_2D, 0);

        glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXATTRIBS]);
        
        glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXUV]);

        if (_lightingShader2D) {
            glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXNORMAL]);
        }
        
        glDisableVertexAttribArray(shaderProgram.attribs[A_VERTEXPOSITION]);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        _labelVertices.clear();
        _labelNormals.clear();
        _labelTexCoords.clear();
        _labelAttribs.clear();
        _labelIndices.clear();

        checkGLError();
    }

    const GLTileRenderer::CompiledBitmap& GLTileRenderer::buildCompiledBitmap(const std::shared_ptr<const Bitmap>& bitmap, bool genMipmaps) {
        auto it = _compiledBitmapMap.find(bitmap);
        if (it == _compiledBitmapMap.end()) {
            CompiledBitmap compiledBitmap;
            createCompiledBitmap(compiledBitmap);

            std::shared_ptr<const Bitmap> scaledBitmap = (genMipmaps ? BitmapManager::scaleToPOT(bitmap) : bitmap);
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (scaledBitmap) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scaledBitmap->width, scaledBitmap->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaledBitmap->data.data());
            }
            if (genMipmaps) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            it = _compiledBitmapMap.emplace(bitmap, compiledBitmap).first;
        }
        return it->second;
    }

    const GLTileRenderer::CompiledBitmap & GLTileRenderer::buildCompiledTileBitmap(const std::shared_ptr<TileBitmap>& tileBitmap) {
        auto it = _compiledTileBitmapMap.find(tileBitmap);
        if (it == _compiledTileBitmapMap.end()) {
            CompiledBitmap compiledTileBitmap;
            createCompiledBitmap(compiledTileBitmap);

            // Use a different strategy if the bitmap is not of POT dimensions, simply do not create the mipmaps
            bool genMipmaps = (tileBitmap->getWidth() & (tileBitmap->getWidth() - 1)) == 0 && (tileBitmap->getHeight() & (tileBitmap->getHeight() - 1)) == 0;
            glBindTexture(GL_TEXTURE_2D, compiledTileBitmap.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            GLenum format = GL_NONE;
            switch (tileBitmap->getFormat()) {
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
            glTexImage2D(GL_TEXTURE_2D, 0, format, tileBitmap->getWidth(), tileBitmap->getHeight(), 0, format, GL_UNSIGNED_BYTE, tileBitmap->getData().empty() ? NULL : tileBitmap->getData().data());
            if (genMipmaps) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            if (!_interactionMode) {
                tileBitmap->releaseBitmap(); // if interaction is enabled, keep the original bitmap
            }

            it = _compiledTileBitmapMap.emplace(tileBitmap, compiledTileBitmap).first;
        }
        return it->second;
    }

    const GLTileRenderer::CompiledGeometry& GLTileRenderer::buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry) {
        auto it = _compiledTileGeometryMap.find(tileGeometry);
        if (it == _compiledTileGeometryMap.end()) {
            CompiledGeometry compiledGeometry;
            createCompiledGeometry(compiledGeometry);

            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);
            glBufferData(GL_ARRAY_BUFFER, tileGeometry->getVertexGeometry().size() * sizeof(std::uint8_t), tileGeometry->getVertexGeometry().data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileGeometry->getIndices().size() * sizeof(std::uint16_t), tileGeometry->getIndices().data(), GL_STATIC_DRAW);

            if (!_interactionMode) {
                tileGeometry->releaseVertexArrays(); // if interaction is enabled, we must keep the vertex arrays. Otherwise optimize for lower memory usage
            }

            it = _compiledTileGeometryMap.emplace(tileGeometry, compiledGeometry).first;
        }
        return it->second;
    }

    const GLTileRenderer::ShaderProgram& GLTileRenderer::buildShaderProgram(const std::string& id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, bool pattern, bool translate, bool derivs) {
        std::string shaderProgramId = id + (pattern ? "_p" : "") + (translate ? "_t" : "") + (derivs ? "_d" : "");
        if (lightingMode != LightingMode::NONE) {
            shaderProgramId += "_l" + std::to_string(static_cast<int>(lightingMode));
        }
        if (filterMode != RasterFilterMode::NONE) {
            shaderProgramId += "_f" + std::to_string(static_cast<int>(filterMode));
        }

        auto it = _shaderProgramMap.find(shaderProgramId);
        if (it == _shaderProgramMap.end()) {
            std::set<std::string> defs;
            if (pattern) {
                defs.insert("PATTERN");
            }
            if (translate) {
                defs.insert("TRANSFORM");
            }
            if (derivs) {
                defs.insert("DERIVATIVES");
            }

            std::string lightingVsh;
            std::string lightingFsh;
            std::string filterFsh;
            if (lightingMode == LightingMode::GEOMETRY2D && _lightingShader2D) {
                defs.insert(_lightingShader2D->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                if (_lightingShader2D->perVertex) {
                    lightingVsh = _lightingShader2D->shader;
                } else {
                    lightingFsh = _lightingShader2D->shader;
                }
            }
            else if (lightingMode == LightingMode::GEOMETRY3D && _lightingShader3D) {
                defs.insert(_lightingShader3D->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                if (_lightingShader3D->perVertex) {
                    lightingVsh = _lightingShader3D->shader;
                } else {
                    lightingFsh = _lightingShader3D->shader;
                }
            }
            else if (lightingMode == LightingMode::NORMALMAP && _lightingShaderNormalMap) {
                defs.insert(_lightingShaderNormalMap->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                if (_lightingShaderNormalMap->perVertex) {
                    lightingVsh = _lightingShaderNormalMap->shader;
                } else {
                    lightingFsh = _lightingShaderNormalMap->shader;
                }
            }
            if (filterMode == RasterFilterMode::NEAREST) {
                defs.insert("FILTER_NEAREST");
                filterFsh = textureFiltersFsh;
            }
            else if (filterMode == RasterFilterMode::BILINEAR) {
                defs.insert("FILTER_BILINEAR");
                filterFsh = textureFiltersFsh;
            }
            else if (filterMode == RasterFilterMode::BICUBIC) {
                defs.insert("FILTER_BICUBIC");
                filterFsh = textureFiltersFsh;
            }

            ShaderProgram shaderProgram;
            createShaderProgram(shaderProgram, commonVsh + lightingVsh + vsh, commonFsh + lightingFsh + filterFsh + fsh, defs, uniformMap, attribMap);
            
            it = _shaderProgramMap.emplace(shaderProgramId, shaderProgram).first;
        }
        return it->second;
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

    void GLTileRenderer::createShaderProgram(ShaderProgram& shaderProgram, const std::string& vsh, const std::string& fsh, const std::set<std::string>& defs, const std::map<std::string, int>& uniformMap, const std::map<std::string, int>& attribMap) {
        auto compileShader = [&defs](GLenum type, const std::string& sh) -> GLuint {
            std::string shaderSourceStr = "#version 100\n";
            for (const std::string& def : defs) {
                shaderSourceStr += "#define " + def + "\n";
            }
            shaderSourceStr += sh;

            GLuint shader = glCreateShader(type);
            const char* shaderSource = shaderSourceStr.c_str();
            glShaderSource(shader, 1, const_cast<const char**>(&shaderSource), NULL);
            glCompileShader(shader);
            GLint isShaderCompiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &isShaderCompiled);
            if (!isShaderCompiled) {
                GLint infoLogLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
                std::vector<char> infoLog(infoLogLength + 1);
                GLsizei charactersWritten = 0;
                glGetShaderInfoLog(shader, infoLogLength, &charactersWritten, infoLog.data());
                std::string msg(infoLog.begin(), infoLog.begin() + charactersWritten);
                glDeleteShader(shader);
                throw std::runtime_error("Shader compiling failed: " + msg);
            }
            return shader;
        };

        GLuint vertexShader = 0;
        GLuint fragmentShader = 0;
        GLuint program = 0;
        try {
            vertexShader = compileShader(GL_VERTEX_SHADER, vsh);
            fragmentShader = compileShader(GL_FRAGMENT_SHADER, fsh);

            program = glCreateProgram();
            glAttachShader(program, fragmentShader);
            glAttachShader(program, vertexShader);
            glLinkProgram(program);
            GLint isLinked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
            if (!isLinked) {
                GLint infoLogLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
                std::vector<char> infoLog(infoLogLength + 1);
                GLsizei charactersWritten = 0;
                glGetProgramInfoLog(program, infoLogLength, &charactersWritten, infoLog.data());
                std::string msg(infoLog.begin(), infoLog.begin() + charactersWritten);
                throw std::runtime_error("Shader program linking failed: " + msg);
            }
        }
        catch (const std::exception&) {
            if (program != 0) {
                glDeleteProgram(program);
            }
            if (vertexShader != 0) {
                glDeleteShader(vertexShader);
            }
            if (fragmentShader != 0) {
                glDeleteShader(fragmentShader);
            }
            throw;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        shaderProgram.program = program;

        shaderProgram.uniforms.resize(std::accumulate(uniformMap.begin(), uniformMap.end(), 0, [](int prev, const std::pair<std::string, int>& item) { return std::max(prev, 1 + item.second); }));
        for (auto it = uniformMap.begin(); it != uniformMap.end(); it++) {
            shaderProgram.uniforms[it->second] = glGetUniformLocation(program, it->first.c_str());;
        }

        shaderProgram.attribs.resize(std::accumulate(attribMap.begin(), attribMap.end(), 0, [](int prev, const std::pair<std::string, int>& item) { return std::max(prev, 1 + item.second); }));
        for (auto it = attribMap.begin(); it != attribMap.end(); it++) {
            shaderProgram.attribs[it->second] = glGetAttribLocation(program, it->first.c_str());;
        }
    }

    void GLTileRenderer::deleteShaderProgram(ShaderProgram& shaderProgram) {
        if (shaderProgram.program != 0) {
            glDeleteProgram(shaderProgram.program);
            shaderProgram.program = 0;
            shaderProgram.uniforms.clear();
            shaderProgram.attribs.clear();
        }
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

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("FrameBuffer not complete: status code " + std::to_string(status));
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
