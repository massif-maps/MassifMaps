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
#include "projections/ProjectionSurface.h"
#include "terrain/ElevationManager.h"
#include "terrain/TerrainOcclusion.h"
#include "terrain/ElevationTileGrid.h"
#include "renderers/utils/ElevationTextureCache.h"

#include <vt/RenderStats.h>
#include <vt/TileTransformer.h>
#include "utils/Const.h"
#include "renderers/utils/TerrainMeshBuffer.h"
#include "utils/Log.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>

namespace massif {

    struct TerrainRenderer::TileMesh {
        std::vector<float> vertices; // planar: x, y in tile coordinates [0..1], z tile-local; spherical: the curved position
        // The grid node heights, tile-local. Kept because on a sphere they are NOT recoverable
        // from a vertex's z, which there is a curved coordinate rather than a height.
        std::vector<float> heights;
        // The grid vertex each SKIRT vertex hangs from, so its shading can be copied from that
        // vertex. Recovering it from the position only works while the tile is a flat unit square.
        std::vector<unsigned short> skirtSources;
        std::vector<unsigned short> indices;
        // Where the SKIRT indices start. The grid is emitted first, so drawing only this many
        // indices draws the surface without its edge walls - see renderTiles' skipSkirts.
        std::size_t gridIndexCount = 0;
        // Surface pass only, filled on first use: nx, ny, nz, elevation in metres per vertex.
        std::vector<float> surfaceAttribs;
        // The TerrainOptions::getNormalSampleDistance the attribs above were built with, so that
        // changing it re-bakes them instead of being ignored until the mesh is evicted. -1 is
        // "not built yet"; 0 is the mesh-derived gradient, which is a real setting.
        float surfaceAttribSampleDistance = -1.0f;
        // The GPU copy. Created on first draw and kept for the mesh's life - see TerrainMeshBuffer
        // for why drawing straight out of the vectors above was the most expensive thing in a frame.
        std::shared_ptr<TerrainMeshBuffer> buffer;
        // The sample distance the ATTRIB buffer holds, so a re-bake re-uploads and nothing else does.
        float uploadedAttribSampleDistance = -2.0f;
        // Whether the attribs above are the FIXED-SCALE ones (read from the DEM at a constant ground
        // step) or the cheap mesh-gradient stand-in baked inline so the tile can draw at once. See
        // refineSurfaceAttribs: the DEM read is four lookups per vertex and measured 14 ms a tile,
        // which is a hitch on the render thread and nothing at all on a worker.
        bool attribsRefined = false;
        // The DEM tile this mesh's grid actually resolved to, for the per-tile debug views: the
        // difference between a tile and its neighbour has been inferred four times over and never
        // once LOOKED AT, so it gets painted on the surface instead.
        int demZoom = -1;
        // The DEM zoom the surface attribs were actually COMPUTED from, which is not the same as the
        // one the tile resolves NOW: attribs are baked once and ensureSurfaceAttribs early-returns
        // forever after, so a tile refined while standing on a coarse ancestor keeps those normals
        // even once its own grid lands. attribsRefined and demZoom both read healthy in that state.
        int attribsDemZoom = -1;
        // The COARSEST grid any of the four stencil reads fell back to, and whether that was coarser
        // than the source could have given. The tile's own grid does not decide this: the reads are
        // at +/- normalSampleDistance, so near an edge they land on NEIGHBOURING DEM tiles, and a
        // tile whose own grid is exact still bakes a smoothed slope if its neighbour's has not
        // arrived. A provisional bake is one that must be redone when more data lands - without it
        // the tile keeps whatever the cache happened to hold at that instant, which is decided by
        // arrival order and so differs from run to run: the same tile, shaded differently each time.
        bool attribsProvisional = false;
        int attribsWorstZoom = -1;
        unsigned int attribsDataVersion = 0; // ElevationManager::getDataVersion() when it was baked
        int attribsRebakes = 0;              // bounded, see MAX_ATTRIB_REBAKES
        bool attribsPending = false; // a refine job is out for this mesh; render thread only
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
        logBuildStamp();
    }

    // WHICH NATIVE BINARY IS ACTUALLY RUNNING. __DATE__/__TIME__ are baked in at compile time, so
    // this is the one thing that cannot be livesynced: JS is pushed to files/app independently of
    // the APK, so neither the install timestamp nor the app's own version can tell a fresh native
    // build from a stale one. Several debugging rounds were spent on changes that had never
    // reached the device, and on dismissing results that had.
    void TerrainRenderer::logBuildStamp() {
        static std::once_flag once;
        std::call_once(once, []() {
            Log::Infof("massif native build: %s %s", __DATE__, __TIME__);
        });
    }

    TerrainRenderer::~TerrainRenderer() {
        stopAttribWorker();
    }

    bool TerrainRenderer::renderDepthPrepass(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        // Depth-only pass into the current framebuffer: the single source of truth 2D draped
        // geometry depth-tests against. Slope-scaled polygon offset, because the pre-pass and the
        // draped meshes are different tesselations and a constant bias cannot cover glancing ones.
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

        // Opaque terrain base fill. Depth is used DURING the pass so near slopes win over far ones,
        // and with keepDepth it subsumes the depth pre-pass. The slope-scaled push keeps the draped
        // content, a different tesselation, in front of the kept depth.
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

        // Color-only mode: the tile layer surface pre-passes provide the terrain depth with their
        // own meshes, so this fill's depth must not survive - it would clip the tile content in
        // triangle-shaped patches wherever the two tesselations disagree.
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

        // World-anchored repeating pattern, matching the flat-map BackgroundRenderer: the bitmap
        // repeats once per map tile of the current integer zoom. The uv transform is reduced modulo
        // 1 in double precision on the CPU, so the shader only interpolates small uvs.
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

    bool TerrainRenderer::renderDepthTexture(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, int meshResolutionCap, bool withNormals, bool forReadback) {
        if (!terrainOptions || !glResourceManager || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }

        // The POST-PROCESS buffer follows the option, where the occlusion read-back below keeps the
        // constant: an effect that differentiates this buffer shows its texels as blocks and comb
        // streaks at close range, and a read-back that samples points does not care.
        int downscale = (forReadback ? BUFFER_DOWNSCALE : std::max(1, terrainOptions->getPostProcessDownscale()));
        int bufferWidth = std::max(1, viewState.getWidth() / downscale);
        int bufferHeight = std::max(1, viewState.getHeight() / downscale);
        // Two buffers, because the read-back's size does not follow the option: a full-resolution
        // glReadPixels is a stall, and an effect's sampling resolution is not the occlusion query's.
        std::shared_ptr<FrameBuffer>& target = (forReadback ? _readbackFrameBuffer : _frameBuffer);
        if (!target || !target->isValid() || target->getWidth() != bufferWidth || target->getHeight() != bufferHeight) {
            target = glResourceManager->create<FrameBuffer>(bufferWidth, bufferHeight, true, true, false);
            _depthTextureMVPMatrix = cglib::mat4x4<double>::zero();
        }
        if (!target) {
            return false;
        }

        // The texture is still there from the last frame, and with the camera and the elevation
        // unchanged it is still the answer. This pass draws the terrain from CPU meshes at full
        // resolution - 9.5 ms of a 19.3 ms peak-finder frame on an Adreno 610.
        unsigned int elevationVersion = (terrainOptions->getElevationManager() ? terrainOptions->getElevationManager()->getVersion() : 0);
        if (!_depthTextureAttribsDirty && _depthTextureMVPMatrix == viewState.getModelviewProjectionMat() && _depthTextureElevationVersion == elevationVersion && _depthTextureMeshResolutionCap == meshResolutionCap && _depthTextureWithNormals == withNormals) {
            return true;
        }
        _depthTextureAttribsDirty = false;
        _depthTextureMVPMatrix = viewState.getModelviewProjectionMat();
        _depthTextureElevationVersion = elevationVersion;
        _depthTextureMeshResolutionCap = meshResolutionCap;
        _depthTextureWithNormals = withNormals;

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, target->getFBOId());
        glViewport(0, 0, bufferWidth, bufferHeight);

        // Clear to 'sky'. Without normals that is maximum depth and zero coverage; with them there
        // is no coverage channel to spare, so the sky IS the depth the terrain is not allowed to
        // write - (1, 0) decodes to exactly 1.0 - and the normal reads straight up so that a sky
        // texel sampled by mistake is at least not a direction.
        glClearColor(1.0f, withNormals ? 0.0f : 1.0f, withNormals ? 0.5f : 1.0f, withNormals ? 0.5f : 0.0f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests

        bool result = false;
        if (withNormals) {
            if (!_normalShader || !_normalShader->isValid()) {
                _normalShader = glResourceManager->create<Shader>("terrainnormaldepth", TERRAIN_NORMAL_DEPTH_VERTEX_SHADER, TERRAIN_NORMAL_DEPTH_FRAGMENT_SHADER);
            }
            if (_normalShader) {
                result = renderTiles(viewState, terrainOptions, glResourceManager, _normalShader, std::function<void(const MapTile&)>(), meshResolutionCap, false, true);
            }
        } else {
            if (!_shader || !_shader->isValid()) {
                _shader = glResourceManager->create<Shader>("terraindepth", TERRAIN_DEPTH_VERTEX_SHADER, TERRAIN_DEPTH_FRAGMENT_SHADER);
            }
            if (_shader) {
                result = renderTiles(viewState, terrainOptions, glResourceManager, _shader, std::function<void(const MapTile&)>(), meshResolutionCap);
            }
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

        // The worker renders on a second GL context the driver has to interleave with the render
        // one, so submitting on every camera change makes that contention the new cost. While the
        // camera moves the occlusion depth may lag; the frame it comes to rest on refreshes at once.
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
        if (_depthElevationVersion != elevationVersion) {
            resetOcclusionVerdicts(); // new ground, so what a position was last told may be wrong
        }
        _depthMVPMatrix = mvpMatrix;
        _depthElevationVersion = elevationVersion;
        _depthStale = true; // the result lands in a later frame; keep rendering until it does
        return true;
    }

    bool TerrainRenderer::updateDepthBufferSync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager) {
        int bufferWidth = std::max(1, viewState.getWidth() / BUFFER_DOWNSCALE);
        int bufferHeight = std::max(1, viewState.getHeight() / BUFFER_DOWNSCALE);

        // The render + read-back only happens when the camera or the elevation changed, so a static
        // frame is free. While the camera moves they are throttled on top: a slightly stale
        // occlusion depth is invisible, a glReadPixels stall every frame is not.
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

        if (!renderDepthTexture(viewState, terrainOptions, glResourceManager, DEPTH_TEXTURE_MESH_RESOLUTION, false, true)) {
            return false;
        }
        auto newDepthData = std::make_shared<TerrainDepthBuffer>();
        newDepthData->data.resize(static_cast<std::size_t>(bufferWidth) * bufferHeight * 4);

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, _readbackFrameBuffer->getFBOId());
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
        if (_depthElevationVersion != elevationVersion) {
            resetOcclusionVerdicts(); // new ground, so what a position was last told may be wrong
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

    long long TerrainRenderer::occlusionVerdictKey(const cglib::vec3<double>& pos) {
        // The bit patterns, not a quantization: a label's centre is recomputed from the same tile
        // geometry on every placement pass and repeats exactly, so this hits. A rebuilt or moved
        // feature misses instead of colliding, and a miss is the behaviour this had before.
        std::uint64_t xBits = 0, yBits = 0;
        double x = pos(0), y = pos(1);
        std::memcpy(&xBits, &x, sizeof(xBits));
        std::memcpy(&yBits, &y, sizeof(yBits));
        return static_cast<long long>(xBits ^ (yBits * 0x9E3779B97F4A7C15ULL));
    }

    bool TerrainRenderer::cachedOcclusionVerdict(long long key) const {
        std::lock_guard<std::mutex> lock(_occlusionVerdictMutex);
        auto it = _occlusionVerdicts.find(key);
        return it != _occlusionVerdicts.end() ? it->second : false; // never seen: as before, place it
    }

    void TerrainRenderer::rememberOcclusionVerdict(long long key, bool occluded) const {
        std::lock_guard<std::mutex> lock(_occlusionVerdictMutex);
        if (_occlusionVerdicts.size() >= MAX_OCCLUSION_VERDICTS) {
            _occlusionVerdicts.clear();
        }
        _occlusionVerdicts[key] = occluded;
    }

    void TerrainRenderer::resetOcclusionVerdicts() {
        std::lock_guard<std::mutex> lock(_occlusionVerdictMutex);
        _occlusionVerdicts.clear();
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

        long long verdictKey = occlusionVerdictKey(pos);
        cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(pos(0), pos(1), pos(2), 1), depthData->mvpMatrix);
        if (clipPos(3) <= 0) {
            return cachedOcclusionVerdict(verdictKey); // behind the buffer's camera: unanswerable
        }
        int x = static_cast<int>((clipPos(0) / clipPos(3) * 0.5 + 0.5) * depthData->width);
        int y = static_cast<int>((0.5 - clipPos(1) / clipPos(3) * 0.5) * depthData->height);
        // Outside the buffer's viewport is UNANSWERABLE, and is not the same thing as a sky pixel
        // inside it - which is a real answer, and the common one for a summit on the horizon. The
        // bounds are tested here rather than left to sampleDepthW, which returns the same 'nothing
        // there' for both.
        if (x < 0 || y < 0 || x >= depthData->width || y >= depthData->height) {
            return cachedOcclusionVerdict(verdictKey);
        }
        float depthW = sampleDepthW(*depthData, x, y);
        if (depthW == std::numeric_limits<float>::max()) {
            rememberOcclusionVerdict(verdictKey, false);
            return false; // sky: nothing in front of it
        }
        // Farthest terrain depth AROUND the position, not the depth of its own pixel: a ground label
        // sits exactly on the terrain and the buffer is read back downscaled, so on a slope an exact
        // comparison lets a label's own ground occlude it - the labels blinking while panning.
        // The SPREAD over the same samples is how obliquely the view meets the ground, which is what
        // decides how much depth a small height error is worth - see TerrainOcclusion::isBehind.
        float nearestW = depthW;
        for (int i = 0; i < 4; i++) {
            int dx = (i & 1 ? OCCLUSION_SAMPLE_OFFSET : -OCCLUSION_SAMPLE_OFFSET);
            int dy = (i & 2 ? OCCLUSION_SAMPLE_OFFSET : -OCCLUSION_SAMPLE_OFFSET);
            float neighbourDepthW = sampleDepthW(*depthData, x + dx, y + dy);
            if (neighbourDepthW < std::numeric_limits<float>::max()) {
                depthW = std::max(depthW, neighbourDepthW);
                nearestW = std::min(nearestW, neighbourDepthW);
            }
        }
        bool occluded = TerrainOcclusion::isBehind(static_cast<float>(clipPos(3)), depthW, depthW - nearestW, tolerance);
        rememberOcclusionVerdict(verdictKey, occluded);
        return occluded;
    }

    void TerrainRenderer::collectVisibleTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, std::vector<MapTile>& tiles) const {
        if (!terrainOptions) {
            return;
        }
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
        if (!elevationManager) {
            return;
        }
        // The cut, and a BUDGET on it: a level coarser everywhere until it fits, the way
        // TileLayer::calculateVisibleTiles keeps its own cover inside TERRAIN_COVER_TILE_BUDGET. See
        // MAX_VISIBLE_MESH_TILES for what overflowing it costs - it is not the drawing, it is the
        // mesh cache being smaller than the frame's working set.
        // The SAME cut is asked for two to four times a frame - the surface, the depth pre-pass, the
        // post-process depth texture, the occlusion read-back - and nothing between them changes it.
        // Keyed on the camera and the elevation version, so it is recomputed exactly when it would
        // differ.
        unsigned int elevationVersion = elevationManager->getVersion();
        {
            std::lock_guard<std::mutex> lock(_visibleTilesMutex);
            if (_visibleTilesValid && _visibleTilesMVP == viewState.getModelviewProjectionMat() && _visibleTilesElevationVersion == elevationVersion) {
                tiles = _visibleTilesCache;
                return;
            }
        }

        // Seeded from the zoom the LAST cut settled on, not from MAX_SUPPORTED_ZOOM_LEVEL. Starting
        // at 24 every time is the expensive part: a first-person camera near the ground subdivides
        // the near tiles as deep as it is allowed, so the first walk returns thousands of tiles and
        // the budget loop then re-walks the WHOLE quadtree from the root, once per level, until it
        // fits - a dozen full walks, each one a frustum test plus a min/max height lookup plus a
        // vertex transformer per node visited. Measured on a Crosscall: 129-150 ms of a 103-165 ms
        // frame, and entirely independent of the mesh resolution, which is why coarsening the mesh
        // changed nothing. From the previous answer it converges in one walk, or two when the view
        // opens up. +1 so it can climb back when the view narrows.
        int maxVisibleTiles = terrainOptions->getMeshCacheSize() / 2;
        if (maxVisibleTiles <= 0) {
            maxVisibleTiles = MAX_VISIBLE_MESH_TILES;
        }
        int maxZoom;
        {
            std::lock_guard<std::mutex> lock(_visibleTilesMutex);
            maxZoom = std::min(Const::MAX_SUPPORTED_ZOOM_LEVEL, _budgetMaxZoom + 1);
        }
        // The app's own ceiling, which is what makes the height field SETTLE rather than keep
        // refining under everything anchored to it - see TerrainOptions::setMaxZoom.
        int zoomCap = terrainOptions->getMaxZoom();
        if (zoomCap > 0) {
            maxZoom = std::min(maxZoom, zoomCap);
        }
        // And the LOD ring cap, relative to the camera, the SAME option TileLayer applies to the
        // draped cut (TileLayer::calculateVisibleTiles). 100 or more disables it, which is the
        // default: terrain LOD is distance based, so a tile close to the camera is meant to sit
        // above the level flat rendering would pick. calculateVisibleTiles used to clamp to the bare
        // camera zoom instead, which is that option pinned at 0 and unreachable - see the note there.
        int zoomOffset = terrainOptions->getMaxTileZoomOffset();
        if (zoomOffset < 100) {
            maxZoom = std::min(maxZoom, static_cast<int>(viewState.getZoom() + 0.001f) + zoomOffset);
        }
        for (;;) {
            tiles.clear();
            calculateVisibleTiles(viewState, elevationManager, MapTile(0, 0, 0, 0), maxZoom, tiles);
            if (static_cast<int>(tiles.size()) <= maxVisibleTiles || maxZoom <= 0) {
                break;
            }
            maxZoom--;
        }

        std::lock_guard<std::mutex> lock(_visibleTilesMutex);
        _budgetMaxZoom = maxZoom;
        _visibleTilesMVP = viewState.getModelviewProjectionMat();
        _visibleTilesElevationVersion = elevationVersion;
        _visibleTilesCache = tiles;
        _visibleTilesValid = true;
    }

    void TerrainRenderer::evictLeastRecentlyUsedMeshes(unsigned int pass, int maxCachedMeshes) {
        // Evict the least-recently-used entries, NOT the whole cache (as
        // ElevationTextureCache::evictLeastRecentlyUsed does). Meshes already drawn in this pass are
        // never victims: dropping one would give the tile a flat mesh for the rest of the frame.
        while (static_cast<int>(_meshCache.size()) >= maxCachedMeshes) {
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
            VT_STAT_INC(terrainMeshEvictions);
            _meshCache.erase(lru);
        }
    }

    int TerrainRenderer::calculateEdgeMask(const MapTile& tile, const std::set<long long>& visibleTileIds,
                                          const std::map<long long, int>& demZooms, int ownDemZoom) {
        // The visible set is a quadtree cut, so a neighbour is either at this tile's zoom or is one
        // of its ANCESTORS. Walking the neighbour's ancestry up from this zoom therefore finds it,
        // and the zoom it is found at is the neighbour's LOD.
        //
        // The cut is NOT restricted - nothing stops two adjacent tiles being several levels apart -
        // so the mask carries the DIFFERENCE per side, three bits each, and not just a flag.
        //
        // AND THE DEM ZOOM, which is the difference that actually produces the seams. A tile's
        // heights come from whatever elevation tile it RESOLVED, not from its own zoom: two
        // neighbours at the same tile zoom can land on different levels, one on its own grid and
        // one on a cached ancestor covering four or sixteen times the ground. They are then built
        // from different height fields and meet along an edge neither agrees about - a seam of
        // EQUAL zoom, which no LOD reasoning explains and which a mask keyed on tile zoom alone
        // cannot see. Measured at a valley viewpoint: neighbours on z12 and z8, and stitching on
        // against off changed 162 pixels of 500000 because the mask was almost always zero.
        //
        // The coarser of the two differences wins: the edge has to be laid out along whichever
        // neighbour spacing is wider, whether it is wider because of the cut or because of the DEM.
        int zoom = tile.getZoom();
        int frameNr = tile.getFrameNr();
        int tileCount = 1 << zoom;
        auto neighbourLevels = [&](int dx, int dy) {
            int nx = tile.getX() + dx;
            int ny = tile.getY() + dy;
            if (ny < 0 || ny >= tileCount) {
                return 0; // off the top or the bottom of the world: nothing to meet
            }
            nx = (nx % tileCount + tileCount) % tileCount; // x wraps
            for (int nzoom = zoom; nzoom >= 0; nzoom--) {
                long long neighbourId = MapTile(nx >> (zoom - nzoom), ny >> (zoom - nzoom), nzoom, frameNr).getTileId();
                if (visibleTileIds.count(neighbourId) > 0) {
                    int levels = zoom - nzoom;
                    // Only when both sides actually resolved something: a tile with no grid has no
                    // spacing to stitch to, and -1 would read as an enormous difference.
                    auto demIt = demZooms.find(neighbourId);
                    if (ownDemZoom >= 0 && demIt != demZooms.end() && demIt->second >= 0) {
                        levels = std::max(levels, ownDemZoom - demIt->second);
                    }
                    return std::min(std::max(levels, 0), EDGE_MAX_LEVELS);
                }
            }
            return 0; // not visible at all, so there is no seam to close
        };

        // Tile y runs south while the mesh's gy runs north, hence the swap.
        return (neighbourLevels(0, 1) << EDGE_SHIFT_SOUTH) | (neighbourLevels(0, -1) << EDGE_SHIFT_NORTH) | (neighbourLevels(-1, 0) << EDGE_SHIFT_WEST) |
               (neighbourLevels(1, 0) << EDGE_SHIFT_EAST);
    }

    void TerrainRenderer::collectTileMeshes(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, int meshResolutionCap, std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > >& tileMeshes) {
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
#if MASSIF_VT_RENDER_STATS
        // THE DEM'S OWN CEILING, once. Whether normalSampleDistance is reachable at all depends on
        // it, and inferring it from which zooms happen to appear in a short sample cannot tell "the
        // source stops here" from "that is as far as loading had got".
        if (elevationManager && elevationManager->getDataSource()) {
            static std::once_flag sourceZoomOnce;
            std::shared_ptr<TileDataSource> demSource = elevationManager->getDataSource();
            std::call_once(sourceZoomOnce, [&demSource]() {
                Log::Infof("TerrainRenderer: DEM source zoom range %d..%d", demSource->getMinZoom(), demSource->getMaxZoom());
            });
        }
#endif

        // Calculate visible terrain tiles. Through collectVisibleTiles, so the meshes are built for
        // the SAME budgeted cut every other pass of this frame walks.
        std::vector<MapTile> tiles;
        collectVisibleTiles(viewState, terrainOptions, tiles);

        float exaggeration = elevationManager->getExaggeration();
        int minZoom = terrainOptions->getMinZoom();
        int meshResolution = terrainOptions->getMeshResolution();
        if (meshResolutionCap > 0) {
            meshResolution = std::min(meshResolution, meshResolutionCap);
        }

        int meshCacheSize = terrainOptions->getMeshCacheSize();
        if (meshCacheSize <= 0) {
            meshCacheSize = MAX_CACHED_MESHES;
        }
        // Anything the worker finished since the last frame, swapped in before the cut is walked so
        // a refined tile uploads and draws in the same frame it lands.
        applyRefinedAttribs();
        unsigned int pass = ++_meshCacheClock;
        // Only where the app asked for it: stitching costs a mesh variant per edge combination, and
        // this renderer's surfaces are already skirted, so it is an improvement rather than a fix
        // for a hole. Same flag the draped path reads (TileRenderer).
        bool stitching = terrainOptions->isTileEdgeStitchingEnabled();
        // Built once for the whole cut, not per tile: the neighbour lookup is a membership test and
        // rebuilding the set inside the loop makes the pass quadratic in the tile count.
        std::set<long long> visibleTileIds;
        if (stitching) {
            for (const MapTile& visibleTile : tiles) {
                visibleTileIds.insert(visibleTile.getTileId());
            }
        }

        // EVERY TILE'S GRID FIRST, because a tile's edge mask depends on what its NEIGHBOURS
        // resolved and the loop below would only know about the tiles it had already reached. The
        // lookup is a cache read and the result is reused in the loop, so this costs one pass over
        // the cut rather than a second round of lookups.
        std::map<long long, std::shared_ptr<ElevationTileGrid> > tileGrids;
        std::map<long long, int> demZooms;
        for (const MapTile& tile : tiles) {
            if (tile.getZoom() < minZoom) {
                continue;
            }
            std::shared_ptr<ElevationTileGrid> grid = elevationManager->getTileGrid(tile, ElevationManager::LoadMode::CACHED_ONLY);
            if (grid) {
                tileGrids[tile.getTileId()] = grid;
                demZooms[tile.getTileId()] = grid->getTile().getZoom();
            }
        }

        tileMeshes.reserve(tiles.size());
        for (const MapTile& tile : tiles) {
            long long tileId = tile.getTileId();
            std::shared_ptr<ElevationTileGrid> grid;
            if (tile.getZoom() >= minZoom) {
                auto gridIt = tileGrids.find(tileId);
                grid = (gridIt != tileGrids.end() ? gridIt->second : std::shared_ptr<ElevationTileGrid>());
            }
            // The same condition ensureSurfaceAttribs uses for its fixed-scale path, so the mesh
            // density and the normal sampling agree about which regime they are in.
            bool fixedScaleNormals = terrainOptions->getNormalSampleDistance() > 0 && !_tileTransformer->isSpherical();
            // NO ELEVATION DATA, NO TILE.
            //
            // buildTileMesh starts from heights.assign(..., 0) and only fills them if it has a grid,
            // so a tile whose DEM is not cached is built as a FLAT PLANE AT SEA LEVEL and drawn as
            // if that were the ground. getTileGrid(CACHED_ONLY) already walks ancestors, so a null
            // here means nothing is cached for this tile or any parent - there is no height to draw,
            // only a guess of zero.
            //
            // In mountains that guess is a large false surface with perfectly flat normals, and
            // WHICH tiles are in that state depends on what has finished loading - so it differs on
            // every run at the same viewpoint, and the tile jumps from 0 m to its real elevation
            // when the DEM lands. Every per-tile property reads correct meanwhile (gridSize,
            // attribsRefined, demZoom and staleness are all right); the heights were simply absent.
            //
            // Skipping leaves a gap that fills in as data arrives, which is honest, instead of a
            // plane at the wrong altitude. Only where the panorama's fixed-scale normals are in use:
            // a draped map treats a missing DEM as flat ground by design, and this does not change it.
            if (fixedScaleNormals && !grid && tile.getZoom() >= minZoom) {
                continue;
            }
            int gridSize = calculateMeshGridSize(tile, grid, meshResolution, fixedScaleNormals);
#if MASSIF_VT_RENDER_STATS
            if (gridSize <= 1) { VT_STAT_INC(terrainMeshGrid1); }
            else if (gridSize <= 4) { VT_STAT_INC(terrainMeshGrid4); }
            else if (gridSize <= 16) { VT_STAT_INC(terrainMeshGrid16); }
            else if (gridSize <= 48) { VT_STAT_INC(terrainMeshGrid48); }
            else { VT_STAT_INC(terrainMeshGridFull); }
#endif
            int edgeMask = stitching ? calculateEdgeMask(tile, visibleTileIds, demZooms,
                                                         grid ? grid->getTile().getZoom() : -1) : 0;
            // The mask belongs to the mesh, so it belongs in the key: the same tile at the same
            // resolution is a different surface once a neighbour coarsens, and a pan changes that
            // without changing anything else.
            int cacheKey = gridSize | (edgeMask << 16);

            // Rebuild the mesh only when its inputs actually changed. This avoids rebuilding
            // every cached mesh each time a new elevation tile arrives during loading.
            auto it = _meshCache.find(std::make_pair(tileId, cacheKey));
            if (it == _meshCache.end() || it->second.grid != grid || it->second.exaggeration != exaggeration || it->second.gridSize != gridSize) {
                if (it == _meshCache.end() && static_cast<int>(_meshCache.size()) >= meshCacheSize) {
                    evictLeastRecentlyUsedMeshes(pass, meshCacheSize);
                }
                MeshCacheEntry entry;
                entry.grid = grid;
                entry.exaggeration = exaggeration;
                entry.gridSize = gridSize;
#if MASSIF_VT_RENDER_STATS
                auto buildStart = std::chrono::steady_clock::now();
#endif
                entry.mesh = buildTileMesh(tile, grid, elevationManager, gridSize, edgeMask);
                // CARRY THE REFINED NORMALS ACROSS THE REBUILD.
                //
                // A rebuild here is almost always "a finer DEM arrived for a tile already on
                // screen", and the new mesh starts with no attribs at all - so renderTiles bakes the
                // cheap mesh-gradient stand-in and queues the DEM-sampled ones. The tile therefore
                // LOSES the relief it was already drawing and gets it back a moment later, which
                // reads as shading dropping out of tiles as they come towards the middle of the view
                // (they are the ones the prefetch is feeding, so they are the ones that get a better
                // grid). Before the bake was split off the render thread this could not happen: the
                // rebuild paid the 14 ms and had the right normals immediately.
                //
                // The old ones are a good stand-in and a far better one than the mesh gradient: the
                // normals are sampled at a fixed GROUND distance, so they belong to the DEM rather
                // than to this mesh, and the grid nodes have not moved. Only the elevation component
                // is stale, by whatever the new DEM disagrees with the old about.
                //
                // attribsRefined stays FALSE, so the refine is still queued and the stale elevation
                // is corrected - this only removes the visible gap, it does not skip the work.
                // ANY cached variant of the SAME TILE will do, not just the one under this exact
                // key. The key carries the edge-stitching mask as well as the grid size, and that
                // mask is built from which NEIGHBOURS are in the visible cut - so simply turning the
                // camera remints the key for every tile whose neighbour set changed, and the lookup
                // above misses. The tile then gets a new mesh with no attribs and falls back to the
                // mesh-gradient stand-in, which is visibly flatter, and it flips back when the old
                // key comes round again. That is "some tiles render differently depending on which
                // way I am looking".
                //
                // The mask only moves the SKIRT and the edge nodes; the interior grid is the same
                // and the normals are sampled at a fixed ground distance anyway, so a sibling's
                // refined attribs are the right answer for all but the tile's outermost ring.
                // _meshCache is ordered by tile id first, so this is a short walk over one tile's
                // own variants rather than a scan.
                if (entry.mesh) {
                    std::size_t wanted = static_cast<std::size_t>(entry.mesh->vertices.size() / 3) * 4;
                    auto sibling = _meshCache.lower_bound(std::make_pair(tileId, std::numeric_limits<int>::min()));
                    bool carried = false;
                    bool sawSibling = false, sawRefinedSibling = false;
                    for (; sibling != _meshCache.end() && sibling->first.first == tileId; sibling++) {
                        if (!sibling->second.mesh) {
                            continue;
                        }
                        sawSibling = true;
                        if (sibling->second.mesh->attribsRefined) {
                            sawRefinedSibling = true;
                        }
                        if (sibling->second.mesh->attribsRefined &&
                            sibling->second.gridSize == gridSize &&
                            sibling->second.mesh->surfaceAttribs.size() == wanted) {
                            entry.mesh->surfaceAttribs = sibling->second.mesh->surfaceAttribs;
                            entry.mesh->surfaceAttribSampleDistance = sibling->second.mesh->surfaceAttribSampleDistance;
                            carried = true;
                            break;
                        }
                    }
#if MASSIF_VT_RENDER_STATS
                    // A MISS is a tile that will draw from the mesh-gradient stand-in until the
                    // worker gets to it - visibly flatter than its neighbours for that window.
                    // Split by CAUSE, because they do not have the same fix: a tile with no cached
                    // variant at all has nothing to carry and never will, while one whose sibling
                    // was refined at a different grid size is a carry this code chose to decline.
                    if (carried) {
                        VT_STAT_INC(terrainAttribCarryHit);
                    } else if (!sawSibling) {
                        VT_STAT_INC(terrainAttribCarryMissNew);
                    } else if (!sawRefinedSibling) {
                        VT_STAT_INC(terrainAttribCarryMissUnrefined);
                    } else {
                        VT_STAT_INC(terrainAttribCarryMissGrid);
                        // WARM-UP OR FLAP? The camera here is only ROTATING, so tile distances never
                        // change and a tile should settle on its grid size once. A sustained rate of
                        // these means it is instead oscillating - its elevation grid being evicted
                        // and re-resolved under it - which is a cache problem, not a carry problem,
                        // and has a far cheaper fix than reworking the carry.
                        static std::chrono::steady_clock::time_point lastGridLog;
                        std::chrono::steady_clock::time_point gridNow = std::chrono::steady_clock::now();
                        if (gridNow - lastGridLog > std::chrono::milliseconds(250)) {
                            lastGridLog = gridNow;
                            int wasGridSize = 0;
                            auto prev = _meshCache.lower_bound(std::make_pair(tileId, std::numeric_limits<int>::min()));
                            for (; prev != _meshCache.end() && prev->first.first == tileId; prev++) {
                                if (prev->second.mesh && prev->second.mesh->attribsRefined) {
                                    wasGridSize = prev->second.gridSize;
                                    break;
                                }
                            }
                            Log::Infof("TerrainRenderer: grid size changed for tile %d/%d/%d: %d -> %d (own DEM grid %s)",
                                       tile.getZoom(), tile.getX(), tile.getY(), wasGridSize, gridSize,
                                       grid ? "present" : "MISSING, standing on an ancestor");
                        }
                    }
#endif
                }
#if MASSIF_VT_RENDER_STATS
                VT_STAT_INC(terrainMeshBuilds);
                VT_STAT_ADD(terrainMeshBuildUs, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - buildStart).count());
                VT_STAT_ADD(terrainMeshVerts, (gridSize + 1) * (gridSize + 1));
#endif
                entry.lastUsed = pass;
                // How far the DEM under this tile was STRETCHED, and what mesh that bought. Two
                // neighbours at the same tile zoom can land on different numbers here - one resolved
                // its own elevation tile, the other fell back to a cached ancestor covering four or
                // sixteen times the ground - and then they are built from different height fields
                // and meet along an edge neither of them agrees about. That is a seam between tiles
                // of EQUAL zoom, which no LOD reasoning explains.
                if (TERRAIN_MESH_TRACE && grid) {
                    double tileSize = Const::WORLD_SIZE / (1 << tile.getZoom());
                    double gridWidth = grid->getInternalBounds().getMax().getX() - grid->getInternalBounds().getMin().getX();
                    Log::Infof("TerrainRenderer::collectTileMeshes: tile %d/%d/%d mesh %d, DEM %d texels stretched over %.2f tiles, edgeMask %d",
                               tile.getZoom(), tile.getX(), tile.getY(), gridSize, grid->getWidth(),
                               tileSize > 0 ? gridWidth / tileSize : 0.0, edgeMask);
                }
                it = _meshCache.insert_or_assign(std::make_pair(tileId, cacheKey), std::move(entry)).first;
            }
            else {
                VT_STAT_INC(terrainMeshCacheHits);
            }
            it->second.lastUsed = pass;
            // Every emitted tile, cached ones included - a cache hit is the common case, and the
            // debug views are useless if they only describe the tiles rebuilt this frame.
            if (it->second.mesh) {
                it->second.mesh->demZoom = (grid ? grid->getTile().getZoom() : -1);
#if MASSIF_VT_RENDER_STATS
                // STALE NORMALS: the attribs were baked from a coarser DEM than this tile resolves
                // NOW. ensureSurfaceAttribs early-returns once surfaceAttribSampleDistance matches,
                // so a tile refined while standing on an ancestor keeps those normals for good -
                // and gridSize, attribsRefined and demZoom all still read correct, which is why the
                // per-tile debug views showed nothing wrong. Which tiles lose that race is decided
                // by load order, so it changes every run: "same tile, different shading each time".
                int staleBy = (it->second.mesh->attribsDemZoom < 0 || it->second.mesh->demZoom < 0
                                   ? -1
                                   : it->second.mesh->demZoom - it->second.mesh->attribsDemZoom);
                if (staleBy <= 0) { VT_STAT_INC(terrainAttribStaleFresh); }
                else if (staleBy == 1) { VT_STAT_INC(terrainAttribStale1); }
                else if (staleBy == 2) { VT_STAT_INC(terrainAttribStale2); }
                else { VT_STAT_INC(terrainAttribStale3); }
#endif
            }
            tileMeshes.emplace_back(tile, it->second.mesh);
        }
    }

    bool TerrainRenderer::renderTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Shader>& shader, const std::function<void(const MapTile&)>& tileUniformsFn, int meshResolutionCap, bool surfaceAttribs, bool normalAttrib, bool skipSkirts) {
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

        // Per-tile properties for the debug views, on whichever pass is drawing. Set from the MESH,
        // so it says what this tile actually is rather than what the cut asked for.
        GLint uTileDebug = glGetUniformLocation(progId, "u_tileDebug");
        // Set here rather than only in renderSurface's callback: the normal/depth pass needs the
        // world position as well now, to find itself in the elevation texture.
        GLint uTileMatAll = glGetUniformLocation(progId, "u_tileMat");

        // The DEM texture this pass samples its normals from, per tile. Looked up once here: a
        // uniform that is not in the shader answers -1 and the whole block then costs nothing.
        GLint uDemTex = glGetUniformLocation(progId, "u_demTex");
        GLint uDemOriginSize = glGetUniformLocation(progId, "u_demOriginSize");
        GLint uDemInvTexSize = glGetUniformLocation(progId, "u_demInvTexSize");
        GLint uDemDecode = glGetUniformLocation(progId, "u_demDecode");
        GLint uDemDecodeOffset = glGetUniformLocation(progId, "u_demDecodeOffset");
        GLint uDemMetersPerTexel = glGetUniformLocation(progId, "u_demMetersPerTexel");
        GLint uDemMercatorYScale = glGetUniformLocation(progId, "u_demMercatorYScale");
        GLint uDemValid = glGetUniformLocation(progId, "u_demValid");
        GLint uDemNormalStep = glGetUniformLocation(progId, "u_demNormalStep");
        bool wantsDem = (uDemValid >= 0 && _elevationTextureCache);
        if (uDemTex >= 0) {
            glUniform1i(uDemTex, 7); // its own unit: the surface pass binds the drape on 0
        }
        if (uDemNormalStep >= 0) {
            glUniform1f(uDemNormalStep, terrainOptions->getNormalSampleDistance());
        }

        GLint aNormal = -1, aElevation = -1;
        std::shared_ptr<ElevationManager> elevationManager;
        // The normal-packing depth pass wants the same per-vertex attributes and not the rest of
        // what a surface pass is: it keeps u_far above, and its shader declares no a_elevation, so
        // the location comes back negative and the array is simply not enabled.
        if (surfaceAttribs || normalAttrib) {
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

            cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileMesh.first);
            cglib::mat4x4<float> tileMVPMat = cglib::mat4x4<float>::convert(mvpMat * tileMatrix);
            glUniformMatrix4fv(uMVPMat, 1, GL_FALSE, tileMVPMat.data());
            if (uTileMatAll >= 0) {
                cglib::mat4x4<float> tileMat = cglib::mat4x4<float>::convert(tileMatrix);
                glUniformMatrix4fv(uTileMatAll, 1, GL_FALSE, tileMat.data());
            }
            if (tileUniformsFn) {
                tileUniformsFn(tileMesh.first);
            }
            if (wantsDem) {
                // The grid may cover an ANCESTOR of this tile, which is why the uv transform comes
                // from the texture rather than from the tile: a tile standing on a z8 grid samples
                // one sixteenth of it, and the shader must be told which sixteenth.
                vt::GLTileRenderer::TerrainTexture demTexture;
                // internalSize is the one that was missing: a texture still being prepared can come
                // back with a zero world extent, and the shader's uv divide then goes infinite, so
                // the four taps sample nothing meaningful and the normal comes out near-horizontal.
                // That is the hard paper/shade blotching visible while the tiles load.
                // AND THE MESH MUST ALREADY CARRY ELEVATION. The texture arrives before the mesh is
                // rebuilt from the same grid, and a per-fragment normal on a mesh that is still flat
                // paints mountain relief onto flat ground - hard paper/shade blotches with the
                // geometry nowhere near them, which is what the load looks like. demZoom below zero
                // is a mesh with no grid behind it yet; until then the interpolated mesh normal is
                // the one that agrees with what is actually drawn.
                bool meshHasElevation = (mesh->demZoom >= 0);
                bool haveDem = meshHasElevation && _elevationTextureCache->getTexture(vt::TileId(tileMesh.first.getZoom(), tileMesh.first.getX(), tileMesh.first.getY()), demTexture)
                               && demTexture.textureId != 0 && demTexture.textureSize(0) > 0 && demTexture.textureSize(1) > 0
                               && demTexture.internalSize(0) > 0 && demTexture.internalSize(1) > 0
                               && demTexture.metersPerTexel > 0;
                // AND NO FINER THAN THE MESH'S OWN GRID. The texture for a tile arrives before the
                // mesh is rebuilt from the same level, so during a load the shading is routinely a
                // level or more ahead of the geometry - fine relief painted onto ground that has
                // not been displaced yet, which saturates into hard-edged patches sitting nowhere
                // near the shape under them. A texture covers a whole tile, so its level follows
                // from the ground it spans.
                if (haveDem && mesh->demZoom >= 0) {
                    double span = demTexture.internalSize(0);
                    int textureZoom = (span > 0 ? static_cast<int>(std::lround(std::log2(Const::WORLD_SIZE / span))) : -1);
                    if (textureZoom > mesh->demZoom) {
                        haveDem = false;
                    }
                }
                glUniform1f(uDemValid, haveDem ? 1.0f : 0.0f);
                if (haveDem) { VT_STAT_INC(terrainDemTextureHits); } else { VT_STAT_INC(terrainDemTextureMisses); }
                if (haveDem) {
                    glActiveTexture(GL_TEXTURE7);
                    glBindTexture(GL_TEXTURE_2D, demTexture.textureId);
                    glActiveTexture(GL_TEXTURE0);
                    if (uDemOriginSize >= 0) {
                        glUniform4f(uDemOriginSize,
                                    static_cast<float>(demTexture.internalOrigin(0)), static_cast<float>(demTexture.internalOrigin(1)),
                                    static_cast<float>(demTexture.internalSize(0)), static_cast<float>(demTexture.internalSize(1)));
                    }
                    if (uDemInvTexSize >= 0) {
                        glUniform2f(uDemInvTexSize, 1.0f / demTexture.textureSize(0), 1.0f / demTexture.textureSize(1));
                    }
                    if (uDemDecode >= 0) {
                        glUniform4f(uDemDecode, demTexture.decode(0), demTexture.decode(1), demTexture.decode(2), demTexture.decode(3));
                    }
                    if (uDemDecodeOffset >= 0) {
                        glUniform1f(uDemDecodeOffset, demTexture.decodeOffset);
                    }
                    if (uDemMetersPerTexel >= 0) {
                        glUniform1f(uDemMetersPerTexel, demTexture.metersPerTexel);
                    }
                    if (uDemMercatorYScale >= 0) {
                        glUniform1f(uDemMercatorYScale, demTexture.mercatorYScale);
                    }
                }
            }
            if (uTileDebug >= 0) {
                // .w was attribsDemZoom, which measured staleness - now proven zero on every tile, so
                // the slot is reused for TILE PARITY. Every per-tile property has come back uniform
                // while regions still differ, so the next thing to test is the assumption underneath
                // all of them: that those regions are tiles at all.
                int parity = ((tileMesh.first.getX() + tileMesh.first.getY()) & 1);
                glUniform4f(uTileDebug, static_cast<float>(mesh->gridSize), mesh->attribsRefined ? 1.0f : 0.0f, static_cast<float>(mesh->demZoom), static_cast<float>(parity));
            }
            // The mesh is cached and its geometry never changes, so it is uploaded once. Creating
            // the resource HERE means create() runs inline - this is the GL thread - and the
            // buffers exist by the time the upload below needs them.
            if (!mesh->buffer && glResourceManager) {
                mesh->buffer = glResourceManager->create<TerrainMeshBuffer>();
            }
            if (mesh->buffer && !mesh->buffer->hasGeometry()) {
                mesh->buffer->uploadGeometry(mesh->vertices, mesh->indices);
            }
            if ((surfaceAttribs || normalAttrib) && elevationManager) {
                float normalSampleDistance = terrainOptions->getNormalSampleDistance();
#if MASSIF_VT_RENDER_STATS
                auto attribStart = std::chrono::steady_clock::now();
                bool attribMiss = mesh->surfaceAttribs.empty() || mesh->surfaceAttribSampleDistance != normalSampleDistance;
#endif
                // INLINE, but only the cheap half: the mesh-gradient normals, which are pure
                // arithmetic over heights this mesh already holds. The tile therefore draws
                // correctly the frame its mesh is built, with no DEM reads on the render thread.
                bool wasEmpty = mesh->surfaceAttribs.empty();
                ensureSurfaceAttribs(tileMesh.first, elevationManager, *mesh, normalSampleDistance, false);
                if (wasEmpty && !mesh->surfaceAttribs.empty()) {
                    mesh->attribsRefined = false;
                }
#if MASSIF_VT_RENDER_STATS
                if (attribMiss) {
                    VT_STAT_INC(terrainAttribBakes);
                    VT_STAT_ADD(terrainAttribUs, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - attribStart).count());
                }
#endif
                // ...and the DEM-sampled ones on the worker. They replace the stand-in when they
                // land, which is what makes neighbouring tiles at different LOD agree about their
                // normals - the gradient becomes a property of the DEM instead of of the mesh.
                // ...and again whenever the bake was PROVISIONAL and elevation data has landed since.
                // Without this the first bake wins for good, and since it reads the DEM cached-only,
                // what it finds there is decided by arrival order: half the normals on screen were
                // measured on an ancestor grid, a different half each run. That is the whole of "the
                // same tile is shaded differently every time I open the panorama".
                //
                // It terminates: a re-bake only happens when the elevation version has MOVED, the
                // version stops moving when loading settles, and the count is capped besides - a
                // tile over ground the source genuinely has no data for would otherwise retry for
                // ever, since its reads can never resolve at the zoom asked for.
                bool attribsStale = mesh->attribsRefined && mesh->attribsProvisional &&
                                    mesh->attribsRebakes < MAX_ATTRIB_REBAKES &&
                                    mesh->attribsDataVersion != elevationManager->getDataVersion();
                if (normalSampleDistance > 0 && (!mesh->attribsRefined || attribsStale) &&
                    !mesh->attribsPending && !_tileTransformer->isSpherical()) {
#if MASSIF_VT_RENDER_STATS
                    if (attribsStale) { VT_STAT_INC(terrainAttribRebakes); }
#endif
                    queueAttribRefine(tileMesh.first, elevationManager, mesh, normalSampleDistance);
                }
                if (mesh->buffer && mesh->uploadedAttribSampleDistance != mesh->surfaceAttribSampleDistance) {
#if MASSIF_VT_RENDER_STATS
                    // COUNTED HERE, not inside the bake: the first version of this stat ran halfway
                    // through ensureSurfaceAttribs, before the skirt fill, and so reported every
                    // skirt vertex as a zero normal every time. It measured the middle of a
                    // function rather than what the GPU is handed. This is the buffer GL gets.
                    {
                        std::size_t gridVerts = static_cast<std::size_t>(mesh->gridSize + 1) * (mesh->gridSize + 1);
                        long long zeroGrid = 0, zeroSkirt = 0;
                        for (std::size_t i = 0; i * 4 + 2 < mesh->surfaceAttribs.size(); i++) {
                            const float* normal = &mesh->surfaceAttribs[i * 4];
                            if (std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]) <= 0.0001f) {
                                if (i < gridVerts) { zeroGrid++; } else { zeroSkirt++; }
                            }
                        }
                        VT_STAT_ADD(terrainAttribZeroGrid, zeroGrid);
                        VT_STAT_ADD(terrainAttribZeroSkirt, zeroSkirt);
                    }
#endif
                    mesh->buffer->uploadAttribs(mesh->surfaceAttribs);
                    mesh->uploadedAttribSampleDistance = mesh->surfaceAttribSampleDistance;
                }
                if (mesh->buffer && mesh->buffer->hasAttribs()) {
                    glBindBuffer(GL_ARRAY_BUFFER, mesh->buffer->getAttribVBO());
                    if (aNormal >= 0) {
                        glVertexAttribPointer(aNormal, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
                    }
                    if (aElevation >= 0) {
                        glVertexAttribPointer(aElevation, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const GLvoid*>(3 * sizeof(float)));
                    }
                } else {
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    if (aNormal >= 0) {
                        glVertexAttribPointer(aNormal, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), mesh->surfaceAttribs.data());
                    }
                    if (aElevation >= 0) {
                        glVertexAttribPointer(aElevation, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), mesh->surfaceAttribs.data() + 3);
                    }
                }
            }
            std::size_t drawCount = (skipSkirts && mesh->gridIndexCount > 0 ? mesh->gridIndexCount : mesh->indices.size());
            if (mesh->buffer && mesh->buffer->hasGeometry()) {
                glBindBuffer(GL_ARRAY_BUFFER, mesh->buffer->getVertexVBO());
                glVertexAttribPointer(aCoord, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->buffer->getIndexVBO());
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(drawCount), GL_UNSIGNED_SHORT, nullptr);
            } else {
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glVertexAttribPointer(aCoord, 3, GL_FLOAT, GL_FALSE, 0, mesh->vertices.data());
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(drawCount), GL_UNSIGNED_SHORT, mesh->indices.data());
            }
        }

        // Unbound before anything else draws: the rest of the renderer still uses client-side
        // arrays in places, and a bound ARRAY_BUFFER silently reinterprets their pointers as offsets.
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        if (aNormal >= 0) {
            glDisableVertexAttribArray(aNormal);
        }
        if (aElevation >= 0) {
            glDisableVertexAttribArray(aElevation);
        }
        glDisableVertexAttribArray(aCoord);
        return true;
    }

    void TerrainRenderer::startAttribWorker() {
        if (_attribWorker.joinable()) {
            return;
        }
        _attribWorkerStop.store(false);
        _attribWorker = std::thread([this]() {
            for (;;) {
                AttribJob job;
                {
                    std::unique_lock<std::mutex> lock(_attribMutex);
                    _attribCondition.wait(lock, [this]() { return _attribWorkerStop.load() || !_attribJobs.empty(); });
                    if (_attribWorkerStop.load()) {
                        return;
                    }
                    // NEWEST FIRST. The queue is a record of where the camera HAS been, and while it
                    // drains the camera keeps moving - so the front of it is the least likely tile to
                    // still be on screen. Taking the back means a pan is refined from where the eye
                    // is now outwards.
                    job = std::move(_attribJobs.back());
                    _attribJobs.pop_back();
                }
                if (!job.mesh || !job.elevationManager) {
                    continue;
                }
                // Into a COPY: the render thread is drawing out of job.mesh->surfaceAttribs right
                // now. Only the vectors the mesh build filled are read here, and those never change
                // after it - the mesh is replaced rather than edited when its inputs do.
                TileMesh scratch;
                scratch.vertices = job.mesh->vertices;
                scratch.heights = job.mesh->heights;
                scratch.skirtSources = job.mesh->skirtSources;
                scratch.gridSize = job.mesh->gridSize;
#if MASSIF_VT_RENDER_STATS
                auto refineStart = std::chrono::steady_clock::now();
#endif
                ensureSurfaceAttribs(job.tile, job.elevationManager, scratch, job.normalSampleDistance, true);
#if MASSIF_VT_RENDER_STATS
                VT_STAT_INC(terrainAttribRefines);
                VT_STAT_ADD(terrainAttribRefineUs, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - refineStart).count());
#endif
                if (scratch.surfaceAttribs.empty()) {
                    continue;
                }
                std::lock_guard<std::mutex> lock(_attribMutex);
                _attribResults.push_back(AttribResult { job.mesh, std::move(scratch.surfaceAttribs), job.normalSampleDistance,
                                                       scratch.attribsDemZoom, scratch.attribsProvisional,
                                                       scratch.attribsWorstZoom, scratch.attribsDataVersion });
            }
        });
    }

    void TerrainRenderer::stopAttribWorker() {
        if (!_attribWorker.joinable()) {
            return;
        }
        _attribWorkerStop.store(true);
        _attribCondition.notify_all();
        _attribWorker.join();
        std::lock_guard<std::mutex> lock(_attribMutex);
        _attribJobs.clear();
        _attribResults.clear();
    }

    void TerrainRenderer::queueAttribRefine(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, const std::shared_ptr<TileMesh>& mesh, float normalSampleDistance) {
        startAttribWorker();
        {
            std::lock_guard<std::mutex> lock(_attribMutex);
            // Bounded, and the OLDEST go: see the worker for why the newest job is the useful one.
            while (_attribJobs.size() >= MAX_PENDING_ATTRIB_JOBS) {
                _attribJobs.front().mesh->attribsPending = false;
                _attribJobs.pop_front();
            }
            _attribJobs.push_back(AttribJob { tile, elevationManager, mesh, normalSampleDistance });
        }
        // Set on the render thread, which is the only thread that reads it - the worker never
        // touches the mesh's own fields.
        mesh->attribsPending = true;
        _attribCondition.notify_one();
    }

    void TerrainRenderer::applyRefinedAttribs() {
        std::vector<AttribResult> results;
        {
            std::lock_guard<std::mutex> lock(_attribMutex);
            if (_attribResults.empty()) {
                return;
            }
            results.swap(_attribResults);
        }
        for (AttribResult& result : results) {
            if (!result.mesh) {
                continue;
            }
            result.mesh->attribsPending = false;
            // Dropped if the mesh has been re-baked at a different sample distance meanwhile: the
            // job answered a question nobody is asking any more.
            if (result.attribs.size() != result.mesh->surfaceAttribs.size()) {
                continue;
            }
            result.mesh->surfaceAttribs = std::move(result.attribs);
            result.mesh->surfaceAttribSampleDistance = result.normalSampleDistance;
            result.mesh->attribsRefined = true;
            result.mesh->attribsDemZoom = result.attribsDemZoom;
            result.mesh->attribsProvisional = result.attribsProvisional;
            result.mesh->attribsWorstZoom = result.attribsWorstZoom;
            result.mesh->attribsDataVersion = result.attribsDataVersion;
            if (result.attribsProvisional) {
                result.mesh->attribsRebakes++;
            }
            // Forces the re-upload in renderTiles, which compares these two.
            result.mesh->uploadedAttribSampleDistance = result.normalSampleDistance - 1.0f;
            // ...and the PACKED texture has to be redrawn, or the post-process goes on reading the
            // normals this mesh was first drawn with. Nothing else in its cache key moves here.
            _depthTextureAttribsDirty = true;
        }
    }

    void TerrainRenderer::ensureSurfaceAttribs(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, TileMesh& mesh, float normalSampleDistance, bool allowFixedScale) const {
        std::size_t vertexCount = mesh.vertices.size() / 3;
        if (vertexCount == 0 || mesh.gridSize < 1) {
            return;
        }
        if (!mesh.surfaceAttribs.empty() && mesh.surfaceAttribSampleDistance == normalSampleDistance) {
            return;
        }

        int gridSize = mesh.gridSize;
        int rowSize = gridSize + 1;
        int tileMask = (1 << tile.getZoom()) - 1;
        double zoomScale = 1.0 / (1 << tile.getZoom());
        double originX = (tile.getX() * zoomScale - 0.5) * Const::WORLD_SIZE;
        double originY = ((tileMask - tile.getY()) * zoomScale - 0.5) * Const::WORLD_SIZE;
        double size = zoomScale * Const::WORLD_SIZE;
        float exaggeration = elevationManager->getExaggeration();

        // On a sphere the tile-local axes are not the world's, so the slope normal has to be
        // rotated into the local east/north/up frame. On a plane that frame IS the identity, and
        // the branch below keeps the exact expression it always had.
        bool spherical = _tileTransformer->isSpherical();

        // The tile-local frame scales x, y and z by the same factor (calculateTileMatrix), so a
        // normal built from the local height field is already a world-space direction.
        //
        // The central difference at a tile EDGE reaches past this mesh, and the node it wants is the
        // neighbour's. It used to clamp to this tile's own edge node instead, which halves the
        // gradient there and leaves every tile boundary carrying a normal that disagrees with the
        // one a pixel away - a seam, one vertex wide on each side of every tile. Invisible while
        // nothing read the normals across a pixel; a drawn line as soon as something does
        // (PostProcessEffect::setTerrainNormalsRequired). The height comes from the elevation
        // manager, which is where buildTileMesh got this tile's own from, so the two agree by
        // construction. CACHED only - a blocking load per edge vertex is not payable - and the old
        // clamp is the fallback where the neighbour's DEM has not arrived.
        auto localZ = [&](int gx, int gy) -> float {
            if (gx >= 0 && gx <= gridSize && gy >= 0 && gy <= gridSize) {
                return mesh.heights[gy * rowSize + gx];
            }
            double nodeY = originY + (static_cast<double>(gy) / gridSize) * size;
            double internalHeight = 0;
            if (elevationManager->getDisplayHeightCached(originX + (static_cast<double>(gx) / gridSize) * size, nodeY, internalHeight)) {
                double localPerInternal = (spherical ? sphericalLocalPerInternal(tile, nodeY) : 1.0 / size);
                return static_cast<float>(internalHeight * localPerInternal);
            }
            int clampedX = std::min(std::max(gx, 0), gridSize);
            int clampedY = std::min(std::max(gy, 0), gridSize);
            return mesh.heights[clampedY * rowSize + clampedX];
        };

        std::shared_ptr<const vt::TileTransformer::VertexTransformer> vertexTransformer;
        if (spherical) {
            vertexTransformer = _tileTransformer->createTileVertexTransformer(vt::TileId(tile.getZoom(), tile.getX(), tile.getY()));
        }

        // A gradient measured over the MESH is measured over a step that halves with every zoom
        // level, because a tile carries the same number of cells whatever ground it covers. Two
        // tiles meeting at an LOD boundary therefore smooth the same hillside by different amounts,
        // and their normals disagree along the whole shared edge - which is a drawn line as soon as
        // anything reads the normals across a pixel, and no edge lookup can fix it: the heights
        // agree, the SCALE does not.
        //
        // Sampling at a fixed ground distance instead makes the normal a property of the DEM rather
        // than of the mesh, so both sides of a boundary return the same value and there is nothing
        // to draw. It is what peakfinder.com gets for free by having no tiles in its geometry at all
        // (one panorama mesh over a DEM texture array). The detail is still bounded by the DEM that
        // is loaded out there - but it is bounded CONTINUOUSLY.
        //
        // 0 keeps the mesh-derived gradient, which is what a draped 2D/3D map wants: there the
        // normals are shaded, never differentiated, and the mesh step is both cheaper and sharper.
        bool fixedScale = allowFixedScale && normalSampleDistance > 0 && !spherical;
        // The DEM is read CACHED_ONLY, and a panorama asks for ground a hundred kilometres out that
        // no grid covers yet - so the fixed-scale path can silently decline for most of a mesh and
        // leave the mesh gradient, and the seam, exactly where they were. Counted so that is visible
        // rather than guessed at.
        int fixedScaleVertices = 0;
        // The coarsest grid any stencil read fell back to, over the whole tile. -1 until one answers.
        int worstStencilZoom = -1;
        // The DEM texel under this tile, in internal units: what the central difference below may
        // not go finer than. Taken from the grid the tile actually resolved, which is the one that
        // decides how much detail there is to read - an ancestor stretched over sixteen tiles
        // answers at a sixteenth of its nominal resolution.
        double gridTexelInternal = 0;
        int resolvedGridZoom = -1;
        std::shared_ptr<ElevationTileGrid> attribGrid = elevationManager->getTileGrid(tile, ElevationManager::LoadMode::CACHED_ONLY);
        if (attribGrid) {
            double gridSpan = attribGrid->getInternalBounds().getMax().getX() - attribGrid->getInternalBounds().getMin().getX();
            if (attribGrid->getWidth() > 1 && gridSpan > 0) {
                gridTexelInternal = gridSpan / (attribGrid->getWidth() - 1);
            }
            resolvedGridZoom = attribGrid->getTile().getZoom();
        }
#if MASSIF_VT_RENDER_STATS
        // HOW MUCH COARSER THAN ASKED this tile's normals actually are. The step below can never go
        // finer than the DEM texel under the tile, so a tile standing on a stretched ANCESTOR gets
        // smoother normals than its neighbour that resolved its own grid - less turn across a pixel,
        // less ridge ink, a visibly LIGHTER tile right beside a fully drawn one. This is the number
        // behind "why are they not the same LOD as the tiles next to them"; the mesh cut cannot
        // explain it, because its zoom cap is global.
        if (fixedScale && gridTexelInternal > 0) {
            double centreScale = elevationManager->getDisplayScale(originY + size * 0.5);
            double asked = normalSampleDistance * centreScale;
            double stretch = (asked > 0 ? gridTexelInternal / asked : 0);
            if (stretch <= 1.01) { VT_STAT_INC(terrainAttribStretch1); }
            else if (stretch <= 2.01) { VT_STAT_INC(terrainAttribStretch2); }
            else if (stretch <= 4.01) { VT_STAT_INC(terrainAttribStretch4); }
            else { VT_STAT_INC(terrainAttribStretchBig); }
            // WHICH tiles are coarse decides the fix, and the totals cannot say. A far tile is drawn
            // from a low-zoom DEM whose texels are kilometres wide whatever the cache holds - nothing
            // to fix there. A tile at the cut's FINEST zoom coming back stretched means it is
            // standing on an ancestor it should not be, which is the prefetch/cache and IS fixable.
            if (stretch > 2.01) {
                static std::chrono::steady_clock::time_point lastStretchLog;
                std::chrono::steady_clock::time_point stretchNow = std::chrono::steady_clock::now();
                if (stretchNow - lastStretchLog > std::chrono::milliseconds(300)) {
                    lastStretchLog = stretchNow;
                    // resolvedGridZoom is the DEM tile it ACTUALLY got. Well below the requested
                    // zoom over a whole area means the source has no data there and the pin in
                    // ElevationManager is telling the truth; scattered means it is a cache/prefetch
                    // failure that the pin then makes permanent.
                    // THREE zooms, because they are three different things and only their gaps say
                    // where the detail is lost: the terrain tile, the DEM tile actually REQUESTED
                    // for it (clampTileZoom walks up by the source's grid-size hint, then the
                    // dataMaxZoom cap), and the one that came back. requested < terrain means the
                    // detail was never asked for; resolved < requested means it was asked for and
                    // has not arrived.
                    Log::Infof("TerrainRenderer: DEM coarser than asked for tile z%d %d/%d: asked %.0f m, DEM texel %.0f m (%.1fx), gridSize %d, DEM requested z%d resolved z%d (sampling is still the asked step)",
                               tile.getZoom(), tile.getX(), tile.getY(), normalSampleDistance,
                               normalSampleDistance * stretch, stretch, gridSize,
                               elevationManager->getDataTile(tile).getZoom(), resolvedGridZoom);
                }
            }
        }
#endif
        mesh.surfaceAttribs.resize(vertexCount * 4);
        mesh.surfaceAttribSampleDistance = normalSampleDistance;
        mesh.attribsDemZoom = resolvedGridZoom;
        for (int gy = 0; gy <= gridSize; gy++) {
            double internalY = originY + (static_cast<double>(gy) / gridSize) * size;
            double displayScale = elevationManager->getDisplayScale(internalY);
            double metersPerLocalZ = (exaggeration > 0 && displayScale > 0 ? size / (exaggeration * displayScale) : 0);
            // Metres into internal units at this latitude, Mercator stretch included. The gradient
            // below is internal z over internal x, which is the same dimensionless slope the local
            // frame's one is - the tile scales all three axes alike.
            // NEVER FINER THAN THE DEM'S OWN TEXEL, which is what left whole tiles perfectly flat.
            //
            // The four reads below are a central difference at +/- this step. If the step lands
            // inside ONE texel of the grid the tile is standing on, all four return the same height,
            // the gradient is exactly zero and the normal comes out exactly vertical - so the tile
            // draws with no slope shading and no ridge ink at all, only its silhouette. A tile that
            // resolved its own elevation tile has texels of tens of metres and 90 m clears them
            // easily; one standing on a cached ANCESTOR covering four or sixteen times the ground
            // has texels of hundreds of metres, and 90 m falls inside one.
            //
            // Which tiles those are depends on what the prefetch has fed, so it changes as the
            // camera moves - hence "some tiles are flat, and which ones depends on orientation".
            //
            // Sampling at the grid's own resolution instead gives a real gradient. It is coarser
            // than asked for, but it is the finest the data under that tile can actually answer.
            // THE CLAMP IS A PER-VERTEX FALLBACK, NOT A PER-TILE RULE.
            //
            // Raising the step to the tile's own texel for the WHOLE tile makes the sampling rate a
            // property of the grid that tile happens to be standing on - which is exactly the
            // per-tile dependence fixed-scale normals exist to remove. At an LOD boundary a z9 tile
            // sampled at 210 m against its z8 neighbour's 419 m, so the two smoothed the same
            // hillside by different amounts and inked differently along the whole shared edge. The
            // guard defeated the feature it was guarding.
            //
            // It also binds when nothing is wrong: 90 m is under the texel of every FAR tile, so the
            // clamp was active on 82% of the cut (measured 1x=23 2x=24 4x=17 8x+=62) and the fixed
            // scale was effectively never in use.
            //
            // Both DEM sampling paths are BILINEAR (ElevationNodeField::sample, and
            // ElevationTileGrid::sampleHeight), so a sub-texel central difference returns the cell's
            // own gradient rather than zero - the flat tiles this clamp was added for cannot come
            // from step size alone. Where a vertex does still come back exactly flat, it retries at
            // the texel below, which keeps that fix without spending it on the whole tile.
            double stepInternal = 0;
            double texelStepInternal = 0;
            if (fixedScale) {
                stepInternal = normalSampleDistance * displayScale;
                if (gridTexelInternal > stepInternal) {
                    texelStepInternal = gridTexelInternal;
                }
            }
            // THIS TILE'S OWN GRID, not a lookup by position.
            //
            // getDisplayHeightCached resolves the point to a tile at the source's MAX zoom and then
            // accepts any cached ancestor of it. Thirty kilometres out no z12 elevation tile is ever
            // loaded, so every read there walks up to whichever ancestor the prefetch happens to have
            // fetched - z8, z9 or z10, differing between runs and between neighbouring vertices. The
            // normals were therefore a function of what the cache held at the instant of the bake,
            // which is exactly the reported fault: the same tile, at the same zoom and the same
            // distance, shaded differently on each opening. Measured at 1.1 M ancestor fallbacks
            // against 1.1 M exact reads in one panorama.
            //
            // Reading the grid the tile is standing on makes the normal a function of (tile, grid)
            // alone, so it is the same on every run. The step can reach past that grid near an edge;
            // clamping there costs a bounded flattening on the outermost vertex, where the old code
            // paid an unbounded and random one over the whole tile. Cross-tile agreement is kept by
            // the step being a fixed GROUND distance, which is what the seam fix turned on.
            auto internalHeightAt = [&](double internalX, double internalYAt, double& height) {
                if (!attribGrid) {
                    return false;
                }
                const MapBounds& gridBounds = attribGrid->getInternalBounds();
                double sampleX = std::min(std::max(internalX, gridBounds.getMin().getX()), gridBounds.getMax().getX());
                double sampleY = std::min(std::max(internalYAt, gridBounds.getMin().getY()), gridBounds.getMax().getY());
                height = attribGrid->sampleNodeHeight(sampleX, sampleY) * exaggeration *
                         elevationManager->getDisplayScale(sampleY);
                worstStencilZoom = attribGrid->getTile().getZoom();
                return true;
            };
            for (int gx = 0; gx <= gridSize; gx++) {
                float dzdx = (localZ(gx + 1, gy) - localZ(gx - 1, gy)) * 0.5f * gridSize;
                float dzdy = (localZ(gx, gy + 1) - localZ(gx, gy - 1)) * 0.5f * gridSize;
                if (stepInternal > 0) {
                    double internalX = originX + (static_cast<double>(gx) / gridSize) * size;
                    double west = 0, east = 0, south = 0, north = 0;
                    // All four or none: a half-resolved stencil is a gradient measured over the
                    // wrong baseline, which is the artefact this exists to remove.
                    if (internalHeightAt(internalX - stepInternal, internalY, west) && internalHeightAt(internalX + stepInternal, internalY, east) &&
                        internalHeightAt(internalX, internalY - stepInternal, south) && internalHeightAt(internalX, internalY + stepInternal, north)) {
                        dzdx = static_cast<float>((east - west) / (2 * stepInternal));
                        dzdy = static_cast<float>((north - south) / (2 * stepInternal));
                        // Counted only when the stencil actually RESOLVED A SLOPE. Counting the
                        // lookups succeeding instead reported 99.8% while whole tiles were coming
                        // out perfectly flat - the reads worked, they just all landed in the same
                        // DEM texel and returned the same height. A stat that cannot distinguish
                        // "answered" from "answered zero" confirmed the wrong thing.
                        if (dzdx != 0.0f || dzdy != 0.0f) {
                            fixedScaleVertices++;
                        } else if (texelStepInternal > 0) {
                            // Exactly flat at the asked-for step, and the grid under this vertex is
                            // coarser than that step: retry once at the texel. Per vertex and only
                            // where it actually came back flat, so a tile keeps the uniform sampling
                            // rate everywhere else and only the degenerate spots pay.
                            if (internalHeightAt(internalX - texelStepInternal, internalY, west) && internalHeightAt(internalX + texelStepInternal, internalY, east) &&
                                internalHeightAt(internalX, internalY - texelStepInternal, south) && internalHeightAt(internalX, internalY + texelStepInternal, north)) {
                                dzdx = static_cast<float>((east - west) / (2 * texelStepInternal));
                                dzdy = static_cast<float>((north - south) / (2 * texelStepInternal));
                                if (dzdx != 0.0f || dzdy != 0.0f) {
                                    fixedScaleVertices++;
                                }
                                VT_STAT_INC(terrainAttribTexelRetry);
                            }
                        }
                    }
                }
                cglib::vec3<float> normal = cglib::unit(cglib::vec3<float>(-dzdx, -dzdy, 1.0f));
                if (spherical) {
                    cglib::vec2<float> tilePos(static_cast<float>(gx) / gridSize, 1.0f - static_cast<float>(gy) / gridSize);
                    cglib::vec3<float> east = cglib::unit(vertexTransformer->calculateVector(tilePos, cglib::vec2<float>(1, 0)));
                    cglib::vec3<float> north = cglib::unit(vertexTransformer->calculateVector(tilePos, cglib::vec2<float>(0, -1)));
                    cglib::vec3<float> up = cglib::unit(vertexTransformer->calculateNormal(tilePos));
                    normal = cglib::unit(east * normal(0) + north * normal(1) + up * normal(2));
                }
                std::size_t offset = static_cast<std::size_t>(gy * rowSize + gx) * 4;
                mesh.surfaceAttribs[offset + 0] = normal(0);
                mesh.surfaceAttribs[offset + 1] = normal(1);
                mesh.surfaceAttribs[offset + 2] = normal(2);
                mesh.surfaceAttribs[offset + 3] = static_cast<float>(localZ(gx, gy) * metersPerLocalZ);
            }
        }

        // PROVISIONAL: at least one stencil read was answered by a grid coarser than the source
        // could have given for this tile, so these normals are smoother than the data allows and
        // have to be rebuilt once the missing grids land. Measured at 1.1 M ancestor fallbacks
        // against 1.1 M exact reads in a single panorama - half of every normal on screen.
        mesh.attribsWorstZoom = worstStencilZoom;
        mesh.attribsProvisional = false;
        if (fixedScale && worstStencilZoom >= 0) {
            mesh.attribsProvisional = (worstStencilZoom < elevationManager->getDataTile(tile).getZoom());
        }
        mesh.attribsDataVersion = elevationManager->getDataVersion();

#if MASSIF_VT_RENDER_STATS
        if (fixedScale) {
            VT_STAT_ADD(terrainAttribFixedVerts, fixedScaleVertices);
            VT_STAT_ADD(terrainAttribTotalVerts, static_cast<long long>(gridSize + 1) * (gridSize + 1));
            if (mesh.attribsProvisional) { VT_STAT_INC(terrainAttribProvisional); }
            else { VT_STAT_INC(terrainAttribFinal); }
        }
#endif
        if (TERRAIN_MESH_TRACE && fixedScale) {
            int gridVertexCount = (gridSize + 1) * (gridSize + 1);
            Log::Infof("TerrainRenderer::ensureSurfaceAttribs: tile %d/%d/%d took the DEM gradient for %d of %d vertices (%.0f m step)",
                       tile.getZoom(), tile.getX(), tile.getY(), fixedScaleVertices, gridVertexCount, normalSampleDistance);
        }

#if MASSIF_VT_RENDER_STATS
        // WHICH TILES BAKED FLAT, BY NAME. Every per-tile counter so far describes ONE run and they
        // all come back uniform - but the complaint is that the SAME tile, at the same zoom and
        // distance, renders differently on different runs. Terrain does not change between runs, so
        // that is non-determinism, and a per-run snapshot cannot see it. Naming the flat tiles lets
        // two runs be compared: if the SET changes, the bake depends on load timing rather than on
        // the ground, and these tile ids say exactly which ones to chase.
        {
            double slopeSum = 0;
            std::size_t gridNodes = static_cast<std::size_t>(rowSize) * rowSize;
            for (std::size_t i = 0; i < gridNodes; i++) {
                const float* normal = &mesh.surfaceAttribs[i * 4];
                slopeSum += std::sqrt(normal[0] * normal[0] + normal[1] * normal[1]);
            }
            double meanSlope = (gridNodes > 0 ? slopeSum / gridNodes : 0);
            if (meanSlope < 0.02) {
                Log::Infof("TerrainRenderer: BAKED FLAT tile %d/%d/%d mean slope %.4f, gridSize %d, DEM z%d, step %.0f m",
                           tile.getZoom(), tile.getX(), tile.getY(), meanSlope, gridSize, resolvedGridZoom, normalSampleDistance);
            }
        }
#endif

        // Skirt vertices duplicate a grid vertex's x/y at a lower z: give them that vertex's
        // values, so the crack-filling walls shade like the edge they hang from instead of
        // showing up as flat-lit bands.
        std::size_t gridVertices = static_cast<std::size_t>(rowSize) * rowSize;
        if (vertexCount > gridVertices && mesh.skirtSources.size() < vertexCount - gridVertices) {
            static std::chrono::steady_clock::time_point lastSkirtLog;
            std::chrono::steady_clock::time_point skirtNow = std::chrono::steady_clock::now();
            if (skirtNow - lastSkirtLog > std::chrono::seconds(1)) {
                lastSkirtLog = skirtNow;
                Log::Infof("TerrainRenderer::ensureSurfaceAttribs: SKIRT SOURCES SHORT - tile %d/%d/%d gridSize %d, vertices %zu, grid %zu, skirts %zu, sources %zu",
                           tile.getZoom(), tile.getX(), tile.getY(), gridSize,
                           vertexCount, gridVertices, vertexCount - gridVertices, mesh.skirtSources.size());
            }
        }
        for (std::size_t i = gridVertices; i < vertexCount; i++) {
            std::size_t skirtIndex = i - gridVertices;
            if (skirtIndex >= mesh.skirtSources.size()) {
                break;
            }
            std::size_t source = static_cast<std::size_t>(mesh.skirtSources[skirtIndex]) * 4;
            if (source + 4 > mesh.surfaceAttribs.size()) {
                // The OTHER way a skirt keeps its zero-filled default, and the one the first
                // diagnostic here did not cover: a source index past the end of the attribs.
                VT_STAT_INC(terrainAttribSkirtOutOfRange);
                continue;
            }
            std::copy(mesh.surfaceAttribs.begin() + source, mesh.surfaceAttribs.begin() + source + 4, mesh.surfaceAttribs.begin() + i * 4);
            // MARKED AS A SKIRT, in the sign of z. A height field's normal always points up, so a
            // negative z is unreachable for real ground and costs no extra attribute to carry.
            //
            // The shader needs to know because a skirt is a VERTICAL wall: every fragment down one
            // of its columns has the same ground position, so a normal sampled per fragment from the
            // elevation texture is constant down the column and jumps between columns - the wall
            // comes out in vertical black and white bands. A skirt has to keep the normal of the
            // edge it hangs from, which is what was copied above.
            mesh.surfaceAttribs[i * 4 + 2] = -std::abs(mesh.surfaceAttribs[i * 4 + 2]);
        }
    }

    void TerrainRenderer::calculateVisibleTiles(const ViewState& viewState, const std::shared_ptr<ElevationManager>& elevationManager, const MapTile& tile, int maxZoom, std::vector<MapTile>& tiles) const {
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

        // The tile's own frame, so the box and the LOD centre follow the surface. On a plane the
        // transformer reproduces the flat box exactly: its bbox is the unit square through the tile
        // matrix, and one internal unit of height is one world unit.
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        cglib::mat4x4<double> tileMatrix = _tileTransformer->calculateTileMatrix(vtTileId, 1.0f);
        std::shared_ptr<const vt::TileTransformer::VertexTransformer> vertexTransformer = _tileTransformer->createTileVertexTransformer(vtTileId);
        cglib::vec2<float> centre(0.5f, 0.5f);
        double worldPerInternal = tileMatrix(0, 0) * (_tileTransformer->isSpherical() ? sphericalLocalPerInternal(tile, minY + size * 0.5) : (1 << tile.getZoom()) / static_cast<double>(Const::WORLD_SIZE));

        cglib::bbox3<double> tileBounds = _tileTransformer->calculateTileBBox(vtTileId);
        cglib::vec3<double> up = cglib::vec3<double>::convert(cglib::unit(vertexTransformer->calculateNormal(centre)));
        for (double height : { minZ, maxZ }) {
            cglib::vec3<double> offset = up * (height * worldPerInternal);
            tileBounds.add(tileBounds.min + offset);
            tileBounds.add(tileBounds.max + offset);
        }

        if (!viewState.getFrustum().inside(tileBounds)) {
            return;
        }

        // Same distance-based subdivision criterion as TileLayer::calculateVisibleTilesRecursive.
        // Like there, the LOD center is at surface level so decisions are stable while
        // elevation data streams in.
        cglib::vec3<double> lodCenter = cglib::transform_point(cglib::vec3<double>::convert(vertexTransformer->calculatePoint(centre)), tileMatrix);
        const cglib::mat4x4<double>& mvpMat = viewState.getModelviewProjectionMat();
        double tileW = lodCenter(0) * mvpMat(3, 0) + lodCenter(1) * mvpMat(3, 1) + lodCenter(2) * mvpMat(3, 2) + mvpMat(3, 3);
        double zoomDistance = tileW * std::pow(2.0, static_cast<double>(tile.getZoom()));
        // The SURFACE's world: tileW is a world length, and the globe's world is twice the plane's,
        // so a planar threshold here stopped the pre-pass mesh a level short (18-globe.md).
        double worldWidth = (viewState.getProjectionSurface() ? viewState.getProjectionSurface()->getWorldWidth() : static_cast<double>(Const::WORLD_SIZE));
        bool subDivide = zoomDistance < worldWidth * Const::SQRT_2;

        // No point in subdividing beyond the resolution of the elevation data + mesh grid
        int maxUsefulZoom = Const::MAX_SUPPORTED_ZOOM_LEVEL;
        if (std::shared_ptr<TileDataSource> dataSource = elevationManager->getDataSource()) {
            maxUsefulZoom = dataSource->getMaxZoom() + 3;
        }
        // The camera's zoom is NOT one of the bounds. It was, and it is the wrong measure for a
        // ground-level camera: an orbiting camera's zoom IS its distance, so on a map the clamp only
        // repeated what the distance rule above already said, but a first-person camera STANDS on the
        // ground with the horizon in frame - its zoom describes what one screen width of that horizon
        // covers (13.6 in the panorama) while the ridge a kilometre in front of it wants z15. Clamped
        // to it, the near ground was cut at z13 whatever the app asked for: 3.4 km tiles, and at the
        // 96-cell grid cap 36 m triangles off a 13.5 m DEM.
        //
        // TerrainOptions::MaxTileZoomOffset is the knob for this, and TileLayer already honours it on
        // the draped path - which is why the same view is finer there. It is folded into maxZoom by
        // collectVisibleTiles, so the two paths now cap the cut by the same rule and the default (100,
        // no cap) leaves the distance rule, the data and the budget to decide.
        int targetTileZoom = std::min(maxUsefulZoom, maxZoom);
        if (targetTileZoom <= tile.getZoom()) {
            subDivide = false;
        }

        if (subDivide) {
            for (int n = 0; n < 4; n++) {
                calculateVisibleTiles(viewState, elevationManager, tile.getChild(n), maxZoom, tiles);
            }
        } else {
            tiles.push_back(tile);
        }
    }

    int TerrainRenderer::calculateMeshGridSize(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int meshResolution, bool fixedScaleNormals) const {
        // THESE TWO CASES ARE NOT THE SAME THING, and treating them alike drew whole tiles as a
        // single flat quad.
        //
        // A grid that is present and has no relief is genuinely flat - sea, a salt pan - and one
        // quad is the right mesh for it. NO grid means only that this tile's own DEM is not in the
        // cache yet; buildTileMesh still reads every node's height from the elevation manager, which
        // answers from a cached ANCESTOR. So the relief is available, and collapsing to four
        // vertices throws it away: the tile becomes one plane, with a valid but CONSTANT normal.
        //
        // That shades correctly in the surface pass and produces exactly zero normal gradient in the
        // post-process, so it takes no ridge ink at all - a large, straight-edged, flat-looking
        // region whose neighbours are fully drawn. Which tiles are in that state follows the
        // prefetch, so it moves as the camera turns.
        //
        // Note this bypassed the MIN_MESH_GRID_SIZE floor below, which exists to stop precisely this.
        if (grid && grid->getMaxHeight() - grid->getMinHeight() <= 0) {
            return 1;
        }
        if (!grid) {
            return MIN_MESH_GRID_SIZE;
        }

        // The pre-pass mesh must never be FINER than the draped tile surfaces: a coarser draped
        // surface cuts through the ridges of a finer pre-pass mesh and fails the depth test. Same
        // bound as the surfaces, so it only ever gets coarser with distance, which is safe.
        double tileSize = Const::WORLD_SIZE / (1 << tile.getZoom());
        double gridWidth = grid->getInternalBounds().getMax().getX() - grid->getInternalBounds().getMin().getX();
        int texelsPerTile = MAX_MESH_GRID_SIZE;
        if (gridWidth > 0) {
            texelsPerTile = static_cast<int>(grid->getWidth() * tileSize / gridWidth + 0.5);
        }
        // WITH FIXED-SCALE NORMALS, THE DEM'S TEXEL COUNT MUST NOT SET THE MESH DENSITY.
        //
        // The mesh grid is not only geometry: ensureSurfaceAttribs stores ONE NORMAL PER VERTEX and
        // the rasteriser interpolates between them, so gridSize is also the resolution of the normal
        // field the ridge term differentiates. Capping it by the DEM under the tile therefore makes
        // the NORMAL density per-tile - a tile on a 32x ancestor carries a 16x16 normal field, and
        // linear interpolation across those cells is smooth whatever distance each normal was
        // measured over. It takes almost no ridge ink while its 96-grid neighbour takes plenty.
        //
        // That is the tile-to-tile discontinuity, and it survived making the SAMPLE DISTANCE uniform
        // because the sample distance was never what carried it. It also explains the flicker and
        // the height jump: gridSize steps 16 -> 96 as the DEM converges, which is a different mesh.
        //
        // Only while the normals are sampled at a fixed ground distance. Without that the normal IS
        // the mesh gradient, and a mesh finer than the DEM would just interpolate the same plane at
        // more vertices - the cap is right there, and the draped path keeps it.
        int gridSize = (fixedScaleNormals ? std::min(meshResolution, MAX_MESH_GRID_SIZE)
                                          : std::min(std::min(texelsPerTile, meshResolution), MAX_MESH_GRID_SIZE));
        return std::max(gridSize, MIN_MESH_GRID_SIZE);
    }

    std::shared_ptr<TerrainRenderer::TileMesh> TerrainRenderer::buildTileMesh(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, const std::shared_ptr<ElevationManager>& elevationManager, int gridSize, int edgeMask) const {
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

        // The mesh is built in the transformer's tile-local frame, so its vertices carry the shape
        // of the surface. Its (x, y) is the vt LOCAL frame, whose y runs opposite to the tile's -
        // hence the 1 - y when asking the transformer about a node.
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        std::shared_ptr<const vt::TileTransformer::VertexTransformer> vertexTransformer = _tileTransformer->createTileVertexTransformer(vtTileId);
        bool spherical = _tileTransformer->isSpherical();

        mesh->vertices.reserve((rowSize * rowSize + 8 * rowSize) * 3); // grid + skirt vertices
        // The heights come FIRST, on their own: the vertices are built from them below, and so are
        // the surface normals (ensureSurfaceAttribs), so the stitching in between reaches both.
        mesh->heights.assign(rowSize * rowSize, 0.0f);
        if (grid) {
            for (int gy = 0; gy <= gridSize; gy++) {
                double internalY = originY + (static_cast<double>(gy) / gridSize) * size;
                double internalPerMeter = exaggeration * elevationManager->getDisplayScale(internalY);
                double localPerInternal = (spherical ? sphericalLocalPerInternal(tile, internalY) : localFromInternal);
                for (int gx = 0; gx <= gridSize; gx++) {
                    double internalX = originX + (static_cast<double>(gx) / gridSize) * size;
                    double meters = grid->sampleNodeHeight(internalX, internalY); // the drawn surface, which this depth stands in for
                    // The height in INTERNAL units is the same on either surface (18-globe.md);
                    // only the internal-to-tile-local factor differs, and the plane's is written
                    // out rather than derived so its depth mesh keeps the values it had.
                    mesh->heights[gy * rowSize + gx] = static_cast<float>(meters * internalPerMeter * localPerInternal);
                }
            }
        }

        // LOD stitching: where the neighbour is coarser its edge carries FEWER nodes, so the detail
        // this tile has in between is exactly what opens the crack. Dropping our edge to the
        // neighbour's spacing - a straight line between the nodes the two share - closes it, and
        // only the one row of edge nodes moves, so nothing inside the tile is smoothed.
        //
        // The neighbour's spacing is assumed to be ours doubled per zoom level it is coarser by,
        // which is what calculateMeshGridSize gives whenever both tiles land on the same cap.
        if (grid && edgeMask != 0) {
            auto stitchEdge = [&](int shift, bool horizontal, int fixedIndex) {
                int levels = (edgeMask >> shift) & EDGE_LEVELS_MASK;
                if (levels <= 0) {
                    return;
                }
                // One level coarser is every second node interpolated away, two levels every
                // fourth, and so on: the neighbour covers twice the ground per level with the same
                // node count.
                int step = 1 << levels;
                auto nodeAt = [&](int index) {
                    int gx = horizontal ? index : fixedIndex;
                    int gy = horizontal ? fixedIndex : index;
                    return gy * rowSize + gx;
                };
                // WHICH of our nodes the neighbour actually has depends on where this tile sits
                // inside the ground the coarse one covers: as the second of two children, its
                // nodes fall on our ODD indices. The tile's offset within that block gives the
                // phase - and the mesh's own index runs north while a tile's y runs south, hence
                // the flip on a vertical edge.
                int block = step - 1;
                int blockIndex = (horizontal ? tile.getX() & block : block - (tile.getY() & block));
                int phase = (step - (blockIndex * gridSize) % step) % step;
                for (int index = phase - step; index < gridSize; index += step) {
                    // An anchor off the end of the edge is the edge's own end node: the corners are
                    // shared with the neighbour in any case.
                    int first = std::max(index, 0);
                    int next = std::min(index + step, gridSize);
                    if (next <= first) {
                        continue;
                    }
                    float height0 = mesh->heights[nodeAt(first)];
                    float height1 = mesh->heights[nodeAt(next)];
                    for (int inner = first + 1; inner < next; inner++) {
                        float ratio = static_cast<float>(inner - first) / static_cast<float>(next - first);
                        mesh->heights[nodeAt(inner)] = height0 + (height1 - height0) * ratio;
                    }
                }
            };
            stitchEdge(EDGE_SHIFT_SOUTH, true, 0);
            stitchEdge(EDGE_SHIFT_NORTH, true, gridSize);
            stitchEdge(EDGE_SHIFT_WEST, false, 0);
            stitchEdge(EDGE_SHIFT_EAST, false, gridSize);
        }

        double minLocalZ = 0;
        for (int gy = 0; gy <= gridSize; gy++) {
            for (int gx = 0; gx <= gridSize; gx++) {
                double x = static_cast<double>(gx) / gridSize;
                double y = static_cast<double>(gy) / gridSize;
                double localZ = mesh->heights[gy * rowSize + gx];
                minLocalZ = std::min(minLocalZ, localZ);
                if (spherical) {
                    cglib::vec2<float> tilePos(static_cast<float>(x), static_cast<float>(1.0 - y));
                    cglib::vec3<float> point = vertexTransformer->calculatePoint(tilePos);
                    cglib::vec3<float> normal = cglib::unit(vertexTransformer->calculateNormal(tilePos));
                    mesh->vertices.push_back(static_cast<float>(point(0) + normal(0) * localZ));
                    mesh->vertices.push_back(static_cast<float>(point(1) + normal(1) * localZ));
                    mesh->vertices.push_back(static_cast<float>(point(2) + normal(2) * localZ));
                } else {
                    mesh->vertices.push_back(static_cast<float>(x));
                    mesh->vertices.push_back(static_cast<float>(y));
                    mesh->vertices.push_back(static_cast<float>(localZ));
                }
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

        mesh->gridIndexCount = mesh->indices.size();

        // Skirts: extrude the tile edges downwards to cover cracks between neighboring
        // tiles of different resolutions in the depth buffer.
        //
        // The drop is in METRES, and it used to be 0.05 in TILE-LOCAL z - where 1.0 is the tile's
        // own width. That is 5% of a tile, so it scaled with the tile: 244 m at z13, 976 m at z11,
        // 3.9 km at z9 and 31 km at z6. Looking down at a map nobody ever sees a skirt. Looking
        // ALONG the ground, as a panorama does, every tile edge on the horizon carries a vertical
        // wall kilometres deep, seen nearly edge-on - and the terrain depth texture carries it too,
        // so an effect drawing lines from that depth inks a straight line along every tile boundary.
        // Which is a seam between tiles of EQUAL zoom, on any DEM, that no normal can smooth: the
        // skirt vertices copy their edge's normal, so the shading is continuous while the geometry
        // is a cliff.
        //
        // What the skirt has to cover is the crack between a coarse sampling of a hillside and a
        // fine one, which is bounded by the local relief - hundreds of metres, not tens of
        // kilometres.
        if (grid) {
            double displayScale = elevationManager->getDisplayScale(originY + size * 0.5);
            double localPerMeter = (exaggeration > 0 ? exaggeration : 1.0) * displayScale * localFromInternal;
            double skirtZ = minLocalZ - SKIRT_DEPTH_METERS * localPerMeter;
            auto addSkirt = [&](const std::vector<unsigned short>& edge, bool flip) {
                for (std::size_t i = 0; i + 1 < edge.size(); i++) {
                    unsigned short i0 = edge[i];
                    unsigned short i1 = edge[i + 1];
                    unsigned short s0 = static_cast<unsigned short>(mesh->vertices.size() / 3);
                    for (unsigned short idx : { i0, i1 }) {
                        mesh->skirtSources.push_back(idx);
                        if (spherical) {
                            // skirtZ is a height, not a z coordinate: hang the skirt off the BASE
                            // surface point along its normal, which on the plane reduces to
                            // (x, y, skirtZ) exactly as the branch below writes it.
                            int gx = idx % rowSize, gy = idx / rowSize;
                            cglib::vec2<float> tilePos(static_cast<float>(gx) / gridSize, 1.0f - static_cast<float>(gy) / gridSize);
                            cglib::vec3<float> point = vertexTransformer->calculatePoint(tilePos);
                            cglib::vec3<float> normal = cglib::unit(vertexTransformer->calculateNormal(tilePos));
                            for (int k = 0; k < 3; k++) {
                                mesh->vertices.push_back(static_cast<float>(point(k) + normal(k) * skirtZ));
                            }
                            continue;
                        }
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

    double TerrainRenderer::sphericalLocalPerInternal(const MapTile& tile, double internalY) {
        // internal -> metres is the Mercator stretch; metres -> spherical tile-local is
        // calculateHeight's 2 * PI convention, which carries no stretch of its own. On the plane
        // the two cancel to (1 << zoom) / WORLD_SIZE, which is why only this case needs writing.
        double sinLatitude = std::tanh(internalY * 2 * Const::PI / Const::WORLD_SIZE);
        double cosLatitude = std::sqrt(std::max(1.0e-6, 1.0 - sinLatitude * sinLatitude));
        return cosLatitude * (1 << tile.getZoom()) * 2 * Const::PI / Const::WORLD_SIZE;
    }

    void TerrainRenderer::setTileTransformer(const std::shared_ptr<vt::TileTransformer>& tileTransformer) {
        if (_tileTransformer == tileTransformer) {
            return;
        }
        // The vertices carry the shape, so a cached mesh built on the other surface is wrong.
        _tileTransformer = tileTransformer;
        _meshCache.clear();
        {
            std::lock_guard<std::mutex> lock(_visibleTilesMutex);
            _visibleTilesValid = false; // the cut is walked against the surface too
        }
    }

    cglib::mat4x4<double> TerrainRenderer::calculateTileMatrix(const MapTile& tile) const {
        // The transformer's own tile frame: a uniform scale and the tile origin, curved or flat.
        return _tileTransformer->calculateTileMatrix(vt::TileId(tile.getZoom(), tile.getX(), tile.getY()), 1.0f);
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

        // THE DEM, PER FRAGMENT.
        //
        // v_worldPos.xy is the internal position, so a fragment can find itself in the elevation
        // texture the tile is standing on and measure the slope THERE, instead of interpolating a
        // normal baked at the mesh's corners. A mesh carries one normal per cell corner; at 64 cells
        // a tile that is hundreds of metres of ground per sample, so every ridge narrower than a
        // cell is smoothed away before any shader sees it. This is what geo-three's terrain material
        // does (MaterialHeightShader: vComputedNormal from d-f, b-h taps of the height texture) and
        // it is most of why its relief is sharp where ours is soft.
        uniform sampler2D u_demTex;
        uniform vec4 u_demOriginSize;   // xy: internal origin of uv (0,0), zw: internal size of uv [0,1]
        uniform vec2 u_demInvTexSize;   // 1 / texture size, in texels
        uniform vec4 u_demDecode;       // texel -> metres, linear part
        uniform float u_demDecodeOffset;
        uniform float u_demMetersPerTexel; // ground metres per texel AT THE EQUATOR
        uniform float u_demMercatorYScale;
        uniform float u_demValid;       // 0 when no elevation texture is bound for this tile
        uniform float u_demNormalStep;  // ground metres between the taps; <= 0 is one texel

        float terrainHeightMetres(vec2 internalPos) {
            vec2 uv = (internalPos - u_demOriginSize.xy) / u_demOriginSize.zw;
            return dot(texture2D(u_demTex, uv), u_demDecode) + u_demDecodeOffset;
        }

        /**
         * The surface normal measured at THIS fragment. stepMetres <= 0 samples at one texel, which
         * is the finest the data can answer; a larger step is a deliberately smoother normal.
         *
         * Mercator: a fixed internal distance covers cos(latitude) as much ground, and
         * cos(latitude) is exactly 1/cosh(mercator y) - so the true ground step is the equator's
         * scaled by that. Without it a slope at latitude 45 comes out 1.41x too steep. GLSL ES 1.0
         * has no cosh, hence the exponentials.
         */
        vec3 terrainNormal(float stepMetres) {
            // A SKIRT (z marked negative by the attribute bake): a vertical crack-filling wall, whose
            // fragments all share one ground position. Sampling the DEM per fragment there bands it
            // vertically; it keeps the normal of the edge it hangs from.
            if (v_normal.z < 0.0) {
                return normalize(vec3(v_normal.xy, -v_normal.z));
            }
            // NO MESH-NORMAL FALLBACK. Returning normalize(v_normal) here was the pre-per-fragment
            // shading, so a tile without an elevation texture yet drew in a visibly different style
            // and the picture appeared to change shader as it loaded. Flat is the honest answer to
            // "no height data": the tile shades as ground until its texture arrives.
            if (u_demValid < 0.5) {
                return vec3(0.0, 0.0, 1.0);
            }
            vec2 texelInternal = u_demOriginSize.zw * u_demInvTexSize;
            float stepTexels = (stepMetres > 0.0 ? max(stepMetres / max(u_demMetersPerTexel, 0.0001), 1.0) : 1.0);
            vec2 stepInternal = texelInternal * stepTexels;
            float west  = terrainHeightMetres(v_worldPos.xy - vec2(stepInternal.x, 0.0));
            float east  = terrainHeightMetres(v_worldPos.xy + vec2(stepInternal.x, 0.0));
            float south = terrainHeightMetres(v_worldPos.xy - vec2(0.0, stepInternal.y));
            float north = terrainHeightMetres(v_worldPos.xy + vec2(0.0, stepInternal.y));
            float mercatorY = v_worldPos.y * u_demMercatorYScale;
            float cosLat = 2.0 / (exp(mercatorY) + exp(-mercatorY));
            float groundStep = max(u_demMetersPerTexel * stepTexels * cosLat, 0.0001);
            return normalize(vec3(-(east - west) / (2.0 * groundStep),
                                  -(north - south) / (2.0 * groundStep),
                                  1.0));
        }
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

    const std::string TerrainRenderer::TERRAIN_NORMAL_DEPTH_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec3 a_coord;
        attribute vec3 a_normal;
        uniform mat4 u_mvpMat;
        uniform mat4 u_tileMat;
        uniform float u_far;
        varying float v_depth;
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        void main() {
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_depth = pos.w / u_far;
            v_normal = a_normal;
            // The post-process differentiates what this pass packs, so it has to read the SAME
            // per-fragment normal the surface is shaded with, or the ink goes on drawing the mesh.
            v_worldPos = (u_tileMat * vec4(a_coord, 1.0)).xyz;
            gl_Position = pos;
        }
    )GLSL";

    // Half the channels for the depth and the other half for the normal. See
    // PostProcessEffect::setTerrainNormalsRequired for what reads this and why.
    const std::string TerrainRenderer::TERRAIN_NORMAL_DEPTH_FRAGMENT_SHADER = R"GLSL(
        #version 100
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        varying float v_depth;
        varying vec3 v_normal;
        varying vec3 v_worldPos;

        // THE DEM, PER FRAGMENT.
        //
        // v_worldPos.xy is the internal position, so a fragment can find itself in the elevation
        // texture the tile is standing on and measure the slope THERE, instead of interpolating a
        // normal baked at the mesh's corners. A mesh carries one normal per cell corner; at 64 cells
        // a tile that is hundreds of metres of ground per sample, so every ridge narrower than a
        // cell is smoothed away before any shader sees it. This is what geo-three's terrain material
        // does (MaterialHeightShader: vComputedNormal from d-f, b-h taps of the height texture) and
        // it is most of why its relief is sharp where ours is soft.
        uniform sampler2D u_demTex;
        uniform vec4 u_demOriginSize;   // xy: internal origin of uv (0,0), zw: internal size of uv [0,1]
        uniform vec2 u_demInvTexSize;   // 1 / texture size, in texels
        uniform vec4 u_demDecode;       // texel -> metres, linear part
        uniform float u_demDecodeOffset;
        uniform float u_demMetersPerTexel; // ground metres per texel AT THE EQUATOR
        uniform float u_demMercatorYScale;
        uniform float u_demValid;       // 0 when no elevation texture is bound for this tile
        uniform float u_demNormalStep;  // ground metres between the taps; <= 0 is one texel

        float terrainHeightMetres(vec2 internalPos) {
            vec2 uv = (internalPos - u_demOriginSize.xy) / u_demOriginSize.zw;
            return dot(texture2D(u_demTex, uv), u_demDecode) + u_demDecodeOffset;
        }

        /**
         * The surface normal measured at THIS fragment. stepMetres <= 0 samples at one texel, which
         * is the finest the data can answer; a larger step is a deliberately smoother normal.
         *
         * Mercator: a fixed internal distance covers cos(latitude) as much ground, and
         * cos(latitude) is exactly 1/cosh(mercator y) - so the true ground step is the equator's
         * scaled by that. Without it a slope at latitude 45 comes out 1.41x too steep. GLSL ES 1.0
         * has no cosh, hence the exponentials.
         */
        vec3 terrainNormal(float stepMetres) {
            // A SKIRT (z marked negative by the attribute bake): a vertical crack-filling wall, whose
            // fragments all share one ground position. Sampling the DEM per fragment there bands it
            // vertically; it keeps the normal of the edge it hangs from.
            if (v_normal.z < 0.0) {
                return normalize(vec3(v_normal.xy, -v_normal.z));
            }
            // NO MESH-NORMAL FALLBACK. Returning normalize(v_normal) here was the pre-per-fragment
            // shading, so a tile without an elevation texture yet drew in a visibly different style
            // and the picture appeared to change shader as it loaded. Flat is the honest answer to
            // "no height data": the tile shades as ground until its texture arrives.
            if (u_demValid < 0.5) {
                return vec3(0.0, 0.0, 1.0);
            }
            vec2 texelInternal = u_demOriginSize.zw * u_demInvTexSize;
            float stepTexels = (stepMetres > 0.0 ? max(stepMetres / max(u_demMetersPerTexel, 0.0001), 1.0) : 1.0);
            vec2 stepInternal = texelInternal * stepTexels;
            float west  = terrainHeightMetres(v_worldPos.xy - vec2(stepInternal.x, 0.0));
            float east  = terrainHeightMetres(v_worldPos.xy + vec2(stepInternal.x, 0.0));
            float south = terrainHeightMetres(v_worldPos.xy - vec2(0.0, stepInternal.y));
            float north = terrainHeightMetres(v_worldPos.xy + vec2(0.0, stepInternal.y));
            float mercatorY = v_worldPos.y * u_demMercatorYScale;
            float cosLat = 2.0 / (exp(mercatorY) + exp(-mercatorY));
            float groundStep = max(u_demMetersPerTexel * stepTexels * cosLat, 0.0001);
            return normalize(vec3(-(east - west) / (2.0 * groundStep),
                                  -(north - south) / (2.0 * groundStep),
                                  1.0));
        }
        void main() {
            // SQRT, so that the sixteen bits are spent where the relief is rather than spread flat
            // over a far plane that is mostly empty. Never quite 1: that value is the sky, and the
            // pass has no coverage channel left to say so with.
            float depth = min(sqrt(clamp(v_depth, 0.0, 1.0)), 0.9995);
            vec2 enc = fract(vec2(1.0, 255.0) * depth);
            enc.x -= enc.y * (1.0 / 255.0);
            // Octahedral, upper hemisphere only - a height field's normal never points down, so the
            // L1 normalisation IS the encoding and the usual fold is unreachable. Degenerate
            // normals (a zero attribute on a mesh built before the attributes existed) fall back to
            // straight up rather than to a division by zero.
            // PER FRAGMENT, from the elevation texture - the same normal the surface is shaded
            // with. Packing the interpolated vertex normal here left the post-process
            // differentiating the MESH, so its ridge lines could never be finer than a mesh cell
            // however sharp the operator on top of it was.
            vec3 n = terrainNormal(u_demNormalStep);
            float l1 = abs(n.x) + abs(n.y) + abs(n.z);
            // DIAGNOSTIC FALLBACK, not the shipping one. Straight up (0, 0) is what a genuinely flat
            // surface encodes to AND what the sky is cleared to, so a degenerate normal used to be
            // indistinguishable from both - which is why three rounds of debugging could not tell
            // "the attribute is zero" from "this pixel was never drawn". (1, -1) has an L1 norm of
            // two, so no real normal can produce it, and it shows up as its own colour.
            vec2 oct = l1 > 0.0001 ? n.xy / l1 : vec2(1.0, -1.0);
            gl_FragColor = vec4(enc, oct * 0.5 + 0.5);
        }
    )GLSL";
}
