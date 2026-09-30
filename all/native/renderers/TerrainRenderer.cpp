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
#include <array>
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
        // Where the skirt indices start: the grid is emitted first, see renderTiles' skipSkirts.
        std::size_t gridIndexCount = 0;
        // Surface pass only, filled on first use: nx, ny, nz, elevation in metres per vertex.
        std::vector<float> surfaceAttribs;
        // TerrainOptions::getNormalSampleDistance the attribs were built with, so a change re-bakes.
        // -1 is "not built yet"; 0 is the mesh-derived gradient, a real setting.
        float surfaceAttribSampleDistance = -1.0f;
        // Created on first draw and kept for the mesh's life, see TerrainMeshBuffer.
        std::shared_ptr<TerrainMeshBuffer> buffer;
        // The sample distance the attrib buffer holds, so only a re-bake re-uploads.
        float uploadedAttribSampleDistance = -2.0f;
        // Fixed-scale DEM-sampled attribs, or the mesh-gradient stand-in baked inline; the DEM read
        // is too slow for the render thread, see refineSurfaceAttribs.
        bool attribsRefined = false;
        // The DEM tile this mesh's grid resolved to, for the per-tile debug views.
        int demZoom = -1;
        // The DEM zoom the attribs were baked from, which can lag demZoom: attribs are not re-baked
        // when a finer grid lands under an existing mesh.
        int attribsDemZoom = -1;
        // Coarsest grid the stencil reads used, and whether it was coarser than the source offers:
        // a provisional bake is redone when more data lands, or the shading depends on arrival order.
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
        unsigned long long edgeSignature = 0; // EdgeHeightResolver::signature the edges were built for
        bool bilinearHeights = false; // read off the DEM rather than the box-averaged node field
    };

    namespace {
        // Edge heights both tiles sharing an edge agree on: a coarser neighbour's polyline wins, equal
        // zooms take the mean of both grids, a corner goes to the coarsest tiles touching it.
        // Each recursion step moves to a strictly coarser tile, so it terminates.
        class EdgeHeightResolver {
        public:
            struct Participant {
                MapTile tile;
                std::shared_ptr<ElevationTileGrid> grid;
                int gridSize;
            };

            // bilinearHeights: the surface buildTileMesh reads, so an edge is on the same one.
            EdgeHeightResolver(const std::shared_ptr<ElevationManager>& elevationManager, bool bilinearHeights) :
                _elevationManager(elevationManager),
                _exaggeration(elevationManager->getExaggeration()),
                _bilinearHeights(bilinearHeights)
            {
            }

            void add(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int gridSize) {
                _participants.emplace(tile.getTileId(), Participant { tile, grid, std::max(1, gridSize) });
            }

            const Participant* find(const MapTile& tile) const {
                auto it = _participants.find(tile.getTileId());
                return (it != _participants.end() ? &it->second : nullptr);
            }

            // Everything the tile's edge heights are read from: a mesh built for another value is
            // stale. Neighbours across each side and every tile touching a corner.
            unsigned long long signature(const Participant& participant) const {
                unsigned long long hash = 1469598103934665603ULL;
                auto mix = [&hash](const Participant* other) {
                    unsigned long long values[3] = { other ? static_cast<unsigned long long>(other->tile.getTileId()) : 0ULL,
                                                     other ? other->grid->getSerial() : 0ULL,
                                                     other ? static_cast<unsigned long long>(other->gridSize) : 0ULL };
                    for (unsigned long long value : values) {
                        hash = (hash ^ value) * 1099511628211ULL;
                    }
                };
                mix(&participant);
                for (int side = 0; side < 4; side++) {
                    mix(neighbour(participant, side));
                }
                double size = tileSize(participant.tile.getZoom());
                for (int corner = 0; corner < 4; corner++) {
                    double x = originX(participant.tile) + (corner & 1 ? size : 0.0);
                    double y = originY(participant.tile) + (corner & 2 ? size : 0.0);
                    for (const Participant* leaf : quadrantLeaves(x, y, participant.tile)) {
                        mix(leaf);
                    }
                }
                return hash;
            }

            // side: 0 south, 1 north, 2 west, 3 east. Heights of nodes 0..gridSize, internal units.
            std::vector<double> sideHeights(const Participant& participant, int side) const {
                std::vector<double> heights(participant.gridSize + 1);
                for (int index = 0; index <= participant.gridSize; index++) {
                    heights[index] = nodeHeight(participant, side, index);
                }
                return heights;
            }

        private:
            static double tileSize(int zoom) {
                return Const::WORLD_SIZE / (1 << zoom);
            }

            static double originX(const MapTile& tile) {
                return tile.getX() * tileSize(tile.getZoom()) - Const::WORLD_SIZE * 0.5;
            }

            static double originY(const MapTile& tile) {
                return ((1 << tile.getZoom()) - 1 - tile.getY()) * tileSize(tile.getZoom()) - Const::WORLD_SIZE * 0.5;
            }

            static double alongOrigin(const MapTile& tile, int side) {
                return (side < 2 ? originX(tile) : originY(tile));
            }

            static double fixedCoordinate(const MapTile& tile, int side) {
                double size = tileSize(tile.getZoom());
                switch (side) {
                case 0: return originY(tile);
                case 1: return originY(tile) + size;
                case 2: return originX(tile);
                default: return originX(tile) + size;
                }
            }

            double internalAt(const Participant& participant, double x, double y) const {
                double meters = (_bilinearHeights ? participant.grid->sampleHeight(x, y) : participant.grid->sampleNodeHeight(x, y));
                return meters * _exaggeration * _elevationManager->getDisplayScale(y);
            }

            // The tile across 'side', at this tile's zoom or coarser (the cut is a quadtree partition).
            // A finer neighbour is deliberately not found: it follows this tile, not the reverse.
            const Participant* neighbour(const Participant& participant, int side) const {
                static const int DX[4] = { 0, 0, -1, 1 };
                static const int DY[4] = { 1, -1, 0, 0 }; // tile y runs south, the mesh's gy north
                const MapTile& tile = participant.tile;
                int zoom = tile.getZoom();
                int tileCount = 1 << zoom;
                int nx = tile.getX() + DX[side];
                int ny = tile.getY() + DY[side];
                if (ny < 0 || ny >= tileCount) {
                    return nullptr;
                }
                nx = (nx % tileCount + tileCount) % tileCount;
                for (int nzoom = zoom; nzoom >= 0; nzoom--) {
                    auto it = _participants.find(MapTile(nx >> (zoom - nzoom), ny >> (zoom - nzoom), nzoom, tile.getFrameNr()).getTileId());
                    if (it != _participants.end()) {
                        return &it->second;
                    }
                }
                return nullptr;
            }

            // The tile in each quadrant around a point, at 'from's zoom or coarser, in a fixed order so
            // every caller sums them alike. Finer ones never decide a corner.
            std::array<const Participant*, 4> quadrantLeaves(double x, double y, const MapTile& from) const {
                std::array<const Participant*, 4> leaves {};
                double epsilon = tileSize(from.getZoom()) * 1.0e-6;
                for (int quadrant = 0; quadrant < 4; quadrant++) {
                    double probeX = x + (quadrant & 1 ? epsilon : -epsilon);
                    double probeY = y + (quadrant & 2 ? epsilon : -epsilon);
                    for (int zoom = from.getZoom(); zoom >= 0; zoom--) {
                        double size = tileSize(zoom);
                        int tileCount = 1 << zoom;
                        long long column = static_cast<long long>(std::floor((probeX + Const::WORLD_SIZE * 0.5) / size));
                        long long row = static_cast<long long>(std::floor((probeY + Const::WORLD_SIZE * 0.5) / size));
                        if (row < 0 || row >= tileCount) {
                            break;
                        }
                        column = (column % tileCount + tileCount) % tileCount;
                        auto it = _participants.find(MapTile(static_cast<int>(column), static_cast<int>(tileCount - 1 - row), zoom, from.getFrameNr()).getTileId());
                        if (it != _participants.end()) {
                            leaves[quadrant] = &it->second;
                            break;
                        }
                    }
                }
                return leaves;
            }

            double nodeHeight(const Participant& participant, int side, int index) const {
                double size = tileSize(participant.tile.getZoom());
                double along = alongOrigin(participant.tile, side) + size * index / participant.gridSize;
                double fixed = fixedCoordinate(participant.tile, side);
                double x = (side < 2 ? along : fixed);
                double y = (side < 2 ? fixed : along);
                if (index == 0 || index == participant.gridSize) {
                    return cornerHeight(x, y, participant.tile);
                }
                const Participant* other = neighbour(participant, side);
                if (!other) {
                    return internalAt(participant, x, y);
                }
                if (other->tile.getZoom() < participant.tile.getZoom() || other->gridSize < participant.gridSize) {
                    return polylineHeight(*other, side ^ 1, along);
                }
                return (internalAt(participant, x, y) + internalAt(*other, x, y)) * 0.5;
            }

            // What the tile's mesh draws along 'side' at 'along': linear between its edge nodes.
            double polylineHeight(const Participant& participant, int side, double along) const {
                double position = (along - alongOrigin(participant.tile, side)) / tileSize(participant.tile.getZoom()) * participant.gridSize;
                position = std::min(std::max(position, 0.0), static_cast<double>(participant.gridSize));
                int first = std::min(static_cast<int>(std::floor(position)), participant.gridSize - 1);
                double ratio = position - first;
                if (ratio < 1.0e-9) {
                    return nodeHeight(participant, side, first);
                }
                if (ratio > 1.0 - 1.0e-9) {
                    return nodeHeight(participant, side, first + 1);
                }
                return nodeHeight(participant, side, first) * (1.0 - ratio) + nodeHeight(participant, side, first + 1) * ratio;
            }

            double cornerHeight(double x, double y, const MapTile& from) const {
                std::array<const Participant*, 4> leaves = quadrantLeaves(x, y, from);
                int coarsest = from.getZoom();
                for (const Participant* leaf : leaves) {
                    if (leaf) {
                        coarsest = std::min(coarsest, leaf->tile.getZoom());
                    }
                }
                double sum = 0;
                int count = 0;
                for (const Participant* leaf : leaves) {
                    if (!leaf || leaf->tile.getZoom() != coarsest) {
                        continue;
                    }
                    double size = tileSize(coarsest);
                    double tolerance = size * 1.0e-9;
                    double left = originX(leaf->tile), bottom = originY(leaf->tile);
                    bool onVertical = std::abs(x - left) < tolerance || std::abs(x - (left + size)) < tolerance;
                    bool onHorizontal = std::abs(y - bottom) < tolerance || std::abs(y - (bottom + size)) < tolerance;
                    if (onVertical && !onHorizontal) {
                        return polylineHeight(*leaf, std::abs(x - left) < tolerance ? 2 : 3, y);
                    }
                    if (onHorizontal && !onVertical) {
                        return polylineHeight(*leaf, std::abs(y - bottom) < tolerance ? 0 : 1, x);
                    }
                    sum += internalAt(*leaf, x, y);
                    count++;
                }
                return (count > 0 ? sum / count : 0.0);
            }

            std::shared_ptr<ElevationManager> _elevationManager;
            double _exaggeration;
            bool _bilinearHeights;
            std::unordered_map<long long, Participant> _participants;
        };
    }

    TerrainRenderer::TerrainRenderer() :
        _frameBuffer(),
        _shader(),
        _meshCache()
    {
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
        if (_surfacePolygonOffset) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

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

        // Only the post-process buffer follows the option: an effect differentiating it shows coarse
        // texels as blocks, while the point-sampling read-back does not care.
        int downscale = (forReadback ? BUFFER_DOWNSCALE : std::max(1, terrainOptions->getPostProcessDownscale()));
        int bufferWidth = std::max(1, viewState.getWidth() / downscale);
        int bufferHeight = std::max(1, viewState.getHeight() / downscale);
        // Two buffers: a full-resolution glReadPixels is a stall, so the read-back keeps its own size.
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

        // Clear to 'sky'. With normals there is no coverage channel, so sky is depth (1, 0) = 1.0,
        // which terrain never writes, and a straight-up normal.
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

    bool TerrainRenderer::renderDepthTextureFromScene(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, unsigned int sceneDepthTexId) {
        static const GLfloat SCREEN_VERTICES[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
        if (!terrainOptions || !glResourceManager || sceneDepthTexId == 0 || viewState.getWidth() <= 0 || viewState.getHeight() <= 0) {
            return false;
        }
        int downscale = std::max(1, terrainOptions->getPostProcessDownscale());
        int bufferWidth = std::max(1, viewState.getWidth() / downscale);
        int bufferHeight = std::max(1, viewState.getHeight() / downscale);
        if (!_frameBuffer || !_frameBuffer->isValid() || _frameBuffer->getWidth() != bufferWidth || _frameBuffer->getHeight() != bufferHeight) {
            _frameBuffer = glResourceManager->create<FrameBuffer>(bufferWidth, bufferHeight, true, true, false);
        }
        if (!_sceneDepthShader || !_sceneDepthShader->isValid()) {
            _sceneDepthShader = glResourceManager->create<Shader>("terrainscenedepth", SCENE_DEPTH_VERTEX_SHADER, SCENE_DEPTH_FRAGMENT_SHADER);
        }
        if (!_frameBuffer || !_sceneDepthShader) {
            return false;
        }
        // What the drawn texture holds is no longer the drawn pass's, so its cache must not answer.
        _depthTextureMVPMatrix = cglib::mat4x4<double>::zero();

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer->getFBOId());
        glViewport(0, 0, bufferWidth, bufferHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLuint progId = _sceneDepthShader->getProgId();
        glUseProgram(progId);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexId);
        glUniform1i(_sceneDepthShader->getUniformLoc("u_depthTex"), 0);
        glUniform2f(_sceneDepthShader->getUniformLoc("u_nearFar"), viewState.getNear(), viewState.getFar());
        GLuint a_coord = _sceneDepthShader->getAttribLoc("a_coord");
        glVertexAttribPointer(a_coord, 2, GL_FLOAT, GL_FALSE, 0, SCREEN_VERTICES);
        glEnableVertexAttribArray(a_coord);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(a_coord);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(0, 0, viewState.getWidth(), viewState.getHeight());
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        GLContext::CheckGLError("TerrainRenderer::renderDepthTextureFromScene");
        return true;
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
            _depthSnapshotVersion++;
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
            _depthSnapshotVersion++;
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
        // Exact bit patterns: a label's centre repeats exactly across placement passes, and a moved
        // feature misses rather than colliding.
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

    bool TerrainRenderer::isOccludedByTerrain(const cglib::vec3<double>& pos, float tolerance, bool* answered) const {
        if (answered) {
            *answered = false;
        }
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
        // Outside the viewport is unanswerable, unlike a sky pixel inside it; tested here because
        // sampleDepthW returns the same 'nothing there' for both.
        if (x < 0 || y < 0 || x >= depthData->width || y >= depthData->height) {
            return cachedOcclusionVerdict(verdictKey);
        }
        if (answered) {
            *answered = true;
        }
        float depthW = sampleDepthW(*depthData, x, y);
        if (depthW == std::numeric_limits<float>::max()) {
            rememberOcclusionVerdict(verdictKey, false);
            return false; // sky: nothing in front of it
        }
        // Farthest terrain depth AROUND the position, not the depth of its own pixel: a ground label
        // sits exactly on the terrain and the buffer is read back downscaled, so on a slope an exact
        // comparison lets a label's own ground occlude it - the labels blinking while panning.
        float nearestW = depthW; // the spread measures view obliquity, see TerrainOcclusion::isBehind
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
        // Budgeted like TileLayer's cover, see MAX_VISIBLE_MESH_TILES. Several passes ask for the same
        // cut each frame, so it is cached on the camera and the elevation version.
        unsigned int elevationVersion = elevationManager->getVersion();
        float subdivideDistance = terrainOptions->getSubdivideDistance();
        {
            std::lock_guard<std::mutex> lock(_visibleTilesMutex);
            if (_visibleTilesValid && _visibleTilesMVP == viewState.getModelviewProjectionMat() && _visibleTilesElevationVersion == elevationVersion && _visibleTilesSubdivideDistance == subdivideDistance) {
                tiles = _visibleTilesCache;
                return;
            }
        }

        // Seeded from the last cut's zoom: starting at MAX_SUPPORTED_ZOOM_LEVEL re-walks the whole
        // quadtree once per level for a ground-level camera. +1 so it can climb back.
        int maxVisibleTiles = terrainOptions->getMeshCacheSize() / 2;
        if (maxVisibleTiles <= 0) {
            maxVisibleTiles = MAX_VISIBLE_MESH_TILES;
        }
        int maxZoom;
        {
            std::lock_guard<std::mutex> lock(_visibleTilesMutex);
            maxZoom = std::min(Const::MAX_SUPPORTED_ZOOM_LEVEL, _budgetMaxZoom + 1);
        }
        // geo-three's cut has no budget: its levels are the distance rule's and setMaxZoom's alone.
        if (subdivideDistance > 0) {
            maxZoom = Const::MAX_SUPPORTED_ZOOM_LEVEL;
            maxVisibleTiles = std::numeric_limits<int>::max();
        }
        // Lets the height field settle rather than keep refining, see TerrainOptions::setMaxZoom.
        int zoomCap = terrainOptions->getMaxZoom();
        if (zoomCap > 0) {
            maxZoom = std::min(maxZoom, zoomCap);
        }
        // The LOD ring cap TileLayer applies to the draped cut. 100 or more (the default) disables it:
        // terrain LOD is distance based, so a near tile may sit above the camera zoom.
        int zoomOffset = terrainOptions->getMaxTileZoomOffset();
        if (zoomOffset < 100) {
            maxZoom = std::min(maxZoom, static_cast<int>(viewState.getZoom() + 0.001f) + zoomOffset);
        }
        for (;;) {
            tiles.clear();
            calculateVisibleTiles(viewState, elevationManager, MapTile(0, 0, 0, 0), maxZoom, subdivideDistance, tiles);
            if (static_cast<int>(tiles.size()) <= maxVisibleTiles || maxZoom <= 0) {
                break;
            }
            maxZoom--;
        }

        std::lock_guard<std::mutex> lock(_visibleTilesMutex);
        _budgetMaxZoom = maxZoom;
        _visibleTilesMVP = viewState.getModelviewProjectionMat();
        _visibleTilesElevationVersion = elevationVersion;
        _visibleTilesSubdivideDistance = subdivideDistance;
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

    void TerrainRenderer::collectTileMeshes(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, int meshResolutionCap, std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > >& tileMeshes) {
        std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager();
#if MASSIF_VT_RENDER_STATS
        // Whether normalSampleDistance is reachable depends on the DEM source's max zoom.
        if (elevationManager && elevationManager->getDataSource()) {
            static std::once_flag sourceZoomOnce;
            std::shared_ptr<TileDataSource> demSource = elevationManager->getDataSource();
            std::call_once(sourceZoomOnce, [&demSource]() {
                Log::Infof("TerrainRenderer: DEM source zoom range %d..%d", demSource->getMinZoom(), demSource->getMaxZoom());
            });
        }
#endif

        // Through collectVisibleTiles, so every pass of this frame walks the same budgeted cut.
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
        // Before the cut is walked, so a refined tile draws in the frame it lands.
        applyRefinedAttribs();
        unsigned int pass = ++_meshCacheClock;
        // Stitching costs a mesh variant per edge combination. Same flag the draped path reads.
        bool stitching = terrainOptions->isTileEdgeStitchingEnabled();
        // Same condition as ensureSurfaceAttribs' fixed-scale path, so mesh density and normals agree.
        bool fixedScaleNormals = terrainOptions->getNormalSampleDistance() > 0 && !_tileTransformer->isSpherical();
        bool referenceMesh = terrainOptions->getSubdivideDistance() > 0;
        bool bilinearHeights = elevationManager->isBilinearSurface();

        // Every tile's grid first: a tile's edges depend on what its neighbours resolved.
        std::map<long long, std::shared_ptr<ElevationTileGrid> > tileGrids;
        EdgeHeightResolver edgeResolver(elevationManager, bilinearHeights);
        for (const MapTile& tile : tiles) {
            if (tile.getZoom() < minZoom) {
                continue;
            }
            std::shared_ptr<ElevationTileGrid> grid = elevationManager->getTileGrid(tile, ElevationManager::LoadMode::CACHED_ONLY);
            if (grid) {
                tileGrids[tile.getTileId()] = grid;
                if (stitching) {
                    edgeResolver.add(tile, grid, calculateMeshGridSize(tile, grid, meshResolution, fixedScaleNormals, referenceMesh));
                }
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
            // No grid for the tile or any ancestor would build a flat plane at sea level; a gap that
            // fills as data lands is better. Fixed-scale only: a draped map keeps flat ground by design.
            if (fixedScaleNormals && !grid && tile.getZoom() >= minZoom) {
                continue;
            }
            int gridSize = calculateMeshGridSize(tile, grid, meshResolution, fixedScaleNormals, referenceMesh);
#if MASSIF_VT_RENDER_STATS
            if (gridSize <= 1) { VT_STAT_INC(terrainMeshGrid1); }
            else if (gridSize <= 4) { VT_STAT_INC(terrainMeshGrid4); }
            else if (gridSize <= 16) { VT_STAT_INC(terrainMeshGrid16); }
            else if (gridSize <= 48) { VT_STAT_INC(terrainMeshGrid48); }
            else { VT_STAT_INC(terrainMeshGridFull); }
#endif
            const EdgeHeightResolver::Participant* participant = (stitching && grid ? edgeResolver.find(tile) : nullptr);
            unsigned long long edgeSignature = (participant ? edgeResolver.signature(*participant) : 0);
            // Part of the edge signature in the key keeps a variant per neighbourhood, so turning back
            // finds the old mesh; the entry holds the full signature.
            int cacheKey = gridSize | static_cast<int>((edgeSignature & 0x7fff) << 16);

            // Rebuild the mesh only when its inputs actually changed. This avoids rebuilding
            // every cached mesh each time a new elevation tile arrives during loading.
            auto it = _meshCache.find(std::make_pair(tileId, cacheKey));
            if (it == _meshCache.end() || it->second.grid != grid || it->second.exaggeration != exaggeration || it->second.gridSize != gridSize || it->second.edgeSignature != edgeSignature || it->second.bilinearHeights != bilinearHeights) {
                if (it == _meshCache.end() && static_cast<int>(_meshCache.size()) >= meshCacheSize) {
                    evictLeastRecentlyUsedMeshes(pass, meshCacheSize);
                }
                MeshCacheEntry entry;
                entry.grid = grid;
                entry.exaggeration = exaggeration;
                entry.gridSize = gridSize;
                entry.edgeSignature = edgeSignature;
                entry.bilinearHeights = bilinearHeights;
#if MASSIF_VT_RENDER_STATS
                auto buildStart = std::chrono::steady_clock::now();
#endif
                std::array<std::vector<double>, 4> edgeHeights;
                if (participant) {
                    for (int side = 0; side < 4; side++) {
                        edgeHeights[side] = edgeResolver.sideHeights(*participant, side);
                    }
                }
                entry.mesh = buildTileMesh(tile, grid, elevationManager, gridSize, edgeHeights, bilinearHeights);
                // Carry refined normals from any cached variant of this tile: they are sampled at a fixed
                // ground distance, so they beat the flatter mesh-gradient stand-in. attribsRefined
                // stays false, so the refine is still queued.
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
                    // Misses split by cause: nothing to carry, or a sibling refined at another grid size.
                    if (carried) {
                        VT_STAT_INC(terrainAttribCarryHit);
                    } else if (!sawSibling) {
                        VT_STAT_INC(terrainAttribCarryMissNew);
                    } else if (!sawRefinedSibling) {
                        VT_STAT_INC(terrainAttribCarryMissUnrefined);
                    } else {
                        VT_STAT_INC(terrainAttribCarryMissGrid);
                        // A sustained rate under a rotating camera means the grid is being evicted and
                        // re-resolved, a cache problem rather than a carry one.
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
                // Equal-zoom neighbours can stand on different DEM levels (own tile vs ancestor),
                // which is a seam no LOD reasoning explains.
                if (TERRAIN_MESH_TRACE && grid) {
                    double tileSize = Const::WORLD_SIZE / (1 << tile.getZoom());
                    double gridWidth = grid->getInternalBounds().getMax().getX() - grid->getInternalBounds().getMin().getX();
                    Log::Infof("TerrainRenderer::collectTileMeshes: tile %d/%d/%d mesh %d, DEM %d texels stretched over %.2f tiles, edges %llx",
                               tile.getZoom(), tile.getX(), tile.getY(), gridSize, grid->getWidth(),
                               tileSize > 0 ? gridWidth / tileSize : 0.0, edgeSignature);
                }
                it = _meshCache.insert_or_assign(std::make_pair(tileId, cacheKey), std::move(entry)).first;
            }
            else {
                VT_STAT_INC(terrainMeshCacheHits);
            }
            it->second.lastUsed = pass;
            // Every emitted tile, cache hits included, so the debug views describe all of them.
            if (it->second.mesh) {
                it->second.mesh->demZoom = (grid ? grid->getTile().getZoom() : -1);
#if MASSIF_VT_RENDER_STATS
                // Attribs baked from a coarser DEM than the tile now resolves; no other per-tile
                // property shows it.
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
#if MASSIF_VT_RENDER_STATS
        // Seam diagnostic: metres between two neighbours' edge polylines along their shared edge.
        // Planar only; every neighbour at this tile's zoom or coarser is measured.
        if (!_tileTransformer->isSpherical()) {
            static std::chrono::steady_clock::time_point lastSeamLog;
            std::chrono::steady_clock::time_point seamNow = std::chrono::steady_clock::now();
            if (seamNow - lastSeamLog > std::chrono::seconds(2)) {
                lastSeamLog = seamNow;
                std::map<long long, std::pair<MapTile, std::shared_ptr<TileMesh> > > meshById;
                for (const auto& tileMesh : tileMeshes) {
                    meshById.emplace(tileMesh.first.getTileId(), tileMesh);
                }
                // side: 0 south (gy 0), 1 north, 2 west (gx 0), 3 east. Returns metres at 'along'.
                auto edgeMeters = [&](const MapTile& edgeTile, const TileMesh& mesh, int side, double along) {
                    double size = Const::WORLD_SIZE / (1 << edgeTile.getZoom());
                    double originX = edgeTile.getX() * size - Const::WORLD_SIZE * 0.5;
                    double originY = (((1 << edgeTile.getZoom()) - 1 - edgeTile.getY()) * size) - Const::WORLD_SIZE * 0.5;
                    bool horizontal = side < 2;
                    int rowSize = mesh.gridSize + 1;
                    int fixedIndex = (side == 0 || side == 2 ? 0 : mesh.gridSize);
                    double position = (along - (horizontal ? originX : originY)) / size * mesh.gridSize;
                    position = std::min(std::max(position, 0.0), static_cast<double>(mesh.gridSize));
                    int first = std::min(static_cast<int>(std::floor(position)), mesh.gridSize - 1);
                    double ratio = position - first;
                    auto nodeAt = [&](int index) {
                        return horizontal ? fixedIndex * rowSize + index : index * rowSize + fixedIndex;
                    };
                    double local = mesh.heights[nodeAt(first)] * (1 - ratio) + mesh.heights[nodeAt(first + 1)] * ratio;
                    double internalY = (horizontal ? originY + fixedIndex * size / mesh.gridSize : along);
                    return local * size / (exaggeration * elevationManager->getDisplayScale(internalY));
                };
                // class: 0 same grid, 1 same DEM zoom other grid, 2 other DEM zoom, 3 coarser tile
                std::array<int, 4> edges {}, bad {};
                std::array<double, 4> worst {}, sum {};
                for (const auto& tileMesh : tileMeshes) {
                    const MapTile& tile = tileMesh.first;
                    const TileMesh& mesh = *tileMesh.second;
                    if (mesh.gridSize < 1 || mesh.heights.empty()) {
                        continue;
                    }
                    int zoom = tile.getZoom();
                    int tileCount = 1 << zoom;
                    const int dxs[4] = { 0, 0, -1, 1 };
                    const int dys[4] = { 1, -1, 0, 0 };
                    for (int side = 0; side < 4; side++) {
                        int nx = tile.getX() + dxs[side];
                        int ny = tile.getY() + dys[side];
                        if (ny < 0 || ny >= tileCount) {
                            continue;
                        }
                        nx = (nx % tileCount + tileCount) % tileCount;
                        for (int nzoom = zoom; nzoom >= 0; nzoom--) {
                            MapTile neighbourTile(nx >> (zoom - nzoom), ny >> (zoom - nzoom), nzoom, tile.getFrameNr());
                            auto found = meshById.find(neighbourTile.getTileId());
                            if (found == meshById.end()) {
                                continue;
                            }
                            const TileMesh& neighbourMesh = *found->second.second;
                            if (neighbourMesh.gridSize < 1 || neighbourMesh.heights.empty()) {
                                break;
                            }
                            auto ownGrid = tileGrids.find(tile.getTileId());
                            auto neighbourGrid = tileGrids.find(neighbourTile.getTileId());
                            if (ownGrid == tileGrids.end() || neighbourGrid == tileGrids.end()) {
                                break;
                            }
                            int edgeClass = (nzoom < zoom ? 3 : ownGrid->second == neighbourGrid->second ? 0 : mesh.demZoom == neighbourMesh.demZoom ? 1 : 2);
                            int opposite = side ^ 1;
                            double size = Const::WORLD_SIZE / tileCount;
                            double start = (side < 2 ? tile.getX() * size : (tileCount - 1 - tile.getY()) * size) - Const::WORLD_SIZE * 0.5;
                            double edgeWorst = 0;
                            int samples = mesh.gridSize * 2;
                            for (int sample = 0; sample <= samples; sample++) {
                                double along = start + size * sample / samples;
                                double difference = std::abs(edgeMeters(tile, mesh, side, along) - edgeMeters(neighbourTile, neighbourMesh, opposite, along));
                                edgeWorst = std::max(edgeWorst, difference);
                            }
                            edges[edgeClass]++;
                            if (edgeWorst > 1.0) {
                                bad[edgeClass]++;
                            }
                            worst[edgeClass] = std::max(worst[edgeClass], edgeWorst);
                            sum[edgeClass] += edgeWorst;
                            break;
                        }
                    }
                }
                Log::Infof("TerrainRenderer: SEAMS stitching %d | same grid %d edges, %d > 1 m, worst %.1f m | same DEM zoom %d, %d, %.1f m | other DEM zoom %d, %d, worst %.1f m, mean %.1f m | coarser tile %d, %d, worst %.1f m, mean %.1f m",
                           stitching ? 1 : 0, edges[0], bad[0], worst[0], edges[1], bad[1], worst[1],
                           edges[2], bad[2], worst[2], edges[2] > 0 ? sum[2] / edges[2] : 0.0,
                           edges[3], bad[3], worst[3], edges[3] > 0 ? sum[3] / edges[3] : 0.0);
            }
        }
#endif
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

        // Per-tile debug properties, set from the mesh rather than from what the cut asked for.
        GLint uTileDebug = glGetUniformLocation(progId, "u_tileDebug");
        // Here, not only in renderSurface's callback: the normal/depth pass needs it for the DEM lookup.
        GLint uTileMatAll = glGetUniformLocation(progId, "u_tileMat");

        // A shader without these answers -1 and the per-tile DEM block costs nothing.
        GLint uDemTex = glGetUniformLocation(progId, "u_demTex");
        GLint uDemOriginSize = glGetUniformLocation(progId, "u_demOriginSize");
        GLint uDemInvTexSize = glGetUniformLocation(progId, "u_demInvTexSize");
        GLint uDemDecode = glGetUniformLocation(progId, "u_demDecode");
        GLint uDemDecodeOffset = glGetUniformLocation(progId, "u_demDecodeOffset");
        GLint uDemMetersPerTexel = glGetUniformLocation(progId, "u_demMetersPerTexel");
        GLint uDemMercatorYScale = glGetUniformLocation(progId, "u_demMercatorYScale");
        GLint uDemValid = glGetUniformLocation(progId, "u_demValid");
        GLint uDemNormalStep = glGetUniformLocation(progId, "u_demNormalStep");
        GLint uDemUvMat = glGetUniformLocation(progId, "u_demUvMat");
        bool wantsDem = (uDemValid >= 0 && _elevationTextureCache);
        if (uDemTex >= 0) {
            glUniform1i(uDemTex, 7); // its own unit: the surface pass binds the drape on 0
        }
        if (uDemNormalStep >= 0) {
            glUniform1f(uDemNormalStep, terrainOptions->getNormalSampleDistance());
        }

        GLint aNormal = -1, aElevation = -1;
        std::shared_ptr<ElevationManager> elevationManager;
        // The normal-packing depth pass declares no a_elevation, so that array is simply not enabled.
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
                // The uv transform comes from the texture: the grid may cover an ancestor of this tile.
                vt::GLTileRenderer::TerrainTexture demTexture;
                // A texture still being prepared can have a zero internalSize (infinite uv divide), and
                // a mesh with no grid yet is flat: per-fragment relief on it blotches.
                bool meshHasElevation = (mesh->demZoom >= 0);
                bool haveDem = meshHasElevation && _elevationTextureCache->getTexture(vt::TileId(tileMesh.first.getZoom(), tileMesh.first.getX(), tileMesh.first.getY()), demTexture)
                               && demTexture.textureId != 0 && demTexture.textureSize(0) > 0 && demTexture.textureSize(1) > 0
                               && demTexture.internalSize(0) > 0 && demTexture.internalSize(1) > 0
                               && demTexture.metersPerTexel > 0;
                // No finer than the mesh's own grid: the texture lands before the mesh is rebuilt, and
                // relief ahead of the geometry blotches. The texture's level follows from its span.
                if (haveDem && mesh->demZoom >= 0) {
                    // The raster's own span, without the border around it.
                    double span = demTexture.internalSize(0) * (demTexture.textureSize(0) - 2 * demTexture.borderTexels) / demTexture.textureSize(0);
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
                    if (uDemUvMat >= 0) {
                        // Composed in double so the shader gets a tile-relative transform, not an
                        // absolute float position.
                        cglib::mat4x4<double> toUv = cglib::mat4x4<double>::identity();
                        toUv(0, 0) = 1.0 / demTexture.internalSize(0);
                        toUv(1, 1) = 1.0 / demTexture.internalSize(1);
                        toUv(0, 3) = -demTexture.internalOrigin(0) / demTexture.internalSize(0);
                        toUv(1, 3) = -demTexture.internalOrigin(1) / demTexture.internalSize(1);
                        cglib::mat4x4<float> demUvMat = cglib::mat4x4<float>::convert(toUv * tileMatrix);
                        glUniformMatrix4fv(uDemUvMat, 1, GL_FALSE, demUvMat.data());
                    }
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
                int parity = ((tileMesh.first.getX() + tileMesh.first.getY()) & 1);
                glUniform4f(uTileDebug, static_cast<float>(mesh->gridSize), mesh->attribsRefined ? 1.0f : 0.0f, static_cast<float>(mesh->demZoom), static_cast<float>(parity));
            }
            // Geometry never changes, so it is uploaded once; created here, on the GL thread, so
            // create() runs inline before the upload.
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
                // Inline only the cheap mesh-gradient normals, so the tile draws the frame it is
                // built with no DEM reads on the render thread.
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
                // The DEM-sampled ones go to the worker, and again after a provisional bake once data
                // has landed. Bounded: only when the data version moved, and capped for ground the
                // source has no data for.
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
                    // Counted on the buffer GL gets, after the skirt fill, not inside the bake.
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
            std::size_t gridCount = (mesh->gridIndexCount > 0 ? mesh->gridIndexCount : mesh->indices.size());
            std::size_t skirtCount = (skipSkirts ? 0 : mesh->indices.size() - gridCount);
            const GLvoid* indexBase = nullptr;
            if (mesh->buffer && mesh->buffer->hasGeometry()) {
                glBindBuffer(GL_ARRAY_BUFFER, mesh->buffer->getVertexVBO());
                glVertexAttribPointer(aCoord, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->buffer->getIndexVBO());
            } else {
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glVertexAttribPointer(aCoord, 3, GL_FLOAT, GL_FALSE, 0, mesh->vertices.data());
                indexBase = mesh->indices.data();
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gridCount), GL_UNSIGNED_SHORT, indexBase);
            // Skirts are culled, the grid is not: a skirt faces out of its tile, and unculled a near
            // tile's far-edge wall draws a pale band in front of the far tile.
            if (skirtCount > 0) {
                GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
                glEnable(GL_CULL_FACE);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(skirtCount), GL_UNSIGNED_SHORT,
                               reinterpret_cast<const GLvoid*>(reinterpret_cast<const unsigned short*>(indexBase) + gridCount));
                if (!cullEnabled) {
                    glDisable(GL_CULL_FACE);
                }
            }
        }

        // Other code still uses client-side arrays, which a bound ARRAY_BUFFER turns into offsets.
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
                    // Newest first: the oldest job is the tile least likely to still be on screen.
                    job = std::move(_attribJobs.back());
                    _attribJobs.pop_back();
                }
                if (!job.mesh || !job.elevationManager) {
                    continue;
                }
                // Into a copy: the render thread draws from job.mesh->surfaceAttribs. The vectors read
                // here never change after the mesh build.
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
            // Bounded; the oldest go, as the worker prefers the newest.
            while (_attribJobs.size() >= MAX_PENDING_ATTRIB_JOBS) {
                _attribJobs.front().mesh->attribsPending = false;
                _attribJobs.pop_front();
            }
            _attribJobs.push_back(AttribJob { tile, elevationManager, mesh, normalSampleDistance });
        }
        // Render thread only; the worker never touches the mesh's own fields.
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
            // Dropped if the mesh was re-baked at a different sample distance meanwhile.
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
            // The packed normal texture's cache key does not see this change.
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

        // The tile-local frame scales x, y and z alike, so this normal is already world-space. Past
        // the edge the neighbour's height is read (cached only): clamping to the own edge node halves
        // the gradient and draws a seam, and is only the fallback.
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

        // A mesh gradient's step halves per zoom, so tiles at an LOD boundary disagree; a fixed
        // ground step makes the normal a property of the DEM. 0 keeps the mesh gradient, cheaper
        // and sharper for a draped map whose normals are never differentiated.
        bool fixedScale = allowFixedScale && normalSampleDistance > 0 && !spherical;
        // Counted: with CACHED_ONLY reads the fixed-scale path can silently decline for most of a mesh.
        int fixedScaleVertices = 0;
        // -1 until a stencil read answers.
        int worstStencilZoom = -1;
        // The texel of the grid the tile actually resolved, in internal units; an ancestor answers
        // at a fraction of its nominal resolution.
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
        // How much coarser than asked the DEM under this tile is: a stretched ancestor gives smoother
        // normals, so a lighter tile beside a fully drawn one.
        if (fixedScale && gridTexelInternal > 0) {
            double centreScale = elevationManager->getDisplayScale(originY + size * 0.5);
            double asked = normalSampleDistance * centreScale;
            double stretch = (asked > 0 ? gridTexelInternal / asked : 0);
            if (stretch <= 1.01) { VT_STAT_INC(terrainAttribStretch1); }
            else if (stretch <= 2.01) { VT_STAT_INC(terrainAttribStretch2); }
            else if (stretch <= 4.01) { VT_STAT_INC(terrainAttribStretch4); }
            else { VT_STAT_INC(terrainAttribStretchBig); }
            // Named, because a stretched far tile is expected while a stretched near one is a cache miss.
            if (stretch > 2.01) {
                static std::chrono::steady_clock::time_point lastStretchLog;
                std::chrono::steady_clock::time_point stretchNow = std::chrono::steady_clock::now();
                if (stretchNow - lastStretchLog > std::chrono::milliseconds(300)) {
                    lastStretchLog = stretchNow;
                    // requested < terrain zoom: detail never asked for; resolved < requested: asked
                    // for and not arrived (or, over a whole area, absent from the source).
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
            // Metres to internal units at this latitude; internal z over internal x is the same slope
            // as the local frame's. The texel step is a per-vertex retry for an exactly flat result,
            // not a per-tile floor, which would make the sampling rate depend on the grid again.
            double stepInternal = 0;
            double texelStepInternal = 0;
            if (fixedScale) {
                stepInternal = normalSampleDistance * displayScale;
                if (gridTexelInternal > stepInternal) {
                    texelStepInternal = gridTexelInternal;
                }
            }
            // This tile's own grid, not a lookup by position: getDisplayHeightCached accepts whatever
            // ancestor is cached, so the normal would depend on load order. Clamped at the grid edge.
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
                    // All four or none: a half-resolved stencil measures over the wrong baseline.
                    if (internalHeightAt(internalX - stepInternal, internalY, west) && internalHeightAt(internalX + stepInternal, internalY, east) &&
                        internalHeightAt(internalX, internalY - stepInternal, south) && internalHeightAt(internalX, internalY + stepInternal, north)) {
                        dzdx = static_cast<float>((east - west) / (2 * stepInternal));
                        dzdy = static_cast<float>((north - south) / (2 * stepInternal));
                        // Counted only when a slope resolved: reads in one texel succeed but answer zero.
                        if (dzdx != 0.0f || dzdy != 0.0f) {
                            fixedScaleVertices++;
                        } else if (texelStepInternal > 0) {
                            // Flat inside one coarse texel: retry once at the texel, for this vertex only.
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

        // Provisional: read from a grid coarser than the source offers, so rebuilt once it lands.
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
        // Flat tiles by id, so two runs can be compared: a changing set means the bake depends on
        // load timing rather than on the ground.
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
#if MASSIF_VT_RENDER_STATS
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
#endif
        for (std::size_t i = gridVertices; i < vertexCount; i++) {
            std::size_t skirtIndex = i - gridVertices;
            if (skirtIndex >= mesh.skirtSources.size()) {
                break;
            }
            std::size_t source = static_cast<std::size_t>(mesh.skirtSources[skirtIndex]) * 4;
            if (source + 4 > mesh.surfaceAttribs.size()) {
                // The other way a skirt keeps its zero-filled default.
                VT_STAT_INC(terrainAttribSkirtOutOfRange);
                continue;
            }
            std::copy(mesh.surfaceAttribs.begin() + source, mesh.surfaceAttribs.begin() + source + 4, mesh.surfaceAttribs.begin() + i * 4);
            // Skirt marked by a negative normal z, unreachable for a height field: the shader must
            // keep the copied edge normal, as per-fragment DEM sampling bands a vertical wall.
            mesh.surfaceAttribs[i * 4 + 2] = -std::abs(mesh.surfaceAttribs[i * 4 + 2]);
        }
    }

    void TerrainRenderer::calculateVisibleTiles(const ViewState& viewState, const std::shared_ptr<ElevationManager>& elevationManager, const MapTile& tile, int maxZoom, float subdivideDistance, std::vector<MapTile>& tiles) const {
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
        if (subdivideDistance > 0) {
            // geo-three's LODFrustum: the straight-line distance from the eye to the centre, in
            // Mercator metres, against a threshold that doubles per level coarser.
            double metres = cglib::length(lodCenter - viewState.getCameraPos()) * Const::EARTH_CIRCUMFERENCE / worldWidth;
            subDivide = metres < subdivideDistance * std::pow(2.0, 20.0 - tile.getZoom());
        }

        // No point in subdividing beyond the resolution of the elevation data + mesh grid
        int maxUsefulZoom = Const::MAX_SUPPORTED_ZOOM_LEVEL;
        if (std::shared_ptr<TileDataSource> dataSource = elevationManager->getDataSource()) {
            maxUsefulZoom = dataSource->getMaxZoom() + 3;
        }
        // Not capped by the camera zoom, which for a ground-level camera describes the horizon and
        // cuts the near ground too coarse; MaxTileZoomOffset is folded into maxZoom instead.
        int targetTileZoom = std::min(maxUsefulZoom, maxZoom);
        if (targetTileZoom <= tile.getZoom()) {
            subDivide = false;
        }

        if (subDivide) {
            for (int n = 0; n < 4; n++) {
                calculateVisibleTiles(viewState, elevationManager, tile.getChild(n), maxZoom, subdivideDistance, tiles);
            }
        } else {
            tiles.push_back(tile);
        }
    }

    int TerrainRenderer::calculateMeshGridSize(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int meshResolution, bool fixedScaleNormals, bool referenceMesh) const {
        // A present grid with no relief is genuinely flat, one quad. A missing grid is not: the
        // heights still come from a cached ancestor, so it keeps the MIN_MESH_GRID_SIZE floor.
        if (grid && grid->getMaxHeight() - grid->getMinHeight() <= 0) {
            return 1;
        }
        if (!grid) {
            return MIN_MESH_GRID_SIZE;
        }
        if (referenceMesh) {
            // geo-three's getGeometry: the same cells per edge up to REFERENCE_MESH_FULL_ZOOM, so a
            // near tile is no denser on the ground than a far one, and no DEM or 96 cap.
            int size = meshResolution >> std::max(0, tile.getZoom() - REFERENCE_MESH_FULL_ZOOM);
            return std::min(std::max(size, REFERENCE_MIN_MESH_GRID_SIZE), MAX_INDEXED_MESH_GRID_SIZE);
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
        // With fixed-scale normals the DEM texel count must not cap the mesh: gridSize is also the
        // normal field's resolution, so the cap would make normal density per-tile. Without them a
        // mesh finer than the DEM only interpolates the same plane, so the cap stays.
        int gridSize = (fixedScaleNormals ? std::min(meshResolution, MAX_MESH_GRID_SIZE)
                                          : std::min(std::min(texelsPerTile, meshResolution), MAX_MESH_GRID_SIZE));
        return std::max(gridSize, MIN_MESH_GRID_SIZE);
    }

    std::shared_ptr<TerrainRenderer::TileMesh> TerrainRenderer::buildTileMesh(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, const std::shared_ptr<ElevationManager>& elevationManager, int gridSize, const std::array<std::vector<double>, 4>& edgeHeights, bool bilinearHeights) const {
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
        // Heights first, so the stitching below reaches both the vertices and the surface normals.
        mesh->heights.assign(rowSize * rowSize, 0.0f);
        if (grid) {
            for (int gy = 0; gy <= gridSize; gy++) {
                double internalY = originY + (static_cast<double>(gy) / gridSize) * size;
                double internalPerMeter = exaggeration * elevationManager->getDisplayScale(internalY);
                double localPerInternal = (spherical ? sphericalLocalPerInternal(tile, internalY) : localFromInternal);
                for (int gx = 0; gx <= gridSize; gx++) {
                    double internalX = originX + (static_cast<double>(gx) / gridSize) * size;
                    // the drawn surface, which this depth stands in for - or geo-three's bilinear read
                    double meters = (bilinearHeights ? grid->sampleHeight(internalX, internalY) : grid->sampleNodeHeight(internalX, internalY));
                    // The height in INTERNAL units is the same on either surface (18-globe.md);
                    // only the internal-to-tile-local factor differs, and the plane's is written
                    // out rather than derived so its depth mesh keeps the values it had.
                    mesh->heights[gy * rowSize + gx] = static_cast<float>(meters * internalPerMeter * localPerInternal);
                }
            }
        }

        // Only the edge nodes move, to the heights both tiles agree on (EdgeHeightResolver).
        if (grid) {
            for (int side = 0; side < 4; side++) {
                const std::vector<double>& edge = edgeHeights[side];
                if (edge.size() != static_cast<std::size_t>(rowSize)) {
                    continue;
                }
                for (int index = 0; index <= gridSize; index++) {
                    int gx = (side < 2 ? index : (side == 2 ? 0 : gridSize));
                    int gy = (side < 2 ? (side == 0 ? 0 : gridSize) : index);
                    double internalY = originY + (static_cast<double>(gy) / gridSize) * size;
                    double localPerInternal = (spherical ? sphericalLocalPerInternal(tile, internalY) : localFromInternal);
                    mesh->heights[gy * rowSize + gx] = static_cast<float>(edge[index] * localPerInternal);
                }
            }
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
        // In metres, not tile-local z, which made walls kilometres deep on coarse horizon tiles.
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
            // Wound to face OUT of the tile, which the skirt draw in renderTiles culls on.
            addSkirt(south, true);
            addSkirt(north, false);
            addSkirt(west, false);
            addSkirt(east, true);
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
        uniform mat4 u_demUvMat;
        uniform float u_metersPerUnit;
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        varying vec2 v_demUv;
        varying float v_elevation;
        varying float v_dist;
        void main() {
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_normal = a_normal;
            v_worldPos = (u_tileMat * vec4(a_coord, 1.0)).xyz;
            v_demUv = (u_demUvMat * vec4(a_coord, 1.0)).xy;
            v_elevation = a_elevation;
            v_dist = pos.w * u_metersPerUnit;
            gl_Position = pos;
        }
    )GLSL";

    const std::string TerrainRenderer::TERRAIN_SURFACE_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #version 100
        // No derivatives #extension: Shader translates to ESSL 3.00, where dFdx/fwidth are core and the
        // directive, landing after the translated header, fails the compile (ANGLE Metal defines the macro).
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        varying vec2 v_demUv;
        varying float v_elevation;
        varying float v_dist;

        // Slope measured per fragment from the DEM (as geo-three's MaterialHeightShader): per-vertex
        // normals smooth away every ridge narrower than a mesh cell.
        uniform sampler2D u_demTex;
        uniform vec4 u_demOriginSize;   // xy: internal origin of uv (0,0), zw: internal size of uv [0,1]
        uniform vec2 u_demInvTexSize;   // 1 / texture size, in texels
        uniform vec4 u_demDecode;       // texel -> metres, linear part
        uniform float u_demDecodeOffset;
        uniform float u_demMetersPerTexel; // ground metres per texel AT THE EQUATOR
        uniform float u_demMercatorYScale;
        uniform float u_demValid;       // 0 when no elevation texture is bound for this tile
        uniform float u_demNormalStep;  // ground metres between the taps; <= 0 is one texel

        float terrainHeightUv(vec2 uv) {
            return dot(texture2D(u_demTex, uv), u_demDecode) + u_demDecodeOffset;
        }
        float terrainHeightMetres(vec2 internalPos) {
            return terrainHeightUv((internalPos - u_demOriginSize.xy) / u_demOriginSize.zw);
        }

        // stepMetres <= 0 samples at one texel. The ground step is scaled by cos(lat) = 1/cosh(mercator y),
        // spelled with exponentials because GLSL ES 1.0 has no cosh.
        vec3 terrainNormal(float stepMetres) {
            // A SKIRT (z marked negative by the attribute bake): a vertical crack-filling wall, whose
            // fragments all share one ground position. Sampling the DEM per fragment there bands it
            // vertically; it keeps the normal of the edge it hangs from.
            if (v_normal.z < 0.0) {
                return normalize(vec3(v_normal.xy, -v_normal.z));
            }
            // Flat, not the mesh normal: a mesh-normal fallback shades in a visibly different style
            // until the elevation texture arrives.
            if (u_demValid < 0.5) {
                return vec3(0.0, 0.0, 1.0);
            }
            // In the texture's own uv (v_demUv), NOT off v_worldPos: that is an absolute internal
            // position in float, whose ulp at a mid latitude is half a z15 texel and more than a
            // z17 one, so taps a texel apart landed on quantised positions and the normal banded.
            float stepTexels = (stepMetres > 0.0 ? max(stepMetres / max(u_demMetersPerTexel, 0.0001), 1.0) : 1.0);
            vec2 stepUv = u_demInvTexSize * stepTexels;
            float west  = terrainHeightUv(v_demUv - vec2(stepUv.x, 0.0));
            float east  = terrainHeightUv(v_demUv + vec2(stepUv.x, 0.0));
            float south = terrainHeightUv(v_demUv - vec2(0.0, stepUv.y));
            float north = terrainHeightUv(v_demUv + vec2(0.0, stepUv.y));
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

    const std::string TerrainRenderer::SCENE_DEPTH_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec2 a_coord;
        varying vec2 v_uv;
        void main() {
            v_uv = a_coord * 0.5 + 0.5;
            gl_Position = vec4(a_coord, 0.0, 1.0);
        }
    )GLSL";

    // The hardware depth back to the eye distance the drawn pass packs (clip w / far), in the same
    // three bytes and with the same sky: coverage 0 where nothing was drawn.
    const std::string TerrainRenderer::SCENE_DEPTH_FRAGMENT_SHADER = R"GLSL(
        #version 100
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        // highp: a sampler defaults to lowp, which Mali honours - far depths came back as 1.0 (sky).
        uniform highp sampler2D u_depthTex;
        uniform vec2 u_nearFar;
        varying vec2 v_uv;
        void main() {
            float d = texture2D(u_depthTex, v_uv).r;
            if (d >= 1.0) {
                gl_FragColor = vec4(1.0, 1.0, 1.0, 0.0);
                return;
            }
            float n = u_nearFar.x;
            float f = u_nearFar.y;
            float w = 2.0 * n * f / ((f + n) - (d * 2.0 - 1.0) * (f - n));
            float depth = clamp(w / f, 0.0, 1.0);
            vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * depth);
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
        uniform mat4 u_demUvMat;
        uniform float u_far;
        varying float v_depth;
        varying vec3 v_normal;
        varying vec3 v_worldPos;
        varying vec2 v_demUv;
        void main() {
            vec4 pos = u_mvpMat * vec4(a_coord, 1.0);
            v_depth = pos.w / u_far;
            v_normal = a_normal;
            // The post-process differentiates what this pass packs, so it has to read the SAME
            // per-fragment normal the surface is shaded with, or the ink goes on drawing the mesh.
            v_worldPos = (u_tileMat * vec4(a_coord, 1.0)).xyz;
            v_demUv = (u_demUvMat * vec4(a_coord, 1.0)).xy;
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
        varying vec2 v_demUv;

        // Slope measured per fragment from the DEM (as geo-three's MaterialHeightShader): per-vertex
        // normals smooth away every ridge narrower than a mesh cell.
        uniform sampler2D u_demTex;
        uniform vec4 u_demOriginSize;   // xy: internal origin of uv (0,0), zw: internal size of uv [0,1]
        uniform vec2 u_demInvTexSize;   // 1 / texture size, in texels
        uniform vec4 u_demDecode;       // texel -> metres, linear part
        uniform float u_demDecodeOffset;
        uniform float u_demMetersPerTexel; // ground metres per texel AT THE EQUATOR
        uniform float u_demMercatorYScale;
        uniform float u_demValid;       // 0 when no elevation texture is bound for this tile
        uniform float u_demNormalStep;  // ground metres between the taps; <= 0 is one texel

        float terrainHeightUv(vec2 uv) {
            return dot(texture2D(u_demTex, uv), u_demDecode) + u_demDecodeOffset;
        }
        float terrainHeightMetres(vec2 internalPos) {
            return terrainHeightUv((internalPos - u_demOriginSize.xy) / u_demOriginSize.zw);
        }

        // stepMetres <= 0 samples at one texel. The ground step is scaled by cos(lat) = 1/cosh(mercator y),
        // spelled with exponentials because GLSL ES 1.0 has no cosh.
        vec3 terrainNormal(float stepMetres) {
            // A SKIRT (z marked negative by the attribute bake): a vertical crack-filling wall, whose
            // fragments all share one ground position. Sampling the DEM per fragment there bands it
            // vertically; it keeps the normal of the edge it hangs from.
            if (v_normal.z < 0.0) {
                return normalize(vec3(v_normal.xy, -v_normal.z));
            }
            // Flat, not the mesh normal: a mesh-normal fallback shades in a visibly different style
            // until the elevation texture arrives.
            if (u_demValid < 0.5) {
                return vec3(0.0, 0.0, 1.0);
            }
            // In the texture's own uv (v_demUv), NOT off v_worldPos: that is an absolute internal
            // position in float, whose ulp at a mid latitude is half a z15 texel and more than a
            // z17 one, so taps a texel apart landed on quantised positions and the normal banded.
            float stepTexels = (stepMetres > 0.0 ? max(stepMetres / max(u_demMetersPerTexel, 0.0001), 1.0) : 1.0);
            vec2 stepUv = u_demInvTexSize * stepTexels;
            float west  = terrainHeightUv(v_demUv - vec2(stepUv.x, 0.0));
            float east  = terrainHeightUv(v_demUv + vec2(stepUv.x, 0.0));
            float south = terrainHeightUv(v_demUv - vec2(0.0, stepUv.y));
            float north = terrainHeightUv(v_demUv + vec2(0.0, stepUv.y));
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
            // Octahedral, upper hemisphere only: a height field's normal never points down, so no fold.
            // Per fragment, the normal the surface is shaded with: a vertex normal caps ridge lines at mesh-cell size.
            vec3 n = terrainNormal(u_demNormalStep);
            float l1 = abs(n.x) + abs(n.y) + abs(n.z);
            // Diagnostic: (1, -1) has L1 norm 2, so no real normal encodes to it, unlike (0, 0) which
            // is both flat ground and the sky clear colour.
            vec2 oct = l1 > 0.0001 ? n.xy / l1 : vec2(1.0, -1.0);
            gl_FragColor = vec4(enc, oct * 0.5 + 0.5);
        }
    )GLSL";
}
