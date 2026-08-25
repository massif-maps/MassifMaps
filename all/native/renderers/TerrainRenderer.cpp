#include "TerrainRenderer.h"
#include "components/Options.h"
#include "components/TerrainOptions.h"
#include "datasources/TileDataSource.h"
#include "graphics/Bitmap.h"
#include "renderers/utils/FrameBuffer.h"
#include "renderers/utils/GLContext.h"
#include "renderers/utils/FogShader.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/Shader.h"
#include "renderers/utils/TerrainDepthWorker.h"
#include "renderers/utils/Texture.h"
#include "terrain/ElevationManager.h"
#include "terrain/ElevationTileGrid.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <algorithm>
#include <limits>
#include <cmath>

namespace massif {

    struct TerrainRenderer::TileMesh {
        std::vector<float> vertices; // x, y in tile coordinates [0..1], z in tile-local units
        std::vector<unsigned short> indices;
        // Surface pass only, filled on first use: nx, ny, nz, elevation in metres per vertex.
        std::vector<float> surfaceAttribs;
        int gridSize = 0;
    };

    struct TerrainRenderer::MeshCacheEntry {
        std::shared_ptr<ElevationTileGrid> grid; // the grid the mesh was built from
        float exaggeration = 1.0f;
        int gridSize = 0;
        std::shared_ptr<TileMesh> mesh;
        unsigned int lastUsed = 0; // _meshCacheClock value of the last pass that drew this mesh
    };

    TerrainRenderer::TerrainRenderer() :
        _frameBuffer(),
        _shader(),
        _meshCache()
    {
    }

    TerrainRenderer::~TerrainRenderer() {
    }

    bool TerrainRenderer::renderDepthPrepass(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        // Depth-only pass into the current framebuffer: this is the single source of truth
        // that 2D draped geometry depth-tests against (with a bias towards the viewer).
        // Slope-scaled polygon offset pushes the pre-pass depth slightly away from the
        // viewer: the pre-pass mesh and the draped tile meshes are different tesselations
        // of the same height field, and near the camera (steep, glancing surfaces) their
        // difference exceeds any practical constant clip-space bias.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
        // Keep the factor moderate: it scales with the per-pixel depth slope, which gets
        // large at ridge silhouettes - too much offset lets geometry behind ridges
        // 'shine through' in a band along every silhouette.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);

        bool result = false;
        if (!_shader || !_shader->isValid()) {
            _shader = glResourceManager->create<Shader>("terraindepth", TERRAIN_DEPTH_VERTEX_SHADER, TERRAIN_DEPTH_FRAGMENT_SHADER);
        }
        if (_shader) {
            result = renderTiles(viewState, terrainOptions, glResourceManager, _shader);
        }

        // Restore state expected by the layer renderers
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        GLContext::CheckGLError("TerrainRenderer::renderDepthPrepass");
        return result;
    }

    bool TerrainRenderer::renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const Color& color, bool keepDepth) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        if (!_colorShader || !_colorShader->isValid()) {
            _colorShader = glResourceManager->create<Shader>("terraincolor", TERRAIN_DEPTH_VERTEX_SHADER, TERRAIN_COLOR_FRAGMENT_SHADER);
        }
        if (!_colorShader) {
            return false;
        }

        // Opaque terrain base fill. Depth is used DURING the pass so that near slopes
        // win over far slopes; with keepDepth it also subsumes the depth pre-pass. The
        // slope-scaled depth push keeps draped tile content (built from different
        // tesselations of the same height field) in front of the kept depth.
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);

        glUseProgram(_colorShader->getProgId());
        glUniform4f(_colorShader->getUniformLoc("u_color"), color.getR() / 255.0f, color.getG() / 255.0f, color.getB() / 255.0f, color.getA() / 255.0f);

        bool result = renderTiles(viewState, terrainOptions, glResourceManager, _colorShader);

        // Color-only mode: the tile layer surface pre-passes provide the terrain depth
        // with their own (differently tesselated) meshes - this fill's depth must not
        // survive, or it would depth-clip the tile content in triangle-shaped patches
        // wherever the meshes disagree.
        if (!keepDepth) {
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // Restore state expected by the layer renderers
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        GLContext::CheckGLError("TerrainRenderer::renderBackground");
        return result;
    }

    bool TerrainRenderer::renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Bitmap>& bitmap, bool keepDepth) {
        if (!terrainOptions || !glResourceManager || !bitmap || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        if (!_bitmapShader || !_bitmapShader->isValid()) {
            _bitmapShader = glResourceManager->create<Shader>("terrainbitmap", TERRAIN_BITMAP_VERTEX_SHADER, TERRAIN_BITMAP_FRAGMENT_SHADER);
        }
        if (!_bitmapShader) {
            return false;
        }
        if (_backgroundBitmap != bitmap || !_backgroundTex || !_backgroundTex->isValid()) {
            _backgroundTex = glResourceManager->create<Texture>(bitmap, true, true);
            _backgroundBitmap = bitmap;
        }
        if (!_backgroundTex) {
            return false;
        }

        // Opaque terrain base fill from the repeating background bitmap, color AND depth
        // (the bitmap variant of the color fill; same slope-scaled depth push).
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);

        glUseProgram(_bitmapShader->getProgId());
        glUniform1i(_bitmapShader->getUniformLoc("u_tex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _backgroundTex->getTexId());

        // World-anchored repeating pattern, matching the flat-map BackgroundRenderer:
        // the bitmap repeats once per map tile of the current (integer) zoom level.
        // The per-tile uv transform is reduced modulo 1 in double precision on the CPU,
        // so the shader only ever interpolates small uv values (no precision jitter).
        double uvWorldScale = static_cast<double>(1 << static_cast<int>(viewState.getZoom())) / Const::WORLD_SIZE;
        GLuint uUVOffsetScale = _bitmapShader->getUniformLoc("u_uvOffsetScale");
        auto tileUniformsFn = [&](const MapTile& tile) {
            int tileMask = (1 << tile.getZoom()) - 1;
            double zoomScale = 1.0 / (1 << tile.getZoom());
            double originX = (tile.getX() * zoomScale - 0.5) * Const::WORLD_SIZE;
            double originY = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
            double size = zoomScale * Const::WORLD_SIZE;
            double offsetS = originX * uvWorldScale;
            double offsetT = originY * uvWorldScale;
            offsetS -= std::floor(offsetS);
            offsetT -= std::floor(offsetT);
            glUniform4f(uUVOffsetScale, static_cast<float>(offsetS), static_cast<float>(offsetT), static_cast<float>(size * uvWorldScale), static_cast<float>(size * uvWorldScale));
        };

        bool result = renderTiles(viewState, terrainOptions, glResourceManager, _bitmapShader, tileUniformsFn);

        // Color-only mode: see the color overload - the fill depth must not survive
        // when the tile layer pre-passes provide the terrain depth.
        if (!keepDepth) {
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // Restore state expected by the layer renderers
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        GLContext::CheckGLError("TerrainRenderer::renderBackground(bitmap)");
        return result;
    }

    bool TerrainRenderer::renderSurface(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const ResolvedLighting& lighting, const ResolvedFog& fog, bool keepDepth) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        std::string shaderSource = terrainOptions->getSurfaceShaderSource();
        if (shaderSource.empty()) {
            return false;
        }
        std::shared_ptr<Shader> shader = updateSurfaceShader(shaderSource, fog.shaderSource, glResourceManager);
        if (!shader) {
            return false;
        }

        // The shaded variant of the terrain base fill: same opaque, depth-resolved pass as the
        // color/bitmap background (see renderBackground for why the depth is pushed and why it
        // is discarded again unless this pass IS the terrain depth source).
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);

        GLuint progId = shader->getProgId();
        glUseProgram(progId);

        GLint loc = -1;
        if ((loc = glGetUniformLocation(progId, "u_metersPerUnit")) >= 0) {
            glUniform1f(loc, static_cast<float>(Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE));
        }
        if ((loc = glGetUniformLocation(progId, "u_sunDir")) >= 0) {
            glUniform3f(loc, lighting.sunDir(0), lighting.sunDir(1), lighting.sunDir(2));
        }
        if ((loc = glGetUniformLocation(progId, "u_sunColor")) >= 0) {
            glUniform4f(loc, lighting.sunColor.getR() / 255.0f, lighting.sunColor.getG() / 255.0f, lighting.sunColor.getB() / 255.0f, lighting.sunColor.getA() / 255.0f);
        }
        if ((loc = glGetUniformLocation(progId, "u_sunIntensity")) >= 0) {
            glUniform1f(loc, lighting.sunIntensity);
        }
        if ((loc = glGetUniformLocation(progId, "u_ambientIntensity")) >= 0) {
            glUniform1f(loc, lighting.ambientIntensity);
        }
        FogShader::setUniforms(progId, fog.active() ? fog : ResolvedFog(), viewState);
        if ((loc = glGetUniformLocation(progId, "u_time")) >= 0) {
            glUniform1f(loc, std::chrono::duration_cast<std::chrono::duration<float> >(std::chrono::steady_clock::now() - _startTime).count());
        }
        if ((loc = glGetUniformLocation(progId, "u_zoom")) >= 0) {
            glUniform1f(loc, viewState.getZoom());
        }
        if ((loc = glGetUniformLocation(progId, "u_resolution")) >= 0) {
            glUniform2f(loc, static_cast<float>(viewState.getWidth()), static_cast<float>(viewState.getHeight()));
        }
        for (const auto& param : terrainOptions->getSurfaceParameters()) {
            if ((loc = glGetUniformLocation(progId, param.first.c_str())) >= 0) {
                glUniform1f(loc, param.second);
            }
        }
        for (const auto& param : terrainOptions->getSurfaceColorParameters()) {
            if ((loc = glGetUniformLocation(progId, param.first.c_str())) >= 0) {
                glUniform4f(loc, param.second.getR() / 255.0f, param.second.getG() / 255.0f, param.second.getB() / 255.0f, param.second.getA() / 255.0f);
            }
        }

        GLint uTileMat = glGetUniformLocation(progId, "u_tileMat");
        auto tileUniformsFn = [&](const MapTile& tile) {
            if (uTileMat >= 0) {
                cglib::mat4x4<float> tileMat = cglib::mat4x4<float>::convert(calculateTileMatrix(tile));
                glUniformMatrix4fv(uTileMat, 1, GL_FALSE, tileMat.data());
            }
        };

        bool result = renderTiles(viewState, terrainOptions, glResourceManager, shader, tileUniformsFn, 0, true);

        // Color-only mode: see renderBackground - the fill depth must not survive when the tile
        // layer pre-passes provide the terrain depth with their own tesselation.
        if (!keepDepth) {
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // Restore state expected by the layer renderers
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        GLContext::CheckGLError("TerrainRenderer::renderSurface");
        return result;
    }

    std::shared_ptr<Shader> TerrainRenderer::updateSurfaceShader(const std::string& shaderSource, const std::string& fogShaderSource, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        bool same = _surfaceShaderSource == shaderSource && _fogShaderSource == fogShaderSource;
        if (_surfaceShader && _surfaceShader->isValid() && same) {
            return _surfaceShader;
        }
        if (_surfaceShaderFailed && same) {
            return std::shared_ptr<Shader>();
        }

        _surfaceShaderSource = shaderSource;
        _fogShaderSource = fogShaderSource;
        _surfaceShaderFailed = false;
        std::shared_ptr<Shader> shader = glResourceManager->create<Shader>("terrainsurface", TERRAIN_SURFACE_VERTEX_SHADER,
                                                                           TERRAIN_SURFACE_FRAGMENT_SHADER_PREFIX + FogShader::buildBlock(fogShaderSource) + shaderSource + TERRAIN_SURFACE_FRAGMENT_SHADER_MAIN);
        if (!shader || shader->getProgId() == 0) {
            Log::Error("TerrainRenderer::updateSurfaceShader: Terrain surface shader failed to compile, falling back to the background bitmap/color");
            _surfaceShaderFailed = true;
            _surfaceShader.reset();
            return std::shared_ptr<Shader>();
        }
        _surfaceShader = shader;
        return _surfaceShader;
    }

    bool TerrainRenderer::renderDepthTexture(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, int meshResolutionCap) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        int bufferWidth = std::max(1, viewState.getWidth() / BUFFER_DOWNSCALE);
        int bufferHeight = std::max(1, viewState.getHeight() / BUFFER_DOWNSCALE);
        if (!_frameBuffer || !_frameBuffer->isValid() || _frameBuffer->getWidth() != bufferWidth || _frameBuffer->getHeight() != bufferHeight) {
            _frameBuffer = glResourceManager->create<FrameBuffer>(bufferWidth, bufferHeight, true, true, false);
            _depthTextureMVPMatrix = cglib::mat4x4<double>::zero();
        }
        if (!_frameBuffer) {
            return false;
        }

        // The texture is still there from the last frame: with the camera and the elevation
        // unchanged it is still the answer. This pass draws the terrain from CPU meshes at the
        // full mesh resolution and was the largest single item in a peak-finder frame (9.5 ms of
        // 19.3 on an Adreno 610), all of it repeated for a map that is standing still.
        unsigned int elevationVersion = (terrainOptions->getElevationManager() ? terrainOptions->getElevationManager()->getVersion() : 0);
        if (_depthTextureMVPMatrix == viewState.getModelviewProjectionMat() && _depthTextureElevationVersion == elevationVersion && _depthTextureMeshResolutionCap == meshResolutionCap) {
            return true;
        }
        _depthTextureMVPMatrix = viewState.getModelviewProjectionMat();
        _depthTextureElevationVersion = elevationVersion;
        _depthTextureMeshResolutionCap = meshResolutionCap;

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer->getFBOId());
        glViewport(0, 0, bufferWidth, bufferHeight);

        // Clear to 'sky': maximum depth, zero coverage
        glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests

        bool result = false;
        if (!_shader || !_shader->isValid()) {
            _shader = glResourceManager->create<Shader>("terraindepth", TERRAIN_DEPTH_VERTEX_SHADER, TERRAIN_DEPTH_FRAGMENT_SHADER);
        }
        if (_shader) {
            result = renderTiles(viewState, terrainOptions, glResourceManager, _shader, std::function<void(const MapTile&)>(), meshResolutionCap);
        }

        // Restore state
        glEnable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(0, 0, viewState.getWidth(), viewState.getHeight());
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TerrainRenderer::renderDepthTexture");
        return result;
    }

    unsigned int TerrainRenderer::getDepthTextureId() const {
        return _frameBuffer && _frameBuffer->isValid() ? _frameBuffer->getColorTexId() : 0;
    }

    bool TerrainRenderer::updateDepthBuffer(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        if (!terrainOptions || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }
        // The worker only reports itself unusable once its thread has tried to create the
        // context, so the choice is made per frame rather than once.
        if (TerrainDepthWorker::isSupported() && (!_depthWorker || _depthWorker->isUsable())) {
            return updateDepthBufferAsync(viewState, terrainOptions);
        }
        return updateDepthBufferSync(viewState, terrainOptions, glResourceManager);
    }

    bool TerrainRenderer::updateDepthBufferAsync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions) {
        if (!_depthWorker) {
            _depthWorker = std::make_unique<TerrainDepthWorker>(TERRAIN_DEPTH_VERTEX_SHADER, TERRAIN_DEPTH_FRAGMENT_SHADER);
        }

        // Whatever the worker finished since the last frame becomes the data the label
        // placement reads from now on.
        if (std::shared_ptr<const TerrainDepthBuffer> result = _depthWorker->takeResult()) {
            // One line, once: whether the occlusion depth comes from the worker or from the
            // synchronous fallback is otherwise invisible in a log.
            static bool firstResultLogged = false;
            if (!firstResultLogged) {
                firstResultLogged = true;
                Log::Infof("TerrainRenderer: terrain occlusion depth read back off the render thread (%d x %d)", result->width, result->height);
            }
            std::lock_guard<std::mutex> lock(_depthMutex);
            _depthDataSnapshot = std::move(result);
        }

        unsigned int elevationVersion = (terrainOptions->getElevationManager() ? terrainOptions->getElevationManager()->getVersion() : 0);
        const cglib::mat4x4<double>& mvpMatrix = viewState.getModelviewProjectionMat();
        int bufferWidth = std::max(1, viewState.getWidth() / BUFFER_DOWNSCALE);
        int bufferHeight = std::max(1, viewState.getHeight() / BUFFER_DOWNSCALE);
        if (_depthMVPMatrix == mvpMatrix && _depthElevationVersion == elevationVersion) {
            // The data in flight (or already published) is for this exact camera. Keep asking
            // for frames only while it has not landed yet.
            _depthStale = _depthWorker->isBusy();
            return true;
        }
        if (_depthWorker->isBusy()) {
            _depthStale = true; // a newer camera, but the worker is still on the previous one
            return true;
        }

        // The worker renders on a second GL context, which the driver has to interleave with the
        // render context - submitting on every camera change makes that contention the new cost.
        // While the camera moves the occlusion depth is allowed to lag (billboards fade), so
        // refresh at an interval; the frame the camera comes to rest on refreshes immediately.
        auto now = std::chrono::steady_clock::now();
        bool moving = (_depthLastSeenMVPMatrix != mvpMatrix);
        _depthLastSeenMVPMatrix = mvpMatrix;
        if (moving && now - _depthReadbackTime < std::chrono::milliseconds(TerrainDepthWorker::getMovingSubmitInterval(DEPTH_SUBMIT_MOVING_INTERVAL))) {
            _depthStale = true;
            return true;
        }
        _depthReadbackTime = now;

        // Collecting the meshes is all the render thread pays for: no GL calls, no read-back.
        std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > > tileMeshes;
        collectTileMeshes(viewState, terrainOptions, DEPTH_TEXTURE_MESH_RESOLUTION, tileMeshes);

        TerrainDepthWorker::Job job;
        job.width = bufferWidth;
        job.height = bufferHeight;
        job.far = viewState.getFar();
        job.mvpMatrix = mvpMatrix;
        job.items.reserve(tileMeshes.size());
        for (const auto& tileMesh : tileMeshes) {
            const std::shared_ptr<TileMesh>& mesh = tileMesh.second;
            if (!mesh || mesh->indices.empty()) {
                continue;
            }
            TerrainDepthWorker::DrawItem item;
            item.mvpMat = cglib::mat4x4<float>::convert(mvpMatrix * calculateTileMatrix(tileMesh.first));
            item.owner = mesh; // the worker draws straight out of the mesh, so it must outlive the job
            item.vertices = mesh->vertices.data();
            item.indices = mesh->indices.data();
            item.indexCount = mesh->indices.size();
            job.items.push_back(std::move(item));
        }

        if (!_depthWorker->submit(std::move(job))) {
            _depthStale = true;
            return true;
        }
        _depthMVPMatrix = mvpMatrix;
        _depthElevationVersion = elevationVersion;
        _depthStale = true; // the result lands in a later frame; keep rendering until it does
        return true;
    }

    bool TerrainRenderer::updateDepthBufferSync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        int bufferWidth = std::max(1, viewState.getWidth() / BUFFER_DOWNSCALE);
        int bufferHeight = std::max(1, viewState.getHeight() / BUFFER_DOWNSCALE);

        // The render + read-back only needs to happen when the camera or the elevation
        // data changed - on static frames (the common case) this is free. While the camera
        // is moving, read-backs are additionally throttled: a slightly stale occlusion
        // depth during motion is invisible (labels fade in/out anyway), while a
        // glReadPixels stall every frame is not.
        unsigned int elevationVersion = (terrainOptions && terrainOptions->getElevationManager() ? terrainOptions->getElevationManager()->getVersion() : 0);
        const cglib::mat4x4<double>& mvpMatrix = viewState.getModelviewProjectionMat();
        std::shared_ptr<const TerrainDepthBuffer> depthData;
        {
            std::lock_guard<std::mutex> lock(_depthMutex);
            depthData = _depthDataSnapshot;
        }
        bool unchanged = (depthData && depthData->width == bufferWidth && depthData->height == bufferHeight &&
            _depthMVPMatrix == mvpMatrix && _depthElevationVersion == elevationVersion);
        if (unchanged) {
            _depthStale = false;
            _depthLastSeenMVPMatrix = mvpMatrix;
            return true;
        }
        auto now = std::chrono::steady_clock::now();
        // Is the camera still moving? The read-back stalls the pipeline, so during a gesture
        // it runs at a coarse interval only and the exact refresh waits for the camera to come
        // to rest - the frame after the one that moved last.
        bool moving = (_depthLastSeenMVPMatrix != mvpMatrix);
        _depthLastSeenMVPMatrix = mvpMatrix;
        bool haveData = (depthData && depthData->width == bufferWidth && depthData->height == bufferHeight);
        int throttle = (moving ? DEPTH_READBACK_MOVING_INTERVAL : DEPTH_READBACK_THROTTLE);
        if (haveData && now - _depthReadbackTime < std::chrono::milliseconds(throttle)) {
            _depthStale = true; // keep the previous (stale) depth data, refresh on a later frame
            return true;
        }
        _depthReadbackTime = now;
        _depthStale = false;

        if (!renderDepthTexture(viewState, terrainOptions, glResourceManager)) {
            return false;
        }
        auto newDepthData = std::make_shared<TerrainDepthBuffer>();
        newDepthData->data.resize(static_cast<std::size_t>(bufferWidth) * bufferHeight * 4);

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer->getFBOId());
        glReadPixels(0, 0, bufferWidth, bufferHeight, GL_RGBA, GL_UNSIGNED_BYTE, newDepthData->data.data());
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

        newDepthData->width = bufferWidth;
        newDepthData->height = bufferHeight;
        newDepthData->far = viewState.getFar();
        newDepthData->mvpMatrix = mvpMatrix;
        {
            std::lock_guard<std::mutex> lock(_depthMutex);
            _depthDataSnapshot = std::move(newDepthData);
        }
        _depthMVPMatrix = viewState.getModelviewProjectionMat();
        _depthElevationVersion = elevationVersion;
        GLContext::CheckGLError("TerrainRenderer::updateDepthBuffer");
        return true;
    }

    float TerrainRenderer::sampleDepthW(const TerrainDepthBuffer& depthData, int x, int y) {
        if (x < 0 || y < 0 || x >= depthData.width || y >= depthData.height) {
            return std::numeric_limits<float>::max();
        }
        // The framebuffer rows start at the bottom of the screen; screen y grows downwards
        const std::uint8_t* ptr = &depthData.data[(static_cast<std::size_t>(depthData.height - 1 - y) * depthData.width + x) * 4];
        if (ptr[3] == 0) {
            return std::numeric_limits<float>::max(); // sky pixel (zero coverage)
        }
        float depth = ptr[0] / 255.0f + ptr[1] / 65025.0f + ptr[2] / 16581375.0f;
        return depth * depthData.far;
    }

    bool TerrainRenderer::isOccludedByTerrain(const cglib::vec3<double>& pos, float tolerance) const {
        std::shared_ptr<const TerrainDepthBuffer> depthData;
        {
            std::lock_guard<std::mutex> lock(_depthMutex);
            depthData = _depthDataSnapshot;
        }
        if (!depthData || depthData->width < 1 || depthData->height < 1 || depthData->mvpMatrix == cglib::mat4x4<double>::zero()) {
            return false;
        }

        cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(pos(0), pos(1), pos(2), 1), depthData->mvpMatrix);
        if (clipPos(3) <= 0) {
            return false;
        }
        int x = static_cast<int>((clipPos(0) / clipPos(3) * 0.5 + 0.5) * depthData->width);
        int y = static_cast<int>((0.5 - clipPos(1) / clipPos(3) * 0.5) * depthData->height);
        float depthW = sampleDepthW(*depthData, x, y);
        if (depthW == std::numeric_limits<float>::max()) {
            return false; // sky, or moved outside what this buffer covers
        }
        // Farthest terrain depth around the position rather than the depth of its own pixel:
        // labels drawn on the ground sit exactly ON the terrain, the depth buffer is read back
        // downscaled, and on a slope the neighbouring pixel can be a good deal nearer - so an
        // exact comparison makes a label's own ground occlude it, differently on every frame,
        // which is what made labels blink while panning.
        for (int i = 0; i < 4; i++) {
            int dx = (i & 1 ? OCCLUSION_SAMPLE_OFFSET : -OCCLUSION_SAMPLE_OFFSET);
            int dy = (i & 2 ? OCCLUSION_SAMPLE_OFFSET : -OCCLUSION_SAMPLE_OFFSET);
            float neighbourDepthW = sampleDepthW(*depthData, x + dx, y + dy);
            if (neighbourDepthW < std::numeric_limits<float>::max()) {
                depthW = std::max(depthW, neighbourDepthW);
            }
        }
        return static_cast<float>(clipPos(3)) > depthW * tolerance;
    }

    void TerrainRenderer::collectVisibleTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, std::vector<MapTile>& tiles) const {
        if (!terrainOptions) {
            return;
        }
        if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
            calculateVisibleTiles(viewState, elevationManager, MapTile(0, 0, 0, 0), tiles);
        }
    }

    void TerrainRenderer::evictLeastRecentlyUsedMeshes(unsigned int pass) {
        // Evict the least-recently-used entries, NOT the whole cache - same reasoning as
        // ElevationTextureCache::evictLeastRecentlyUsed. A full flush rebuilt every mesh of every
        // pass whenever the working set crossed the cap, which is exactly what a multi-level zoom
        // out does. Meshes already drawn in this pass are never victims: dropping one would give
        // the tile a flat mesh for the rest of the frame.
        while (_meshCache.size() >= MAX_CACHED_MESHES) {
            auto lru = _meshCache.end();
            for (auto entryIt = _meshCache.begin(); entryIt != _meshCache.end(); entryIt++) {
                if (entryIt->second.lastUsed >= pass) {
                    continue;
                }
                if (lru == _meshCache.end() || entryIt->second.lastUsed < lru->second.lastUsed) {
                    lru = entryIt;
                }
            }
            if (lru == _meshCache.end()) {
                break; // every entry belongs to this pass: let the cache exceed the cap for one pass
            }
            _meshCache.erase(lru);
        }
    }

    void TerrainRenderer::collectTileMeshes(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, int meshResolutionCap, std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > >& tileMeshes) {
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();

        // Calculate visible terrain tiles
        std::vector<MapTile> tiles;
        calculateVisibleTiles(viewState, elevationManager, MapTile(0, 0, 0, 0), tiles);

        float exaggeration = elevationManager->getExaggeration();
        int minZoom = terrainOptions->getMinZoom();
        int meshResolution = terrainOptions->getMeshResolution();
        if (meshResolutionCap > 0) {
            meshResolution = std::min(meshResolution, meshResolutionCap);
        }

        unsigned int pass = ++_meshCacheClock;

        tileMeshes.reserve(tiles.size());
        for (const MapTile& tile : tiles) {
            long long tileId = tile.getTileId();
            std::shared_ptr<ElevationTileGrid> grid;
            if (tile.getZoom() >= minZoom) {
                grid = elevationManager->getTileGrid(tile, ElevationManager::LoadMode::CACHED_ONLY);
            }
            int gridSize = calculateMeshGridSize(tile, grid, meshResolution);

            // Rebuild the mesh only when its inputs actually changed. This avoids rebuilding
            // every cached mesh each time a new elevation tile arrives during loading.
            auto it = _meshCache.find(std::make_pair(tileId, gridSize));
            if (it == _meshCache.end() || it->second.grid != grid || it->second.exaggeration != exaggeration || it->second.gridSize != gridSize) {
                if (it == _meshCache.end() && _meshCache.size() >= MAX_CACHED_MESHES) {
                    evictLeastRecentlyUsedMeshes(pass);
                }
                MeshCacheEntry entry;
                entry.grid = grid;
                entry.exaggeration = exaggeration;
                entry.gridSize = gridSize;
                entry.mesh = buildTileMesh(tile, grid, elevationManager, gridSize);
                entry.lastUsed = pass;
                it = _meshCache.insert_or_assign(std::make_pair(tileId, gridSize), std::move(entry)).first;
            }
            it->second.lastUsed = pass;
            tileMeshes.emplace_back(tile, it->second.mesh);
        }
    }

    bool TerrainRenderer::renderTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Shader>& shader, const std::function<void(const MapTile&)>& tileUniformsFn, int meshResolutionCap, bool surfaceAttribs) {
        std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > > tileMeshes;
        collectTileMeshes(viewState, terrainOptions, meshResolutionCap, tileMeshes);

        GLuint progId = shader->getProgId();
        glUseProgram(progId);
        GLuint aCoord = shader->getAttribLoc("a_coord");
        GLuint uMVPMat = shader->getUniformLoc("u_mvpMat");
        glEnableVertexAttribArray(aCoord);
        // The depth passes all declare u_far; the surface shader works in metres and does not,
        // and Shader::getUniformLoc answers 0 - a valid location - for a uniform that is not there.
        if (!surfaceAttribs) {
            glUniform1f(shader->getUniformLoc("u_far"), viewState.getFar());
        }

        GLint aNormal = -1, aElevation = -1;
        std::shared_ptr<ElevationManager> elevationManager;
        if (surfaceAttribs) {
            aNormal = glGetAttribLocation(progId, "a_normal");
            aElevation = glGetAttribLocation(progId, "a_elevation");
            elevationManager = terrainOptions->getElevationManager();
            if (aNormal >= 0) {
                glEnableVertexAttribArray(aNormal);
            }
            if (aElevation >= 0) {
                glEnableVertexAttribArray(aElevation);
            }
        }

        const cglib::mat4x4<double>& mvpMat = viewState.getModelviewProjectionMat();
        for (const auto& tileMesh : tileMeshes) {
            const std::shared_ptr<TileMesh>& mesh = tileMesh.second;
            if (!mesh || mesh->indices.empty()) {
                continue;
            }

            cglib::mat4x4<float> tileMVPMat = cglib::mat4x4<float>::convert(mvpMat * calculateTileMatrix(tileMesh.first));
            glUniformMatrix4fv(uMVPMat, 1, GL_FALSE, tileMVPMat.data());
            if (tileUniformsFn) {
                tileUniformsFn(tileMesh.first);
            }
            if (surfaceAttribs && elevationManager) {
                ensureSurfaceAttribs(tileMesh.first, elevationManager, *mesh);
                if (aNormal >= 0) {
                    glVertexAttribPointer(aNormal, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), mesh->surfaceAttribs.data());
                }
                if (aElevation >= 0) {
                    glVertexAttribPointer(aElevation, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), mesh->surfaceAttribs.data() + 3);
                }
            }
            glVertexAttribPointer(aCoord, 3, GL_FLOAT, GL_FALSE, 0, mesh->vertices.data());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->indices.size()), GL_UNSIGNED_SHORT, mesh->indices.data());
        }

        if (aNormal >= 0) {
            glDisableVertexAttribArray(aNormal);
        }
        if (aElevation >= 0) {
            glDisableVertexAttribArray(aElevation);
        }
        glDisableVertexAttribArray(aCoord);
        return true;
    }

    void TerrainRenderer::ensureSurfaceAttribs(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, TileMesh& mesh) const {
        std::size_t vertexCount = mesh.vertices.size() / 3;
        if (!mesh.surfaceAttribs.empty() || vertexCount == 0 || mesh.gridSize < 1) {
            return;
        }

        int gridSize = mesh.gridSize;
        int rowSize = gridSize + 1;
        int tileMask = (1 << tile.getZoom()) - 1;
        double zoomScale = 1.0 / (1 << tile.getZoom());
        double originY = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
        double size = zoomScale * Const::WORLD_SIZE;
        float exaggeration = elevationManager->getExaggeration();

        // The tile-local frame scales x, y and z by the same factor (calculateTileMatrix), so a
        // normal built from the local height field is already a world-space direction.
        auto localZ = [&](int gx, int gy) {
            gx = std::min(std::max(gx, 0), gridSize);
            gy = std::min(std::max(gy, 0), gridSize);
            return mesh.vertices[(gy * rowSize + gx) * 3 + 2];
        };

        mesh.surfaceAttribs.resize(vertexCount * 4);
        for (int gy = 0; gy <= gridSize; gy++) {
            double internalY = originY + (static_cast<double>(gy) / gridSize) * size;
            double displayScale = elevationManager->getDisplayScale(internalY);
            double metersPerLocalZ = (exaggeration > 0 && displayScale > 0 ? size / (exaggeration * displayScale) : 0);
            for (int gx = 0; gx <= gridSize; gx++) {
                float dzdx = (localZ(gx + 1, gy) - localZ(gx - 1, gy)) * 0.5f * gridSize;
                float dzdy = (localZ(gx, gy + 1) - localZ(gx, gy - 1)) * 0.5f * gridSize;
                cglib::vec3<float> normal = cglib::unit(cglib::vec3<float>(-dzdx, -dzdy, 1.0f));
                std::size_t offset = static_cast<std::size_t>(gy * rowSize + gx) * 4;
                mesh.surfaceAttribs[offset + 0] = normal(0);
                mesh.surfaceAttribs[offset + 1] = normal(1);
                mesh.surfaceAttribs[offset + 2] = normal(2);
                mesh.surfaceAttribs[offset + 3] = static_cast<float>(localZ(gx, gy) * metersPerLocalZ);
            }
        }

        // Skirt vertices duplicate a grid vertex's x/y at a lower z: give them that vertex's
        // values, so the crack-filling walls shade like the edge they hang from instead of
        // showing up as flat-lit bands.
        for (std::size_t i = static_cast<std::size_t>(rowSize) * rowSize; i < vertexCount; i++) {
            int gx = static_cast<int>(mesh.vertices[i * 3 + 0] * gridSize + 0.5f);
            int gy = static_cast<int>(mesh.vertices[i * 3 + 1] * gridSize + 0.5f);
            gx = std::min(std::max(gx, 0), gridSize);
            gy = std::min(std::max(gy, 0), gridSize);
            std::size_t source = static_cast<std::size_t>(gy * rowSize + gx) * 4;
            std::copy(mesh.surfaceAttribs.begin() + source, mesh.surfaceAttribs.begin() + source + 4, mesh.surfaceAttribs.begin() + i * 4);
        }
    }

    void TerrainRenderer::calculateVisibleTiles(const ViewState& viewState, const std::shared_ptr<ElevationManager>& elevationManager, const MapTile& tile, std::vector<MapTile>& tiles) const {
        if (tile.getZoom() > Const::MAX_SUPPORTED_ZOOM_LEVEL) {
            return;
        }

        // Tile bounds in internal coordinates (same convention as DefaultTileTransformer)
        int tileMask = (1 << tile.getZoom()) - 1;
        double zoomScale = 1.0 / (1 << tile.getZoom());
        double minX = (tile.getX() * zoomScale - 0.5) * Const::WORLD_SIZE;
        double minY = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
        double size = zoomScale * Const::WORLD_SIZE;
        double minZ = 0, maxZ = 0;
        elevationManager->getMinMaxDisplayHeight(tile, minZ, maxZ);
        cglib::bbox3<double> tileBounds(cglib::vec3<double>(minX, minY, minZ), cglib::vec3<double>(minX + size, minY + size, maxZ));

        if (!viewState.getFrustum().inside(tileBounds)) {
            return;
        }

        // Same distance-based subdivision criterion as TileLayer::calculateVisibleTilesRecursive.
        // Like there, the LOD center is at surface level so decisions are stable while
        // elevation data streams in.
        cglib::vec3<double> lodCenter(minX + size * 0.5, minY + size * 0.5, 0);
        const cglib::mat4x4<double>& mvpMat = viewState.getModelviewProjectionMat();
        double tileW = lodCenter(0) * mvpMat(3, 0) + lodCenter(1) * mvpMat(3, 1) + lodCenter(2) * mvpMat(3, 2) + mvpMat(3, 3);
        double zoomDistance = tileW * std::pow(2.0, static_cast<double>(tile.getZoom()));
        bool subDivide = zoomDistance < Const::WORLD_SIZE * Const::SQRT_2;

        // No point in subdividing beyond the resolution of the elevation data + mesh grid
        int maxUsefulZoom = Const::MAX_SUPPORTED_ZOOM_LEVEL;
        if (std::shared_ptr<TileDataSource> dataSource = elevationManager->getDataSource()) {
            maxUsefulZoom = dataSource->getMaxZoom() + 3;
        }
        int targetTileZoom = std::min(maxUsefulZoom, static_cast<int>(viewState.getZoom() + 0.001f));
        if (targetTileZoom <= tile.getZoom()) {
            subDivide = false;
        }

        if (subDivide) {
            for (int n = 0; n < 4; n++) {
                calculateVisibleTiles(viewState, elevationManager, tile.getChild(n), tiles);
            }
        } else {
            tiles.push_back(tile);
        }
    }

    int TerrainRenderer::calculateMeshGridSize(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int meshResolution) const {
        if (!grid || grid->getMaxHeight() - grid->getMinHeight() <= 0) {
            return 1;
        }

        // The pre-pass mesh must never be FINER than the draped tile surfaces: a coarser
        // draped surface would linearly cut through ridges of a finer pre-pass mesh and
        // fail the depth test (see-through holes in the terrain). The draped surfaces are
        // tesselated to min(meshResolution, elevation texels per tile) cells per tile edge,
        // so the pre-pass uses the same bound and only gets coarser with distance (a
        // coarser pre-pass merely weakens ridge occlusion, which is safe).
        double tileSize = Const::WORLD_SIZE / (1 << tile.getZoom());
        double gridWidth = grid->getInternalBounds().getMax().getX() - grid->getInternalBounds().getMin().getX();
        int texelsPerTile = MAX_MESH_GRID_SIZE;
        if (gridWidth > 0) {
            texelsPerTile = static_cast<int>(grid->getWidth() * tileSize / gridWidth + 0.5);
        }
        // No distance-based coarsening: a pre-pass mesh coarser than the draped surfaces
        // smooths out ridges in the depth buffer and lets geometry behind them show
        // through (weak occlusion, visible especially at low zoom levels/grazing angles).
        int gridSize = std::min(std::min(texelsPerTile, meshResolution), MAX_MESH_GRID_SIZE);
        return std::max(gridSize, MIN_MESH_GRID_SIZE);
    }

    std::shared_ptr<TerrainRenderer::TileMesh> TerrainRenderer::buildTileMesh(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, const std::shared_ptr<ElevationManager>& elevationManager, int gridSize) const {
        auto mesh = std::make_shared<TileMesh>();

        int tileMask = (1 << tile.getZoom()) - 1;
        double zoomScale = 1.0 / (1 << tile.getZoom());
        double originX = (tile.getX() * zoomScale - 0.5) * Const::WORLD_SIZE;
        double originY = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
        double size = zoomScale * Const::WORLD_SIZE;
        double localFromInternal = 1.0 / size;

        float exaggeration = elevationManager->getExaggeration();

        gridSize = std::max(1, gridSize);
        int rowSize = gridSize + 1;
        mesh->gridSize = gridSize;

        mesh->vertices.reserve((rowSize * rowSize + 8 * rowSize) * 3); // grid + skirt vertices
        double minLocalZ = 0;
        for (int gy = 0; gy <= gridSize; gy++) {
            for (int gx = 0; gx <= gridSize; gx++) {
                double x = static_cast<double>(gx) / gridSize;
                double y = static_cast<double>(gy) / gridSize;
                double internalX = originX + x * size;
                double internalY = originY + y * size;
                double localZ = 0;
                if (grid) {
                    double meters = grid->sampleHeight(internalX, internalY);
                    localZ = meters * exaggeration * elevationManager->getDisplayScale(internalY) * localFromInternal;
                }
                minLocalZ = std::min(minLocalZ, localZ);
                mesh->vertices.push_back(static_cast<float>(x));
                mesh->vertices.push_back(static_cast<float>(y));
                mesh->vertices.push_back(static_cast<float>(localZ));
            }
        }

        mesh->indices.reserve(gridSize * gridSize * 6 + gridSize * 4 * 6);
        for (int gy = 0; gy < gridSize; gy++) {
            for (int gx = 0; gx < gridSize; gx++) {
                unsigned short i00 = static_cast<unsigned short>(gy * rowSize + gx);
                unsigned short i10 = i00 + 1;
                unsigned short i01 = static_cast<unsigned short>((gy + 1) * rowSize + gx);
                unsigned short i11 = i01 + 1;
                mesh->indices.insert(mesh->indices.end(), { i00, i10, i11, i00, i11, i01 });
            }
        }

        // Skirts: extrude the tile edges downwards to cover cracks between neighboring
        // tiles of different resolutions in the depth buffer.
        if (grid) {
            double skirtZ = minLocalZ - 0.05;
            auto addSkirt = [&](const std::vector<unsigned short>& edge, bool flip) {
                for (std::size_t i = 0; i + 1 < edge.size(); i++) {
                    unsigned short i0 = edge[i];
                    unsigned short i1 = edge[i + 1];
                    unsigned short s0 = static_cast<unsigned short>(mesh->vertices.size() / 3);
                    for (unsigned short idx : { i0, i1 }) {
                        mesh->vertices.push_back(mesh->vertices[idx * 3 + 0]);
                        mesh->vertices.push_back(mesh->vertices[idx * 3 + 1]);
                        mesh->vertices.push_back(static_cast<float>(skirtZ));
                    }
                    if (flip) {
                        mesh->indices.insert(mesh->indices.end(), { i0, s0, static_cast<unsigned short>(s0 + 1), i0, static_cast<unsigned short>(s0 + 1), i1 });
                    } else {
                        mesh->indices.insert(mesh->indices.end(), { i0, static_cast<unsigned short>(s0 + 1), s0, i0, i1, static_cast<unsigned short>(s0 + 1) });
                    }
                }
            };
            std::vector<unsigned short> south, north, west, east;
            for (int g = 0; g <= gridSize; g++) {
                south.push_back(static_cast<unsigned short>(g));
                north.push_back(static_cast<unsigned short>(gridSize * rowSize + g));
                west.push_back(static_cast<unsigned short>(g * rowSize));
                east.push_back(static_cast<unsigned short>(g * rowSize + gridSize));
            }
            addSkirt(south, false);
            addSkirt(north, true);
            addSkirt(west, true);
            addSkirt(east, false);
        }

        return mesh;
    }

    cglib::mat4x4<double> TerrainRenderer::calculateTileMatrix(const MapTile& tile) const {
        int tileMask = (1 << tile.getZoom()) - 1;
        double zoomScale = 1.0 / (1 << tile.getZoom());
        double s = zoomScale * Const::WORLD_SIZE;
        cglib::mat4x4<double> m = cglib::mat4x4<double>::zero();
        m(0, 0) = s;
        m(1, 1) = s;
        m(2, 2) = s;
        m(0, 3) = (tile.getX() * zoomScale - 0.5) * Const::WORLD_SIZE;
        m(1, 3) = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
        m(2, 3) = 0;
        m(3, 3) = 1;
        return m;
    }

    const std::string TerrainRenderer::TERRAIN_DEPTH_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec3 a_coord;
        uniform mat4 u_mvpMat;
        uniform float u_far;
        varying float v_depth;
        void main() {
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_depth = pos.w / u_far;
            gl_Position = pos;
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_COLOR_FRAGMENT_SHADER = R"GLSL(
        #version 100
        precision mediump float;
        uniform vec4 u_color;
        void main() {
            gl_FragColor = u_color;
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_BITMAP_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec3 a_coord;
        uniform mat4 u_mvpMat;
        uniform float u_far;
        uniform vec4 u_uvOffsetScale;
        varying vec2 v_uv;
        varying float v_depth;
        void main() {
            v_uv = u_uvOffsetScale.xy + a_coord.xy * u_uvOffsetScale.zw;
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_depth = pos.w / u_far; // keeps u_far active; renderTiles sets it for every shader
            gl_Position = pos;
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_BITMAP_FRAGMENT_SHADER = R"GLSL(
        #version 100
        precision mediump float;
        uniform sampler2D u_tex;
        varying vec2 v_uv;
        void main() {
            gl_FragColor = texture2D(u_tex, v_uv);
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_SURFACE_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec3 a_coord;
        attribute vec3 a_normal;
        attribute float a_elevation;
        uniform mat4 u_mvpMat;
        uniform mat4 u_tileMat;
        uniform float u_metersPerUnit;
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        varying float v_elevation;
        varying float v_dist;
        void main() {
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_normal = a_normal;
            v_worldPos = (u_tileMat * vec4(a_coord, 1.0)).xyz;
            v_elevation = a_elevation;
            v_dist = pos.w * u_metersPerUnit;
            gl_Position = pos;
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_SURFACE_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #version 100
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        varying float v_elevation;
        varying float v_dist;
        uniform vec3 u_sunDir;
        uniform vec4 u_sunColor;
        uniform float u_sunIntensity;
        uniform float u_ambientIntensity;
        uniform float u_time;
        uniform float u_zoom;
        uniform vec2 u_resolution;
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_SURFACE_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            vec4 color = surfaceColor();
            // The surface used to fog itself, through a private ramp in metres that agreed with
            // nothing else in the frame. It now goes through the one shared block, so a custom fog
            // shader reaches the relief surface too.
            gl_FragColor = applyFog(vec4(color.rgb * color.a, color.a));
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_DEPTH_FRAGMENT_SHADER = R"GLSL(
        #version 100
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        varying float v_depth;
        void main() {
            float depth = clamp(v_depth, 0.0, 1.0);
            vec3 enc = vec3(1.0, 255.0, 65025.0) * depth;
            enc = fract(enc);
            enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
            gl_FragColor = vec4(enc, 1.0);
        }
    )GLSL";
}
