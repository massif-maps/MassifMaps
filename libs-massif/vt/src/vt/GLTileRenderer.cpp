#include "GLTileRenderer.h"

#include <cctype>
#include "SpanGeometry.h"
#include "SpanDrapeLight.h"
#include "ExtrusionFloor.h"
#include "GLTileRendererShaders.h"
#include "Color.h"
#include "TileGeometryIterator.h"
#include "TileSurfaceBuilder.h"
#include "TerrainElevationScale.h"
#include "BitmapManager.h"
#include "RenderTileBlend.h"
#include "GroundCover.h"
#include "LabelCuller.h"
#include "RenderStats.h"
#include "ShadowBox.h"

#include <array>
#include <cassert>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>

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

    // A linker-dropped attribute has location -1, which as an attribute index is GL_INVALID_VALUE
    // (the depth-only shadow caster programs).
    void enableVertexAttrib(GLint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* offset) {
        if (index < 0) {
            return;
        }
        glVertexAttribPointer(static_cast<GLuint>(index), size, type, normalized, stride, offset);
        glEnableVertexAttribArray(static_cast<GLuint>(index));
    }

    void disableVertexAttrib(GLint index) {
        if (index >= 0) {
            glDisableVertexAttribArray(static_cast<GLuint>(index));
        }
    }

    void setConstVertexAttrib(GLint index, float x, float y, float z) {
        if (index >= 0) {
            glVertexAttrib3f(static_cast<GLuint>(index), x, y, z);
        }
    }

    void setConstVertexAttrib(GLint index, float x, float y, float z, float w) {
        if (index >= 0) {
            glVertexAttrib4f(static_cast<GLuint>(index), x, y, z, w);
        }
    }

}

namespace massif::vt {
    // SHADOW_CUTOUT_DISTANCE_FACTOR (header) also sets the fade range. Cascades step by this from the
    // cutout, so two cascades split at cutout/3 - mapbox's 1.5x against a 4.5x cutout.
    static constexpr double SHADOW_CASCADE_STEP = 3.0;
    // Light box margin over its sphere, as a fraction of the radius: the box holds still while the
    // camera moves inside it, so the cached caster pages are reused rather than redrawn every frame.
    static constexpr double SHADOW_BOX_PADDING = 0.2;
    // Shadow-side terrain grid cap: the caster and mask passes cost a full terrain draw each, and at 128
    // the mask alone doubled the frame (08-lighting-sky-fog.md).
    static constexpr int SHADOW_GRID_MAX_RESOLUTION = 64;
    // mapbox's noShadowCutoff (draw_fill_extrusion.ts): below it a FADING extrusion stops casting.
    static constexpr float SHADOW_NO_CAST_OPACITY_CUTOFF = 0.65f;
    // Globe ground-caster surfaces tessellated per pass: each costs ms and a sun move sweeps ~100 in.
    static constexpr int SHADOW_CASTER_SURFACE_BUDGET = 2;
    // Scene light quantisation in a drape fingerprint: every step crossed re-bakes the whole cover.
    static constexpr float DRAPE_LIGHT_STEPS = 16.0f;
    // Layer opacity quantisation in a drape fingerprint: below one 8-bit alpha step it cannot show.
    static constexpr float DRAPE_OPACITY_STEPS = 255.0f;
    // maplibre covering_tiles.ts / mercator_utils.ts verbatim: tallest assumed feature, the angle
    // above the horizon where the culling box starts growing to hold it, and the horizon.
    static constexpr double ASSUMED_MAX_FEATURE_HEIGHT_METERS = 500.0;
    static constexpr double TILE_CULLING_HORIZON_ONSET_DEGREES = 15.0;
    static constexpr double MAX_HORIZON_ANGLE_DEGREES = 89.25;
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

        bool changed = (_lightingShaderNormalMap.has_value() != lightingShaderNormalMap.has_value()) ||
                       (_lightingShaderNormalMap && (_lightingShaderNormalMap->shader != lightingShaderNormalMap->shader || _lightingShaderNormalMap->perVertex != lightingShaderNormalMap->perVertex));
        _lightingShaderNormalMap = lightingShaderNormalMap;
        if (!changed) {
            return;
        }
        // Only the programs compiled with it: the program ids carry the lighting mode, not the shader
        // text. On the GL thread, as setFogShaderSource.
        for (LightingMode mode : { LightingMode::NORMALMAP, LightingMode::TERRAINPAINT }) {
            std::string tag = "_l" + std::to_string(static_cast<int>(mode));
            for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); ) {
                std::size_t pos = it->first.find(tag);
                std::size_t end = pos + tag.size();
                if (pos != std::string::npos && (end == it->first.size() || !std::isdigit(static_cast<unsigned char>(it->first[end])))) {
                    deleteShaderProgram(it->second);
                    it = _shaderProgramMap.erase(it);
                } else {
                    ++it;
                }
            }
        }
        _shaderProgramCache.clear();
        resetProgramState();
    }

    void GLTileRenderer::setInteractionMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _interactionMode = enabled;
    }

    void GLTileRenderer::setTerrainMode(bool enabled, float depthBias) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainMode = enabled;
        _terrainDepthBias = depthBias;
        updateTerrainSkirts();
    }

    void GLTileRenderer::setTerrainRegularGrid(bool enabled, int resolution) {
        std::lock_guard<std::mutex> lock(_mutex);

        bool wasEnabled = _terrainRegularGrid;
        _terrainRegularGrid = enabled;
        if (!enabled) {
            _terrainGridSurfaces.clear();
            _terrainGridSkirtSurfaces.clear();
        } else if (resolution != _terrainRegularGridResolution) {
            _terrainRegularGridResolution = resolution;
            _terrainGridSurfaces.clear(); // rebuilt lazily; the old compiled VBO is released in endFrame
            _terrainGridSkirtSurfaces.clear();
        }
        if (wasEnabled != enabled) {
            buildTerrainEdgeCoarsening(); // stitching only exists in regular grid mode
        }
    }

    void GLTileRenderer::setTerrainLayerOrdinalBase(int base) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainLayerOrdinalBase = base;
    }

    int GLTileRenderer::getStyleLayerCount() const {
        std::lock_guard<std::mutex> lock(_mutex);

        return _terrainStyleLayersDrawn;
    }

    void GLTileRenderer::setTerrainEdgeStitching(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_terrainEdgeStitching != enabled) {
            _terrainEdgeStitching = enabled;
            buildTerrainEdgeCoarsening();
        }
    }

    void GLTileRenderer::setTerrainSlackScale(float slackScale) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainSlackScale = slackScale;
    }

    void GLTileRenderer::setTerrainLineClearance(float clearance) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainLineClearance = clearance;
    }

    void GLTileRenderer::setTerrainContentDepthShift(float depthShift) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainContentDepthShift = depthShift;
    }

    void GLTileRenderer::setSpansEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_spanResolver.isEnabled() == enabled) {
            return;
        }
        _spanResolver.setEnabled(enabled); // forgets every chord
        _spanDrapeBounds.clear();
        invalidateExtrusionBases();
        _pendingLabelElevationAll = true;
    }

    void GLTileRenderer::setTerrainDrapeFills(bool enabled, bool includeLines) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainDrapeFills = enabled;
        _terrainDrapeLines = enabled && includeLines;
    }

    void GLTileRenderer::setTerrainDrapeResolution(int resolution) {
        std::lock_guard<std::mutex> lock(_mutex);

        int size = std::min(2048, std::max(128, resolution));
        if (size != _drapeTextureSize) {
            _drapeTextureSize = size;
            // Cached textures are the old size; drop them (and the pool) so they re-bake.
            _drapeStaleTextures.insert(_drapeStaleTextures.end(), _drapeTexturePool.begin(), _drapeTexturePool.end());
            _drapeTexturePool.clear();
            for (auto it = _drapeTextures.begin(); it != _drapeTextures.end(); it++) {
                _drapeStaleTextures.push_back(it->second);
            }
            _drapeTextures.clear();
            _drapeFingerprints.clear();
        }
    }

    void GLTileRenderer::setTerrainLighting(const TerrainLighting& lighting) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainLighting = lighting;
    }

    void GLTileRenderer::setTerrainPaint(const TerrainPaint& paint) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainPaint = paint;
    }

    void GLTileRenderer::setTerrainPaintOnGround(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainPaintOnGround = enabled;
    }

    void GLTileRenderer::setTerrainDemTaps(int taps) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainDemTaps = taps;
    }

    void GLTileRenderer::setTerrainTileBackgrounds(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainTileBackgrounds = enabled;
    }

    void GLTileRenderer::setTileMasks(int mode) {
        std::lock_guard<std::mutex> lock(_mutex);

        _tileMasks = mode;
    }

    void GLTileRenderer::setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const cglib::vec3<float>& depthBias, const std::array<float, MAX_SHADOW_CASCADES>& depthScales, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec2<float>& fadeRange, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainShadowTexture = texture;
        _terrainShadowMapSize = mapSize;
        _terrainShadowCascades = std::max(1, std::min(MAX_SHADOW_CASCADES, cascades));
        _terrainShadowBias = depthBias;
        _terrainShadowDepthScales = depthScales;
        _terrainShadowStrength = strength;
        _terrainShadowSoftness = softness;
        _terrainShadowDepthTexture = depthTexture;
        _terrainShadowHardwarePCF = hardwarePCF;
        _terrainShadowNormalOffset = normalOffset;
        _terrainShadowFadeRange = fadeRange;
        _terrainShadowSunDir = sunDir;
        _terrainShadowViewProjs = lightViewProjs;
        // Pages past the cascade count are never uploaded nor sampled (shadowReceiverFlags compiles
        // for the count), so CLAMP_TO_EDGE never reads a neighbouring page.
    }

    void GLTileRenderer::setFog(const Color& color, float startDistance, float distance, float rangeScale, float horizonBlend) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogColor = color;
        _fogRangeScale = std::max(1.0e-9f, rangeScale);
        _fogStartDistance = startDistance / _fogRangeScale;
        _fogDistance = distance / _fogRangeScale;
        // 0 would mean no fog in the sky, not a sharp horizon edge; mapbox's floor.
        _fogHorizonBlend = std::max(0.0005f, horizonBlend);
    }

    void GLTileRenderer::setFogVertical(float startMeters, float endMeters, float metersPerUnit, float cameraHeightMeters) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogVerticalStart = startMeters;
        _fogVerticalEnd = endMeters;
        _fogMetersPerUnit = metersPerUnit;
        _fogCameraHeight = cameraHeightMeters;
    }

    void GLTileRenderer::setFogRayBasis(const cglib::mat3x3<float>& rayBasis) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogRayBasis = rayBasis;
    }

    void GLTileRenderer::setFogColors(const Color& highColor, const Color& spaceColor) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogHighColor = highColor;
        _fogSpaceColor = spaceColor;
    }

    void GLTileRenderer::setFogShaderSource(const std::string& shaderSource) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_fogShaderSource == shaderSource) {
            return;
        }
        _fogShaderSource = shaderSource;
        // The blend is compiled into every program. Deleting is safe: we are on the GL thread.
        for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); it++) {
            deleteShaderProgram(it->second);
        }
        _shaderProgramMap.clear();
        _shaderProgramCache.clear();
        resetProgramState();
    }

    bool GLTileRenderer::calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const std::vector<std::pair<double, double> >& casterHeights, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (tileIds.empty()) {
            texelMeters = -1; // diagnostic: which fit bail-out fired
            return false;
        }
        // A globe has no world rectangle nor world-Z slab (up is radial); both only refine a box the
        // frustum already gives, so the spherical path skips them (18-globe.md).
        bool spherical = _transformer->isSpherical();
        // Heights go through the DEM's metres-to-internal factor: internal units rescale per projection.
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool first = true;
        if (!spherical) {
            for (const TileId& tileId : tileIds) {
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileId, 1.0f);
                for (int corner = 0; corner < 4; corner++) {
                    cglib::vec4<double> p = cglib::transform(cglib::vec4<double>(corner & 1 ? 1.0 : 0.0, corner & 2 ? 1.0 : 0.0, 0.0, 1.0), tileMatrix);
                    if (first) {
                        minX = maxX = p(0);
                        minY = maxY = p(1);
                        first = false;
                    } else {
                        minX = std::min(minX, p(0)); maxX = std::max(maxX, p(0));
                        minY = std::min(minY, p(1)); maxY = std::max(maxY, p(1));
                    }
                }
            }
            if (first || maxX <= minX || maxY <= minY) {
                texelMeters = -2; // diagnostic: which fit bail-out fired
                return false;
            }
        }
        // The factor is the projection's, so any tile's DEM gives it - the front tile's CACHED_ONLY
        // DEM may not be decoded yet.
        double metersToInternal = 0;
        for (const TileId& tileId : tileIds) {
            const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
            if (resolved.first && resolved.second.metersToInternal > 0) {
                metersToInternal = resolved.second.metersToInternal;
                break;
            }
        }
        if (metersToInternal <= 0) {
            // No DEM (flat 2D map): the caller's projection factor still lets buildings cast.
            metersToInternal = _metersToInternal;
        }
        if (metersToInternal <= 0) {
            texelMeters = -3; // diagnostic: which fit bail-out fired
            return false;
        }
        double minZ = -1000.0 * metersToInternal;
        double maxZ = 9000.0 * metersToInternal;
        if (maxHeight > minHeight) {
            minZ = minHeight;
            maxZ = maxHeight;
        }
        // Cascade box = minimum bounding sphere of its frustum slice (mapbox createLightMatrix): it
        // depends on slice distances and fov only, so the texel size is camera-independent.
        double sphereRadius = 0;
        cglib::vec3<double> sphereCenter(0, 0, 0);
        {
            // Cutout = factor x camera-to-focus distance. _viewState is this frame's camera:
            // prepareTerrainDrapeFrame sets it before the shadow pass.
            double cutout = (cameraDistance > 0 ? (distanceFactor > 0 ? distanceFactor : SHADOW_CUTOUT_DISTANCE_FACTOR) * cameraDistance : 0);
            if (!(cutout > 0)) {
                texelMeters = -4; // no camera-to-focus distance: nothing to fit a slice to
                return false;
            }
            auto sliceFarAt = [&](int index) {
                double f = cutout;
                for (int i = index; i + 1 < cascadeCount; i++) {
                    f /= SHADOW_CASCADE_STEP;
                }
                return f;
            };
            double sliceNear = (cascade > 0 ? sliceFarAt(cascade - 1) : 0.0);
            double sliceFar = sliceFarAt(cascade);
            // k = tan of the half-angle to a frustum corner: m(0,0) = 1/tan(fovX/2), m(1,1) = 1/tan(fovY/2).
            const cglib::mat4x4<double>& proj = _viewState.projectionMatrix;
            double tanX = (std::abs(proj(0, 0)) > 1.0e-12 ? 1.0 / std::abs(proj(0, 0)) : 1.0);
            double tanY = (std::abs(proj(1, 1)) > 1.0e-12 ? 1.0 / std::abs(proj(1, 1)) : 1.0);
            double k2 = tanX * tanX + tanY * tanY;
            double farMinusNear = sliceFar - sliceNear, farPlusNear = sliceFar + sliceNear;
            double centerDepth = 0;
            if (k2 > farMinusNear / std::max(1.0e-12, farPlusNear)) {
                centerDepth = sliceFar;
                sphereRadius = sliceFar * std::sqrt(k2);
            } else {
                centerDepth = 0.5 * farPlusNear * (1.0 + k2);
                sphereRadius = 0.5 * std::sqrt(farMinusNear * farMinusNear + 2.0 * (sliceFar * sliceFar + sliceNear * sliceNear) * k2 + farPlusNear * farPlusNear * k2 * k2);
            }
            // Half a texel of slack, so snapping the box below cannot uncover its own edge.
            sphereRadius *= static_cast<double>(mapSize) / std::max(1, mapSize - 1);
            // The camera looks along -Z of its own basis; orientation holds that basis in world.
            cglib::vec3<double> viewDir = -cglib::vec3<double>::convert(_viewState.orientation[2]);
            sphereCenter = _viewState.origin + viewDir * centerDepth;
            // The slab narrowing and caster prefilter need a rectangle: the sphere's, clipped to the tiles.
            if (!spherical) {
                double trimMinX = std::max(minX, sphereCenter(0) - sphereRadius), trimMaxX = std::min(maxX, sphereCenter(0) + sphereRadius);
                double trimMinY = std::max(minY, sphereCenter(1) - sphereRadius), trimMaxY = std::min(maxY, sphereCenter(1) + sphereRadius);
                if (trimMaxX > trimMinX && trimMaxY > trimMinY) {
                    minX = trimMinX; maxX = trimMaxX;
                    minY = trimMinY; maxY = trimMaxY;
                }
            }
        }

        // Narrow the slab to this cascade's ground, or at a low sun the whole relief sizes every box.
        double casterMinZ = minZ, casterMaxZ = maxZ; // the casters' slab, before narrowing
        if (tileHeights.size() == tileIds.size() && !spherical) {
            double localMinZ = 0, localMaxZ = 0;
            bool localFirst = true;
            for (std::size_t i = 0; i < tileIds.size(); i++) {
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileIds[i], 1.0f);
                double tileMinX = 0, tileMinY = 0, tileMaxX = 0, tileMaxY = 0;
                for (int corner = 0; corner < 4; corner++) {
                    cglib::vec4<double> p = cglib::transform(cglib::vec4<double>(corner & 1 ? 1.0 : 0.0, corner & 2 ? 1.0 : 0.0, 0.0, 1.0), tileMatrix);
                    if (corner == 0) {
                        tileMinX = tileMaxX = p(0);
                        tileMinY = tileMaxY = p(1);
                    } else {
                        tileMinX = std::min(tileMinX, p(0)); tileMaxX = std::max(tileMaxX, p(0));
                        tileMinY = std::min(tileMinY, p(1)); tileMaxY = std::max(tileMaxY, p(1));
                    }
                }
                if (tileMaxX < minX || tileMinX > maxX || tileMaxY < minY || tileMinY > maxY) {
                    continue;
                }
                if (localFirst) {
                    localMinZ = tileHeights[i].first;
                    localMaxZ = tileHeights[i].second;
                    localFirst = false;
                } else {
                    localMinZ = std::min(localMinZ, tileHeights[i].first);
                    localMaxZ = std::max(localMaxZ, tileHeights[i].second);
                }
            }
            // Floor the thickness (an unloaded tile reports an empty range) by widening, not by
            // refusing to narrow - a valley cascade would keep the whole massif's slab.
            double minThickness = (casterMaxZ - casterMinZ) * 0.02;
            if (!localFirst) {
                double slack = 0.5 * std::max(0.0, minThickness - (localMaxZ - localMinZ));
                double narrowedMinZ = std::max(minZ, localMinZ - slack);
                double narrowedMaxZ = std::min(maxZ, localMaxZ + slack);
                if (narrowedMaxZ > narrowedMinZ) {
                    minZ = narrowedMinZ;
                    maxZ = narrowedMaxZ;
                }
            }
        }
        // Metric headroom for what stands on the DEM, or roofs get clipped as casters and fall outside
        // every page as receivers; a relief-relative margin is nothing on flat ground.
        double standingHeadroom = 200.0 * metersToInternal;
        maxZ += standingHeadroom;
        casterMaxZ += standingHeadroom;

        // World units from here: a globe's height is radial, not frame z, so convert (18-globe.md).
        double worldPerMeter = metersToInternal;
        if (spherical) {
            const TileId& scaleTile = tileIds.front();
            worldPerMeter = _transformer->createTileVertexTransformer(scaleTile)->calculateHeight(cglib::vec2<float>(0.5f, 0.5f), 1.0f)
                          * calculateTileMatrix(scaleTile, 1.0f)(2, 2);
            if (!(worldPerMeter > 0)) {
                texelMeters = -3; // diagnostic: which fit bail-out fired
                return false;
            }
            double worldPerInternal = worldPerMeter / metersToInternal;
            minZ *= worldPerInternal; maxZ *= worldPerInternal;
            casterMinZ *= worldPerInternal; casterMaxZ *= worldPerInternal;
        }

        // The sun is in map ENU: the world frame on a plane, the view's local frame on a globe
        // (as uLightingFrame), so the light box and the shading agree.
        cglib::vec3<double> dir = cglib::unit(cglib::vec3<double>(sunDir(0), sunDir(1), sunDir(2)));
        double sunUp = dir(2);
        if (sunUp < 0.05) {
            texelMeters = -6; // diagnostic: which fit bail-out fired
            return false; // sun at or below the horizon: nothing is meaningfully lit
        }
        if (spherical) {
            cglib::vec3<double> anchorUp = _viewState.origin;
            double anchorLen = cglib::length(anchorUp);
            anchorUp = (anchorLen > 0 ? anchorUp * (1.0 / anchorLen) : cglib::vec3<double>(0, 0, 1));
            double h = std::sqrt(anchorUp(0) * anchorUp(0) + anchorUp(1) * anchorUp(1));
            cglib::vec3<double> east = (h > 1.0e-9 ? cglib::vec3<double>(-anchorUp(1) / h, anchorUp(0) / h, 0) : cglib::vec3<double>(1, 0, 0));
            cglib::vec3<double> north = cglib::vector_product(anchorUp, east);
            dir = cglib::unit(east * dir(0) + north * dir(1) + anchorUp * dir(2));
        }
        cglib::vec3<double> up = std::abs(dir(2)) > 0.99 ? cglib::vec3<double>(0, 1, 0) : cglib::vec3<double>(0, 0, 1);
        // A pure rotation about the world origin, not a look-at on the box: the texel lattice stays
        // world-anchored instead of sliding under the terrain as the camera moves.
        cglib::mat4x4<double> lightView = cglib::lookat4_matrix(dir, cglib::vec3<double>(0, 0, 0), up);

        // Sides from the bounding sphere: it projects to the same square from any direction, so the
        // texel size ignores pitch, bearing and sun azimuth.
        cglib::vec4<double> lightCenter = cglib::transform(cglib::vec4<double>(sphereCenter(0), sphereCenter(1), sphereCenter(2), 1.0), lightView);
        ShadowBox box = ShadowBox::fit(lightCenter(0), lightCenter(1), -lightCenter(2), sphereRadius, SHADOW_BOX_PADDING, mapSize);
        double l = box.left, r = box.right, b = box.bottom, t = box.top;
        // Depth: the same box plus caster headroom (mapbox lightMatrixNearZ/FarZ); the drawn
        // rectangle gave ranges 24-bit depth cannot separate.
        double casterHeadroom = (casterMaxZ - casterMinZ) / std::max(0.05, sunUp);
        double centerDepth = box.centerDepth, halfSize = box.halfSize;
        double n = centerDepth - halfSize - casterHeadroom;
        double f = centerDepth + halfSize + casterHeadroom;
        double marginX = (r - l) / std::max(1, mapSize), marginY = (t - b) / std::max(1, mapSize);
        for (std::size_t casterIndex = 0; casterIndex < casterTileIds.size(); casterIndex++) {
            const TileId& tileId = casterTileIds[casterIndex];
            cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileId, 1.0f);
            // The tile's own column, not the whole slab: a valley tile swept up to the summits falls in
            // every cascade. Clamped to the slab, whose headroom holds what stands on the ground.
            double tileMinZ = casterMinZ, tileMaxZ = casterMaxZ;
            if (casterHeights.size() == casterTileIds.size() && !spherical) {
                tileMinZ = std::max(casterMinZ, casterHeights[casterIndex].first);
                tileMaxZ = std::min(casterMaxZ, casterHeights[casterIndex].second + standingHeadroom);
            }
            double tileL = 0, tileR = 0, tileB = 0, tileT = 0, tileN = 0, tileF = 0;
            bool firstPoint = true;
            auto addPoint = [&](const cglib::vec3<double>& world) {
                cglib::vec4<double> p = cglib::transform(cglib::vec4<double>(world(0), world(1), world(2), 1.0), lightView);
                if (firstPoint) {
                    tileL = tileR = p(0); tileB = tileT = p(1); tileN = tileF = -p(2);
                    firstPoint = false;
                } else {
                    tileL = std::min(tileL, p(0)); tileR = std::max(tileR, p(0));
                    tileB = std::min(tileB, p(1)); tileT = std::max(tileT, p(1));
                    tileN = std::min(tileN, -p(2)); tileF = std::max(tileF, -p(2));
                }
            };
            if (spherical) {
                // The tile's patch of the ball, raised along each sample's own radial; the centre
                // sample keeps the bulge a four-corner hull cuts off.
                static const cglib::vec2<float> PATCH_UVS[5] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0.5f, 0.5f } };
                std::shared_ptr<const TileTransformer::VertexTransformer> vertexTransformer = _transformer->createTileVertexTransformer(tileId);
                for (const cglib::vec2<float>& uv : PATCH_UVS) {
                    cglib::vec3<double> ground = cglib::transform_point(cglib::vec3<double>::convert(vertexTransformer->calculatePoint(uv)), tileMatrix);
                    double groundLen = cglib::length(ground);
                    cglib::vec3<double> up = (groundLen > 0 ? ground * (1.0 / groundLen) : cglib::vec3<double>(0, 0, 1));
                    addPoint(ground + up * casterMinZ);
                    addPoint(ground + up * casterMaxZ);
                }
            } else {
                for (int corner = 0; corner < 8; corner++) {
                    cglib::vec4<double> local(corner & 1 ? 1.0 : 0.0, corner & 2 ? 1.0 : 0.0, 0.0, 1.0);
                    cglib::vec4<double> world = cglib::transform(local, tileMatrix);
                    world(2) = (corner & 4 ? tileMaxZ : tileMinZ);
                    addPoint(cglib::vec3<double>(world(0), world(1), world(2)));
                }
            }
            // Light-space xy is constant along a ray, so a tile missing the box cannot cast into it.
            // This is also the cascade's caster draw list.
            if (tileR < l - marginX || tileL > r + marginX || tileT < b - marginY || tileB > t + marginY) {
                continue;
            }
            boxCasterTileIds.push_back(tileId);
        }
        {
            // Eighths of a power of two, on a lattice of that step: the range, hence the normalised
            // bias, stays constant, and a still matrix lets the caster pass be skipped.
            double step = std::pow(2.0, std::floor(std::log2(f - n)) - 3.0);
            double quantSize = std::ceil((f - n) / step) * step;
            if (quantSize - (f - n) < step) {
                quantSize += step; // snapping moves the low edge down by up to one step
            }
            n = std::floor(n / step) * step;
            f = n + quantSize;
        }
        lightViewProj = cglib::ortho4_matrix(l, r, b, t, n, f) * lightView;
        // The shader bias is a fraction of normalised depth; the caller divides its metric bias by this.
        depthRangeMeters = (f - n) / worldPerMeter;
        // Ground metres per shadow texel, reported for logging.
        texelMeters = std::max(r - l, t - b) / std::max(1, mapSize) / worldPerMeter;
        return true;
    }

    bool GLTileRenderer::extrusionCastsShadow(const RenderTileLayer& renderLayer) const {
        // mapbox's rule: on terrain a ZOOM-DEPENDENT opacity below the cutoff stops casting; a constant
        // translucent one still casts.
        const FloatFunction& opacityFunc = renderLayer.layer->getOpacityFunc();
        if (opacityFunc.function() && opacityFunc(_viewState) < SHADOW_NO_CAST_OPACITY_CUTOFF) {
            return false;
        }
        // Ours: the tile arrival blend too, or a fading-in building casts a full shadow. mapbox never
        // casts from outside its render set.
        return renderLayer.blend >= SHADOW_NO_CAST_OPACITY_CUTOFF;
    }

    template <typename Func>
    void GLTileRenderer::forEachVisibleExtrusion(const std::vector<TileId>* coveredBy, bool offscreen, Func&& func) const {
        if (!_visibleRenderTiles) {
            return;
        }
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible && !offscreen) {
                continue;
            }
            // Off-screen render tiles are the shadow caster ring, next to the view by construction.
            if (coveredBy && renderTile.visible) {
                bool covered = false;
                for (const TileId& tileId : *coveredBy) {
                    // Either way round: a caster ring tile is often coarser than the render tile inside it.
                    if (renderTile.targetTileId.intersects(tileId)) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    continue;
                }
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.layer) {
                    continue;
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        continue;
                    }
                    if (!func(renderLayer, geometry)) {
                        break;
                    }
                }
            }
        }
    }

    // The shared grid surface is a flat unit square a spherical tile matrix cannot curve, so the globe
    // takes per-tile surfaces. Only the geometry differs: the painter depth model is the same.
    bool GLTileRenderer::terrainGridSurfaces() const {
        return _terrainRegularGrid && !(_transformer && _transformer->isSpherical());
    }

    int GLTileRenderer::renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!(_terrainMode && _terrainTextureProvider)) {
            return 0;
        }
        int draws = 0;
        // One program and one VBO for every tile's ground: on a translated GL (emulator, ANGLE) the
        // call count, not the triangle count, is the cost.
        if (castGround && !terrainGridSurfaces()) {
            // A globe has no shared grid: each tile has its own curved surface (18-globe.md).
            cglib::mat4x4<double> surfaceFrame = cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
            cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(lightViewProj * surfaceFrame);
            const ShaderProgram& shaderProgram = buildShaderProgram("shadowcaster", backgroundVsh, shadowCasterFsh, LightingMode::NONE, RasterFilterMode::NONE, TERRAIN_VTF_FLAG);
            useProgram(shaderProgram);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
            // Caster tiles are often off screen with no cached surface: ration new tessellations, or a
            // sun move sweeping a new ring in stalls the frame (18-globe.md).
            int surfaceBudget = SHADOW_CASTER_SURFACE_BUDGET;
            for (const TileId& tileId : tileIds) {
                if (!_tileSurfaceBuilder.isTileSurfaceCached(tileId) && surfaceBudget-- <= 0) {
                    continue;
                }
                if (!setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, false)) {
                    _shadowCastersMissingElevation++;
                    continue;
                }
                for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTileSurfaces(tileId)) {
                    const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
                    const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

                    glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
                    bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);
                    glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
                    VT_STAT_INC(surfaceDraws);
                    VT_STAT_INC(surfShadowDraws);
                    VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
                    draws++;

                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                }
            }
            checkGLError();
        } else if (castGround) {
            for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTerrainShadowGridSurfaces()) {
                const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
                const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

                const ShaderProgram& shaderProgram = buildShaderProgram("shadowcaster", backgroundVsh, shadowCasterFsh, LightingMode::NONE, RasterFilterMode::NONE, TERRAIN_VTF_FLAG);
                useProgram(shaderProgram);
                glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

                for (const TileId& tileId : tileIds) {
                    cglib::mat4x4<double> surfaceFrame = calculateTileMatrix(tileId, 1.0f);
                    cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(lightViewProj * surfaceFrame);
                    // No elevation yet: it would cast as a sea-level plane, and it receives no shadow either.
                    if (!setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, true)) {
                        _shadowCastersMissingElevation++;
                        continue;
                    }
                    glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
                    glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfShadowDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
                    draws++;
                }

                disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                checkGLError();
            }
        }

        // Extrusions cast too (the drape cannot hold them), from both faces: culling front faces
        // detached the shadow from its footprint. Acne is left to the slope-scaled caster offset.
        _shadowCasterViewProj = &lightViewProj;
        _shadowCasterSun = true;
        forEachVisibleExtrusion(&tileIds, true, [this, &draws](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>& geometry) {
            if (!extrusionCastsShadow(renderLayer)) {
                return true;
            }
            // Same rule as the on-screen draw, which runs after: an unresolved span deck still holds the
            // sentinel and would cast a wall to the ground.
            bool baseResolved = resolveExtrusionBases(renderLayer.sourceTileId, renderLayer.targetTileId, geometry);
            if (!geometry->getSpanRecords().empty() && !baseResolved) {
                return true;
            }
            renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
            draws++;
            return true;
        });
        _shadowCasterSun = false;
        _shadowCasterViewProj = nullptr;
        return draws;
    }

    float GLTileRenderer::shadowCasterFadeSignature(const std::vector<TileId>* coveredBy) const {
        std::lock_guard<std::mutex> lock(_mutex);

        // Moves exactly as fast as the caster set, so the owner refreshes the shadow map on a visible
        // change rather than every fade frame. Caster height no longer follows the blend.
        float signature = 0.0f;
        int count = 0;
        forEachVisibleExtrusion(coveredBy, true, [this, &signature, &count](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>&) {
            // Only casters, or a layer crossing the no-cast cutoff changes the set unseen.
            if (!extrusionCastsShadow(renderLayer)) {
                return false;
            }
            signature += renderLayer.blend;
            count++;
            return false; // one contribution per layer, not per geometry batch
        });
        // The mean, or tiles fading together move it faster; times the height scale (a style can ramp
        // it), plus the count (extrusions usually arrive at blend 1).
        return count > 0 ? _buildingHeightScale * (static_cast<float>(count) + signature / count) : 0.0f;
    }

    float GLTileRenderer::groundAOZoomFade(float zoom) {
        // A contact shadow is metres wide: at low zoom it is sub-pixel, all cost. Faded, not switched,
        // or a city's shadows pop in one frame.
        return std::max(0.0f, std::min(1.0f, zoom - GROUND_AO_MIN_ZOOM));
    }

    void GLTileRenderer::setGroundAO(float intensity, float attenuation) {
        std::lock_guard<std::mutex> lock(_mutex);

        _groundAOIntensity = intensity;
        _groundAOAttenuation = attenuation;
        refreshGroundAOBakeable();
    }

    void GLTileRenderer::setBuildingHeight(float scale, float viewScale, bool growOnAppear, bool fadeOnAppear) {
        std::lock_guard<std::mutex> lock(_mutex);

        _buildingHeightScale = scale;
        _buildingHeightViewScale = viewScale;
        _buildingGrowOnAppear = growOnAppear;
        _buildingFadeOnAppear = fadeOnAppear;
    }

    bool GLTileRenderer::isGroundAOActive() const {
        std::lock_guard<std::mutex> lock(_mutex);

        return hasGroundAOTiles(groundAOZoomFade(_viewState.zoom));
    }

    // A cached flag, so the per-frame drape fingerprint never takes _mutex. No zoom fade, unlike the
    // screen-space pass: a bake is redone only when tile content changes, so camera state stays out.
    bool GLTileRenderer::isGroundAOBakeable() const {
        return _groundAOBakeable.load();
    }

    void GLTileRenderer::refreshGroundAOBakeable() {
        _groundAOBakeable.store(hasGroundAOTiles(1.0f));
    }

    bool GLTileRenderer::hasGroundAOTiles(float fade) const {
        if (!_visibleRenderTiles || !(_groundAOIntensity * fade > 0.0f)) {
            return false;
        }
        // ...and something to draw: the mask bind and clear cost the same with no capsule to follow.
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                if (hasGroundAOContent(it->second)) {
                    return true;
                }
            }
        }
        return false;
    }

    int GLTileRenderer::renderGroundAOMask() {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles || !(_groundAOIntensity * groundAOZoomFade(_viewState.zoom) > 0.0f)) {
            return 0;
        }

        // MIN into a white-cleared mask: overlapping capsules take the darkest instead of compounding
        // to black - the reason this pass exists.
        glDisable(GL_CULL_FACE); // a capsule quad's winding follows its edge; both sides count

        // Seed the mask depth with the terrain cover, as the 3D overlay does, or a building behind a
        // ridge shadows the slope in front of it.
        bool terrainOccluders = _terrainMode && static_cast<bool>(_terrainTextureProvider);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        if (terrainOccluders) {
            _terrainDrawDepthBias = _terrainDepthBias;
            _terrainDrawDepthClipUnits = 0.0f;
            if (_terrainSharedGround) {
                for (const TileId& tileId : _terrainGroundTiles) {
                    renderTileSurfaceFill(tileId, Color());
                }
            } else {
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileSurfaceFill(renderTile.targetTileId, Color());
                    }
                }
            }
        }
        // Same clearance an extrusion gets, or a capsule lying ON the surface is rejected by it.
        if (terrainOccluders) {
            _terrainDrawDepthBias = _terrainDepthBias + TERRAIN_EXTRUSION_DEPTH_DELTAS * TERRAIN_LAYER_DEPTH_DELTA;
            _terrainDrawDepthClipUnits = (_terrainRegularGrid ? 2.0f : 12.0f);
        }
        // ...and the extrusions, the occluder that matters in a city: a base rim hidden behind the
        // front building would otherwise land on its wall.
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                if (_rendererLayerIndexRange && (it->first < _rendererLayerIndexRange->first || it->first >= _rendererLayerIndexRange->second)) {
                    continue;
                }
                const RenderTileLayer& renderLayer = it->second;
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
                    }
                }
            }
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // LEQUAL: a capsule meets its own wall exactly at the base line, where it is darkest.
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_MIN);

        _groundAOMaskPass = true;
        int draws = 0;
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                if (_rendererLayerIndexRange && (it->first < _rendererLayerIndexRange->first || it->first >= _rendererLayerIndexRange->second)) {
                    continue;
                }
                const RenderTileLayer& renderLayer = it->second;
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
                        draws++;
                    }
                }
            }
        }
        _groundAOMaskPass = false;

        if (terrainOccluders) {
            _terrainDrawDepthBias = _terrainDepthBias;
            _terrainDrawDepthClipUnits = 0.0f;
        }
        glDepthFunc(GL_LESS);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        checkGLError();
        return draws;
    }

    void GLTileRenderer::setTerrainDepthWrite(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainDepthWrite = enabled;
    }

    void GLTileRenderer::setTerrainTextureProvider(TerrainTextureProvider provider) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainTextureProvider = std::move(provider);
        updateTerrainSkirts();
    }

    void GLTileRenderer::setDebugTileBorders(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _debugTileBorders = enabled;
    }

    void GLTileRenderer::setDebugWireframe(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _debugWireframe = enabled;
    }

    void GLTileRenderer::setDebugSurfacePrefill(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _debugSurfacePrefill = enabled;
    }

    void GLTileRenderer::setTerrainBackgroundColor(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainBackgroundColor = color;
    }

    void GLTileRenderer::setLabelElevationProvider(std::function<double(const cglib::vec3<double>&, int)> provider) {
        std::lock_guard<std::mutex> lock(_mutex);

        bool had = static_cast<bool>(_labelElevationProvider);
        _labelElevationProvider = std::move(provider);
        // Going flat withdraws the provider after the ramp's last anchoring, above 0: nothing would
        // anchor the labels again, so drop them to the flat ground here.
        if (had && !_labelElevationProvider) {
            std::shared_ptr<const TileTransformer> transformer = _transformer;
            std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> flat = [transformer](const cglib::vec3<double>& pos, int) { return transformer->calculateElevatedPos(pos, 0.0); };
            std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> flatRoof = roofAnchorFunc(flat);
            for (const std::shared_ptr<Label>& label : _labels) {
                label->updateElevation(label->isZElevated() ? flatRoof : flat);
                label->setElevationDirty(false);
                label->clearElevationAnchor();
            }
        }
    }

    void GLTileRenderer::invalidateLabelElevation() {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelElevationGeneration++;

        _pendingLabelElevationAll = true;
        _pendingLabelElevationTiles.clear();
    }

    void GLTileRenderer::invalidateLabelElevation(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_pendingLabelElevationAll) {
            return;
        }
        _pendingLabelElevationTiles.insert(_pendingLabelElevationTiles.end(), tileIds.begin(), tileIds.end());
    }

    void GLTileRenderer::setExtrusionElevationProvider(std::function<bool(const cglib::vec3<double>&, int, bool, double&)> provider) {
        std::lock_guard<std::mutex> lock(_mutex);

        _extrusionElevationProvider = std::move(provider);
        _spanResolver.setElevationProvider(_extrusionElevationProvider);
        invalidateExtrusionBases();
    }

    void GLTileRenderer::invalidateExtrusionBases() {
        // A counter rather than the labels' per-tile list: setVertexBase is a no-op when the height
        // has not moved, so a needless re-resolve costs the elevation queries and uploads nothing.
        _extrusionBaseVersion.fetch_add(1, std::memory_order_relaxed);
        VT_STAT_INC(extrusionVersionBumps);
    }

    void GLTileRenderer::invalidateExtrusionBases(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        _pendingExtrusionBaseTiles.insert(_pendingExtrusionBaseTiles.end(), tileIds.begin(), tileIds.end());
        VT_STAT_ADD(extrusionPendingTiles, static_cast<long long>(tileIds.size()));
    }

    void GLTileRenderer::updateTerrainSkirts() {
        // Skirts are disabled: their walls rasterize over neighbouring content where a displaced edge
        // leans off-nadir. Same-level borders are seam-free via the shared elevation texture.
        bool skirts = false;
        if (skirts != _terrainSkirtsEnabled) {
            _terrainSkirtsEnabled = skirts;
            _tileSurfaceBuilder.setTerrainSkirts(skirts);
            _tileSurfaceMap.clear();
        }
    }

    void GLTileRenderer::setLabelOcclusionTest(std::function<bool(const cglib::vec3<double>&)> occlusionTest) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelOcclusionTest = std::move(occlusionTest);
    }

    void GLTileRenderer::setLayerBlendingSpeed(float speed) {
        std::lock_guard<std::mutex> lock(_mutex);

        _layerBlendingSpeed = speed;
    }

    void GLTileRenderer::setLabelBlendingSpeed(float speed) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelBlendingSpeed = speed;
    }

    void GLTileRenderer::setRasterFilterMode(RasterFilterMode filterMode) {
        std::lock_guard<std::mutex> lock(_mutex);

        _rasterFilterMode = filterMode;
    }

    void GLTileRenderer::setRendererLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);

        _rendererLayerFilter = filter;
        // The skip guard's set signature says nothing about the filter, which also decides the maps.
        _labelTilesSignature.reset();
    }

    void GLTileRenderer::setRendererLayerIndexRange(const std::optional<std::pair<int, int>>& range) {
        std::lock_guard<std::mutex> lock(_mutex);

        _rendererLayerIndexRange = range;
    }

    void GLTileRenderer::setNoDrapeLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);

        _noDrapeLayerFilter = filter;
        _noDrapeLayerCache.clear();
    }

    void GLTileRenderer::setClickHandlerLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);

        _clickHandlerLayerFilter = filter;
    }

    // Device pixels per line-width unit: on a dense screen the one-unit antialias ramp blurs thin
    // lines, and only the host knows the viewport's real pixel size.
    void GLTileRenderer::setLineAntialiasScale(float scale) {
        std::lock_guard<std::mutex> lock(_mutex);

        _lineAntialiasScale = std::max(1.0f, scale);
    }

    void GLTileRenderer::setViewState(const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        _cameraProjMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
        _fullResolution = viewState.resolution;
        _halfResolution = viewState.resolution * 0.5f;
        _viewState = viewState;
        _viewState.zoomScale *= _scale;
        _floatFuncCache.clear();
        _colorFuncCache.clear();
        _tileMatrixCache.clear();
        _tileMVPMatrixCache.clear();
        _terrainTextureCache.clear();
        // The view's ENU, columns in GL order: on a globe a geometry normal is the sphere's, and the
        // lighting shaders want the map's frame (18-globe.md).
        cglib::vec3<double> up = _viewState.origin;
        double len = cglib::length(up);
        up = (len > 0 ? up * (1.0 / len) : cglib::vec3<double>(0, 0, 1));
        double h = std::sqrt(up(0) * up(0) + up(1) * up(1));
        cglib::vec3<double> east = (h > 1.0e-9 ? cglib::vec3<double>(-up(1) / h, up(0) / h, 0) : cglib::vec3<double>(1, 0, 0));
        cglib::vec3<double> north = cglib::vector_product(up, east);
        _sphereLightingFrame = {
            static_cast<GLfloat>(east(0)), static_cast<GLfloat>(east(1)), static_cast<GLfloat>(east(2)),
            static_cast<GLfloat>(north(0)), static_cast<GLfloat>(north(1)), static_cast<GLfloat>(north(2)),
            static_cast<GLfloat>(up(0)), static_cast<GLfloat>(up(1)), static_cast<GLfloat>(up(2))
        };
        VT_STAT_INC(viewStateChanges);
    }

    float GLTileRenderer::evaluateFloatFunc(const FloatFunction& func) {
        // Constants carry no function object; only expression-backed ones are worth a lookup.
        const void* key = func.function().get();
        if (!key) {
            VT_STAT_INC(styleFuncConstants);
            return func.value();
        }
        VT_STAT_INC(styleFuncLookups);
        auto it = _floatFuncCache.find(key);
        if (it != _floatFuncCache.end()) {
            return it->second.second;
        }
        VT_STAT_INC(styleFuncMisses);
        VT_STAT_CLOCK(evalClock);
        float value = func(_viewState);
        VT_STAT_SPLIT(styleFuncEvalNs, evalClock);
        return _floatFuncCache.emplace(key, std::make_pair(func.function(), value)).first->second.second;
    }

    Color GLTileRenderer::evaluateColorFunc(const ColorFunction& func) {
        const void* key = func.function().get();
        if (!key) {
            VT_STAT_INC(styleFuncConstants);
            return func.value();
        }
        VT_STAT_INC(styleFuncLookups);
        auto it = _colorFuncCache.find(key);
        if (it != _colorFuncCache.end()) {
            return it->second.second;
        }
        VT_STAT_INC(styleFuncMisses);
        VT_STAT_CLOCK(evalClock);
        Color value = func(_viewState);
        VT_STAT_SPLIT(styleFuncEvalNs, evalClock);
        return _colorFuncCache.emplace(key, std::make_pair(func.function(), value)).first->second.second;
    }
    
    void GLTileRenderer::setVisibleTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::map<TileId, std::shared_ptr<const Tile>>& labelOnlyTiles, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles, const std::map<TileId, std::shared_ptr<const Tile>>& shadowCasterTiles) {
        using TilePair = std::pair<TileId, std::shared_ptr<const Tile>>;

        std::set<TileId> tileIds;
        std::vector<std::shared_ptr<const Tile>> labelTiles;
        auto addLabelTile = [&labelTiles](const std::shared_ptr<const Tile>& tile) {
            if (!tile) {
                return;
            }
            // Unique tiles ordered by zoom, or labels redefined at several zoom levels flicker.
            auto it = std::lower_bound(labelTiles.begin(), labelTiles.end(), tile, [](const std::shared_ptr<const Tile>& tile1, const std::shared_ptr<const Tile>& tile2) {
                return std::make_pair(tile2->getTileId(), tile2) < std::make_pair(tile1->getTileId(), tile1);
            });
            if (it == labelTiles.end() || *it != tile) {
                labelTiles.insert(it, tile);
            }
        };
        for (TilePair tilePair : tiles) {
            tileIds.insert(tilePair.first);
            addLabelTile(tilePair.second);
        }
        // Labels only: no tileId, so no surface and no render tile - see setVisibleTiles' comment.
        for (TilePair tilePair : labelOnlyTiles) {
            addLabelTile(tilePair.second);
        }

        // The expensive label map rebuild runs off the mutex against a snapshot; an unchanged set skips the snapshot.
        LabelMapBuild labelBuild;
        labelBuild.signature = calculateLabelTilesSignature(labelTiles);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            labelBuild.unchanged = (labelBuild.signature == _labelTilesSignature && !_layerLabelMap.empty());
            labelBuild.generation = _labelMapGeneration;
            if (!labelBuild.unchanged) {
                labelBuild.oldLabelMap = _layerLabelMap;      // shared_ptr copies, not labels
                labelBuild.layerFilter = _rendererLayerFilter; // set under the mutex, and it decides the maps
                labelBuild.transformer = _transformer;
            }
        }
        if (labelBuild.unchanged) {
            VT_STAT_INC(labelMapSkips);
        } else {
            prepareLabelMaps(labelTiles, labelBuild.oldLabelMap, labelBuild.layerFilter, labelBuild);
        }

        // All other operations must be synchronized
        VT_STAT_CLOCK(visibleClock);
        std::vector<std::shared_ptr<Label>> dirtyLabels;
        unsigned int labelElevationGeneration = 0;
        std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> anchorFunc;
        {
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(setVisibleTilesLockNs, visibleClock);

        VT_STAT_INC(visibleTileSetChanges);
        _visibleTileIds = tileIds;
        buildTerrainEdgeCoarsening();
        VT_STAT_SPLIT(terrainCoarseningNs, visibleClock);
        buildTileSurfaces(tileIds);
        VT_STAT_SPLIT(tileSurfacesNs, visibleClock);
        if (!labelBuild.unchanged) {
            // deinitializeRenderer may have cleared the live maps while the prepare ran: redo it here.
            if (labelBuild.generation != _labelMapGeneration) {
                LabelMapBuild rebuild;
                rebuild.signature = labelBuild.signature;
                rebuild.generation = _labelMapGeneration;
                rebuild.transformer = _transformer;
                prepareLabelMaps(labelTiles, _layerLabelMap, _rendererLayerFilter, rebuild);
                commitLabelMaps(rebuild);
            } else {
                commitLabelMaps(labelBuild);
            }
        }
        VT_STAT_SPLIT(labelMapsNs, visibleClock);
        if (shadowCasterTiles.empty()) {
            buildRenderTiles(tiles);
        } else {
            std::map<TileId, std::shared_ptr<const Tile>> renderTileMap = shadowCasterTiles;
            for (const TilePair& tilePair : tiles) {
                renderTileMap[tilePair.first] = tilePair.second;
            }
            buildRenderTiles(renderTileMap);
        }
        VT_STAT_SPLIT(renderTilesNs, visibleClock);
        _spanResolver.build(tiles, spanReferenceTiles, _visibleTileIds, _extrusionBaseVersion.load(std::memory_order_relaxed));
        if (_spanResolver.takeLabelsDirty()) {
            _pendingLabelElevationAll = true;
        }
        VT_STAT_SPLIT(spanUnionsNs, visibleClock);
        // Name this tile set's dirty labels (all, when a deck resolved); sampled off the lock below.
        if (_labelElevationProvider && _labelAnchorOnCull) {
            markPendingLabelsDirty();
            for (const std::shared_ptr<Label>& label : _labels) {
                if (label->isElevationDirty() && !label->isZElevated()) {
                    dirtyLabels.push_back(label);
                }
            }
            anchorFunc = labelAnchorFunc();
            labelElevationGeneration = _labelElevationGeneration;
        }
        }

        // Anchored here on the cull thread, lock RELEASED, not on the render thread: a sample reads
        // only x,y, which nothing changes after a label is built.
        if (!dirtyLabels.empty()) {
            std::vector<std::vector<cglib::vec3<double>>> positions(dirtyLabels.size());
            for (std::size_t i = 0; i < dirtyLabels.size(); i++) {
                positions[i] = dirtyLabels[i]->sampleElevation(anchorFunc);
            }
            std::lock_guard<std::mutex> lock(_mutex);
            for (std::size_t i = 0; i < dirtyLabels.size(); i++) {
                if (dirtyLabels[i]->isElevationDirty()) {
                    // Cleared even without elevation: markPendingLabelsDirty re-dirties it when data
                    // arrives, and isElevationAnchored() is what carries "height unknown".
                    dirtyLabels[i]->applyElevation(positions[i]);
                    dirtyLabels[i]->setElevationGeneration(labelElevationGeneration);
                    dirtyLabels[i]->setElevationDirty(false);
                }
            }
            markDeckAnchoredLabels(dirtyLabels);
            VT_STAT_SPLIT(labelAnchorNs, visibleClock);
        }
    }

    const std::set<TileId>& GLTileRenderer::terrainSurfaceTileIds() const {
        // The owner's cover wins when present: a drape uses its normalised leaves, a paint has no tiles.
        return (_terrainCoverTileIds.empty() ? _visibleTileIds : _terrainCoverTileIds);
    }

    void GLTileRenderer::buildTerrainEdgeCoarsening() {
        // Per tile, how much coarser each edge neighbour is: the fine tile must chord across the
        // neighbour's 2^k wider DEM nodes or the shared edge cracks.
        _terrainEdgeCoarseningMap.clear();
        if (!(_terrainEdgeStitching && terrainGridSurfaces())) {
            return;
        }
        const std::set<TileId>& tileIds = terrainSurfaceTileIds();
        int maxLevels = 0;
        for (int res = _terrainRegularGridResolution; res > 0 && (res & 1) == 0; res >>= 1) {
            maxLevels++;
        }
        if (maxLevels < 1) {
            return; // odd resolution: no coarser lattice is a subset of this one
        }

        auto edgeFactor = [&tileIds, maxLevels](const TileId& tileId, int dx, int dy) -> float {
            TileId neighbour(tileId.zoom, tileId.x + dx, tileId.y + dy);
            if (tileIds.count(neighbour) > 0) {
                return 1.0f; // same level (a finer neighbour stitches on its own side)
            }
            TileId ancestor = neighbour;
            for (int k = 1; k <= maxLevels && ancestor.zoom > 0; k++) {
                ancestor = ancestor.getParent();
                if (tileIds.count(ancestor) > 0) {
                    return static_cast<float>(1 << k);
                }
            }
            return 1.0f; // not visible, or coarser than the lattices can follow
        };

        for (const TileId& tileId : tileIds) {
            cglib::vec4<float> coarsening(
                edgeFactor(tileId, -1, 0), // west
                edgeFactor(tileId,  1, 0), // east
                edgeFactor(tileId, 0,  1), // south (XYZ: y grows south)
                edgeFactor(tileId, 0, -1)  // north
            );
            if (coarsening != cglib::vec4<float>(1, 1, 1, 1)) {
                _terrainEdgeCoarseningMap.emplace(tileId, coarsening);
            }
        }
    }

    void GLTileRenderer::teleportVisibleTiles(int dx, int dy) {
        std::lock_guard<std::mutex> lock(_mutex);

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
            _passLabels[pass] = std::make_shared<PassLabels>();
        }
    }
    
    void GLTileRenderer::resetRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Drop all caches with shader/texture/FBO/VBO references
        _shaderProgramMap.clear();
        _shaderProgramCache.clear();
        _compiledBitmapMap.clear();
        _compiledTileBitmapMap.clear();
        _compiledTileSurfaceMap.clear();
        _compiledTileGeometryMap.clear();
        _compiledLabelBatches.clear();
        _overlayBuffer2D = FrameBuffer();
        _overlayBuffer3D = FrameBuffer();
        _screenQuad = CompiledQuad();
        _drapeTextures.clear();
        _drapeFingerprints.clear();
        _drapeTexturePool.clear();
        _drapeTilesThisFrame.clear();
        _externalDrapeTiles.clear();
        _drapeCoverageMasks.clear();
        _drapeCoverageLayerMasks.clear();
        _drapeMaskTexture = 0;
        _drapeFBO = 0;
    }
        
    void GLTileRenderer::setTransformer(std::shared_ptr<const TileTransformer> transformer) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (transformer == _transformer) {
            return;
        }
        _transformer = std::move(transformer);
        VT_STAT_ADD(tileSurfacesInvalidated, static_cast<long long>(_tileSurfaceMap.size()));
        _tileSurfaceMap.clear();
        _tileSurfaceBuilder.setTransformer(_transformer);
    }

    void GLTileRenderer::resetTileSurfaces() {
        std::lock_guard<std::mutex> lock(_mutex);

        // Drop built tile surfaces. They will be lazily rebuilt with the current
        // transformer state; the corresponding compiled VBOs are released in endFrame.
        VT_STAT_ADD(tileSurfacesInvalidated, static_cast<long long>(_tileSurfaceMap.size()));
        _tileSurfaceMap.clear();
        _tileSurfaceBuilder.invalidateCaches();
    }

    void GLTileRenderer::invalidateTileSurfaces(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        // Only the surfaces over the given elevation tiles: a full reset re-tessellates the whole
        // screen again for every tile of the initial elevation stream.
        if (tileIds.empty()) {
            return;
        }
        for (auto it = _tileSurfaceMap.begin(); it != _tileSurfaceMap.end(); ) {
            TileId tileId = it->first.getWrapped();
            bool invalidate = false;
            for (const TileId& changedTileId : tileIds) {
                if (tileId.intersects(changedTileId)) {
                    invalidate = true;
                    break;
                }
            }
            if (invalidate) {
                VT_STAT_INC(tileSurfacesInvalidated);
            }
            it = (invalidate ? _tileSurfaceMap.erase(it) : std::next(it));
        }
        _tileSurfaceBuilder.invalidateCaches(tileIds);
    }

    void GLTileRenderer::deinitializeRenderer() {
        std::lock_guard<std::mutex> lock(_mutex);

        for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); it++) {
            deleteShaderProgram(it->second);
        }
        _shaderProgramMap.clear();
        _shaderProgramCache.clear();

        for (auto it = _compiledBitmapMap.begin(); it != _compiledBitmapMap.end(); it++) {
            deleteCompiledBitmap(it->second);
        }
        _compiledBitmapMap.clear();

        for (auto it = _compiledTileBitmapMap.begin(); it != _compiledTileBitmapMap.end(); it++) {
            deleteCompiledBitmap(it->second);
        }
        _compiledTileBitmapMap.clear();

        for (auto it = _compiledTileSurfaceMap.begin(); it != _compiledTileSurfaceMap.end(); it++) {
            deleteCompiledSurface(it->second);
        }
        _compiledTileSurfaceMap.clear();

        for (auto it = _compiledTileGeometryMap.begin(); it != _compiledTileGeometryMap.end(); it++) {
            deleteCompiledGeometry(it->second.geometry);
        }
        _compiledTileGeometryMap.clear();

        for (auto it = _compiledLabelBatches.begin(); it != _compiledLabelBatches.end(); it++) {
            deleteCompiledLabelBatch(it->second);
        }
        _compiledLabelBatches.clear();
        
        deleteFrameBuffer(_overlayBuffer2D);
        deleteFrameBuffer(_overlayBuffer3D);

        deleteDrapeResources();

        deleteCompiledQuad(_screenQuad);
        
        _renderTiles.reset();
        _visibleRenderTiles.reset();
        for (int pass = 0; pass < 2; pass++) {
            _passLabels[pass].reset();
            _visiblePassLabels[pass].reset();
        }
        _labels.clear();
        _layerLabelMap.clear();
        _labelMapGeneration++; // invalidates a prepare running off the mutex
    }
    
    bool GLTileRenderer::startFrame(float dt) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw
        warmTerrainRasterShader();

        bool refresh = false;

        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] != _screenWidth || viewport[3] != _screenHeight) {
            _screenWidth = viewport[2];
            _screenHeight = viewport[3];

            deleteFrameBuffer(_overlayBuffer2D);
            deleteFrameBuffer(_overlayBuffer3D);
        }

        _visibleRenderTiles = _renderTiles;
        _frameOccludersValid = false;
        refreshGroundAOBakeable(); // the tiles it answers from are the ones just published
        VT_STAT_CLOCK(prepClock);
        float dBlend = (_layerBlendingSpeed > 0.0f ? dt * _layerBlendingSpeed : 1.0f);
        for (RenderTile& renderTile : *_visibleRenderTiles) {
            refresh = updateRenderTile(renderTile, dBlend) || refresh;
        }
        VT_STAT_SPLIT(prepTileBlendNs, prepClock);
        
        // Re-anchor only for elevation tiles landed since the last frame; NEW labels are anchored on
        // the cull thread in setVisibleTiles, off the render thread, all but those on roofs.
        bool roofsMoved = refreshRoofSurfaces();
        if (_labelElevationProvider || roofsMoved || !_roofSurfaces.empty()) {
            refresh = anchorDirtyLabels() || refresh;
            VT_STAT_SPLIT(prepElevUpdateNs, prepClock);
        }

        // Only extrusions over a newly landed elevation tile: a centroid lies in its own tile, so
        // matching the geometry's SOURCE tile is exact.
        if (!_pendingExtrusionBaseTiles.empty()) {
            if (_renderTiles) {
                for (const RenderTile& renderTile : *_renderTiles) {
                    for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                        const RenderTileLayer& renderLayer = it->second;
                        if (!renderLayer.layer) {
                            continue;
                        }
                        bool affected = false;
                        for (const TileId& tileId : _pendingExtrusionBaseTiles) {
                            if (renderLayer.sourceTileId.getWrapped().intersects(tileId)) { // elevation and content tiles are different sets
                                affected = true;
                                break;
                            }
                        }
                        if (!affected && !renderLayer.layer->hasSpanGeometry()) {
                            continue; // nothing here to re-resolve
                        }
                        for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                            // A span samples portals often outside the tile, so no tile test applies; few, so redo all.
                            bool span = !geometry->getSpanRecords().empty();
                            if (span || (affected && geometry->getType() == TileGeometry::Type::POLYGON3D)) {
                                geometry->setBaseResolved(false);
                                VT_STAT_INC(extrusionBasesCleared);
                            }
                        }
                    }
                }
            }
            _pendingExtrusionBaseTiles.clear();
        }

        _visiblePassLabels = _passLabels;
        // A forced placement replaces the whole screen: a crossfade would double every label (mapbox: fadeDuration 0).
        float dOpacity = labelOpacityStep(dt, _labelBlendingSpeed, _snapLabelTransition.exchange(false));
        for (int pass = 0; pass < 2; pass++) {
            for (const std::shared_ptr<Label>& label : *_visiblePassLabels[pass]) {
                refresh = updateLabel(label, dOpacity) || refresh;
            }
        }
        VT_STAT_SPLIT(prepLabelBlendNs, prepClock);
        
        _labelBatchCounter = 0;

        return refresh;
    }
    
    void GLTileRenderer::renderGeometry(bool geom2D, bool geom3D, bool inline3D, float layerOpacity) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles) {
            return;
        }

        if (geom2D) {
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


            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            // Terrain mode: 2D content is displaced onto the terrain and depth-tested against the ground
            // so ridges occlude it; the two-component depth bias covers the surface-vs-mesh deviation.
            if (_terrainMode) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(GL_FALSE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(0);
            if (_terrainMode) {
                // Displaced surfaces can face away near ridge crests; culling them holes colour and
                // depth along the silhouettes.
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            // Terrain pre-pass: this layer's own depth domain. Skipped under a shared ground or a
            // cross-layer drape, whose surface glClear(DEPTH) would discard; content passes at EQUAL.
            bool leEqualDepth = _terrainMode && (_terrainDrapeFills || _terrainSharedGround);
            if (leEqualDepth) {
                glDepthFunc(GL_LEQUAL);
            }

            if (_terrainPaint.enabled && !_externalDrapeTarget && !(_terrainPaintOnGround && _terrainSharedGround)) {
                // A paint with no drape draws in this layer's slot - unless it IS the ground, which
                // the ground pass already drew.
                renderTerrainPaintSurfaces();
            }
            // A shared ground is drawn once for the stack: re-clearing depth for a private domain lets
            // a contour or an element leak through the ridge in front of it.
            if (_terrainMode && _terrainTextureProvider && !_externalDrapeTarget && !_terrainSharedGround) {
                bool colorFill = (_terrainBackgroundColor.value() != 0);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_STENCIL_TEST);
                glClear(GL_DEPTH_BUFFER_BIT); // own depth domain per tile layer; cross-layer stacking is painter's order
                if (!colorFill) {
                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                }
                _terrainDrawDepthBias = _terrainDepthBias;
                _terrainDrawDepthClipUnits = _terrainRegularGrid ? -TERRAIN_PAINTER_SURFACE_BACK : 0.0f; // painter-order: the ground surface is pushed back like the backgrounds/bitmaps
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileSurfaceFill(renderTile.targetTileId, _terrainBackgroundColor);
                    }
                }
                _terrainDrawDepthBias = _terrainDepthBias;
                _terrainDrawDepthClipUnits = 0.0f;
                if (!colorFill) {
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                }
                glDepthMask(GL_FALSE);

                // Drape: bake fills flat into per-tile textures, then texture the terrain with them.
                if (_terrainDrapeFills) {
                    renderDrapeTextures(*_visibleRenderTiles);
                    // The drape surface IS the terrain: true depth, writing, so a near ridge hides the
                    // far slope. A pushed-back surface let far slopes show through.
                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_STENCIL_TEST);
                    setCompOp(CompOp::SRC_OVER);
                    _terrainDrawDepthBias = 0.0f;
                    _terrainDrawDepthClipUnits = 0.0f;
                    for (const RenderTile& renderTile : *_visibleRenderTiles) {
                        if (renderTile.visible) {
                            renderTileSurfaceDrape(renderTile.targetTileId, 0.0f, 0.0f, 1.0f);
                        }
                    }
                    glDepthMask(GL_FALSE);
                }
            }

            if (_debugSurfacePrefill) {
                // Depth-resolved within the prefill, then cleared so the real passes start pristine.
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_STENCIL_TEST);
                // Cyan in a see-through spot = back faces visible (winding/mesh); magenta = the far
                // slope's front faces (occlusion failure).
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK); // front faces remain
                Color frontColor(1.0f, 0.0f, 1.0f, 1.0f); // magenta
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileSurfaceFill(renderTile.targetTileId, frontColor);
                    }
                }
                glCullFace(GL_FRONT); // back faces remain
                Color backColor(0.0f, 1.0f, 1.0f, 1.0f); // cyan
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileSurfaceFill(renderTile.targetTileId, backColor);
                    }
                }
                glCullFace(GL_BACK);
                glClear(GL_DEPTH_BUFFER_BIT);
                glDepthMask(GL_FALSE);
                if (_terrainMode) {
                    glDisable(GL_CULL_FACE); // terrain mode draws surfaces unculled
                    glEnable(GL_DEPTH_TEST);
                } else {
                    glDisable(GL_DEPTH_TEST);
                }
            }

            // Lattice-clamped content coincides with the true-depth ground: LEQUAL and no forward
            // bias, so no ridge leak, and no stencil masks.
            renderGeometry2D(*_visibleRenderTiles, stencilBits);
            if (leEqualDepth) {
                glDepthFunc(GL_LESS);
            }

            if (_debugTileBorders) {
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_STENCIL_TEST);
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileBorder(renderTile.targetTileId, renderTile.tile ? renderTile.tile->getTileId() : renderTile.targetTileId);
                    }
                }
                glEnable(GL_DEPTH_TEST);
            }

            // Debug: final stencil contents (a colour per value, black = unowned) and red surface wireframes.
            if (_debugWireframe) {
                glDisable(GL_DEPTH_TEST);
                renderStencilDebugOverlay();
                glDisable(GL_STENCIL_TEST);
                for (const RenderTile& renderTile : *_visibleRenderTiles) {
                    if (renderTile.visible) {
                        renderTileWireframe(renderTile.targetTileId);
                    }
                }
            }

            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(255);
            glEnable(GL_CULL_FACE);
        }

        if (geom3D) {
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(0);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);

            renderGeometry3D(*_visibleRenderTiles, inline3D, layerOpacity);

            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glEnable(GL_BLEND);
            glStencilMask(255);
        }
    }
    
    void GLTileRenderer::renderLabels(bool labels2D, bool labels3D, float layerOpacity) {
        VT_STAT_CLOCK(lockClock);
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(mutexWaitNs, lockClock);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visiblePassLabels[0] || !_visiblePassLabels[1]) {
            return;
        }

        // Labels are batched with their own colours, bitmap icons included: fade them as one image.
        bool fade = layerOpacity < 1.0f && ((labels2D && !_visiblePassLabels[0]->empty()) || (labels3D && !_visiblePassLabels[1]->empty()));
        GLint previousFBO = 0;
        if (fade) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
            if (_overlayBuffer3D.fbo == 0) {
                createFrameBuffer(_overlayBuffer3D, true, true, true);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer3D.fbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && labels2D) || (pass == 1 && labels3D)) {
                renderLabels(*_visiblePassLabels[pass]);
            }
        }

        if (fade) {
            glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
            blendScreenTexture(layerOpacity, _overlayBuffer3D.colorTexture);
        }
        
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glStencilMask(255);
    }
    
    bool GLTileRenderer::endFrame() {
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_CLOCK(statClock);
        if (--_resourceSweepCounter > 0) {
            return false;
        }
        _resourceSweepCounter = RESOURCE_SWEEP_INTERVAL_FRAMES;
        VT_STAT_ADD(endFrameSwept, _compiledBitmapMap.size() + _compiledTileBitmapMap.size() + _compiledTileSurfaceMap.size() + _compiledTileGeometryMap.size());



        for (auto it = _compiledBitmapMap.begin(); it != _compiledBitmapMap.end();) {
            if (it->first.expired()) {
                deleteCompiledBitmap(it->second);
                it = _compiledBitmapMap.erase(it);
            } else {
                it++;
            }
        }
        
        for (auto it = _compiledTileBitmapMap.begin(); it != _compiledTileBitmapMap.end();) {
            if (it->first.expired()) {
                deleteCompiledBitmap(it->second);
                it = _compiledTileBitmapMap.erase(it);
            } else {
                it++;
            }
        }

        for (auto it = _compiledTileSurfaceMap.begin(); it != _compiledTileSurfaceMap.end();) {
            if (it->first.expired()) {
                deleteCompiledSurface(it->second);
                it = _compiledTileSurfaceMap.erase(it);
            } else {
                it++;
            }
        }

        for (auto it = _compiledTileGeometryMap.begin(); it != _compiledTileGeometryMap.end();) {
            if (it->second.owner.expired()) {
                deleteCompiledGeometry(it->second.geometry);
                it = _compiledTileGeometryMap.erase(it);
            } else {
                it++;
            }
        }

        // Unused label batches are kept: they are unlikely to be big and can be reused.
        VT_STAT_SPLIT(endFrameNs, statClock);
        return false;
    }

    void GLTileRenderer::snapLabelTransition() {
        _snapLabelTransition.store(true);
    }

    void GLTileRenderer::restartLabelPlacement() {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelCullCursor = 0;
    }

    bool GLTileRenderer::cullLabels(LabelCuller& culler, bool& empty) {
        std::vector<std::shared_ptr<Label>> labels;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            labels = _labels;
        }
        empty = labels.empty();

        culler.process(labels, _mutex, _labelCullCursor);
        if (_labelCullCursor >= labels.size()) {
            _labelCullCursor = 0;
            return true;
        }
        return false;
    }
    
    bool GLTileRenderer::findBitmapIntersections(const std::vector<cglib::ray3<double>>& rays, std::vector<BitmapIntersectionInfo>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        std::size_t initialResultCount = results.size();
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.active) {
                    continue;
                }
                if (!testLayerFilter(renderLayer.layer->getLayerName(), _clickHandlerLayerFilter)) {
                    continue;
                }

                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderLayer.targetTileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderLayer.sourceTileId);

                if (!std::any_of(rays.begin(), rays.end(), [&](const cglib::ray3<double>& ray) { return cglib::intersect_bbox(tileBBox, ray); })) {
                    continue;
                }
                
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
        
        std::size_t initialResultCount = results.size();
        std::vector<GeometryIntersectionInfo> results3D;
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.active) {
                    continue;
                }
                if (!testLayerFilter(renderLayer.layer->getLayerName(), _clickHandlerLayerFilter)) {
                    continue;
                }

                cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(renderLayer.targetTileId);
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId);
                cglib::mat4x4<double> invTileMatrix = cglib::inverse(tileMatrix);
                std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(renderLayer.sourceTileId);

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
                        GeometryIntersectionInfo intersectionInfo(resultTile.tileId, renderLayer.layer->getLayerIndex(), resultTile.featureId, resultTile.geoPointIndex, resultTile.rayIndex, rayT);
                        if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                            results.push_back(std::move(intersectionInfo));
                        } else {
                            results3D.push_back(std::move(intersectionInfo));
                        }
                    }
                }
            }
        }

        std::stable_sort(results3D.begin(), results3D.end(), [](const GeometryIntersectionInfo& result1, const GeometryIntersectionInfo& result2) {
            return result1.rayT > result2.rayT;
        });
        results.insert(results.end(), results3D.begin(), results3D.end());

        return results.size() > initialResultCount;
    }
    
    bool GLTileRenderer::findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const {

        std::lock_guard<std::mutex> lock(_mutex);

        std::set<int> clickHandlerLayerIdxs;
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (testLayerFilter(renderLayer.layer->getLayerName(), _clickHandlerLayerFilter)) {
                    clickHandlerLayerIdxs.insert(renderLayer.layer->getLayerIndex());
                }
            }
        }

        // The order may differ from the render order; harmless while labels do not overlap.
        std::size_t initialResultCount = results.size();
        for (int pass = 0; pass < 2; pass++) {
            if ((pass == 0 && !labels2D) || (pass == 1 && !labels3D)) {
                continue;
            }

            for (const std::shared_ptr<Label>& label : *_passLabels[pass]) {
                if (!label->isValid() || !label->isVisible() || !label->isActive() || label->getOpacity() <= 0) {
                    continue;
                }
                if (clickHandlerLayerIdxs.count(label->getLayerIndex()) == 0) {
                    continue;
                }

                std::vector<GeometryIntersectionInfo> resultsLocal;
                findLabelIntersections(label, rays, buffer, resultsLocal);
                
                for (const GeometryIntersectionInfo& result : resultsLocal) {
                    // Facing test on the anchor's ground normal. A CALLOUT is exempt: its quad always faces
                    // the viewer, and one lifted into the sky is hit by an upward ray.
                    if (label->getStyle()->orientation != LabelOrientation::CALLOUT && cglib::dot_product(label->getNormal(), cglib::vec3<float>::convert(rays[result.rayIndex].direction)) >= 0) {
                        continue;
                    }

                    results.emplace_back(result.tileId, label->getLayerIndex(), result.featureId, result.geoPointIndex, result.rayIndex, result.rayT);
                }
            }
        }

        return results.size() > initialResultCount;
    }

    bool GLTileRenderer::isTileVisible(const TileId& tileId) const {
        cglib::bbox3<double> bbox = _transformer->calculateTileBBox(tileId);
        // The box is the tile's ground, but an extrusion stands out of it: dropping the tile drops a
        // building still on screen. maplibre grows the culling elevation for the same reason.
        double headroom = tileCullingHeadroom();
        if (headroom > 0) {
            // Every axis: the surface need not be planar, and generosity costs culling, not a draw.
            bbox = cglib::bbox3<double>(bbox.min - cglib::vec3<double>(headroom, headroom, headroom),
                                        bbox.max + cglib::vec3<double>(headroom, headroom, headroom));
        }
        return _viewState.frustum.inside(bbox);
    }

    GLsizei GLTileRenderer::drawSurfaceElements(const TileId& tileId, const TileSurface& surface, bool gridSurface) const {
        GLsizei drawn = 0;
        for (const std::pair<GLsizei, GLsizei>& run : visibleGridIndexRuns(tileId, surface, gridSurface)) {
            glDrawElements(GL_TRIANGLES, run.second, GL_UNSIGNED_SHORT, bufferGLOffset(static_cast<int>(run.first * sizeof(std::uint16_t))));
            drawn += run.second;
        }
        return drawn;
    }

    GLsizei GLTileRenderer::drawTerrainSkirts(const TileId& tileId, const ShaderProgram& shaderProgram) {
        auto it = _terrainSkirtDropMap.find(tileId);
        if (it == _terrainSkirtDropMap.end() || _transformer->isSpherical()) {
            return 0;
        }
        GLsizei drawn = 0;
        for (const std::shared_ptr<TileSurface>& skirtSurface : buildCompiledTerrainGridSkirtSurfaces()) {
            const TileSurface::VertexGeometryLayoutParameters& layout = skirtSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledSurface = _compiledTileSurfaceMap[skirtSurface];
            glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, layout.vertexSize, bufferGLOffset(layout.coordOffset));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
            GLsizei edgeIndices = static_cast<GLsizei>(skirtSurface->getIndicesCount() / 4);
            for (int edge = 0; edge < 4; edge++) {
                if (it->second(edge) > 0.0f) {
                    glDrawElements(GL_TRIANGLES, edgeIndices, GL_UNSIGNED_SHORT, bufferGLOffset(static_cast<int>(edge * edgeIndices * sizeof(std::uint16_t))));
                    drawn += edgeIndices;
                }
            }
        }
        return drawn;
    }

    std::vector<std::pair<GLsizei, GLsizei>> GLTileRenderer::visibleGridIndexRuns(const TileId& tileId, const TileSurface& gridSurface, bool culled) const {
        std::vector<std::pair<GLsizei, GLsizei>> runs;
        if (!culled) {
            runs.emplace_back(0, static_cast<GLsizei>(gridSurface.getIndicesCount()));
            return runs;
        }
        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
        const TerrainTexture& terrainTexture = resolved.second;
        int res = static_cast<int>(std::lround(std::sqrt(gridSurface.getIndicesCount() / 6.0)));
        if (_transformer->isSpherical() || !resolved.first || !(terrainTexture.minHeight <= terrainTexture.maxHeight) || gridSurface.getIndicesCount() != static_cast<unsigned int>(res * res * 6)) {
            runs.emplace_back(0, static_cast<GLsizei>(gridSurface.getIndicesCount()));
            return runs;
        }
        cglib::bbox3<double> tileBBox = _transformer->calculateTileBBox(tileId);
        cglib::vec3<double> tileSize = tileBBox.size();
        // World z is meters * metersToInternal * cosh(mercator y), steepest at the tile's poleward edge; the
        // margin covers the neighbour texels the border samples and the node filter.
        double coshY = std::max(std::cosh(tileBBox.min(1) * terrainTexture.mercatorYScale), std::cosh(tileBBox.max(1) * terrainTexture.mercatorYScale));
        double margin = 0.1 * (terrainTexture.maxHeight - terrainTexture.minHeight) + 10.0;
        double lowZ = (terrainTexture.minHeight - margin) * terrainTexture.metersToInternal;
        double highZ = (terrainTexture.maxHeight + margin) * terrainTexture.metersToInternal;
        double minZ = std::min(lowZ, lowZ * coshY), maxZ = std::max(highZ, highZ * coshY);
        GLsizei first = 0;
        for (int blockY = 0; blockY < TileSurfaceBuilder::GRID_CULL_BLOCKS; blockY++) {
            int y0 = TileSurfaceBuilder::gridBlockStart(res, blockY), y1 = TileSurfaceBuilder::gridBlockStart(res, blockY + 1);
            for (int blockX = 0; blockX < TileSurfaceBuilder::GRID_CULL_BLOCKS; blockX++) {
                int x0 = TileSurfaceBuilder::gridBlockStart(res, blockX), x1 = TileSurfaceBuilder::gridBlockStart(res, blockX + 1);
                GLsizei count = static_cast<GLsizei>((x1 - x0) * (y1 - y0) * 6);
                // Grid row 0 is the tile's north edge.
                cglib::bbox3<double> blockBBox(
                    cglib::vec3<double>(tileBBox.min(0) + tileSize(0) * x0 / res, tileBBox.min(1) + tileSize(1) * (res - y1) / res, minZ),
                    cglib::vec3<double>(tileBBox.min(0) + tileSize(0) * x1 / res, tileBBox.min(1) + tileSize(1) * (res - y0) / res, maxZ));
                if (count > 0 && _viewState.frustum.inside(blockBBox)) {
                    if (!runs.empty() && runs.back().first + runs.back().second == first) {
                        runs.back().second += count;
                    } else {
                        runs.emplace_back(first, count);
                    }
                }
                first += count;
            }
        }
        return runs;
    }

    double GLTileRenderer::tileCullingHeadroom() const {
        // maplibre's rule: none while looking down, growing to the assumed max feature height as the
        // frustum's bottom edge nears the horizon.
        if (!(_metersToInternal > 0)) {
            return 0; // no projection scale stated: fall back to the ground box, as before
        }
        double fovYDegrees = 2.0 * std::atan(1.0 / _viewState.projectionMatrix(1, 1)) * 180.0 / 3.14159265358979323846;
        double bottomEdgeDegrees = MAX_HORIZON_ANGLE_DEGREES - _viewState.tilt - fovYDegrees / 2;
        double proximity = (TILE_CULLING_HORIZON_ONSET_DEGREES - bottomEdgeDegrees) / TILE_CULLING_HORIZON_ONSET_DEGREES;
        proximity = std::min(1.0, std::max(0.0, proximity));
        return proximity * ASSUMED_MAX_FEATURE_HEIGHT_METERS * _metersToInternal;
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

    unsigned int GLTileRenderer::fogFlag() const {
        // The drape bake never fogs: it is fogged once on the terrain surface, and fog baked into the
        // cached texture would survive the fog being turned off.
        if (_drapeMVPOverride) {
            return 0;
        }
        // Transparent fog or a zero range means none was asked for: programs are built without it.
        return (_fogColor[3] > 0.0f && _fogDistance > _fogStartDistance ? FOG_FLAG : 0);
    }

    unsigned int GLTileRenderer::coverageFlag() const {
        return _drapeCoveragePass ? COVERAGE_FLAG : 0;
    }

    unsigned int GLTileRenderer::drapeMaskFlag() const {
        return _drapeMaskTexture != 0 ? DRAPE_MASK_FLAG : 0;
    }

    unsigned int GLTileRenderer::shadowReceiverFlags() const {
        // The cascade count is compiled in: each is a matrix per vertex and a highp varying per
        // fragment (docs/internals/rendering/08-lighting-sky-fog.md).
        unsigned int cascadeFlag = (_terrainShadowCascades >= 4 ? SHADOW_CASCADES4_FLAG :
                                    _terrainShadowCascades == 3 ? SHADOW_CASCADES3_FLAG :
                                    _terrainShadowCascades == 2 ? SHADOW_CASCADES2_FLAG : 0);
        // Hardware comparison needs a real depth texture; without one, manual taps, not no shadows.
        unsigned int hwFlags = 0;
        if (_terrainShadowDepthTexture) {
            hwFlags = SHADOW_DEPTH_TEXTURE_FLAG | (_terrainShadowHardwarePCF ? SHADOW_HW_FLAG : 0);
        }
        return TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG | cascadeFlag | hwFlags;
    }

    void GLTileRenderer::warmTerrainRasterShader() {
        // The lit raster program is first asked for mid-gesture (an integer zoom out): build it on an
        // ordinary frame. Flags are re-derived each frame, so a config change warms itself.
        if (!_terrainMode || !_terrainTextureProvider || !_terrainLighting.enabled) {
            return;
        }
        bool shadowed = _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        unsigned int flags = PATTERN_FLAG | TERRAIN_FLAG | TERRAIN_VTF_FLAG | TERRAIN_LIGHT_FLAG | (shadowed ? surfaceShadowFlags() : 0) | fogFlag();
        if (flags == _warmedRasterShaderFlags) {
            return;
        }
        _warmedRasterShaderFlags = flags;
        buildShaderProgram("tilecolormap", colormapVsh, colormapFsh, LightingMode::GEOMETRY2D, _rasterFilterMode, flags);
    }

    unsigned int GLTileRenderer::surfaceShadowFlags() const {
        // The terrain surface covers the screen twice (drape, paint), so its shadow is resolved once
        // into a screen-space mask; only the pass producing the mask computes it analytically.
        if (_terrainShadowMaskPass) {
            return shadowReceiverFlags() | SHADOW_MASK_OUT_FLAG;
        }
        return shadowReceiverFlags() | (_terrainShadowMaskTexture != 0 ? SHADOW_MASK_IN_FLAG : 0);
    }

    void GLTileRenderer::setupShadowNormalOffsetUniforms(const ShaderProgram& shaderProgram, const cglib::mat4x4<double>& tileFrame) const {
        cglib::vec4<float> offsets = calculateShadowNormalOffsets(tileFrame);
        glUniform4f(shaderProgram.uniforms[U_SHADOWNORMALOFFSET], offsets(0), offsets(1), offsets(2), offsets(3));
        glUniform3f(shaderProgram.uniforms[U_SHADOWSUNDIR], _terrainShadowSunDir(0), _terrainShadowSunDir(1), _terrainShadowSunDir(2));
    }

    void GLTileRenderer::setupShadowFadeRangeUniform(const ShaderProgram& shaderProgram) const {
        // Zeroed for the orthographic drape bake (gl_FragCoord.w is 1): a view-depth fade would burn
        // a shadowless bake into the cached texture.
        cglib::vec2<float> fadeRange = _drapeMVPOverride ? cglib::vec2<float>(0.0f, 0.0f) : _terrainShadowFadeRange;
        glUniform2f(shaderProgram.uniforms[U_SHADOWFADERANGE], fadeRange(0), fadeRange(1));
    }

    cglib::vec4<float> GLTileRenderer::calculateShadowNormalOffsets(const cglib::mat4x4<double>& tileFrame) const {
        // Offset in shadow TEXELS on a tile-local position. A texel = box width / mapSize, and the box
        // width is the first matrix row's length (ortho x scale 2/(r-l), view a pure rotation).
        cglib::vec4<float> offsets(0.0f, 0.0f, 0.0f, 0.0f);
        double tileScale = cglib::length(cglib::vec3<double>(tileFrame(0, 0), tileFrame(0, 1), tileFrame(0, 2)));
        if (!(tileScale > 0) || !(_terrainShadowNormalOffset > 0) || _terrainShadowMapSize <= 0) {
            return offsets;
        }
        // Each cascade its own texel, as mapbox: the near cascade's is far too small to lift an outer
        // page's walls off their own depth - acne.
        for (int i = 0; i < _terrainShadowCascades; i++) {
            const cglib::mat4x4<double>& m = _terrainShadowViewProjs[i];
            double boxScale = cglib::length(cglib::vec3<double>(m(0, 0), m(0, 1), m(0, 2)));
            if (!(boxScale > 0)) {
                continue;
            }
            offsets[i] = static_cast<float>(_terrainShadowNormalOffset * 2.0 / (boxScale * _terrainShadowMapSize) / tileScale);
        }
        return offsets;
    }

    void GLTileRenderer::setupSurfaceShadowUniforms(const ShaderProgram& shaderProgram, const cglib::mat4x4<double>& surfaceFrame, bool hasElevation) {
        if (!_terrainShadowMaskPass && _terrainShadowMaskTexture != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, _terrainShadowMaskTexture);
            glUniform1i(shaderProgram.uniforms[U_SHADOWMASK], 2);
            glActiveTexture(GL_TEXTURE0);
            glUniform2f(shaderProgram.uniforms[U_SHADOWMASKSCALE], _terrainShadowMaskScale(0), _terrainShadowMaskScale(1));
            return;
        }
        std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
        for (int i = 0; i < _terrainShadowCascades; i++) {
            shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * surfaceFrame);
        }
        glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], _terrainShadowCascades, GL_FALSE, shadowMatrices[0].data());
        setupShadowNormalOffsetUniforms(shaderProgram, surfaceFrame);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
        glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
        glActiveTexture(GL_TEXTURE0);
        // A tile without elevation is drawn flat at zero, maybe a kilometre below its neighbours: no
        // shadow until its heights arrive, or it reads as a dark block.
        glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), hasElevation ? _terrainShadowStrength : 0.0f, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
        glUniform3f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBias(0), _terrainShadowBias(1), _terrainShadowBias(2));
        glUniform4f(shaderProgram.uniforms[U_SHADOWDEPTHSCALE], _terrainShadowDepthScales[0], _terrainShadowDepthScales[1], _terrainShadowDepthScales[2], _terrainShadowDepthScales[3]);
        setupShadowFadeRangeUniform(shaderProgram);
    }

    void GLTileRenderer::setTerrainShadowMask(GLuint texture, float invScreenWidth, float invScreenHeight) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainShadowMaskTexture = texture;
        _terrainShadowMaskScale = cglib::vec2<float>(invScreenWidth, invScreenHeight);
    }

    int GLTileRenderer::renderTerrainShadowMask(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        // The fill path serves the grid and a globe's per-tile surfaces alike: require terrain only.
        if (!(_terrainMode && _terrainTextureProvider)) {
            return 0;
        }
        if (_terrainShadowTexture == 0 || _terrainShadowStrength <= 0.0f || !_terrainLighting.enabled) {
            return 0;
        }
        // The fill path IS the surface draw, with a fragment shader that stops at the shadow value.
        _terrainShadowMaskPass = true;
        int draws = 0;
        for (const TileId& tileId : tileIds) {
            renderTileSurfaceFill(tileId, Color(1.0f, 1.0f, 1.0f, 1.0f), true);
            draws++;
        }
        _terrainShadowMaskPass = false;
        checkGLError();
        return draws;
    }

    void GLTileRenderer::useProgram(const ShaderProgram& shaderProgram) {
        // glUseProgram is expensive on a tiler and the loop is layer-major, so most calls are
        // redundant. resetProgramState wherever another renderer may have bound its own.
        if (_lastUsedProgram != shaderProgram.program) {
            _lastUsedProgram = shaderProgram.program;
            glUseProgram(shaderProgram.program);
        }
        if (_transformer && _transformer->isSpherical()) {
            // World -> view ENU for the lighting shaders: a view constant, but a uniform is per program.
            glUniformMatrix3fv(shaderProgram.uniforms[U_LIGHTINGFRAME], 1, GL_FALSE, _sphereLightingFrame.data());
        }
    }

    void GLTileRenderer::resetProgramState() {
        _lastUsedProgram = 0;
    }

    void GLTileRenderer::setupFogUniforms(const ShaderProgram& shaderProgram) const {
        if (!fogFlag()) {
            return;
        }
        glUniform4f(shaderProgram.uniforms[U_FOGCOLOR], _fogColor[0], _fogColor[1], _fogColor[2], _fogColor[3]);
        glUniform4f(shaderProgram.uniforms[U_FOGHIGHCOLOR], _fogHighColor[0], _fogHighColor[1], _fogHighColor[2], _fogHighColor[3]);
        glUniform4f(shaderProgram.uniforms[U_FOGSPACECOLOR], _fogSpaceColor[0], _fogSpaceColor[1], _fogSpaceColor[2], _fogSpaceColor[3]);
        glUniform4f(shaderProgram.uniforms[U_FOGPARAMS], _fogStartDistance, 1.0f / std::max(1.0e-9f, _fogDistance - _fogStartDistance), 1.0f / _fogRangeScale, _fogHorizonBlend);
        glUniform4f(shaderProgram.uniforms[U_FOGVERTICAL], _fogVerticalStart, _fogVerticalEnd, _fogMetersPerUnit, _fogCameraHeight);
        glUniformMatrix3fv(shaderProgram.uniforms[U_FOGRAY], 1, GL_FALSE, _fogRayBasis.data());
    }

    cglib::mat4x4<double> GLTileRenderer::calculateTileMatrix(const TileId& tileId, float coordScale) const {
        TileMatrixKey key { tileId, coordScale };
        auto it = _tileMatrixCache.find(key);
        if (it != _tileMatrixCache.end()) {
            return it->second;
        }
        return _tileMatrixCache.emplace(key, _transformer->calculateTileMatrix(tileId, coordScale)).first->second;
    }
    
    cglib::mat3x3<double> GLTileRenderer::calculateTileMatrix2D(const TileId& tileId, float coordScale) const {
        return SpanResolver::tileMatrix2D(tileId, coordScale);
    }

    cglib::mat4x4<float> GLTileRenderer::calculateTileMVPMatrix(const TileId& tileId, float coordScale) const {
        TileMatrixKey key { tileId, coordScale };
        auto it = _tileMVPMatrixCache.find(key);
        if (it != _tileMVPMatrixCache.end()) {
            return it->second;
        }
        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_cameraProjMatrix * calculateTileMatrix(tileId, coordScale));
        return _tileMVPMatrixCache.emplace(key, mvpMatrix).first->second;
    }

    bool GLTileRenderer::testLayerFilter(const std::string& layerName, const std::optional<std::regex>& filter) const {
        if (!filter) {
            return true;
        }
        return std::regex_match(layerName, *filter);
    }

    bool GLTileRenderer::isLayerDraped(const std::shared_ptr<const TileLayer>& layer) const {
        if (!_noDrapeLayerFilter || !layer) {
            return true;
        }
        auto it = _noDrapeLayerCache.find(layer->getLayerName());
        if (it == _noDrapeLayerCache.end()) {
            it = _noDrapeLayerCache.emplace(layer->getLayerName(), !std::regex_match(layer->getLayerName(), *_noDrapeLayerFilter)).first;
        }
        return it->second;
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
                float alpha = Color::fromValue(pattern->bitmap->data[y * pattern->bitmap->width + x])[3];
                if (alpha > ALPHA_HIT_THRESHOLD) {
                    mask |= (x >= xp ? 1 : 0);
                    mask |= (x <= xp ? 2 : 0);
                }
            }
        }
        for (int x = xp, y = y0; y <= y1; y++) {
            if (x >= 0 && x < pattern->bitmap->width && y >= 0 && y < pattern->bitmap->height) {
                float alpha = Color::fromValue(pattern->bitmap->data[y * pattern->bitmap->width + x])[3];
                if (alpha > ALPHA_HIT_THRESHOLD) {
                    mask |= (y >= yp ? 4 : 0);
                    mask |= (y <= yp ? 8 : 0);
                }
            }
        }
        return mask == 15;
    }

    void GLTileRenderer::buildTileSurfaces(const std::set<TileId>& tileIds) {
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
    }

    void GLTileRenderer::buildRenderTiles(const std::map<TileId, std::shared_ptr<const Tile>>& tiles) {
        std::vector<RenderTile> renderTiles;
        renderTiles.reserve(tiles.size() + _renderTiles->size());

        for (auto it = tiles.begin(); it != tiles.end(); it++) {
            RenderTile& renderTile = renderTiles.emplace_back();
            initializeRenderTile(it->first, renderTile, it->second, *_renderTiles);
        }

        for (auto it = _renderTiles->begin(); it != _renderTiles->end(); it++) {
            RenderTile existingRenderTile = *it;
            if (existingRenderTile.visible) {
                mergeExistingRenderTile(existingRenderTile.targetTileId, existingRenderTile, renderTiles, 1);
            }
        }

        _renderTiles = std::make_shared<std::vector<RenderTile>>(std::move(renderTiles));
    }

    void GLTileRenderer::initializeRenderTile(TileId targetTileId, RenderTile& renderTile, const std::shared_ptr<const Tile>& tile, const std::vector<RenderTile>& existingRenderTiles) const {
        // Apply 'root shift' to source tile id. Adjust target tile id, if needed.
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
        renderTile.current = true;
        for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
            if (!testLayerFilter(layer->getLayerName(), _rendererLayerFilter)) {
                continue;
            }

            RenderTileLayer renderLayer;
            renderLayer.targetTileId = targetTileId;
            renderLayer.sourceTileId = sourceTileId;
            renderLayer.layer = layer;
            renderLayer.tileSize = tile->getTileSize();
            renderLayer.active = true;
            renderLayer.blend = 0.0f;
            renderTile.renderLayers.insert({ layer->getLayerIndex(), std::move(renderLayer) });
        }

        // Check if this tile intersects with any existing tile. Then reuse the state from the existing tile.
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
        renderTile.current = false;
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            RenderTileLayer& renderLayer = it->second;
            renderLayer.targetTileId = (targetTileId.zoom > renderLayer.targetTileId.zoom ? targetTileId : renderLayer.targetTileId);
            renderLayer.active = false;
        }
        renderTiles.push_back(renderTile);
    }

    bool GLTileRenderer::updateRenderTile(RenderTile& renderTile, float dBlend) const {
        renderTile.visible = isTileVisible(renderTile.targetTileId);

        bool refresh = false;
        for (auto it = renderTile.renderLayers.end(); it != renderTile.renderLayers.begin(); ) {
            it--;
            RenderTileLayer& renderLayer = it->second;

            // If layer is not visible, make it visible/hidden in one step. Otherwise use actual delta blend value.
            float delta = renderTile.visible ? dBlend : 1.0f;
            if (renderLayer.active) {
                renderLayer.blend = std::min(1.0f, renderLayer.blend + delta);
                refresh = (renderLayer.blend < 1.0f) || refresh;

                // Try to remove old layers when a new layer has reached full visibility and covers old layer.
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
                // An inactive layer under a still-fading ACTIVE one is the only paint there, so it
                // holds: fading both leaves blend + (1-blend)^2 coverage, a blink at every zoom step.
                bool replaced = false;
                auto it2 = it;
                for (it2++; it2 != renderTile.renderLayers.end() && it2->first == it->first; it2++) {
                    if (it2->second.active && it2->second.targetTileId.covers(renderLayer.targetTileId)) {
                        replaced = true;
                        break;
                    }
                }
                bool anyActive = false;
                for (auto it2 = renderTile.renderLayers.begin(); it2 != renderTile.renderLayers.end(); it2++) {
                    anyActive = anyActive || it2->second.active;
                }
                if (retainedLayerFades(replaced, renderTile.visible, anyActive, renderTile.current)) {
                    renderLayer.blend = std::max(0.0f, renderLayer.blend - delta);
                    refresh = (renderLayer.blend > 0.0f) || refresh;

                    if (renderLayer.blend <= 0.0f) {
                        it = renderTile.renderLayers.erase(it);
                    }
                }
            }
        }
        return refresh;
    }

    long long GLTileRenderer::calculateLabelGeometryHash(const Tile* tile, long long localId) {
        std::uint64_t hash = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(tile));
        hash ^= static_cast<std::uint64_t>(localId) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33;
        return static_cast<long long>(hash);
    }

    // Keyed on the tile objects: a re-decoded tile can re-serve the same id and must rebuild.
    long long GLTileRenderer::calculateLabelTilesSignature(const std::vector<std::shared_ptr<const Tile>>& labelTiles) {
        long long signature = static_cast<long long>(labelTiles.size());
        for (const std::shared_ptr<const Tile>& labelTile : labelTiles) {
            signature += calculateLabelGeometryHash(labelTile.get(), 0); // order-independent, as pass 1 is
        }
        return signature;
    }

    // The two expensive phases, off _mutex against a snapshot: they write no renderer state, and a
    // reused live label only has its build-time signature read. The caller checks the generation.
    void GLTileRenderer::prepareLabelMaps(const std::vector<std::shared_ptr<const Tile>>& labelTiles, const std::map<int, GlobalIdLabelMap>& oldLayerLabelMap, const std::optional<std::regex>& layerFilter, LabelMapBuild& build) const {
        VT_STAT_INC(labelMapRebuilds);

        VT_STAT_CLOCK(labelMapClock);
        std::map<int, std::unordered_map<long long, std::pair<long long, int>>>& newLayerSignatureMap = build.signatures;
        std::map<int, GlobalIdLabelMap>& newLayerLabelMap = build.labelMap;
        std::map<int, std::unordered_set<long long>>& reusedLayerLabelIds = build.reusedIds;
        // Pass 1: which tile geometries build each label, building nothing. The hashes are summed, so
        // the signature does not depend on visit order.
        auto oldLabelCount = [&oldLayerLabelMap](int layerIndex) {
            auto it = oldLayerLabelMap.find(layerIndex);
            return it != oldLayerLabelMap.end() ? it->second.size() : static_cast<std::size_t>(0);
        };
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                if (!testLayerFilter(layer->getLayerName(), layerFilter)) {
                    continue;
                }

                std::unordered_map<long long, std::pair<long long, int>>& signatureMap = newLayerSignatureMap[layer->getLayerIndex()];
                if (signatureMap.empty()) {
                    // Sized from last time's count, as the merge pass does, to avoid rehashing every rebuild.
                    signatureMap.reserve(oldLabelCount(layer->getLayerIndex()) + 64);
                }
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    std::pair<long long, int>& signature = signatureMap[tileLabel->getGlobalId()];
                    signature.first += calculateLabelGeometryHash(tile.get(), tileLabel->getLocalId());
                    signature.second++;
                }
            }
        }

        VT_STAT_SPLIT(labelSignatureNs, labelMapClock);
        // Pass 2: build, reuse or merge the labels.
        static const GlobalIdLabelMap emptyLabelMap;
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            cglib::mat4x4<double> tileMatrix = build.transformer->calculateTileMatrix(tile->getTileId(), 1.0f);
            std::shared_ptr<const TileTransformer::VertexTransformer> transformer = build.transformer->createTileVertexTransformer(tile->getTileId());
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                if (!testLayerFilter(layer->getLayerName(), layerFilter)) {
                    continue;
                }

                GlobalIdLabelMap& newLabelMap = newLayerLabelMap[layer->getLayerIndex()];
                if (newLabelMap.empty()) {
                    newLabelMap.reserve(oldLabelCount(layer->getLayerIndex()) + 64);
                }
                auto oldLabelMapIt = oldLayerLabelMap.find(layer->getLayerIndex());
                const GlobalIdLabelMap& oldLabelMap = (oldLabelMapIt != oldLayerLabelMap.end() ? oldLabelMapIt->second : emptyLabelMap);
                const std::unordered_map<long long, std::pair<long long, int>>& signatureMap = newLayerSignatureMap[layer->getLayerIndex()];
                std::unordered_set<long long>& reusedLabelIds = reusedLayerLabelIds[layer->getLayerIndex()];
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    long long globalId = tileLabel->getGlobalId();
                    std::shared_ptr<Label>& label = newLabelMap[globalId];
                    if (label) {
                        // A reused label already holds every contribution; merging would duplicate it.
                        if (reusedLabelIds.count(globalId) > 0) {
                            continue;
                        }
                        Label newLabel(*tileLabel, tile->getTileId(), layer->getLayerIndex(), tileMatrix, transformer);
                        label->mergeGeometries(newLabel);
                        VT_STAT_INC(labelsAllocated); // the merge copy costs the same geometry transform
                        continue;
                    }

                    // Reuse the label when every contribution is unchanged: a rebuild re-transforms, drops
                    // cached vertex data and re-snaps the anchor, churning labels as tiles stream in.
                    const std::pair<long long, int>& signature = signatureMap.at(globalId);
                    auto oldLabelIt = oldLabelMap.find(globalId);
                    if (oldLabelIt != oldLabelMap.end() && oldLabelIt->second->hasGeometrySignature(signature.first, signature.second)) {
                        label = oldLabelIt->second;
                        reusedLabelIds.insert(globalId);
                        VT_STAT_INC(labelsReused);
                        continue;
                    }

                    label = std::make_shared<Label>(*tileLabel, tile->getTileId(), layer->getLayerIndex(), tileMatrix, transformer);
                    VT_STAT_INC(labelsAllocated);
                }
            }
        }

        VT_STAT_SPLIT(labelMergeNs, labelMapClock);
        // Stamp only now that every tile is merged: stamped at construction a label looks complete to
        // the merge branch. Still off the mutex: the renderer has never seen these.
        for (auto newLayerLabelIt = newLayerLabelMap.begin(); newLayerLabelIt != newLayerLabelMap.end(); newLayerLabelIt++) {
            const std::unordered_map<long long, std::pair<long long, int>>& signatureMap = newLayerSignatureMap[newLayerLabelIt->first];
            const std::unordered_set<long long>& reusedLabelIds = reusedLayerLabelIds[newLayerLabelIt->first];
            for (auto newLabelIt = newLayerLabelIt->second.begin(); newLabelIt != newLayerLabelIt->second.end(); newLabelIt++) {
                if (reusedLabelIds.count(newLabelIt->first) > 0) {
                    continue;
                }
                const std::pair<long long, int>& signature = signatureMap.at(newLabelIt->first);
                newLabelIt->second->setGeometrySignature(signature.first, signature.second);
            }
        }

        VT_STAT_SPLIT(labelStampNs, labelMapClock);
    }

    // Mutates the live maps and draw lists; caller holds _mutex. Kept short: erase, carry placement, sort.
    void GLTileRenderer::commitLabelMaps(LabelMapBuild& build) {
        std::map<int, GlobalIdLabelMap>& newLayerLabelMap = build.labelMap;
        std::map<int, std::unordered_set<long long>>& reusedLayerLabelIds = build.reusedIds;
        _labelTilesSignature = build.signature;

        VT_STAT_CLOCK(labelMapClock);
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

        VT_STAT_SPLIT(labelReleaseNs, labelMapClock);
        for (auto newLayerLabelIt = newLayerLabelMap.begin(); newLayerLabelIt != newLayerLabelMap.end(); newLayerLabelIt++) {
            const GlobalIdLabelMap& newLabelMap = newLayerLabelIt->second;
            GlobalIdLabelMap& labelMap = _layerLabelMap[newLayerLabelIt->first];
            const std::unordered_set<long long>& reusedLabelIds = reusedLayerLabelIds[newLayerLabelIt->first];
            for (auto newLabelIt = newLabelMap.begin(); newLabelIt != newLabelMap.end(); newLabelIt++) {
                const std::shared_ptr<Label>& newLabel = newLabelIt->second;
                std::shared_ptr<Label>& label = labelMap[newLabelIt->first];
                // A reused object IS the previous label, already current. Not decidable from the map
                // entry: the release pass erases entries whose label has faded out.
                if (reusedLabelIds.count(newLabelIt->first) > 0) {
                    // nothing to carry over
                }
                else if (label) {
                    newLabel->setVisible(label->isVisible());
                    newLabel->setOpacity(label->getOpacity());
                    newLabel->setTextOpacity(label->getTextOpacity());
                    newLabel->snapPlacement(*label);
                }
                else {
                    newLabel->setVisible(false);
                    newLabel->setOpacity(0);
                    newLabel->setTextOpacity(0);
                }
                newLabel->setActive(true);
                label = newLabel;
            }
        }

        VT_STAT_SPLIT(labelCarryNs, labelMapClock);
        // One list per pass in draw order - the style's (priority, layer, id) alone. Grouping by glyph
        // atlas first made the order across atlases a pointer hash.
        std::vector<std::shared_ptr<Label>> labels;
        labels.reserve(_labels.size() + 64);
        std::array<std::shared_ptr<PassLabels>, 2> passLabels;
        for (int pass = 0; pass < 2; pass++) {
            passLabels[pass] = std::make_shared<PassLabels>();
            passLabels[pass]->reserve(_passLabels[pass] ? _passLabels[pass]->size() + 64 : 64);
        }
        for (auto layerLabelIt = _layerLabelMap.begin(); layerLabelIt != _layerLabelMap.end(); layerLabelIt++) {
            const GlobalIdLabelMap& labelMap = layerLabelIt->second;
            for (auto labelIt = labelMap.begin(); labelIt != labelMap.end(); labelIt++) {
                const std::shared_ptr<Label>& label = labelIt->second;
                int pass = ((label->getStyle()->orientation == LabelOrientation::BILLBOARD_3D || label->getStyle()->orientation == LabelOrientation::LINE_BILLBOARD_3D) ? 1 : 0);
                passLabels[pass]->push_back(label);
                labels.push_back(label);
            }
        }
        // Cull order (opposite of the draw order below), most important first, and global: a rationed
        // cycle only sorts its own slice, so a sliced cycle would otherwise differ from a whole one.
        std::stable_sort(labels.begin(), labels.end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
            if (label1->getPriority() != label2->getPriority()) {
                return label1->getPriority() > label2->getPriority();
            }
            if (label1->getLayerIndex() != label2->getLayerIndex()) {
                return label1->getLayerIndex() < label2->getLayerIndex();
            }
            return label1->getGlobalId() < label2->getGlobalId();
        });

        for (int pass = 0; pass < 2; pass++) {
            // DRAW order, bottom to top - the reverse of the culler's, which places the most important
            // first; copying it drew a road name over the town name it crosses.
            std::stable_sort(passLabels[pass]->begin(), passLabels[pass]->end(), [](const std::shared_ptr<Label>& label1, const std::shared_ptr<Label>& label2) {
                if (label1->getPriority() != label2->getPriority()) {
                    return label1->getPriority() < label2->getPriority();
                }
                if (label1->getLayerIndex() != label2->getLayerIndex()) {
                    return label1->getLayerIndex() < label2->getLayerIndex();
                }
                return label1->getGlobalId() > label2->getGlobalId();
            });
        }

        _labels = std::move(labels);
        _passLabels = std::move(passLabels);
        VT_STAT_SPLIT(labelListNs, labelMapClock);
        VT_STAT_SET(labelsLive, static_cast<long long>(_labels.size()));
    }

    bool GLTileRenderer::updateLabel(const std::shared_ptr<Label>& label, float dOpacity) const {
        bool refresh = false;
        if (label->isValid()) {
            bool occluded = false;
            // Not while the height is unknown: the flat decode height would hide it under the ground.
            if (_labelOcclusionTest && label->isVisible() && label->isActive() && label->isElevationAnchored() && !label->isElevationStale()) {
                cglib::vec3<double> center(0, 0, 0);
                if (label->calculateCenter(center)) {
                    occluded = _labelOcclusionTest(center);
                }
            }
            bool shown = label->isVisible() && label->isActive() && !occluded;
            if (shown) {
                float opacity = std::min(1.0f, label->getOpacity() + dOpacity);
                label->setOpacity(opacity);
                refresh = (opacity < 1.0f) || refresh;
            }
            else {
                float opacity = std::max(0.0f, label->getOpacity() - dOpacity);
                label->setOpacity(opacity);
                refresh = (opacity > 0.0f) || refresh;
            }
            // The text fades on its own, so a name dropped for the icon-only variant fades while its
            // icon stays (Label::getTextOpacity). With no icon, drawsText() is always true.
            if (shown && label->drawsText()) {
                float opacity = std::min(1.0f, label->getTextOpacity() + dOpacity);
                label->setTextOpacity(opacity);
                refresh = (opacity < 1.0f) || refresh;
            }
            else {
                float opacity = std::max(0.0f, label->getTextOpacity() - dOpacity);
                label->setTextOpacity(opacity);
                refresh = (opacity > 0.0f) || refresh;
            }
        }
        return refresh;
    }
    
    void GLTileRenderer::findTileGeometryIntersections(const TileId& tileId, const std::shared_ptr<const TileGeometry>& geometry, const std::vector<cglib::ray3<double>>& rays, float tileSize, float pointBuffer, float lineBuffer, float heightScale, std::vector<GeometryIntersectionInfo>& results) const {
        float scale = geometry->getGeometryScale() / tileSize / std::pow(2.0f, _viewState.zoom - tileId.zoom);
        for (TileGeometryIterator it(tileId, geometry, _transformer, _viewState, pointBuffer, lineBuffer, scale, heightScale); it; ++it) {
            size_t indicesCount = geometry->getIndicesCount();
            size_t featuresCount = geometry->getFeatureCount();
            size_t geoPosIndexesCount = geometry->getGeoPosIndexesCount();
            TileGeometryIterator::TriangleCoords coords = it.triangleCoords();

            for (std::size_t i = 0; i < rays.size(); i++) {
                double t = 0;
                cglib::vec2<double> uv(0.0f, 0.0f);
                if (cglib::intersect_triangle(cglib::vec3<double>::convert(coords[0]), cglib::vec3<double>::convert(coords[1]), cglib::vec3<double>::convert(coords[2]), rays[i], &t, &uv)) {
                    if (geometry->getType() == TileGeometry::Type::POINT && it.attribs()[1] != 0) {
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
                    long long featureId = it.id();
                    if (!results.empty()) {
                        const GeometryIntersectionInfo& result = results.back();
                        if (result.tileId == tileId && result.featureId == featureId) {
                            break;
                        }
                    }
                    std::uint16_t geoPosIndex = it.geoPosIndex();
                    results.emplace_back(tileId, -1, featureId, geoPosIndex, i, t);
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
                results.emplace_back(label->getTileId(), -1, label->getLocalId(), label->getGeoPointIndex(), i, t);
                break;
            }
        }
    }

    void GLTileRenderer::findTileBitmapIntersections(const TileId& tileId, const std::shared_ptr<const TileBitmap>& bitmap, const std::shared_ptr<const TileSurface>& tileSurface, const std::vector<cglib::ray3<double>>& rays, float tileSize, std::vector<BitmapIntersectionInfo>& results) const {
        cglib::mat4x4<double> surfaceToTileTransform = cglib::inverse(calculateTileMatrix(tileId)) * cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
        for (std::size_t index = 0; index + 2 < tileSurface->getIndices().size(); index += 3) {
            std::array<cglib::vec3<double>, 3> triangle;
            bool skirt = false;
            for (int i = 0; i < 3; i++) {
                std::size_t coordOffset = tileSurface->getIndices()[index + i] * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.coordOffset;
                const float* coordPtr = reinterpret_cast<const float*>(&tileSurface->getVertexGeometry()[coordOffset]);
                skirt = skirt || coordPtr[2] < -900000.0f; // skirt bottoms carry sentinel z values
                triangle[i] = cglib::transform_point(cglib::vec3<double>(coordPtr[0], coordPtr[1], coordPtr[2]), surfaceToTileTransform);
            }
            if (skirt) {
                continue;
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
        std::map<int, std::vector<const RenderTileLayer*>> renderLayerMap;
        // Tangram's maxVisS: the deepest level on screen, which a proxy tile's depth is measured against.
        int maxVisibleZoom = 0;
        for (const RenderTile& renderTile : renderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            maxVisibleZoom = std::max(maxVisibleZoom, renderTile.targetTileId.zoom);
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const std::shared_ptr<const TileLayer>& layer = it->second.layer;

                bool contains2DGeometry = !layer->getBackgrounds().empty() || !layer->getBitmaps().empty();
                for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
                    contains2DGeometry = (geometry->getType() != TileGeometry::Type::POLYGON3D) || contains2DGeometry;
                }
                if (contains2DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    if (!_rendererLayerIndexRange || (it->first >= _rendererLayerIndexRange->first && it->first < _rendererLayerIndexRange->second)) {
                        renderLayerMap[it->first].push_back(&it->second);
                    }
                }
            }
        }

        // Tangram's proxy depth (tileManager.cpp setProxyDepth): levels coarser than the deepest on
        // screen, not a flat 1 - a tile two levels up is a different height field twice over.
        auto proxyDepth = [maxVisibleZoom](const RenderTileLayer* renderLayer) -> float {
            if (renderLayer->active) {
                return 0.0f;
            }
            return std::max(static_cast<float>(maxVisibleZoom - renderLayer->targetTileId.zoom), TERRAIN_PROXY_DEPTH_UNITS);
        };

        // Stencil masks clip a tile's content to its footprint. On terrain a mask is a full displaced
        // grid per tile, so they are dropped - except for a comp-op layer, whose overlay has no depth.
        GLint maskStencilBits = (_terrainSharedGround ? 0 : stencilBits);
        if (maskStencilBits > 0 && _tileMasks < 0 && _terrainMode) {
            bool anyCompOp = false;
            for (auto it = renderLayerMap.begin(); it != renderLayerMap.end() && !anyCompOp; it++) {
                for (const RenderTileLayer* renderLayer : it->second) {
                    if (renderLayer->layer->getCompOp()) {
                        anyCompOp = true;
                        break;
                    }
                }
            }
            if (!anyCompOp) {
                maskStencilBits = 0;
            }
        } else if (_tileMasks == 0) {
            maskStencilBits = 0;
        }

        std::map<TileId, GLint> tileStencilMap;
        std::set<TileId> activeStencilTiles;
        if (maskStencilBits > 0) {
            for (const RenderTile& renderTile : renderTiles) {
                if (!renderTile.visible || renderTile.renderLayers.empty()) {
                    continue;
                }
                auto it = renderTile.renderLayers.begin();
                bool active = it->second.active;
                TileId targetTileId = it->second.targetTileId;
                while (++it != renderTile.renderLayers.end()) {
                    active = active || it->second.active;
                    if (it->second.targetTileId.zoom < targetTileId.zoom) {
                        targetTileId = it->second.targetTileId;
                    }
                }
                tileStencilMap[targetTileId] = static_cast<int>(tileStencilMap.size() + 1);
                if (active) {
                    activeStencilTiles.insert(targetTileId);
                }
            }
            glEnable(GL_STENCIL_TEST);
        }
        
        // Terrain: a style layer's tiles near-to-far. Content writes depth, so near tiles occlude far
        // ones and far translucent content cannot blend under near content already drawn.
        if (_terrainMode && _terrainTextureProvider) {
            // One distance per tile, not per comparison: on terrain calculateTileBBox samples the DEM.
            std::map<TileId, double> tileDistances;
            auto tileDistance = [this, &tileDistances](const TileId& tileId) -> double {
                auto it = tileDistances.find(tileId);
                if (it == tileDistances.end()) {
                    cglib::vec3<double> center = _transformer->calculateTileBBox(tileId).center();
                    it = tileDistances.emplace(tileId, cglib::length(center - _viewState.origin)).first;
                }
                return it->second;
            };
            for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
                for (const RenderTileLayer* renderLayer : it->second) {
                    tileDistance(renderLayer->targetTileId);
                }
                std::sort(it->second.begin(), it->second.end(), [&tileDistance](const RenderTileLayer* layer1, const RenderTileLayer* layer2) {
                    // Retained blend-out (proxy) tiles first: without stencil masks nothing else stops a
                    // stale crossfade tile painting over the live one replacing it.
                    if (layer1->active != layer2->active) {
                        return layer2->active;
                    }
                    return tileDistance(layer1->targetTileId) < tileDistance(layer2->targetTileId);
                });
            }
        }

        VT_STAT_ADD(styleLayersDrawn, static_cast<long long>(renderLayerMap.size()));
        VT_STAT_ADD(renderTilesDrawn, static_cast<long long>(renderTiles.size()));

        bool resetStencil = true;
        std::optional<CompOp> currentCompOp;
        // Tangram's style-layer order as a dense index (docs/internals/rendering/05-depth-model.md), over
        // every layer EVER drawn: ranking only those present renumbers the stack as tiles come and go.
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            _terrainStyleLayerIndices.insert(it->first);
        }
        _terrainStyleLayersDrawn = static_cast<int>(_terrainStyleLayerIndices.size()); // the owner numbers the next renderer from here

        // Without a spare stencil bit above the tile masks (or under a comp-op overlay) a group draws as plain layers.
        bool drawOnceStencil = stencilBits >= 8 && tileStencilMap.size() < DRAW_ONCE_STENCIL_BIT;
        auto drawOnceGroup = [drawOnceStencil](const std::vector<const RenderTileLayer*>& renderLayers) -> std::string {
            if (!drawOnceStencil || renderLayers.empty() || renderLayers.front()->layer->getCompOp()) {
                return std::string();
            }
            return renderLayers.front()->layer->getDrawOnceGroup();
        };
        std::vector<decltype(renderLayerMap)::iterator> layerOrder;
        layerOrder.reserve(renderLayerMap.size());
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            layerOrder.push_back(it);
        }
        auto schedule = drawOnceSchedule(layerOrder, [&drawOnceGroup](decltype(renderLayerMap)::iterator it) {
            return drawOnceGroup(it->second);
        });

        std::string activeDrawOnceGroup;
        for (const auto& [it, drawOncePass] : schedule) {
            int layerOrdinal = _terrainLayerOrdinalBase + static_cast<int>(std::distance(_terrainStyleLayerIndices.begin(), _terrainStyleLayerIndices.find(it->first)));
            const std::vector<const RenderTileLayer*>& renderLayers = it->second;
            if (renderLayers.empty()) {
                continue;
            }
            const std::shared_ptr<const TileLayer>& layer = renderLayers.front()->layer;

            float layerOpacity = (layer->getOpacityFunc())(_viewState);
            float geometryOpacity = 1.0f;
            if (!layer->getCompOp()) { // a 'useful' hack - we use real layer opacity only if comp-op is explicitly defined; otherwise we translate it into element opacity, which is in many cases close enough
                std::swap(layerOpacity, geometryOpacity);
            }
            CompOp layerCompOp = (layer->getCompOp() ? *layer->getCompOp() : CompOp::SRC_OVER);

            std::string layerDrawOnceGroup = drawOnceGroup(renderLayers);
            if (layerDrawOnceGroup != activeDrawOnceGroup) {
                if (!activeDrawOnceGroup.empty()) {
                    if (maskStencilBits > 0) {
                        // The stamps have their own bit: clearing it leaves the tile masks below intact.
                        glStencilMask(DRAW_ONCE_STENCIL_BIT);
                        glClearStencil(0);
                        glClear(GL_STENCIL_BUFFER_BIT);
                        glStencilMask(0);
                    } else {
                        glDisable(GL_STENCIL_TEST);
                    }
                }
                if (!layerDrawOnceGroup.empty() && maskStencilBits == 0) {
                    glEnable(GL_STENCIL_TEST);
                    glStencilMask(255);
                    glClearStencil(0);
                    glClear(GL_STENCIL_BUFFER_BIT);
                    glStencilMask(0);
                    glStencilFunc(GL_EQUAL, 0, 255);
                }
                activeDrawOnceGroup = layerDrawOnceGroup;
            }
            _drawOncePass = drawOncePass;

            GLint currentFBO = 0;
            if (layer->getCompOp()) {
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                if (_overlayBuffer2D.fbo == 0) {
                    createFrameBuffer(_overlayBuffer2D, true, false, maskStencilBits > 0);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer2D.fbo);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                resetStencil = true;
            }

            // Stencil masks are screen-space clipping, not depth-tested: mask surfaces carry no depth bias.
            if (resetStencil && maskStencilBits > 0) {
                resetStencil = false;

                if (_terrainMode) {
                    glDisable(GL_DEPTH_TEST);
                }
                glStencilMask(255);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
                std::vector<std::pair<TileId, GLint>> orderedTileMasks(tileStencilMap.begin(), tileStencilMap.end());
                if (_terrainMode) {
                    // Displaced tiles overlap, so mask order decides who owns a pixel: retained blend-out
                    // tiles first, then zoom ascending (a child beats its parent), then farthest first.
                    std::vector<std::tuple<int, int, double, std::size_t>> tileMaskOrder(orderedTileMasks.size());
                    for (std::size_t i = 0; i < orderedTileMasks.size(); i++) {
                        cglib::vec3<double> center = _transformer->calculateTileBBox(orderedTileMasks[i].first).center();
                        int activeRank = (activeStencilTiles.count(orderedTileMasks[i].first) > 0 ? 1 : 0);
                        tileMaskOrder[i] = std::make_tuple(activeRank, orderedTileMasks[i].first.zoom, -cglib::length(center - _viewState.origin), i);
                    }
                    std::sort(tileMaskOrder.begin(), tileMaskOrder.end());
                    std::vector<std::pair<TileId, GLint>> sortedTileMasks;
                    sortedTileMasks.reserve(orderedTileMasks.size());
                    for (const std::tuple<int, int, double, std::size_t>& order : tileMaskOrder) {
                        sortedTileMasks.push_back(orderedTileMasks[std::get<3>(order)]);
                    }
                    orderedTileMasks = std::move(sortedTileMasks);
                }
                for (auto it = orderedTileMasks.begin(); it != orderedTileMasks.end(); it++) {
                    glStencilFunc(GL_ALWAYS, it->second, 255);
                    renderTileMask(it->first);
                }
                if (_debugWireframe) {
                    _debugOrderedTileMasks = orderedTileMasks; // for the debug overlay pass
                }
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glStencilMask(0);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                if (_terrainMode) {
                    glEnable(GL_DEPTH_TEST);
                }
            }

            for (const RenderTileLayer* renderLayer : renderLayers) {
                if (maskStencilBits > 0) {
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

                // GPU draping: all 2D content writes real depth, LEQUAL + painter's order stacks coplanar
                // layers, proxy tiles go back one delta. CPU fallback: slope-scaled polygon offsets.
                bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
                bool contentDepthWrite = _terrainMode && !layer->getCompOp() && (terrainVTF || _terrainDepthWrite);
                if (_terrainMode) {
                    if (contentDepthWrite) {
                        glDepthMask(GL_TRUE);
                    }
                    if (_terrainSharedGround) {
                        // Tangram's model whole: writes + per-layer ordinal + constant-clip depth_shift;
                        // none works alone (docs/internals/rendering/05-depth-model.md).
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Backgrounds/bitmaps render the pre-pass meshes, so they draw at TRUE depth: a
                        // pushback is rejected by GL_LESS once depths quantize together.
                        float proxyBias = (renderLayer->active ? 0.0f : 1.0f * TERRAIN_LAYER_DEPTH_DELTA);
                        _terrainDrawDepthBias = _terrainDepthBias - proxyBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                    } else {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        if (_terrainDepthWrite && !layer->getCompOp()) {
                            // Constant push only: a slope-scaled one pushes tall steep faces back far
                            // enough to show what is behind the ridge.
                            glPolygonOffset(0.0f, 2.0f);
                        } else {
                            glPolygonOffset(-1.0f, -2.0f);
                        }
                    }
                }

                // Draped content lives in the drape texture and must not also draw as geometry; proxy
                // layers are draped too, through the sub-rect bake.
                bool drapedTile = isTileDraped(renderLayer->targetTileId);
                // Two tessellations of one height field disagree, so under a shared ground this draws on
                // the COVER tiles - once per leaf, with a uv sub-rect, for a coarser layer.
                const std::vector<TileId>& groundTiles = collectGroundLeaves(renderLayer->targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer->layer->getBackgrounds()) {
                    // Draped native backgrounds are baked into the surface texture already.
                    if (drapedTile || _drawOncePass == DrawOncePass::CORE) {
                        continue;
                    }
                    CompOp backgroundCompOp = CompOp::SRC_OVER;
                    if (currentCompOp != backgroundCompOp) {
                        setCompOp(backgroundCompOp);
                        currentCompOp = backgroundCompOp;
                    }
                    // Under a shared ground the ground pass owns the ground colour: a patternless
                    // background is skipped, as tangram has no per-tile background mesh.
                    if (_terrainSharedGround && !background->getPattern() && !_terrainTileBackgrounds) {
                        continue;
                    }
                    for (const TileId& groundTileId : groundTiles) {
                        // The pattern is anchored in the tile's own uv, so a leaf covering a
                        // quarter of the tile repeats it a quarter as often.
                        float groundTileSize = renderLayer->tileSize * std::exp2(static_cast<float>(renderLayer->targetTileId.zoom - groundTileId.zoom));
                        renderTileBackground(groundTileId, renderLayer->blend, geometryOpacity, groundTileSize, background);
                    }
                }

                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer->layer->getBitmaps()) {
                    // Draped rasters (hillshade, imagery) are baked into the drape texture already.
                    if (drapedTile || _drawOncePass == DrawOncePass::CORE) {
                        continue;
                    }
                    CompOp bitmapCompOp = CompOp::SRC_OVER;
                    if (currentCompOp != bitmapCompOp) {
                        setCompOp(bitmapCompOp);
                        currentCompOp = bitmapCompOp;
                    }
                    for (const TileId& groundTileId : groundTiles) {
                        renderTileBitmap(renderLayer->sourceTileId, groundTileId, renderLayer->blend, geometryOpacity, bitmap);
                    }
                }

                if (_terrainMode) {
                    // Outside a shared ground geometry writes no depth: it stacks by painter's order, and
                    // vector elements drawn after the tile layers stay in front.
                    if (contentDepthWrite && !_terrainSharedGround) {
                        glDepthMask(GL_FALSE);
                    }
                    if (_terrainSharedGround) {
                        // Geometry writes too, as tangram, with its layer's ordinal: one step separates
                        // COPLANAR layers. An un-subdivided fill needs more, still open.
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Real depth: the clearance comes from pushing the SURFACE back, and lattice-clamped
                        // content coincides with it under LEQUAL; any forward bias leaks over ridges at range.
                        float proxyBias = (renderLayer->active ? 0.0f : 1.0f * TERRAIN_LAYER_DEPTH_DELTA);
                        _terrainDrawDepthBias = (_terrainRegularGrid ? 0.0f : _terrainDepthBias + 1.0f * TERRAIN_LAYER_DEPTH_DELTA) - proxyBias;
                        // Adaptive meshes keep the calibrated, distance-growing slack.
                        _terrainDrawDepthClipUnits = _terrainRegularGrid ? 0.0f : 12.0f;
                    } else {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(-1.0f, -2.0f);
                    }
                }

                // A no-drape layer draws after the whole drape composite: mask it with the coverage of
                // the draped layers after it in style order, so they still win (#175).
                _drapeMaskTexture = 0;
                if (drapedTile && !isLayerDraped(renderLayer->layer)) {
                    if (!resolveDrapeCoverageMask(renderLayer->targetTileId, renderLayer->layer->getLayerIndex(), _drapeMaskTexture, _drapeMaskUVTransform)) {
                        _drapeMaskTexture = 0;
                    }
                }

                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    // BEFORE the drape test, which asks whether the chord resolved: else a draped span
                    // never lifts - baked because unlifted, unlifted because baked.
                    bool span = !geometry->getSpanRecords().empty();
                    if (span) {
                        _spanResolver.resolve(renderLayer->sourceTileId, geometry, _extrusionBaseVersion.load(std::memory_order_relaxed));
                    }
                    // Draped fills/lines are baked already, unless the layer opted out (setNoDrapeLayerFilter).
                    if (drapedTile && isDrapeableGeometry(geometry) && isLayerDraped(renderLayer->layer)) {
                        continue;
                    }
                    // POLYGON3DGROUND is a contact shadow for the 3D pass, not 2D content.
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D && geometry->getType() != TileGeometry::Type::POLYGON3DGROUND) {
                        CompOp geometryCompOp = geometry->getStyleParameters().compOp;
                        if (currentCompOp != geometryCompOp) {
                            setCompOp(geometryCompOp);
                            currentCompOp = geometryCompOp;
                        }
                        // Undraped lines and points are decals: flat quads over a curved surface, half cut
                        // away at zero bias. The offset scales with depth slope, as the sag does.
                        bool decal = terrainVTF && !span && (geometry->getType() == TileGeometry::Type::LINE || geometry->getType() == TileGeometry::Type::POINT);
                        if (decal) {
                            glEnable(GL_POLYGON_OFFSET_FILL);
                            glPolygonOffset(-2.0f, -8.0f);
                        }
                        // A line chords over the relief and is cut wherever it sags into a depth-writing
                        // ground; the clearance is in METRES, which no ordinal or depth bias can express.
                        if (decal) {
                            _terrainDrawClearance = _terrainLineClearance;
                        }
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, geometry);
                        _terrainDrawClearance = 0.0f;
                        if (decal) {
                            glDisable(GL_POLYGON_OFFSET_FILL);
                            glPolygonOffset(0.0f, 0.0f);
                        }
                    }
                }

                _drapeMaskTexture = 0;

                if (_terrainMode) {
                    if (contentDepthWrite) {
                        glDepthMask(GL_FALSE);
                    }
                    _terrainDrawLayerOffset = 0.0f;
                    if (!terrainVTF) {
                        glDisable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(0.0f, 0.0f);
                    }
                }
            }

            if (layer->getCompOp()) {
                if (!_overlayBuffer2D.depthStencilAttachments.empty()) {
                    glInvalidateFramebuffer(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer2D.depthStencilAttachments.size()), _overlayBuffer2D.depthStencilAttachments.data());
                }

                glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

                if (maskStencilBits > 0) {
                    glDisable(GL_STENCIL_TEST);
                }
                if (currentCompOp != layerCompOp) {
                    setCompOp(layerCompOp);
                    currentCompOp = layerCompOp;
                }
                blendScreenTexture(layerOpacity, _overlayBuffer2D.colorTexture);
                if (maskStencilBits > 0) {
                    glEnable(GL_STENCIL_TEST);
                }
            }
        }
        if (!activeDrawOnceGroup.empty() && maskStencilBits == 0) {
            glDisable(GL_STENCIL_TEST);
        }
        _drawOncePass = DrawOncePass::NONE;
    }
    
    void GLTileRenderer::renderGeometry3D(const std::vector<RenderTile>& renderTiles, bool allowInline, float layerOpacity) {
        std::map<int, std::vector<const RenderTileLayer*>> renderLayerMap;
        for (const RenderTile& renderTile : renderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const std::shared_ptr<const TileLayer>& layer = it->second.layer;

                bool contains3DGeometry = false;
                for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
                    contains3DGeometry = (geometry->getType() == TileGeometry::Type::POLYGON3D || geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) || contains3DGeometry;
                }
                if (contains3DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    if (!_rendererLayerIndexRange || (it->first >= _rendererLayerIndexRange->first && it->first < _rendererLayerIndexRange->second)) {
                        renderLayerMap[it->first].push_back(&it->second);
                    }
                }
            }
        }

        // Extrusions share the 2D content's style-layer numbering: at ordinal 0, roads and clipped
        // text out-pulled buildings shorter than their pull.
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            _terrainStyleLayerIndices.insert(it->first);
        }
        _terrainStyleLayersDrawn = static_cast<int>(_terrainStyleLayerIndices.size());

        // Tangram's proxy depth, as renderGeometry2D computes it.
        int maxVisibleZoom = 0;
        for (const RenderTile& renderTile : renderTiles) {
            if (renderTile.visible) {
                maxVisibleZoom = std::max(maxVisibleZoom, renderTile.targetTileId.zoom);
            }
        }
        auto proxyDepth = [maxVisibleZoom](const RenderTileLayer* renderLayer) -> float {
            if (renderLayer->active) {
                return 0.0f;
            }
            return std::max(static_cast<float>(maxVisibleZoom - renderLayer->targetTileId.zoom), TERRAIN_PROXY_DEPTH_UNITS);
        };

        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            const std::vector<const RenderTileLayer*>& renderLayers = it->second;
            if (renderLayers.empty()) {
                continue;
            }
            int layerOrdinal = _terrainLayerOrdinalBase + static_cast<int>(std::distance(_terrainStyleLayerIndices.begin(), _terrainStyleLayerIndices.find(it->first)));
            Pass3DState pass = begin3DPass(renderLayers, renderTiles, allowInline, layerOpacity);

            // Translucent extrusions draw twice - depth only, then colour with depth pulled one unit
            // forward - so one fragment per pixel blends (mapbox's depth prepass, minus the stencil).
            auto drawTileLayers = [&](bool depthOnly) {
            for (const RenderTileLayer* renderLayer : renderLayers) {
                // The ordinal model applies only under the shared ground; elsewhere the surface is pushed back.
                if (_terrainSharedGround) {
                    _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (depthOnly && geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        continue;
                    }
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        // Always drawn: an extrusion whose ground has not resolved yet keeps the
                        // sentinel and the shader falls back to the ground under each vertex.
                        bool baseResolved = resolveExtrusionBases(renderLayer->sourceTileId, renderLayer->targetTileId, geometry);
                        // ...safe only for a BUILDING: a deck's base is its chord, and sentinel vertices
                        // fan it across the valley. "Not yet" rather than somewhere plausible.
                        if (!geometry->getSpanRecords().empty() && (!_terrainMode || !baseResolved)) {
                            continue;
                        }
                        // NOTE: geometry comp op is not supported for 3D polygons. Blending is disabled, setGLBlendState not needed
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, pass.geometryOpacity, renderLayer->tileSize, geometry);
                    }
                }
                _terrainDrawLayerOffset = 0.0f;
            }
            };
            if (pass.translucentExtrusions) {
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                drawTileLayers(true);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(0.0f, -1.0f);
                drawTileLayers(false);
                glDisable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(0.0f, 0.0f);
            } else {
                drawTileLayers(false);
            }

            end3DPass(pass);
        }
    }

    GLTileRenderer::Pass3DState GLTileRenderer::begin3DPass(const std::vector<const RenderTileLayer*>& renderLayers, const std::vector<RenderTile>& renderTiles, bool allowInline, float layerOpacity) {
        Pass3DState state;
        const std::shared_ptr<const TileLayer>& layer = renderLayers.front()->layer;

        state.layerOpacity = (layer->getOpacityFunc())(_viewState) * layerOpacity;
        if (!layer->getCompOp()) { // use the hack to conform with normal '2D' layers
            std::swap(state.layerOpacity, state.geometryOpacity);
        }
        state.layerCompOp = (layer->getCompOp() ? *layer->getCompOp() : CompOp::SRC_OVER);

        // The overlay buys comp-op and flatten-then-blend for a fading layer, at a clear, pre-pass and
        // composite per frame. Inline only when extrusions are the last tile content (they write depth).
        state.useOverlay = !allowInline || static_cast<bool>(layer->getCompOp()) || state.geometryOpacity < 1.0f - 1.0f / 255.0f;
        // A translucent fill COLOUR is per feature, not layer opacity: blended into the scene it shows
        // every wall through every other, so mark it for a nearest-first resolve.
        if (!state.useOverlay) {
            for (const RenderTileLayer* renderLayer : renderLayers) {
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        continue;
                    }
                    const TileGeometry::StyleParameters& styleParams = geometry->getStyleParameters();
                    for (int i = 0; i < styleParams.parameterCount && !state.translucentExtrusions; i++) {
                        float alpha = evaluateColorFunc(styleParams.colorFuncs[i]).rgba()[3];
                        state.translucentExtrusions = alpha > 1.0f / 256.0f && alpha < 1.0f - 1.0f / 256.0f;
                    }
                }
                if (_buildingFadeOnAppear && renderLayer->blend < 1.0f) {
                    state.translucentExtrusions = true;
                }
            }
        }

        if (state.useOverlay) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.previousFBO);

            if (_overlayBuffer3D.fbo == 0) {
                // Packed depth-stencil, the only route to 24-bit depth here: 16 bits over a terrain
                // range eats the bottom of every extrusion. The stencil is unused.
                createFrameBuffer(_overlayBuffer3D, true, true, true);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer3D.fbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        // Seed the overlay's depth with the terrain: it composites back as a flat quad without depth
        // testing, so a building behind a ridge would paint over it. Skipped inline and without extrusions.
        state.terrainOccluders = _terrainMode && static_cast<bool>(_terrainTextureProvider) &&
            std::any_of(renderLayers.begin(), renderLayers.end(), [](const RenderTileLayer* renderLayer) {
                const std::vector<std::shared_ptr<TileGeometry>>& geometries = renderLayer->layer->getGeometries();
                return std::any_of(geometries.begin(), geometries.end(), [](const std::shared_ptr<TileGeometry>& geometry) {
                    return geometry->getType() == TileGeometry::Type::POLYGON3D;
                });
            });
        if (state.terrainOccluders) {
            if (state.useOverlay) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
                glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                _terrainDrawDepthBias = _terrainDepthBias;
                _terrainDrawDepthClipUnits = 0.0f; // TRUE depth: the occluder is never pushed back
                // Its own depth buffer needs seeding even under a shared ground, from the SAME cover.
                if (_terrainSharedGround) {
                    for (const TileId& tileId : _terrainGroundTiles) {
                        renderTileSurfaceFill(tileId, Color());
                    }
                } else {
                    for (const RenderTile& renderTile : renderTiles) {
                        if (renderTile.visible) {
                            renderTileSurfaceFill(renderTile.targetTileId, Color());
                        }
                    }
                }
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            // Extrusion clearance for two errors: the base ring samples elevation off the mesh's cell
            // interpolation, and depth resolution grows as distance^2/near. Uniform, so buildings agree.
            _terrainDrawDepthBias = _terrainDepthBias + TERRAIN_EXTRUSION_DEPTH_DELTAS * TERRAIN_LAYER_DEPTH_DELTA;
            _terrainDrawDepthClipUnits = (_terrainRegularGrid ? 2.0f : 12.0f);
        }

        if (!state.useOverlay) {
            // The depth state the overlay's pre-pass leaves, so extrusions resolve the same either way.
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            // A fading tile's extrusions are premultiplied by their blend, which the overlay composite
            // resolves; inline they must blend here or a half-faded building turns near-black.
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
        }
        return state;
    }

    void GLTileRenderer::end3DPass(const Pass3DState& state) {
        if (!state.useOverlay) {
            glDisable(GL_BLEND); // the overlay path below composites instead
        }

        if (state.terrainOccluders) {
            _terrainDrawDepthBias = _terrainDepthBias;
            _terrainDrawDepthClipUnits = 0.0f;
        }

        if (state.useOverlay) {
            if (!_overlayBuffer3D.depthStencilAttachments.empty()) {
                // TODO: glInvalidateFramebuffer on the 3D overlay crashes; find out why.
            }

            glBindFramebuffer(GL_FRAMEBUFFER, state.previousFBO);

            glEnable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            setCompOp(state.layerCompOp);
            blendScreenTexture(state.layerOpacity, _overlayBuffer3D.colorTexture);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
        }
    }
    
    void GLTileRenderer::renderLabels(const std::vector<std::shared_ptr<Label>>& labels) {
        // All leader lines before all text: a line crossing a neighbour's glyphs reads as a strike-through.
        bool anyCallout = std::any_of(labels.begin(), labels.end(), [](const std::shared_ptr<Label>& label) {
            return label->getStyle()->orientation == LabelOrientation::CALLOUT && label->getStyle()->calloutLineGlyph;
        });
        if (anyCallout) {
            renderLabelPass(labels, Label::DrawPass::CALLOUT_LINE);
        }
        renderLabelPass(labels, anyCallout ? Label::DrawPass::TEXT : Label::DrawPass::ALL);
    }

    void GLTileRenderer::renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, Label::DrawPass pass) {
        // Group only labels needing a GPU height by tile (a batch binds one tile's elevation); anchored
        // ones share a batch to keep the draw count down. Stable, so the culler's order survives.
        VT_STAT_CLOCK(sortClock);
        std::vector<std::shared_ptr<Label>> grouped;
        if (_terrainMode) {
            grouped = labels;
            std::stable_sort(grouped.begin(), grouped.end(), [this](const std::shared_ptr<Label>& a, const std::shared_ptr<Label>& b) {
                return labelBatchTileId(a) < labelBatchTileId(b);
            });
        }
        VT_STAT_SPLIT(labelPassSortNs, sortClock);
        const std::vector<std::shared_ptr<Label>>& drawOrder = _terrainMode ? grouped : labels;

        LabelBatchParameters labelBatchParams;
        // The batch's atlas: a batch samples ONE texture, so another atlas ends it and draw order holds.
        std::shared_ptr<const Bitmap> bitmap;
        std::shared_ptr<const TileLabel::Style> lastLabelStyle;
        int styleIndex = -1;
        int haloStyleIndex = -1;
        LabelPlateIndices plateIndices;
        int secondaryStyleIndex = -1;
        int iconStyleIndex = -1;
        int iconHaloStyleIndex = -1;
        for (const std::shared_ptr<Label>& label : drawOrder) {
            if (!label->isValid()) {
                continue;
            }
            if (label->getOpacity() <= 0.0f) {
                continue;
            }
            const std::shared_ptr<const TileLabel::Style>& labelStyle = label->getStyle();
            if (pass == Label::DrawPass::CALLOUT_LINE && !(labelStyle->orientation == LabelOrientation::CALLOUT && labelStyle->calloutLineGlyph)) {
                continue;
            }

            // Held by value for the whole iteration: getBitmapPattern returns a temporary, a tile
            // thread can reset the map's pattern, and a reference through its -> is not extended.
            VT_STAT_CLOCK(patternClock);
            std::shared_ptr<const BitmapPattern> labelPattern = labelStyle->glyphMap->getBitmapPattern();
            VT_STAT_SPLIT(labelPassPatternNs, patternClock);
            const std::shared_ptr<const Bitmap>& labelBitmap = labelPattern->bitmap;
            VT_STAT_CLOCK(styleClock);
            if (lastLabelStyle != labelStyle) {
                // Scene light, as far as the emissive lets it through. mapbox defaults label emissive
                // to 1, a no-op here, which keeps names legible over a night map.
                float labelEmissive = evaluateFloatFunc(labelStyle->emissiveFunc);
                auto lit = [this, labelEmissive](const cglib::vec4<float>& rgba) {
                    if (labelEmissive >= 1.0f) {
                        return rgba;
                    }
                    cglib::vec4<float> out = rgba;
                    for (int c = 0; c < 3; c++) {
                        out(c) *= labelEmissive + (1.0f - labelEmissive) * _radiance(c);
                    }
                    return out;
                };
                // The halo may be lit harder than its ink: over a darkening map the outline must darken
                // too, or both meet at one grey.
                float haloEmissive = labelStyle->haloEmissiveFunc ? evaluateFloatFunc(*labelStyle->haloEmissiveFunc) : labelEmissive;
                auto litHalo = [this, haloEmissive](const cglib::vec4<float>& rgba) {
                    if (haloEmissive >= 1.0f) {
                        return rgba;
                    }
                    cglib::vec4<float> out = rgba;
                    for (int c = 0; c < 3; c++) {
                        out(c) *= haloEmissive + (1.0f - haloEmissive) * _radiance(c);
                    }
                    return out;
                };
                cglib::vec4<float> color = lit(cglib::vec4<float>(evaluateColorFunc(labelStyle->colorFunc).rgba()));
                float size = evaluateFloatFunc(labelStyle->sizeFunc);
                cglib::vec4<float> haloColor = litHalo(cglib::vec4<float>(evaluateColorFunc(labelStyle->haloColorFunc).rgba()));
                // Already SCREEN PIXELS (the symbolizer applied the pixel scale), as labelFsh's one-pixel
                // antialias ramp expects. The cap is where the encoded field runs out.
                float haloRadius = std::min(evaluateFloatFunc(labelStyle->haloRadiusFunc), MAX_HALO_PIXELS);

                // Each plate colour is one more batch slot, like the halo; a bordered plate needs two
                // CONSECUTIVE slots, as one quad draws both (LabelPlateIndices).
                const TileLabel::Style::Plate* plateList[2] = { &labelStyle->textPlate, &labelStyle->iconPlate };
                int plateCount = 0;
                for (int i = 0; i < 2; i++) {
                    plateCount += (plateList[i]->draws() ? (plateList[i]->drawsBorder() ? 2 : 1) : 0);
                }
                // The second text run may have its own colour: one more slot.
                bool hasSecondaryColor = static_cast<bool>(labelStyle->secondaryColorFunc);
                cglib::vec4<float> secondaryColor = hasSecondaryColor ? lit(cglib::vec4<float>(evaluateColorFunc(*labelStyle->secondaryColorFunc).rgba())) : color;
                // And so may the icon run - a font icon in its own colour next to the name.
                bool hasIconColor = static_cast<bool>(labelStyle->iconColorFunc);
                cglib::vec4<float> iconColor = hasIconColor ? lit(cglib::vec4<float>(evaluateColorFunc(*labelStyle->iconColorFunc).rgba())) : color;
                // And its own halo - an icon never takes the text's (see Label::appendLabelPlates).
                float iconHaloRadius = labelStyle->iconHaloRadiusFunc ? std::min(evaluateFloatFunc(*labelStyle->iconHaloRadiusFunc), MAX_ICON_HALO_PIXELS) : 0.0f;
                bool hasIconHalo = iconHaloRadius > 0.0f && labelStyle->iconHaloColorFunc;
                cglib::vec4<float> iconHaloColor = hasIconHalo ? litHalo(cglib::vec4<float>(evaluateColorFunc(*labelStyle->iconHaloColorFunc).rgba())) : color;
                // The icon run's own em size: labelVsh divides uSDFRamp by it, and the text's ramp
                // closed every thin icon stroke.
                float iconSize = labelStyle->iconRefSize > 0.0f ? labelStyle->iconRefSize : size;
                if (labelStyle->iconScaleFunc && labelStyle->iconRefScale > 0.0f) {
                    iconSize *= evaluateFloatFunc(*labelStyle->iconScaleFunc) / labelStyle->iconRefScale;
                }
                // A slot of its own whenever that ramp would differ, not only for a coloured icon.
                bool hasIconRun = hasIconColor || iconSize != size;
                // The anchor tile ends a batch only for a label that needs the GPU's height.
                TileId labelTileId = labelBatchTileId(label);
                if (bitmap != labelBitmap || labelBatchParams.tileId != labelTileId || labelBatchParams.scale != labelStyle->scale || labelBatchParams.glyphRenderSize != labelStyle->glyphRenderSize || labelBatchParams.parameterCount + 2 + plateCount + (hasSecondaryColor ? 1 : 0) + (hasIconRun ? 1 : 0) + (hasIconHalo ? 1 : 0) > LabelBatchParameters::MAX_PARAMETERS) {
                    // The flush is an upload and a draw: bank the style time so far, not count the flush as style.
                    VT_STAT_SPLIT(labelPassStyleNs, styleClock);
                    renderLabelBatch(labelBatchParams, bitmap);
                    VT_STAT_SPLIT(labelBatchNs, styleClock);
                    bitmap = labelBitmap;
                    labelBatchParams.labelCount = 0;
                    labelBatchParams.parameterCount = 0;
                    labelBatchParams.scale = labelStyle->scale;
                    labelBatchParams.glyphRenderSize = labelStyle->glyphRenderSize;
                    labelBatchParams.tileId = labelTileId;
                    labelBatchParams.labelMatrix = _viewState.cameraMatrix * cglib::translate4_matrix(_viewState.origin);

                    styleIndex = -1;
                    haloStyleIndex = -1;
                    plateIndices = LabelPlateIndices();
                    secondaryStyleIndex = -1;
                    iconStyleIndex = -1;
                    iconHaloStyleIndex = -1;
                } else {
                    for (styleIndex = labelBatchParams.parameterCount; --styleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[styleIndex] == color && labelBatchParams.widthTable[styleIndex] == size && labelBatchParams.strokeWidthTable[styleIndex] == 0) {
                            break;
                        }
                    }
                    for (haloStyleIndex = haloRadius > 0.0f ? labelBatchParams.parameterCount : 0; --haloStyleIndex >= 0; ) {
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
                plateIndices = LabelPlateIndices();
                int* plateTargets[2] = { &plateIndices.text, &plateIndices.icon };
                for (int i = 0; i < 2; i++) {
                    const TileLabel::Style::Plate& plate = *plateList[i];
                    if (!plate.draws()) {
                        continue;
                    }
                    // The fill's slot, and the border's right after it - the quad reads that one
                    // as styleIndex + 1, so a REUSED pair has to be consecutive as well.
                    bool border = plate.drawsBorder();
                    int slots = (border ? 2 : 1);
                    // The icon's plate is its background, so icon-opacity fades it LIVE with the glyph:
                    // that opacity is a zoom ramp, and baked at decode a hidden icon kept its disc.
                    float plateOpacity = (i == 1 && labelStyle->iconOpacityFunc ? evaluateFloatFunc(*labelStyle->iconOpacityFunc) : 1.0f);
                    Color plateFill = plate.style.colorFunc ? evaluateColorFunc(*plate.style.colorFunc) : plate.style.color;
                    Color plateBorder = plate.style.borderColorFunc ? evaluateColorFunc(*plate.style.borderColorFunc) : plate.style.borderColor;
                    cglib::vec4<float> fillColor = lit(cglib::vec4<float>(plateFill.rgba())) * plateOpacity;
                    cglib::vec4<float> borderColor = lit(cglib::vec4<float>(plateBorder.rgba())) * plateOpacity;
                    int index = labelBatchParams.parameterCount - slots;
                    for (; index >= 0; index--) {
                        if (labelBatchParams.colorTable[index] == fillColor && labelBatchParams.widthTable[index] == size && labelBatchParams.strokeWidthTable[index] == 0
                            && (!border || labelBatchParams.colorTable[index + 1] == borderColor)) {
                            break;
                        }
                    }
                    if (index < 0 && labelBatchParams.parameterCount + slots <= LabelBatchParameters::MAX_PARAMETERS) {
                        index = labelBatchParams.parameterCount;
                        for (int j = 0; j < slots; j++) {
                            int slot = labelBatchParams.parameterCount++;
                            labelBatchParams.colorTable[slot] = (j == 0 ? fillColor : borderColor);
                            labelBatchParams.widthTable[slot] = size;
                            labelBatchParams.strokeWidthTable[slot] = 0;
                        }
                    }
                    *plateTargets[i] = index;
                }

                secondaryStyleIndex = -1;
                if (hasSecondaryColor) {
                    for (secondaryStyleIndex = labelBatchParams.parameterCount; --secondaryStyleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[secondaryStyleIndex] == secondaryColor && labelBatchParams.widthTable[secondaryStyleIndex] == size && labelBatchParams.strokeWidthTable[secondaryStyleIndex] == 0) {
                            break;
                        }
                    }
                    if (secondaryStyleIndex < 0) {
                        secondaryStyleIndex = labelBatchParams.parameterCount++;
                        labelBatchParams.colorTable[secondaryStyleIndex] = secondaryColor;
                        labelBatchParams.widthTable[secondaryStyleIndex] = size;
                        labelBatchParams.strokeWidthTable[secondaryStyleIndex] = 0;
                    }
                }

                iconHaloStyleIndex = -1;
                if (hasIconHalo) {
                    for (iconHaloStyleIndex = labelBatchParams.parameterCount; --iconHaloStyleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[iconHaloStyleIndex] == iconHaloColor && labelBatchParams.widthTable[iconHaloStyleIndex] == iconSize && labelBatchParams.strokeWidthTable[iconHaloStyleIndex] == iconHaloRadius) {
                            break;
                        }
                    }
                    if (iconHaloStyleIndex < 0) {
                        iconHaloStyleIndex = labelBatchParams.parameterCount++;
                        labelBatchParams.colorTable[iconHaloStyleIndex] = iconHaloColor;
                        labelBatchParams.widthTable[iconHaloStyleIndex] = iconSize;
                        labelBatchParams.strokeWidthTable[iconHaloStyleIndex] = iconHaloRadius;
                    }
                }

                iconStyleIndex = -1;
                if (hasIconRun) {
                    for (iconStyleIndex = labelBatchParams.parameterCount; --iconStyleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[iconStyleIndex] == iconColor && labelBatchParams.widthTable[iconStyleIndex] == iconSize && labelBatchParams.strokeWidthTable[iconStyleIndex] == 0) {
                            break;
                        }
                    }
                    if (iconStyleIndex < 0) {
                        iconStyleIndex = labelBatchParams.parameterCount++;
                        labelBatchParams.colorTable[iconStyleIndex] = iconColor;
                        labelBatchParams.widthTable[iconStyleIndex] = iconSize;
                        labelBatchParams.strokeWidthTable[iconStyleIndex] = 0;
                    }
                }

                lastLabelStyle = labelStyle;
            }
            VT_STAT_SPLIT(labelPassStyleNs, styleClock);

            // The style layer's occluded opacity, else this layer's default.
            float occludedOpacity = labelStyle->occlusionOpacity.value_or(_labelOcclusionOpacity);
            label->setOcclusion(occludedOpacity < 1.0f ? occludedOpacity + (1.0f - occludedOpacity) * calculateLabelVisibility(*label) : 1.0f);

            VT_STAT_CLOCK(statClock);
            std::size_t labelVertexOffset = _labelVertices.size();
            label->calculateVertexData(labelBatchParams.widthTable[styleIndex], _viewState, styleIndex, haloStyleIndex, _labelVertices, _labelOffsets, _labelNormals, _labelTexCoords, _labelAttribs, _labelIndices, pass, pass == Label::DrawPass::CALLOUT_LINE ? LabelPlateIndices() : plateIndices, pass == Label::DrawPass::CALLOUT_LINE ? -1 : secondaryStyleIndex, pass == Label::DrawPass::CALLOUT_LINE ? -1 : iconStyleIndex, pass == Label::DrawPass::CALLOUT_LINE ? -1 : iconHaloStyleIndex, static_cast<bool>(_lightingShader2D));
            if (labelStyle->transform) {
                // Conjugated by the tile matrix the translate is a pure world shift, so it rides on the
                // vertices; as a batch matrix it made every such label its own draw.
                float zoomScale = std::pow(2.0f, label->getTileId().zoom - _viewState.zoom);
                cglib::vec2<float> translate = labelStyle->transform->translate() * zoomScale;
                cglib::mat4x4<double> translateMatrix = cglib::mat4x4<double>::convert(_transformer->calculateTileTransform(label->getTileId(), translate, 1.0f));
                cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(label->getTileId(), 1);
                cglib::mat4x4<double> worldTransform = tileMatrix * translateMatrix * cglib::inverse(tileMatrix);
                cglib::vec3<float> delta = cglib::vec3<float>::convert(cglib::transform_point(cglib::vec3<double>(0, 0, 0), worldTransform));
                for (std::size_t i = labelVertexOffset; i < _labelVertices.size(); i++) {
                    _labelVertices[i] += delta;
                }
            }
            VT_STAT_SPLIT(labelVertexBuildNs, statClock);
            VT_STAT_INC(labelsDrawnVertices);

            labelBatchParams.labelCount++;

            if (_labelVertices.size() >= 32768) { // flush the batch if largest vertex index is getting 'close' to 64k limit
                renderLabelBatch(labelBatchParams, bitmap);
                VT_STAT_SPLIT(labelBatchNs, statClock);
            }
        }

        VT_STAT_CLOCK(batchClock);
        renderLabelBatch(labelBatchParams, bitmap);
        VT_STAT_SPLIT(labelBatchNs, batchClock);
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

        // Never depth-tested: in terrain mode the depth buffer holds the pre-pass, which would clip it.
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        if (depthTestEnabled) {
            glDisable(GL_DEPTH_TEST);
        }

        const ShaderProgram& shaderProgram = buildShaderProgram("blendscreen", blendVsh, blendFsh, LightingMode::NONE, RasterFilterMode::NONE, 0);
        useProgram(shaderProgram);
        
        if (_screenQuad.vbo == 0) {
            createCompiledQuad(_screenQuad);
        }
        glBindBuffer(GL_ARRAY_BUFFER, _screenQuad.vbo);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);
        
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

        disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (depthTestEnabled) {
            glEnable(GL_DEPTH_TEST);
        }

        checkGLError();
    }

    const std::pair<bool, GLTileRenderer::TerrainTexture>& GLTileRenderer::resolveTerrainTexture(const TileId& tileId) const {
        // Memoised per frame: called per draw, constant within a frame. The provider call is also the
        // elevation cache's LRU touch, which needs only one a frame.
        auto it = _terrainTextureCache.find(tileId);
        if (it != _terrainTextureCache.end()) {
            return it->second;
        }
        std::pair<bool, TerrainTexture> resolved(false, TerrainTexture());
        resolved.first = _terrainTextureProvider && _terrainTextureProvider(tileId, resolved.second);
        return _terrainTextureCache.emplace(tileId, resolved).first->second;
    }

    // A longitude difference into (-pi, pi]: the frame origin and the tile may sit either side of
    // the antimeridian, and every spherical uv uniform is now a difference.
    static double wrapRadians(double x) {
        return x - 6.283185307179586 * std::floor(x * 0.15915494309189535 + 0.5);
    }

    double GLTileRenderer::sphereWorldRadius() const {
        // The zoom-0 tile matrix diagonal is the transformer's scale: a sphere's radius in world units.
        double radius = _transformer->calculateTileMatrix(TileId(0, 0, 0), 1.0f)(0, 0);
        return radius > 0 ? radius : 1.0;
    }

    cglib::vec2<double> GLTileRenderer::sphereFrameMercator(const cglib::mat4x4<double>& vertexFrameMatrix) const {
        double sphereRadius = sphereWorldRadius();
        cglib::vec3<double> o(vertexFrameMatrix(0, 3) / sphereRadius, vertexFrameMatrix(1, 3) / sphereRadius, vertexFrameMatrix(2, 3) / sphereRadius);
        double len = cglib::length(o);
        double rz = std::min(0.999999, std::max(-0.999999, o(2) / (len > 0 ? len : 1.0)));
        return cglib::vec2<double>(std::atan2(o(1), o(0)), 0.5 * std::log((1.0 + rz) / (1.0 - rz)));
    }

    void GLTileRenderer::setupSphericalUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix) {
        if (!_transformer->isSpherical()) {
            return;
        }
        // The unit-sphere point under a vertex, from the frame matrix, so the shader needs no tile.
        double sphereRadius = sphereWorldRadius();
        glUniform3f(shaderProgram.uniforms[U_TERRAINSPHEREORIGIN],
            static_cast<float>(vertexFrameMatrix(0, 3) / sphereRadius),
            static_cast<float>(vertexFrameMatrix(1, 3) / sphereRadius),
            static_cast<float>(vertexFrameMatrix(2, 3) / sphereRadius));
        glUniform3f(shaderProgram.uniforms[U_TERRAINSPHERESCALE],
            static_cast<float>(vertexFrameMatrix(0, 0) / sphereRadius),
            static_cast<float>(vertexFrameMatrix(1, 1) / sphereRadius),
            static_cast<float>(vertexFrameMatrix(2, 2) / sphereRadius));
        // The TARGET tile the clip tests against: a zoom level spans 2pi Mercator radians, y from the
        // south. Relative to the frame origin: the absolute value does not survive fp32.
        cglib::vec2<double> frameMercator = sphereFrameMercator(vertexFrameMatrix);
        double tileCount = static_cast<double>(1 << tileId.zoom);
        double tileSizeRadians = 6.283185307179586 / tileCount;
        glUniform1f(shaderProgram.uniforms[U_DRAPEBAKE], _drapeMVPOverride ? 1.0f : 0.0f);
        glUniform4f(shaderProgram.uniforms[U_TERRAINSPHERETILEUV],
            static_cast<float>(wrapRadians((tileId.x / tileCount - 0.5) * 6.283185307179586 - frameMercator(0))),
            static_cast<float>(((tileCount - 1 - tileId.y) / tileCount - 0.5) * 6.283185307179586 - frameMercator(1)),
            static_cast<float>(1.0 / tileSizeRadians),
            static_cast<float>(1.0 / tileSizeRadians));
    }

    bool GLTileRenderer::setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix, bool gridSurface) {
        // Clip slack grows linearly with eye distance, as the surface-vs-geometry error does;
        // NEGATIVE units push the draw AWAY.
        float clipUnits = _terrainDrawDepthClipUnits;
        double tileSize = std::abs(_transformer->calculateTileMatrix(tileId, 1.0f)(0, 0));
        double projScaleZ = std::abs(_viewState.projectionMatrix(2, 2));
        // The interpolation error is quadratic in the cell size, so a linear slack calibrated at low
        // zoom overshoots at high zoom, and the excess ignores occlusion. Anchored at zoom 11 tiles.
        double slackScale = tileSize * std::min(4.0, tileSize / TERRAIN_DEPTH_CLIP_REF_TILE_SIZE) * _terrainSlackScale;
        // Under a shared ground tangram's depth_shift rides in the per-layer term below; not twice.
        double contentShift = (gridSurface || _terrainSharedGround ? 0.0 : _terrainContentDepthShift * projScaleZ);
        glUniform1f(shaderProgram.uniforms[U_DEPTHBIASCLIP], static_cast<float>(clipUnits * TERRAIN_DEPTH_CLIP_SLACK * slackScale * projScaleZ + contentShift));
        // Tangram's per-layer term, (proxy - layer) * (2^-19 * w + depth_shift): content writes depth
        // without layers z-fighting, and a live tile beats its proxy. Surfaces (a paint) take it too.
        glUniform1f(shaderProgram.uniforms[U_LAYERDEPTHOFFSET], _terrainDrawLayerOffset);
        // depth_shift, tangram's terrain-3d.yaml verbatim: a flat 0.02 in clip - large near the camera,
        // where fill chord error is, dying as 1/w at range, where a pull would leak over a ridge.
        double depthShift = (_terrainSharedGround ? _terrainContentDepthShift : 0.0);
        glUniform1f(shaderProgram.uniforms[U_DEPTHSHIFT], static_cast<float>(depthShift));

        // Metre-constant clearance (see applyDepthBias): proj[2][3] turns it into the clip offset the
        // shader divides by w. Zero for what does not chord over the ground.
        glUniform1f(shaderProgram.uniforms[U_DEPTHCLEARANCE], static_cast<float>(_terrainDrawClearance * _viewState.projectionMatrix(2, 3)));

        // Stitching bends the edge shared with a coarser neighbour onto its chords. Draped content
        // takes it too, or a road's halves meet at different heights across the seam.
        cglib::vec4<float> edgeCoarsening(1, 1, 1, 1);
        if (!_terrainEdgeCoarseningMap.empty()) {
            auto edgeIt = _terrainEdgeCoarseningMap.find(tileId);
            if (edgeIt != _terrainEdgeCoarseningMap.end()) {
                edgeCoarsening = edgeIt->second;
            }
        }
        glUniform4f(shaderProgram.uniforms[U_TERRAINEDGECOARSENING], edgeCoarsening(0), edgeCoarsening(1), edgeCoarsening(2), edgeCoarsening(3));
        auto skirtIt = _terrainSkirtDropMap.find(tileId);
        cglib::vec4<float> skirtDrop = (skirtIt != _terrainSkirtDropMap.end() ? skirtIt->second : cglib::vec4<float>(0, 0, 0, 0));
        glUniform4f(shaderProgram.uniforms[U_TERRAINSKIRTDROP], skirtDrop(0), skirtDrop(1), skirtDrop(2), skirtDrop(3));
        // Vertex frame -> TARGET tile units, for the edge test and the fragment clip. The offset makes
        // it hold for a stand-in, whose unit square would otherwise land in [0, 2^dz].
        const cglib::mat4x4<double> targetTileMatrix = _transformer->calculateTileMatrix(tileId, 1.0f);
        double unitScaleX = 1.0, unitScaleY = 1.0, unitOffsetX = 0.0, unitOffsetY = 0.0;
        if (targetTileMatrix(0, 0) != 0 && targetTileMatrix(1, 1) != 0) {
            unitScaleX = vertexFrameMatrix(0, 0) / targetTileMatrix(0, 0);
            unitScaleY = vertexFrameMatrix(1, 1) / targetTileMatrix(1, 1);
            unitOffsetX = (vertexFrameMatrix(0, 3) - targetTileMatrix(0, 3)) / targetTileMatrix(0, 0);
            unitOffsetY = (vertexFrameMatrix(1, 3) - targetTileMatrix(1, 3)) / targetTileMatrix(1, 1);
        }
        glUniform2f(shaderProgram.uniforms[U_TILEUNITSCALE], static_cast<float>(unitScaleX), static_cast<float>(unitScaleY));
        glUniform2f(shaderProgram.uniforms[U_TILEUNITOFFSET], static_cast<float>(unitOffsetX), static_cast<float>(unitOffsetY));

        // Before the no-elevation bail-out below: a tile drawn flat still curves and is still lit.
        setupSphericalUniforms(shaderProgram, tileId, vertexFrameMatrix);

        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
        bool valid = resolved.first;
        const TerrainTexture& terrainTexture = resolved.second;
        if (!valid || terrainTexture.textureId == 0 || terrainTexture.internalSize(0) <= 0 || terrainTexture.internalSize(1) <= 0) {
            // No elevation data (yet): render the tile flat, consistently across all layers
            glUniform1i(shaderProgram.uniforms[U_ELEVATIONTEXTURE], 1);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONUV], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONDECODE], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_ELEVATIONOFFSET], 0.0f);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONSCALE], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_BASESCALE], 0.0f);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONTEXELSIZE], 1.0f, 1.0f, 1.0f, 1.0f);
            glUniform2f(shaderProgram.uniforms[U_ELEVATIONLATTICECELL], 0.0f, 0.0f);
            glUniform1i(shaderProgram.uniforms[U_ELEVATIONNODETEXTURE], 1);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONNODEUV], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONNODETEXELSIZE], 1.0f, 1.0f, 1.0f, 1.0f);
            if (_transformer->isSpherical()) {
                // Left unset they keep another tile's coverage; below terrain's minZoom every tile comes here.
                glUniform4f(shaderProgram.uniforms[U_TERRAINSPHERENODEUV], 0.0f, 0.0f, 0.0f, 0.0f);
                glUniform4f(shaderProgram.uniforms[U_TERRAINSPHEREELEVUV], 0.0f, 0.0f, 0.0f, 0.0f);
            }
            glUniform2f(shaderProgram.uniforms[U_TILEUNITSCALE], 0.0f, 0.0f); // no tile clipping without elevation
            glUniform2f(shaderProgram.uniforms[U_TILEUNITOFFSET], 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_LAYERDEPTHOFFSET], 0.0f);
            glUniform1f(shaderProgram.uniforms[U_DEPTHSHIFT], 0.0f);
            return false;
        }

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, terrainTexture.textureId);
        glUniform1i(shaderProgram.uniforms[U_ELEVATIONTEXTURE], 1);
        // The node texture the VERTEX stage displaces from (commonVsh); without one, the full DEM.
        bool nodes = terrainTexture.nodeTextureId != 0 && terrainTexture.nodeTextureSize(0) > 0 && terrainTexture.nodeTextureSize(1) > 0
                  && terrainTexture.nodeSize(0) > 0 && terrainTexture.nodeSize(1) > 0;
                glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, nodes ? terrainTexture.nodeTextureId : terrainTexture.textureId);
        glUniform1i(shaderProgram.uniforms[U_ELEVATIONNODETEXTURE], 5);
        glActiveTexture(GL_TEXTURE0);

        cglib::vec2<double> frameOrigin(vertexFrameMatrix(0, 3), vertexFrameMatrix(1, 3));
        cglib::vec2<double> frameScale(vertexFrameMatrix(0, 0), vertexFrameMatrix(1, 1));
        double invSizeX = 1.0 / terrainTexture.internalSize(0);
        double invSizeY = 1.0 / terrainTexture.internalSize(1);
        const cglib::vec2<double>& nodeOrigin = nodes ? terrainTexture.nodeOrigin : terrainTexture.internalOrigin;
        double invNodeSizeX = 1.0 / (nodes ? terrainTexture.nodeSize(0) : terrainTexture.internalSize(0));
        double invNodeSizeY = 1.0 / (nodes ? terrainTexture.nodeSize(1) : terrainTexture.internalSize(1));
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONNODEUV],
            static_cast<float>((frameOrigin(0) - nodeOrigin(0)) * invNodeSizeX),
            static_cast<float>((frameOrigin(1) - nodeOrigin(1)) * invNodeSizeY),
            static_cast<float>(frameScale(0) * invNodeSizeX),
            static_cast<float>(frameScale(1) * invNodeSizeY));
        float nodeTexelSizeX = static_cast<float>(std::max(1, nodes ? terrainTexture.nodeTextureSize(0) : terrainTexture.textureSize(0)));
        float nodeTexelSizeY = static_cast<float>(std::max(1, nodes ? terrainTexture.nodeTextureSize(1) : terrainTexture.textureSize(1)));
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONNODETEXELSIZE], nodeTexelSizeX, nodeTexelSizeY, 1.0f / nodeTexelSizeX, 1.0f / nodeTexelSizeY);
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONUV],
            static_cast<float>((frameOrigin(0) - terrainTexture.internalOrigin(0)) * invSizeX),
            static_cast<float>((frameOrigin(1) - terrainTexture.internalOrigin(1)) * invSizeY),
            static_cast<float>(frameScale(0) * invSizeX),
            static_cast<float>(frameScale(1) * invSizeY));
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONDECODE], terrainTexture.decode(0), terrainTexture.decode(1), terrainTexture.decode(2), terrainTexture.decode(3));
        glUniform1f(shaderProgram.uniforms[U_ELEVATIONOFFSET], terrainTexture.decodeOffset);
        float texelSizeX = static_cast<float>(std::max(1, terrainTexture.textureSize(0)));
        float texelSizeY = static_cast<float>(std::max(1, terrainTexture.textureSize(1)));
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONTEXELSIZE], texelSizeX, texelSizeY, 1.0f / texelSizeX, 1.0f / texelSizeY);
        // Lattice clamp: draped geometry snaps its height to the surface's grid, in node-uv units (0 =
        // off). The surface itself needs it only on a stitched edge. Never on a sphere: tile-local xy
        // is curved there (docs/internals/rendering/18-globe.md).
        bool latticeNodes = (gridSurface && edgeCoarsening == cglib::vec4<float>(1, 1, 1, 1)) || _transformer->isSpherical();
        if (terrainGridSurfaces() && _terrainRegularGridResolution > 0 && _terrainDemTaps >= 16 && !latticeNodes) {
            double worldTileSize = std::abs(_transformer->calculateTileMatrix(tileId, 1.0f)(0, 0));
            float latticeCellX = static_cast<float>(worldTileSize * invNodeSizeX / _terrainRegularGridResolution);
            float latticeCellY = static_cast<float>(worldTileSize * invNodeSizeY / _terrainRegularGridResolution);
            glUniform2f(shaderProgram.uniforms[U_ELEVATIONLATTICECELL], latticeCellX, latticeCellY);
        } else {
            glUniform2f(shaderProgram.uniforms[U_ELEVATIONLATTICECELL], 0.0f, 0.0f);
        }
        if (_transformer->isSpherical()) {
            // Node uv from Mercator RADIANS (the shader's inverse): internal = radians * WORLD_SIZE / 2pi.
            double internalPerRadian = sphereWorldRadius() * 0.5;
            cglib::vec2<double> frameMercator = sphereFrameMercator(vertexFrameMatrix);
            glUniform4f(shaderProgram.uniforms[U_TERRAINSPHERENODEUV],
                static_cast<float>(wrapRadians(nodeOrigin(0) / internalPerRadian - frameMercator(0))),
                static_cast<float>(nodeOrigin(1) / internalPerRadian - frameMercator(1)),
                static_cast<float>(internalPerRadian * invNodeSizeX),
                static_cast<float>(internalPerRadian * invNodeSizeY));
            // The same for the FULL texture, which the fragment stage shades and shadows from.
            glUniform4f(shaderProgram.uniforms[U_TERRAINSPHEREELEVUV],
                static_cast<float>(wrapRadians(terrainTexture.internalOrigin(0) / internalPerRadian - frameMercator(0))),
                static_cast<float>(terrainTexture.internalOrigin(1) / internalPerRadian - frameMercator(1)),
                static_cast<float>(internalPerRadian * invSizeX),
                static_cast<float>(internalPerRadian * invSizeY));
        }

        double frameScaleZ = (vertexFrameMatrix(2, 2) != 0 ? vertexFrameMatrix(2, 2) : 1.0);
        // An extrusion's CPU base is already internal z (exaggeration and Mercator stretch applied),
        // so it owes only 1/frameScaleZ, as uElevationScale.x does.
        if (_transformer->isSpherical() && terrainTexture.metersToInternal > 0) {
            // ...on a sphere the radial scale, through metres, so a base rides its ground's
            // displacement; the cosh is the stretch metersToInternal leaves out (18-globe.md).
            double mercY = 6.283185307179586 * ((tileId.y + 0.5) / (1 << tileId.zoom) - 0.5);
            glUniform1f(shaderProgram.uniforms[U_BASESCALE],
                static_cast<float>(sphericalMetersToFrame(*_transformer, tileId, vertexFrameMatrix) / (terrainTexture.metersToInternal * std::cosh(mercY))));
        } else {
            glUniform1f(shaderProgram.uniforms[U_BASESCALE], static_cast<float>(1.0 / frameScaleZ));
        }
        if (_transformer->isSpherical()) {
            // A sphere height is RADIAL: no Mercator stretch (y, z zero, cosh 1) and no frame z offset.
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONSCALE],
                static_cast<float>(sphericalMetersToFrame(*_transformer, tileId, vertexFrameMatrix)), 0.0f, 0.0f, 0.0f);
        } else {
            glUniform4f(shaderProgram.uniforms[U_ELEVATIONSCALE],
                static_cast<float>(terrainTexture.metersToInternal / frameScaleZ),
                static_cast<float>(frameOrigin(1) * terrainTexture.mercatorYScale),
                static_cast<float>(frameScale(1) * terrainTexture.mercatorYScale),
                static_cast<float>(-vertexFrameMatrix(2, 3) / frameScaleZ)); // tile surface frames are origin-relative, with a non-zero origin z in terrain mode
        }
        return true;
    }

    void GLTileRenderer::setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix) {
        // Metres -> world units per elevation-uv, so the fragment's central difference reproduces the
        // displaced slope; the Mercator stretch comes per fragment via vElevCosh.
        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
        bool valid = resolved.first;
        const TerrainTexture& terrainTexture = resolved.second;
        float slopeX = 0.0f, slopeY = 0.0f;
        if (valid && terrainTexture.internalSize(0) > 0 && terrainTexture.internalSize(1) > 0) {
            slopeX = static_cast<float>(terrainTexture.metersToInternal / terrainTexture.internalSize(0));
            slopeY = static_cast<float>(terrainTexture.metersToInternal / terrainTexture.internalSize(1));
        }
        glUniform2f(shaderProgram.uniforms[U_TERRAINSLOPESCALE], slopeX, slopeY);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, valid ? terrainTexture.gradientTextureId : 0);
        glUniform1i(shaderProgram.uniforms[U_ELEVATIONGRADIENT], 7);
        glActiveTexture(GL_TEXTURE0);
        glUniform3f(shaderProgram.uniforms[U_SUNDIR], _terrainLighting.sunDir(0), _terrainLighting.sunDir(1), _terrainLighting.sunDir(2));
        glUniform4f(shaderProgram.uniforms[U_SUNCOLOR], _terrainLighting.sunColor(0), _terrainLighting.sunColor(1), _terrainLighting.sunColor(2), 1.0f);
        glUniform4f(shaderProgram.uniforms[U_AMBIENTCOLOR], _terrainLighting.ambientColor(0), _terrainLighting.ambientColor(1), _terrainLighting.ambientColor(2), 1.0f);
        glUniform2f(shaderProgram.uniforms[U_LIGHTPARAMS], _terrainLighting.sunIntensity, _terrainLighting.ambientIntensity);
    }

    bool GLTileRenderer::resolveExtrusionBases(const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry) const {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry->getVertexGeometryLayoutParameters();
        // A DECK stands on its own chord: its base comes per vertex from the span union, and its
        // min-height and height are a thickness from the deck.
        if (!geometry->getSpanRecords().empty()) {
            return _spanResolver.resolve(sourceTileId, geometry, _extrusionBaseVersion.load(std::memory_order_relaxed));
        }
        if (params.baseOffset < 0 || params.texCoordOffset < 0 || !_extrusionElevationProvider) {
            return true; // not an extrusion, or no elevation at all - the ground is the base
        }
        unsigned int version = _extrusionBaseVersion.load(std::memory_order_relaxed);
        VT_STAT_INC(extrusionResolveCalls);
        if (geometry->isBaseResolved() && geometry->getBaseElevationVersion() == version) {
            VT_STAT_INC(extrusionResolveHits);
            return true;
        }
        VT_STAT_CLOCK(resolveClock);
        // No DEM on the TARGET tile (what the draw binds) yet: keep the sentinel and retry. Still
        // drawn - the shader falls back to the per-vertex ground.
        const std::pair<bool, TerrainTexture>& terrain = resolveTerrainTexture(targetTileId);
        if (!terrain.first) {
            VT_STAT_INC(extrusionResolveUnresolved);
            return false;
        }
        float metersToInternal = terrain.second.metersToInternal;
        const VertexArray<std::uint8_t>& vertexGeometry = geometry->getVertexGeometry();
        if (vertexGeometry.empty() || params.vertexSize <= 0) {
            return false;
        }

        cglib::mat3x3<double> tileMatrix = calculateTileMatrix2D(sourceTileId, 1.0f);
        std::shared_ptr<const TileTransformer::VertexTransformer> tileTransformer = _transformer->createTileVertexTransformer(sourceTileId);
        std::size_t vertexCount = vertexGeometry.size() / params.vertexSize;
        // Footprints depend on vertex data alone, so they are found once: a DEM arrival only
        // re-samples the ground (performance-log 26).
        if (geometry->getBaseRuns().empty()) {
            buildExtrusionBaseFootprints(geometry, params, vertexGeometry, vertexCount);
        }
        const std::vector<TileGeometry::BaseAnchor>& anchors = geometry->getBaseAnchors();
        const std::vector<TileGeometry::BaseRun>& runs = geometry->getBaseRuns();
        VT_STAT_ADD(extrusionResolveVertices, static_cast<long long>(vertexCount));

        // The anchor rides in the texcoord slot at coord scale (packGeometry), y flipped when stored;
        // unflipped here, as polygon3DVsh does, to agree with the tile matrix.
        auto sampleGround = [&](const cglib::vec2<float>& tilePos, bool smoothed, double& out) {
            cglib::vec2<double> at = cglib::transform_point(cglib::vec2<double>(tilePos(0), 1.0 - tilePos(1)), tileMatrix);
            VT_STAT_INC(extrusionElevQueries);
            return _extrusionElevationProvider(cglib::vec3<double>(at(0), at(1), 0), sourceTileId.zoom, smoothed, out);
        };

        std::vector<double> anchorBases(anchors.size(), 0.0);
        for (std::size_t a = 0; a < anchors.size(); a++) {
            const TileGeometry::BaseAnchor& anchor = anchors[a];
            // The SMOOTHED field, so a building's pieces agree without seeing each other. An undecoded
            // DEM keeps the sentinel: writing the provider's 0 buries the prism.
            double base = 0;
            if (!sampleGround(anchor.pos, true, base)) {
                VT_STAT_INC(extrusionResolveUnresolved);
                VT_STAT_SPLIT(extrusionResolveNs, resolveClock);
                return false; // still drawn, on the per-vertex ground, until the elevation lands
            }
            double tileUnitsPerMeter = tileTransformer->calculateHeight(anchor.pos, 1.0f);
            if (anchor.haveSupports && tileUnitsPerMeter > 0) {
                cglib::vec2<double> anchorPos = cglib::transform_point(cglib::vec2<double>(anchor.pos(0), 1.0 - anchor.pos(1)), tileMatrix);
                double metersToZ = metersToInternal * std::cosh(2.0 * 3.14159265358979323846 * anchorPos(1));
                // mapbox's floor, over the support points: a building keeps 2 m above the highest ground
                // under it, so a part anchored under its own street is not a hole.
                double maxGround = 0;
                bool haveGround = false;
                for (int d = 0; d < ExtrusionFloor::SUPPORT_DIRECTIONS; d++) {
                    bool repeat = false;
                    for (int e = 0; e < d && !repeat; e++) { // a small footprint extremises several directions at one vertex
                        repeat = anchor.supports[e](0) == anchor.supports[d](0) && anchor.supports[e](1) == anchor.supports[d](1);
                    }
                    if (repeat) {
                        continue;
                    }
                    double ground = 0;
                    if (sampleGround(anchor.supports[d], false, ground)) {
                        maxGround = haveGround ? std::max(maxGround, ground) : ground;
                        haveGround = true;
                    }
                }
                if (haveGround) {
                    double maxHeightZ = anchor.maxHeightUnits / static_cast<double>(params.heightScale) / tileUnitsPerMeter * metersToZ;
                    base = std::max(base, maxGround + 2.0 * metersToZ - maxHeightZ);
                }
            }
            anchorBases[a] = base;
        }
        for (const TileGeometry::BaseRun& run : runs) {
            float base = static_cast<float>(anchorBases[run.anchorIndex]);
            for (std::uint32_t k = run.begin; k < run.end; k++) {
                geometry->setVertexBase(k, base);
            }
        }
        geometry->setBaseResolved(true);
        geometry->setBaseElevationVersion(version);
        VT_STAT_SPLIT(extrusionResolveNs, resolveClock);
        return true;
    }

    void GLTileRenderer::buildExtrusionBaseFootprints(const std::shared_ptr<TileGeometry>& geometry, const TileGeometry::VertexGeometryLayoutParameters& params, const VertexArray<std::uint8_t>& vertexGeometry, std::size_t vertexCount) const {
        std::vector<TileGeometry::BaseAnchor> anchors;
        std::vector<TileGeometry::BaseRun> runs;
        std::map<std::pair<std::int32_t, std::int32_t>, std::size_t> anchorIndices;
        std::vector<std::array<float, ExtrusionFloor::SUPPORT_DIRECTIONS> > supportScores;
        bool haveFootprint = params.heightOffset >= 0 && params.coordOffset >= 0 && params.coordScale > 0;
        for (std::size_t i = 0; i < vertexCount; ) {
            const std::int16_t* texCoordPtr = reinterpret_cast<const std::int16_t*>(vertexGeometry.data() + i * params.vertexSize + params.texCoordOffset);
            std::int32_t u = texCoordPtr[0];
            std::int32_t v = texCoordPtr[1];
            std::size_t j = i + 1;
            for (; j < vertexCount; j++) {
                const std::int16_t* next = reinterpret_cast<const std::int16_t*>(vertexGeometry.data() + j * params.vertexSize + params.texCoordOffset);
                if (next[0] != u || next[1] != v) {
                    break;
                }
            }
            auto anchorIt = anchorIndices.find(std::make_pair(u, v));
            if (anchorIt == anchorIndices.end()) {
                TileGeometry::BaseAnchor anchor;
                anchor.pos = cglib::vec2<float>(u / params.texCoordScale, v / params.texCoordScale);
                anchor.supports.fill(anchor.pos);
                anchorIt = anchorIndices.emplace(std::make_pair(u, v), anchors.size()).first;
                anchors.push_back(anchor);
                supportScores.emplace_back();
            }
            // Accumulated over every run: a building the source split into parts is ONE prism.
            if (haveFootprint) {
                TileGeometry::BaseAnchor& anchor = anchors[anchorIt->second];
                std::array<float, ExtrusionFloor::SUPPORT_DIRECTIONS>& scores = supportScores[anchorIt->second];
                for (std::size_t k = i; k < j; k++) {
                    const std::uint8_t* vertex = vertexGeometry.data() + k * params.vertexSize;
                    std::int16_t heightUnits = reinterpret_cast<const std::int16_t*>(vertex + params.heightOffset)[0];
                    if (heightUnits <= 0) {
                        continue; // a wall's foot, not the footprint outline the roof is carried on
                    }
                    anchor.maxHeightUnits = std::max(anchor.maxHeightUnits, static_cast<float>(heightUnits));
                    const std::int16_t* coord = reinterpret_cast<const std::int16_t*>(vertex + params.coordOffset);
                    cglib::vec2<float> at(coord[0] / params.coordScale, coord[1] / params.coordScale);
                    for (int d = 0; d < ExtrusionFloor::SUPPORT_DIRECTIONS; d++) {
                        float score = ExtrusionFloor::supportScore(d, at(0), at(1));
                        if (!anchor.haveSupports || score > scores[d]) {
                            scores[d] = score;
                            anchor.supports[d] = at;
                        }
                    }
                    anchor.haveSupports = true;
                }
            }
            runs.push_back(TileGeometry::BaseRun { static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j), static_cast<std::uint32_t>(anchorIt->second) });
            i = j;
        }
        geometry->setBaseFootprints(std::move(anchors), std::move(runs));
    }

    void GLTileRenderer::markPendingLabelsDirty() {
        // Labels are built flat: a new one is always dirty, an old one only when elevation under its
        // tiles changed. Every label per arrival resamples the screen constantly while panning.
        if (!_pendingLabelElevationAll && _pendingLabelElevationTiles.empty()) {
            return;
        }
        for (const std::shared_ptr<Label>& label : _labels) {
            if (label->isElevationDirty()) {
                continue;
            }
            if (_pendingLabelElevationAll) {
                label->setElevationDirty(true);
                continue;
            }
            for (const TileId& tileId : _pendingLabelElevationTiles) {
                if (label->hasGeometryOverTile(tileId)) {
                    label->setElevationDirty(true);
                    break;
                }
            }
        }
        _pendingLabelElevationAll = false;
        _pendingLabelElevationTiles.clear();
    }

    std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> GLTileRenderer::labelAnchorFunc() const {
        // A label ON a bridge belongs to the deck (road names, POIs, one-way arrows alike). Chords
        // and provider are copied: the sampler outlives the lock it was made under.
        std::vector<SpanResolver::SpanChord> chords = _spanResolver.chords(_extrusionBaseVersion.load(std::memory_order_relaxed));
        std::function<double(const cglib::vec3<double>&, int)> provider = _labelElevationProvider;
        std::shared_ptr<const TileTransformer> transformer = _transformer;
        double scale = _labelPositionScale;
        // A world anchor in, a world anchor ON the terrain out: the lookup is keyed by internal
        // Mercator and the lift is along the surface, neither of which is the vertex z on a globe.
        return [chords, provider, transformer, scale](const cglib::vec3<double>& pos, int tileZoom) {
            cglib::vec3<double> mercatorPos = transformer->calculateMercatorPos(pos);
            double height = 0;
            if (!(!chords.empty() && SpanResolver::chordHeightAt(chords, cglib::vec2<double>(mercatorPos(0) * scale, mercatorPos(1) * scale), height))) {
                height = provider ? provider(mercatorPos, tileZoom) : 0.0;
            }
            return transformer->calculateElevatedPos(pos, height);
        };
    }

    bool GLTileRenderer::anchorDirtyLabels() {
        // Under the lock: the few labels over newly landed elevation. Runs to completion, or a dirty
        // label is drawn and culled at its old height; the bulk is sampled in setVisibleTiles.
        VT_STAT_CLOCK(anchorClock);
        // Whatever path marked a label clean, one sampled before the last whole-set invalidation (a 2D/3D
        // ramp step) is anchored again: the cull thread's off-lock sample can land after a step.
        for (const std::shared_ptr<Label>& label : _labels) {
            if (label->getElevationGeneration() != _labelElevationGeneration) {
                label->setElevationDirty(true);
            }
        }
        markPendingLabelsDirty();
        VT_STAT_SPLIT(prepElevDirtyNs, anchorClock);
        std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> anchorFunc = labelAnchorFunc();
        std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> roofFunc = roofAnchorFunc(anchorFunc);
        bool anchored = false;
        std::vector<std::shared_ptr<Label>> dirty;
        for (const std::shared_ptr<Label>& label : _labels) {
            // A flat map anchors nothing but the labels standing on roofs.
            if (label->isElevationDirty() && (_labelElevationProvider || label->isZElevated())) {
                label->updateElevation(label->isZElevated() ? roofFunc : anchorFunc);
                label->setElevationGeneration(_labelElevationGeneration);
                label->setElevationDirty(false); // see the bulk path in setVisibleTiles
                dirty.push_back(label);
                anchored = true;
            }
        }
        markDeckAnchoredLabels(dirty);
        if (anchored) {
            _labelsReanchored = true;
        }
        return anchored;
    }

    TileId GLTileRenderer::labelBatchTileId(const std::shared_ptr<Label>& label) const {
        if (!_terrainMode || label->isElevationAnchored()) {
            return TileId(-1, -1, -1);
        }
        return label->getTileId();
    }

    void GLTileRenderer::markDeckAnchoredLabels(const std::vector<std::shared_ptr<Label>>& labels) const {
        std::vector<SpanResolver::SpanChord> chords = _spanResolver.chords(_extrusionBaseVersion.load(std::memory_order_relaxed));
        double scale = _labelPositionScale;
        for (const std::shared_ptr<Label>& label : labels) {
            cglib::vec3<double> center(0, 0, 0);
            double deck = 0;
            bool onDeck = !chords.empty() && label->calculateCenter(center)
                && SpanResolver::chordHeightAt(chords, cglib::vec2<double>(center(0) * scale, center(1) * scale), deck);
            bool onRoof = label->isZElevated() && label->getAnchorPosition() && roofHeightAt(*label->getAnchorPosition());
            label->setAbsoluteHeight(onDeck || onRoof);
        }
    }

    bool GLTileRenderer::spanHeightAt(const cglib::vec2<double>& pos, double& height) const {
        return _spanResolver.heightAt(pos, height);
    }

    void GLTileRenderer::renderTileMask(const TileId& tileId) {
#if MASSIF_VT_RENDER_STATS
        VT_STAT_CLOCK(maskClock);
        struct MaskTimer { std::chrono::steady_clock::time_point& c; ~MaskTimer() { VT_STAT_SPLIT(surfMaskNs, c); } } maskTimer { maskClock };
#endif
        bool gridMode = terrainGridSurfaces() && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        for (const std::shared_ptr<TileSurface>& tileSurface : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(tileId))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider ? TERRAIN_VTF_FLAG : 0);
            const ShaderProgram& shaderProgram = buildShaderProgram("tilemask", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag);
            useProgram(shaderProgram);
            if (terrainFlag != 0) {
                setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            Color color(0, 0, 0, 0);
            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 0);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfMaskDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }
    
    void GLTileRenderer::renderStencilDebugOverlay() {
        // Debug: the stencil contents at the end of the 2D pass, a translucent colour per value;
        // 0 (owned by no tile mask) is black.
        if (_debugOrderedTileMasks.empty()) {
            return;
        }

        glEnable(GL_STENCIL_TEST);
        glStencilMask(0);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        const ShaderProgram& shaderProgram = buildShaderProgram("tilemask", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, 0);
        useProgram(shaderProgram);

        if (_screenQuad.vbo == 0) {
            createCompiledQuad(_screenQuad);
        }
        glBindBuffer(GL_ARRAY_BUFFER, _screenQuad.vbo);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 2, GL_FLOAT, GL_FALSE, 0, 0);

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::identity();
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        glUniform1f(shaderProgram.uniforms[U_OPACITY], 0.45f);

        glStencilFunc(GL_EQUAL, 0, 255);
        Color black(0.0f, 0.0f, 0.0f, 1.0f);
        glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, black.rgba().data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        for (const std::pair<TileId, GLint>& tileMask : _debugOrderedTileMasks) {
            int v = tileMask.second;
            glStencilFunc(GL_EQUAL, v, 255);
            // Colour by tile zoom: <=10 grey, 11 red, 12 orange, 13 yellow, 14 green, 15 cyan, 16+ magenta.
            Color color(0.5f, 0.5f, 0.5f, 1.0f);
            switch (tileMask.first.zoom) {
            case 11: color = Color(1.0f, 0.0f, 0.0f, 1.0f); break;
            case 12: color = Color(1.0f, 0.5f, 0.0f, 1.0f); break;
            case 13: color = Color(1.0f, 1.0f, 0.0f, 1.0f); break;
            case 14: color = Color(0.0f, 1.0f, 0.0f, 1.0f); break;
            case 15: color = Color(0.0f, 1.0f, 1.0f, 1.0f); break;
            default: if (tileMask.first.zoom >= 16) color = Color(1.0f, 0.0f, 1.0f, 1.0f); break;
            }
            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glStencilFunc(GL_ALWAYS, 0, 255);

        checkGLError();
    }

    void GLTileRenderer::renderTileSurfaceFill(const TileId& tileId, const Color& color, bool lit) {
        // The displaced surface as a solid colour, or depth-only when transparent; the depth bias
        // pushes the pre-pass slightly back so content passes over it at its real depth.
        bool gridMode = terrainGridSurfaces() && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        for (const std::shared_ptr<TileSurface>& tileSurface : (gridMode ? (_terrainShadowMaskPass ? buildCompiledTerrainShadowGridSurfaces() : buildCompiledTerrainGridSurfaces()) : buildCompiledTileSurfaces(tileId))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0);
            // The shared ground is the lit terrain surface, like the drape; the colour-masked depth
            // pre-passes ask for the plain fill, where lighting is pure cost.
            bool litSurface = lit && terrainFlag != 0 && _terrainLighting.enabled;
            bool shadowedSurface = litSurface && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
            unsigned int lightFlags = (litSurface ? TERRAIN_LIGHT_FLAG : 0) | (shadowedSurface ? surfaceShadowFlags() : 0);
            const ShaderProgram& shaderProgram = buildShaderProgram("tilesurfacefill", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag | lightFlags | fogFlag());
            useProgram(shaderProgram);
            setupFogUniforms(shaderProgram);
            bool hasElevation = true;
            if (terrainFlag != 0) {
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
                hasElevation = setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode);
            }
            if (litSurface) {
                setupTerrainLightingUniforms(shaderProgram, tileId, surfaceFrame);
            }
            if (shadowedSurface) {
                setupSurfaceShadowUniforms(shaderProgram, surfaceFrame, hasElevation);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            GLsizei drawnIndices = drawSurfaceElements(tileId, *tileSurface, gridMode);
            if (gridMode && !_terrainShadowMaskPass) {
                drawnIndices += drawTerrainSkirts(tileId, shaderProgram);
            }
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfFillDraws);
            VT_STAT_ADD(surfaceIndices, drawnIndices);

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }

    GLuint GLTileRenderer::ensureDrapeTexture(const TileId& tileId) {
        GLuint& tex = _drapeTextures[tileId];
        if (tex == 0) {
            // Pooled: tiles enter and leave constantly while panning, and per-tile allocation churns.
            if (!_drapeTexturePool.empty()) {
                tex = _drapeTexturePool.back();
                _drapeTexturePool.pop_back();
                return tex;
            }
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _drapeTextureSize, _drapeTextureSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return tex;
    }

    void GLTileRenderer::setExternalDrapeTarget(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (enabled != _externalDrapeTarget) {
            _externalDrapeTarget = enabled;
            // Ownership of the textures changes hands; drop ours (deleted on the GL thread).
            _drapeStaleTextures.insert(_drapeStaleTextures.end(), _drapeTexturePool.begin(), _drapeTexturePool.end());
            _drapeTexturePool.clear();
            for (auto it = _drapeTextures.begin(); it != _drapeTextures.end(); it++) {
                _drapeStaleTextures.push_back(it->second);
            }
            _drapeTextures.clear();
            _drapeFingerprints.clear();
        }
    }

    void GLTileRenderer::setExternalDrapeTiles(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        _externalDrapeTiles.assign(tileIds.begin(), tileIds.end());
        updateTerrainCoverTiles();
    }

    void GLTileRenderer::updateTerrainCoverTiles() {
        // Precedence drape > ground > paint: under a drape a paint bakes into it flat. Handed in
        // every frame, so rebuild only on a real change.
        std::set<TileId> coverTileIds;
        if (!_externalDrapeTiles.empty()) {
            coverTileIds.insert(_externalDrapeTiles.begin(), _externalDrapeTiles.end());
        } else if (!_terrainGroundTiles.empty()) {
            coverTileIds.insert(_terrainGroundTiles.begin(), _terrainGroundTiles.end());
        } else if (!_terrainPaintTiles.empty()) {
            coverTileIds.insert(_terrainPaintTiles.begin(), _terrainPaintTiles.end());
        }
        if (coverTileIds != _terrainCoverTileIds) {
            _terrainCoverTileIds = std::move(coverTileIds);
            buildTerrainEdgeCoarsening();
        }
    }

    void GLTileRenderer::setTerrainGroundTiles(const std::vector<TileId>& tileIds, const std::vector<int>& proxyDepths) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainGroundTiles = tileIds;
        _terrainGroundProxyDepths = proxyDepths;
        _terrainGroundProxyDepths.resize(tileIds.size(), 0);
        _terrainSharedGround = !tileIds.empty();
        _groundLeafCache.clear();
        updateTerrainCoverTiles();
    }

    void GLTileRenderer::setTerrainSkirtDrops(const std::map<TileId, cglib::vec4<float>>& drops) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainSkirtDropMap = drops;
    }

    const std::vector<TileId>& GLTileRenderer::collectGroundLeaves(const TileId& targetTileId) const {
        auto it = _groundLeafCache.find(targetTileId);
        if (it != _groundLeafCache.end()) {
            return it->second;
        }
        std::vector<TileId> leaves;
        for (const TileId& groundTileId : _terrainGroundTiles) {
            if (tileCovers(targetTileId, groundTileId)) {
                leaves.push_back(groundTileId);
            }
        }
        if (leaves.empty()) {
            // No leaf of its own (no shared ground, or a coarser cover here): draw on its own surface,
            // a finer tessellation of the same height field, within the content slack.
            leaves.push_back(targetTileId);
        }
        return _groundLeafCache.emplace(targetTileId, std::move(leaves)).first->second;
    }

    int GLTileRenderer::renderTerrainGround(const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!(_terrainMode && _terrainTextureProvider) || _terrainGroundTiles.empty()) {
            return 0;
        }

        // The only depth-writing terrain geometry, at TRUE depth, so it hides a ridge's far slope
        // exactly and later draws use LEQUAL with no pull. Pushed back, it opens a see-through band.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away from the camera near ridge crests
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);
        _terrainDrawDepthBias = _terrainDepthBias;
        _terrainDrawDepthClipUnits = 0.0f;
        _terrainGroundColor = color;

        // With the paint AS the ground, one draw per tile: the paint shades this colour as its base,
        // as tangram's terrain raster does. The fill covers only where the paint cannot draw.
        bool paintIsGround = _terrainPaintOnGround && _terrainPaint.enabled && _lightingShaderNormalMap && !_lightingShaderNormalMap->perVertex && terrainGridSurfaces() && _terrainTextureProvider;

        int surfaceDraws = 0;
        for (std::size_t i = 0; i < _terrainGroundTiles.size(); i++) {
            if (paintIsGround) {
                const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(_terrainGroundTiles[i]);
                if (resolved.first && resolved.second.textureId != 0 && resolved.second.metersPerTexel > 0.0f) {
                    continue; // the paint draws this tile, base colour included
                }
            }
            // Offset 0 unless standing in on a coarser level, then pushed back hard (tangram's
            // `proxy *= 48`): a stand-in is a different height field and pokes through the content.
            _terrainDrawLayerOffset = _terrainGroundProxyDepths[i] * TERRAIN_RASTER_PROXY_SCALE;
            renderTileSurfaceFill(_terrainGroundTiles[i], color, true); // lit and shadowed: this IS the terrain surface
            surfaceDraws++;
        }
        _terrainDrawLayerOffset = 0.0f;
        if (paintIsGround) {
            surfaceDraws += renderTerrainPaintSurfaces(true);
            resetProgramState(); // the paint bound its own program and buffers
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
        }

        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        return surfaceDraws;
    }

    void GLTileRenderer::collectDrapeTiles(std::map<TileId, std::size_t>& drapeTiles) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_terrainPaint.enabled) {
            // A paint holds no tiles: nothing to fingerprint, and last frame's cover would make every
            // new tile look incomplete. Its changes show through the drape STACK signature instead.
            return;
        }
        if (!_visibleRenderTiles) {
            return;
        }
        // The fingerprint part shared by every tile: if it moves, the whole cover goes stale at once.
        std::size_t globalTerm = 0;
        for (int i = 0; i < 3; i++) {
            globalTerm = globalTerm * 31 + static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _radiance(i))) * 64.0f);
        }
        globalTerm = globalTerm * 31 + static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _backgroundEmissive)) * 64.0f);
        if (globalTerm != _lastDrapeGlobalTerm) {
            _lastDrapeGlobalTerm = globalTerm;
            VT_STAT_INC(drapeGlobalTermChanges);
        }
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            // EVERY visible tile, loaded or not: the drape replaces the per-layer pre-pass, so an
            // omitted tile gets no surface. Combined across render tiles sharing a target.
            std::size_t& fingerprint = drapeTiles[renderTile.targetTileId];
            std::size_t contribution = calculateDrapeFingerprint(renderTile);
            if (contribution == 0) {
                continue; // reported for the cover, but nothing here to bake: the entry stays 0
            }
            // Never mix in a zero: a non-zero fingerprint makes the owner wait for a bake that never comes.
            fingerprint ^= contribution + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
        }
    }

    int GLTileRenderer::bakeDrapeTile(const TileId& targetTileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (_terrainPaint.enabled) {
            return renderTerrainPaint(targetTileId);
        }
        return bakeDrapeUnits(targetTileId, std::numeric_limits<int>::min());
    }

    void GLTileRenderer::setSpanDrapeTextures(const std::map<TileId, GLuint>& textures) {
        std::lock_guard<std::mutex> lock(_mutex);

        _spanDrapeTextures = textures;
    }

    bool GLTileRenderer::resolveSpanDrape(const TileId& targetTileId, GLuint& texture, cglib::vec4<float>& uvTransform) const {
        if (_spanDrapeTextures.empty()) {
            return false; // no bridge anywhere in the view - the ordinary case, and it costs one test
        }
        // The deck's tile or the ancestor holding its drape: the coverage masks' sub-rect rule.
        for (TileId tileId = targetTileId; true; tileId = tileId.getParent()) {
            auto it = _spanDrapeTextures.find(tileId);
            if (it != _spanDrapeTextures.end()) {
                float scale = 1.0f / (1 << (targetTileId.zoom - tileId.zoom));
                float u = (targetTileId.x - (tileId.x << (targetTileId.zoom - tileId.zoom))) * scale;
                float v = (targetTileId.y - (tileId.y << (targetTileId.zoom - tileId.zoom))) * scale;
                texture = it->second;
                uvTransform = cglib::vec4<float>(u, v, scale, scale);
                // ...into the bounds the drape tile was actually baked over (bakeSpanDrapeTile).
                auto boundsIt = _spanDrapeBounds.find(tileId);
                if (boundsIt != _spanDrapeBounds.end()) {
                    uvTransform = SpanGeometry::drapeTransformInBounds(uvTransform, boundsIt->second);
                }
                return true;
            }
            if (tileId.zoom <= 0) {
                return false;
            }
        }
    }

    void GLTileRenderer::setGroundDrapeTextures(const std::map<TileId, GroundDrape>& drapes) {
        std::lock_guard<std::mutex> lock(_mutex);

        _groundDrapes = drapes;
    }

    bool GLTileRenderer::resolveGroundDrape(const TileId& targetTileId, GLuint& texture, cglib::vec4<float>& uvTransform) const {
        if (_groundDrapes.empty()) {
            return false;
        }
        // The drape tile or an ancestor, the owner's sub-rect composed with the target's share. A finer
        // drape stack would need several textures for one draw, so it gets none.
        for (TileId tileId = targetTileId; true; tileId = tileId.getParent()) {
            auto it = _groundDrapes.find(tileId);
            if (it != _groundDrapes.end() && it->second.texture != 0) {
                float scale = 1.0f / (1 << (targetTileId.zoom - tileId.zoom));
                float u = (targetTileId.x - (tileId.x << (targetTileId.zoom - tileId.zoom))) * scale;
                float v = (targetTileId.y - (tileId.y << (targetTileId.zoom - tileId.zoom))) * scale;
                texture = it->second.texture;
                uvTransform = cglib::vec4<float>(it->second.uvOffsetX + u * it->second.uvScale, it->second.uvOffsetY + v * it->second.uvScale, scale * it->second.uvScale, scale * it->second.uvScale);
                return true;
            }
            if (tileId.zoom <= 0) {
                return false;
            }
        }
    }

    cglib::vec3<float> GLTileRenderer::spanDrapeLight() const {
        return SpanDrapeLight::resolve(_terrainLighting.enabled, _terrainLighting.sunDir, _terrainLighting.sunColor, _terrainLighting.sunIntensity, _terrainLighting.ambientColor, _terrainLighting.ambientIntensity);
    }

    int GLTileRenderer::bakeSpanDrapeTile(const TileId& targetTileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState();

        if (_terrainPaint.enabled) {
            return 0; // a paint shades the ground; a deck is not the ground
        }
        // Only the deck's bounds are baked: raised nearer the camera its drape is magnified more, and a
        // tile-wide bake of a narrow deck wasted most of the texture.
        cglib::mat4x4<float> clipZoom = cglib::mat4x4<float>::identity();
        auto boundsIt = _spanDrapeBounds.find(targetTileId);
        if (boundsIt != _spanDrapeBounds.end()) {
            clipZoom = SpanGeometry::clipZoomToBounds(boundsIt->second);
        }
        return bakeDrapeUnits(targetTileId, std::numeric_limits<int>::min(), true, &clipZoom);
    }

    void GLTileRenderer::collectSpanDrapeTiles(std::map<TileId, std::size_t>& spanTiles) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_terrainPaint.enabled || !_visibleRenderTiles || !_spanResolver.isEnabled()) {
            return;
        }
        // Only tiles carrying a bridge or tunnel, with the drape-uv bounds of their spans plus a margin
        // for deck width and stroke (4% of the tile, at least ~25 m).
        std::set<TileId> keep;
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            cglib::mat3x3<double> invTargetMatrix = cglib::inverse(calculateTileMatrix2D(renderTile.targetTileId, 1.0f));
            cglib::vec4<float> bounds(1.0f, 1.0f, 0.0f, 0.0f);
            bool anySpan = false;
            bool anyDraped = false;
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.layer || !renderLayer.layer->hasSpanGeometry()) {
                    continue;
                }
                // The bake holds the DRAPED spans (the road) but the bounds must hold the DECK sampling
                // it; bounded by the road, a wider deck smeared the clamped edge down its outer lanes.
                bool draped = hasSpanContent(renderLayer);
                anyDraped = anyDraped || draped;
                cglib::mat3x3<double> sourceMatrix = calculateTileMatrix2D(renderLayer.sourceTileId, 1.0f);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getSpanRecords().empty()) {
                        continue;
                    }
                    if (draped) {
                        std::size_t& fingerprint = spanTiles[renderTile.targetTileId];
                        fingerprint ^= reinterpret_cast<std::size_t>(geometry.get()) + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
                    }
                    for (const TileGeometry::SpanRecord& record : geometry->getSpanRecords()) {
                        for (const cglib::vec2<float>& p : { record.p0, record.p1 }) {
                            // Source-frame record -> target-tile drape uv, y up, as polygon3DFsh samples it.
                            cglib::vec2<double> w = cglib::transform_point(cglib::vec2<double>(p(0), 1.0 - p(1)), sourceMatrix);
                            cglib::vec2<double> t = cglib::transform_point(w, invTargetMatrix);
                            float u = static_cast<float>(t(0)), v = static_cast<float>(1.0 - t(1));
                            bounds = cglib::vec4<float>(std::min(bounds(0), u), std::min(bounds(1), v), std::max(bounds(2), u), std::max(bounds(3), v));
                            anySpan = true;
                        }
                    }
                }
            }
            if (anySpan && anyDraped) {
                double tileMeters = 40075017.0 / (1 << renderTile.targetTileId.zoom);
                float margin = static_cast<float>(std::max(0.04, 25.0 / tileMeters));
                _spanDrapeBounds[renderTile.targetTileId] = SpanGeometry::expandBounds(bounds, margin);
                keep.insert(renderTile.targetTileId);
            }
        }
        // Bounds for on-screen tiles only, or they grow for as long as the map is panned.
        for (auto it = _spanDrapeBounds.begin(); it != _spanDrapeBounds.end(); ) {
            it = keep.count(it->first) ? std::next(it) : _spanDrapeBounds.erase(it);
        }
    }

    void GLTileRenderer::collectUnresolvedSpanEnds(std::vector<std::pair<int, cglib::vec2<double>>>& ends) const {
        std::lock_guard<std::mutex> lock(_mutex);

        const std::vector<std::pair<int, cglib::vec2<double>>>& unresolved = _spanResolver.unresolvedEnds();
        ends.insert(ends.end(), unresolved.begin(), unresolved.end());
    }

    bool GLTileRenderer::hasSpanContent(const RenderTileLayer& renderLayer) const {
        if (!renderLayer.layer || !isLayerDraped(renderLayer.layer)) {
            return false;
        }
        if (!renderLayer.layer->hasSpanGeometry()) {
            return false; // the cheap per-layer test, so an ordinary tile stops here
        }
        for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
            if (!geometry->getSpanRecords().empty()) {
                return true;
            }
        }
        return false;
    }

    int GLTileRenderer::bakeDrapeCoverage(const TileId& targetTileId, int fromStyleLayerIdx) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState();

        if (_terrainPaint.enabled) {
            // A paint SHADES the drape's ground; as an occluder it would hide every live layer under
            // it. No coverage, so a contour below a paint stays visible.
            return 0;
        }
        _drapeCoveragePass = true;
        int baked = bakeDrapeUnits(targetTileId, fromStyleLayerIdx);
        _drapeCoveragePass = false;
        return baked;
    }

    int GLTileRenderer::bakeDrapeUnits(const TileId& targetTileId, int fromStyleLayerIdx, bool spanOnly, const cglib::mat4x4<float>* clipZoom) {
        if (!_visibleRenderTiles) {
            return 0;
        }
        _drapeMaskTexture = 0; // nothing baked is ever masked - the mask is what the bake produces
        int bakedPrimitives = 0;
        cglib::mat4x4<float> drapeOrtho;
        _drapeMVPOverride = &drapeOrtho;

        // The bake owns its GL state: it runs before any layer pass. Culling above all - the bake
        // matrix has no y flip, so windings reverse and culling would discard the fills.
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);

        // Only tiles that COVER this tile, coarsest first, or a parent's background paints over a
        // child. Strictly covering: a finer tile minified without mipmaps is white aliasing noise.
        std::vector<const RenderTile*> coveringTiles;
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (renderTile.visible && tileCovers(renderTile.targetTileId, targetTileId)) {
                coveringTiles.push_back(&renderTile);
            }
        }
        std::stable_sort(coveringTiles.begin(), coveringTiles.end(), [](const RenderTile* tile1, const RenderTile* tile2) {
            return tile1->targetTileId.zoom < tile2->targetTileId.zoom;
        });

        for (const RenderTile* renderTilePtr : coveringTiles) {
            auto bakes = [&](const RenderTileLayer& renderLayer) {
                if (!(spanOnly ? hasSpanContent(renderLayer) : hasDrapeableContent(renderLayer))) {
                    return false;
                }
                // A coverage bake starts part-way up the stack: only units ABOVE the masked layer occlude.
                if (renderLayer.layer->getLayerIndex() < fromStyleLayerIdx) {
                    return false;
                }
                // A render layer can be finer than its render tile (retained children) and outside this
                // tile: it must COVER it too, or a neighbour's content bakes in.
                return tileCovers(renderLayer.targetTileId, targetTileId);
            };
            bakeLayersDrawOnce(*renderTilePtr, bakes, [&](const RenderTileLayer& renderLayer) {
                // Backgrounds/rasters draw their target tile's mesh, geometry is in source tile coords;
                // both may be coarser than this tile, hence a sub-rect each.
                float geometryOpacity = calculateDrapeOpacity(renderLayer);
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                if (clipZoom) {
                    drapeOrtho = *clipZoom * drapeOrtho;
                }
                for (const std::shared_ptr<TileBackground>& background : (spanOnly || _drawOncePass == DrawOncePass::CORE ? std::vector<std::shared_ptr<TileBackground>>() : renderLayer.layer->getBackgrounds())) {
                    renderTileBackground(renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, background);
                    bakedPrimitives++;
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : (spanOnly || _drawOncePass == DrawOncePass::CORE ? std::vector<std::shared_ptr<TileBitmap>>() : renderLayer.layer->getBitmaps())) {
                    renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, bitmap);
                    bakedPrimitives++;
                }
                // The sphere places a vertex through the TARGET tile uv setupTerrainUniforms uploads.
                drapeOrtho = calculateDrapeMVPMatrix(_transformer && _transformer->isSpherical() ? renderLayer.targetTileId : renderLayer.sourceTileId, targetTileId);
                if (clipZoom) {
                    drapeOrtho = *clipZoom * drapeOrtho;
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    // A span leaves the GROUND's bake (a baked pixel IS the ground); the deck's drape is
                    // the complement. Whether or not the chord resolved, or the deck came up bare.
                    bool span = !geometry->getSpanRecords().empty();
                    bool wanted = spanOnly ? span : isDrapeableGeometry(geometry);
                    if (wanted && isLayerDraped(renderLayer.layer)) {
                        VT_STAT_CLOCK(bakeClock);
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, geometry);
                        if (geometry->getType() == TileGeometry::Type::LINE) {
                            VT_STAT_INC(drapeBakeLineDraws);
                            VT_STAT_SPLIT(drapeBakeLineNs, bakeClock);
                        } else if (geometry->getType() == TileGeometry::Type::POLYGON) {
                            VT_STAT_INC(drapeBakePolygonDraws);
                            VT_STAT_SPLIT(drapeBakePolygonNs, bakeClock);
                        } else {
                            VT_STAT_INC(drapeBakeOtherDraws);
                            VT_STAT_SPLIT(drapeBakeOtherNs, bakeClock);
                        }
                        bakedPrimitives++;
                    }
                }
            });
        }

        _drapeMVPOverride = nullptr;
        checkGLError();
        return bakedPrimitives;
    }

    void GLTileRenderer::bakeLayersDrawOnce(const RenderTile& renderTile, const std::function<bool(const RenderTileLayer&)>& wanted, const std::function<void(const RenderTileLayer&)>& draw) {
        bool anyGroup = false;
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end() && !anyGroup; it++) {
            anyGroup = !it->second.layer->getDrawOnceGroup().empty() && wanted(it->second);
        }
        if (!anyGroup) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                if (wanted(it->second)) {
                    draw(it->second);
                }
            }
            return;
        }

        // The drape FBO is colour only: a stencil the size of the bake joins it for this tile.
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        cglib::vec2<int> size(viewport[2], viewport[3]);
        if (_drapeStencilRB == 0 || _drapeStencilSize != size) {
            if (_drapeStencilRB == 0) {
                glGenRenderbuffers(1, &_drapeStencilRB);
            }
            glBindRenderbuffer(GL_RENDERBUFFER, _drapeStencilRB);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, size(0), size(1));
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            _drapeStencilSize = size;
        }
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _drapeStencilRB);

        std::vector<const RenderTileLayer*> layers;
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            if (wanted(it->second)) {
                layers.push_back(&it->second);
            }
        }
        std::string activeGroup;
        for (const auto& [renderLayer, pass] : drawOnceSchedule(layers, [](const RenderTileLayer* layer) { return layer->layer->getDrawOnceGroup(); })) {
            const std::string& group = renderLayer->layer->getDrawOnceGroup();
            if (group != activeGroup) {
                if (group.empty()) {
                    glDisable(GL_STENCIL_TEST);
                } else {
                    glEnable(GL_STENCIL_TEST);
                    glStencilMask(255);
                    glClearStencil(0);
                    glClear(GL_STENCIL_BUFFER_BIT);
                    glStencilMask(0);
                    glStencilFunc(GL_EQUAL, 0, 255);
                }
                activeGroup = group;
            }
            _drawOncePass = pass;
            draw(*renderLayer);
        }
        _drawOncePass = DrawOncePass::NONE;
        glDisable(GL_STENCIL_TEST);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    }

    void GLTileRenderer::collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_terrainPaint.enabled || !_visibleRenderTiles) {
            return; // a paint has no style layers of its own; see bakeDrapeCoverage
        }
        // Layer INDEX order, as both passes draw; one entry per style layer - the cut is the stack's.
        std::map<int, bool> drapedByLayer;
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.layer) {
                    continue;
                }
                bool draped = isLayerDraped(renderLayer.layer);
                bool drapeable = !renderLayer.layer->getBackgrounds().empty() || !renderLayer.layer->getBitmaps().empty();
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    drapeable = drapeable || isDrapeableGeometry(geometry);
                }
                // An all-span layer still holds its stack place, or every later coverage-mask index
                // shifts. Spans only: points and 3D buildings were never listed.
                if (drapeable || renderLayer.layer->hasSpanGeometry()) {
                    drapedByLayer[renderLayer.layer->getLayerIndex()] = draped && drapeable;
                }
            }
        }
        units.insert(units.end(), drapedByLayer.begin(), drapedByLayer.end());
    }

    void GLTileRenderer::setDrapeCoverageMasks(const std::vector<std::map<TileId, GLuint> >& maskTextures, const std::map<int, int>& styleLayerMasks) {
        std::lock_guard<std::mutex> lock(_mutex);

        _drapeCoverageMasks = maskTextures;
        _drapeCoverageLayerMasks = styleLayerMasks;
    }

    bool GLTileRenderer::resolveDrapeCoverageMask(const TileId& targetTileId, int styleLayerIdx, GLuint& texture, cglib::vec4<float>& uvTransform) const {
        auto maskIt = _drapeCoverageLayerMasks.find(styleLayerIdx);
        if (maskIt == _drapeCoverageLayerMasks.end() || maskIt->second < 0 || maskIt->second >= static_cast<int>(_drapeCoverageMasks.size())) {
            return false;
        }
        const std::map<TileId, GLuint>& masks = _drapeCoverageMasks[maskIt->second];
        for (auto it = masks.begin(); it != masks.end(); it++) {
            // Coarser or equal only: a finer mask needs several textures per draw (pre-#175 behaviour).
            if (it->second == 0 || !tileCovers(it->first, targetTileId)) {
                continue;
            }
            int deltaZoom = targetTileId.zoom - it->first.zoom;
            float span = static_cast<float>(1 << deltaZoom);
            float ix = static_cast<float>(targetTileId.x - (it->first.x << deltaZoom));
            float iy = static_cast<float>(targetTileId.y - (it->first.y << deltaZoom));
            // Mirrored y, as in the drape seed: texture v runs north, the XYZ tile y runs south.
            texture = it->second;
            uvTransform = cglib::vec4<float>(ix / span, (span - 1.0f - iy) / span, 1.0f / span, 1.0f / span);
            return true;
        }
        return false;
    }

    float GLTileRenderer::calculateLabelVisibility(const Label& label) {
        const cglib::vec3<double>* anchor = label.getAnchorPosition();
        if (!anchor || _transformer->isSpherical() || _viewState.tilt >= LABEL_OCCLUSION_MAX_TILT) {
            return 1.0f;
        }
        if (!_frameOccludersValid) {
            _frameOccludersValid = true;
            _frameOccluders.clear();
            forEachVisibleExtrusion(nullptr, false, [this](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>& geometry) {
                // A SPAN is the surface its own symbols stand on; a translated layer is not where its mesh says.
                const ExtrusionOccluder* occluder = geometry->getOccluder().get();
                if (!occluder || !geometry->getSpanRecords().empty() || geometry->getStyleParameters().translate) {
                    return true;
                }
                // The tile's own blend: an extrusion grows in, and a full-height occluder would hide
                // labels behind a building that is not there yet.
                cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId, 1.0f);
                FrameOccluder frameOccluder;
                frameOccluder.occluder = occluder;
                frameOccluder.geometry = geometry.get();
                frameOccluder.origin = cglib::vec3<double>(tileMatrix(0, 3), tileMatrix(1, 3), tileMatrix(2, 3));
                frameOccluder.scale = tileMatrix(0, 0);
                frameOccluder.heightScale = buildingHeightScale(renderLayer.blend) / geometry->getVertexGeometryLayoutParameters().heightScale * tileMatrix(2, 2);
                frameOccluder.bounds = _transformer->calculateTileBBox(renderLayer.sourceTileId);
                if (frameOccluder.heightScale > 0) {
                    _frameOccluders.push_back(frameOccluder);
                }
                return true;
            });
        }
        if (_frameOccluders.empty()) {
            return 1.0f;
        }

        // Tested from the roof of the building it stands in, if any: from the ground, its own roof hid it.
        cglib::vec3<double> target = *anchor;
        double margin = LABEL_OCCLUSION_MARGIN_METERS * _metersToInternal;
        bool onRoof = false;
        for (const FrameOccluder& frameOccluder : _frameOccluders) {
            if (target(0) < frameOccluder.bounds.min(0) || target(0) > frameOccluder.bounds.max(0) || target(1) < frameOccluder.bounds.min(1) || target(1) > frameOccluder.bounds.max(1)) {
                continue;
            }
            std::optional<double> roof = frameOccluder.occluder->roofAt((target(0) - frameOccluder.origin(0)) / frameOccluder.scale, (target(1) - frameOccluder.origin(1)) / frameOccluder.scale, *frameOccluder.geometry, frameOccluder.heightScale, frameOccluder.origin(2), !_extrusionElevationProvider);
            if (roof && *roof > target(2) - margin) {
                target(2) = std::max(target(2), *roof);
                onRoof = true;
            }
        }
        // Standing on its roof, as mapbox Standard's are, it is not occluded: at street tilt any roof a few metres
        // taller in front hid the anchor while the billboard drew over it (06-labels.mdx).
        if (onRoof && label.isZElevated()) {
            return 1.0f;
        }

        // Four rays, mapbox's square of taps: from the eye to the corners of the occluder square around
        // the anchor, at the anchor's distance. On a roof the square stands on it: its lower half was inside.
        const cglib::vec3<double>& eye = _viewState.origin;
        double distance = cglib::length(target - eye);
        double pixel = 2.0 * distance / (_viewState.projectionMatrix(1, 1) * std::max(1, _screenHeight));
        double half = 0.5 * LABEL_OCCLUSION_SIZE_PIXELS * pixel;
        cglib::vec3<double> right = cglib::vec3<double>::convert(_viewState.orientation[0]) * half;
        cglib::vec3<double> up = cglib::vec3<double>::convert(_viewState.orientation[1]) * half;
        int visible = 0;
        for (int tap = 0; tap < 4; tap++) {
            cglib::vec3<double> tapTarget = target + right * (tap & 1 ? 1.0 : -1.0) + up * (onRoof ? (tap & 2 ? 2.0 : 0.0) : (tap & 2 ? 1.0 : -1.0));
            cglib::vec3<double> dir = tapTarget - eye;
            double length = cglib::length(dir);
            double t1 = (length > margin ? 1.0 - margin / length : 0.0);
            double x0 = std::min(eye(0), tapTarget(0)), x1 = std::max(eye(0), tapTarget(0));
            double y0 = std::min(eye(1), tapTarget(1)), y1 = std::max(eye(1), tapTarget(1));
            bool blocked = false;
            for (const FrameOccluder& frameOccluder : _frameOccluders) {
                if (x1 < frameOccluder.bounds.min(0) || x0 > frameOccluder.bounds.max(0) || y1 < frameOccluder.bounds.min(1) || y0 > frameOccluder.bounds.max(1)) {
                    continue;
                }
                cglib::vec3<double> localOrigin((eye(0) - frameOccluder.origin(0)) / frameOccluder.scale, (eye(1) - frameOccluder.origin(1)) / frameOccluder.scale, eye(2));
                cglib::vec3<double> localDir(dir(0) / frameOccluder.scale, dir(1) / frameOccluder.scale, dir(2));
                if (frameOccluder.occluder->intersects(localOrigin, localDir, 0.0, t1, *frameOccluder.geometry, frameOccluder.heightScale, frameOccluder.origin(2), !_extrusionElevationProvider)) {
                    blocked = true;
                    break;
                }
            }
            visible += (blocked ? 0 : 1);
        }
        return visible * 0.25f;
    }

    bool GLTileRenderer::refreshRoofSurfaces() {
        std::vector<RoofSurface> roofs;
        if (_transformer->isSpherical()) {
            bool moved = !_roofSurfaces.empty();
            _roofSurfaces.clear();
            _roofSignature = 0;
            return moved;
        }
        float growth = buildingHeightScale(1.0f);
        std::size_t signature = std::hash<float>()(growth) ^ (static_cast<std::size_t>(_extrusionBaseVersion.load(std::memory_order_relaxed)) << 1) ^ (_extrusionElevationProvider ? 1 : 0);
        // Off-screen tiles too: the set then moves as tiles load, not with every pan.
        forEachVisibleExtrusion(nullptr, true, [&](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>& geometry) {
            if (!geometry->getOccluder() || !geometry->getSpanRecords().empty() || geometry->getStyleParameters().translate) {
                return true;
            }
            cglib::mat4x4<double> tileMatrix = calculateTileMatrix(renderLayer.sourceTileId, 1.0f);
            RoofSurface roof;
            roof.geometry = geometry;
            roof.origin = cglib::vec3<double>(tileMatrix(0, 3), tileMatrix(1, 3), tileMatrix(2, 3));
            roof.scale = tileMatrix(0, 0);
            roof.heightScale = growth / geometry->getVertexGeometryLayoutParameters().heightScale * tileMatrix(2, 2);
            roof.bounds = _transformer->calculateTileBBox(renderLayer.sourceTileId);
            if (roof.heightScale > 0) {
                signature ^= std::hash<const void*>()(geometry.get()) + 0x9e3779b9 + (signature << 6) + (signature >> 2);
                roofs.push_back(roof);
            }
            return true;
        });
        if (signature == _roofSignature) {
            return false;
        }
        _roofSignature = signature;
        _roofSurfaces = std::move(roofs);
        for (const std::shared_ptr<Label>& label : _labels) {
            if (label->isZElevated()) {
                label->setElevationDirty(true);
            }
        }
        return true;
    }

    std::optional<double> GLTileRenderer::roofHeightAt(const cglib::vec3<double>& pos) const {
        std::optional<double> height;
        for (const RoofSurface& roof : _roofSurfaces) {
            if (pos(0) < roof.bounds.min(0) || pos(0) > roof.bounds.max(0) || pos(1) < roof.bounds.min(1) || pos(1) > roof.bounds.max(1)) {
                continue;
            }
            std::optional<double> z = roof.geometry->getOccluder()->roofAt((pos(0) - roof.origin(0)) / roof.scale, (pos(1) - roof.origin(1)) / roof.scale, *roof.geometry, roof.heightScale, roof.origin(2), !_extrusionElevationProvider);
            if (z && (!height || *z > *height)) {
                height = z;
            }
        }
        return height;
    }

    std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> GLTileRenderer::roofAnchorFunc(std::function<cglib::vec3<double>(const cglib::vec3<double>&, int)> anchorFunc) const {
        return [this, anchorFunc](const cglib::vec3<double>& pos, int tileZoom) {
            cglib::vec3<double> anchored = anchorFunc(pos, tileZoom);
            std::optional<double> roof = roofHeightAt(anchored);
            if (roof && *roof > anchored(2)) {
                anchored(2) = *roof;
            }
            return anchored;
        };
    }

    bool GLTileRenderer::hasGroundContent() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_renderTiles) {
            return false;
        }
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const std::shared_ptr<const TileLayer>& layer = it->second.layer;
                if (!it->second.active || !layer) {
                    continue;
                }
                if (!layer->getBitmaps().empty() || !layer->getGeometries().empty()) {
                    return true;
                }
                // Every decoded tile carries the style's background, transparent when it sets none.
                for (const std::shared_ptr<TileBackground>& background : layer->getBackgrounds()) {
                    if (background->getPattern() || background->getColorFunc().function() || background->getColorFunc().value().value() != 0) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool GLTileRenderer::coversGround(const cglib::frustum3<double>& frustum) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_renderTiles || _terrainMode || _transformer->isSpherical()) {
            return false;
        }
        std::unordered_set<TileId> opaqueTiles;
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (renderLayer.blend < 1.0f || !renderLayer.layer || renderLayer.layer->getCompOp() || (renderLayer.layer->getOpacityFunc())(_viewState) < 1.0f) {
                    continue;
                }
                for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                    if (!background->getPattern() && background->getColorFunc()(_viewState).alpha() >= 1.0f) {
                        opaqueTiles.insert(renderLayer.targetTileId);
                    }
                }
            }
        }
        return coversVisibleGround(opaqueTiles, [this](const TileId& tileId) {
            return _transformer->calculateTileBBox(tileId);
        }, [&frustum](const cglib::bbox3<double>& bbox) {
            return frustum.inside(bbox);
        });
    }

    void GLTileRenderer::setLabelOcclusionOpacity(float occludedOpacity) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelOcclusionOpacity = occludedOpacity;
    }

    int GLTileRenderer::bakeGroundAOMask(const TileId& targetTileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles || !(_groundAOIntensity > 0.0f)) {
            return 0;
        }
        // STATE-NEUTRAL: runs inside the drape bake, which set its own state (culling above all); the
        // caller owns blend, cull, depth and the framebuffer.
        int baked = 0;
        cglib::mat4x4<float> drapeOrtho;
        const cglib::mat4x4<float>* previousOverride = _drapeMVPOverride;
        _drapeMVPOverride = &drapeOrtho;
        _groundAOMaskPass = true;
        _groundAOBakePass = true;

        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible || !tileCovers(renderTile.targetTileId, targetTileId)) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!tileCovers(renderLayer.targetTileId, targetTileId)) {
                    continue;
                }
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) {
                        // Blend 1, like every bake: the fade-in is per frame and this picture is cached;
                        // baked on arrival it froze at the fade's start - nothing.
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, 1.0f, renderLayer.tileSize, geometry);
                        baked++;
                    }
                }
            }
        }

        _groundAOMaskPass = false;
        _groundAOBakePass = false;
        _drapeMVPOverride = previousOverride;
        checkGLError();
        return baked;
    }

    int GLTileRenderer::renderDrapedSurface(const TileId& targetTileId, GLuint drapeTexture, float uvOffsetX, float uvOffsetY, float uvScale) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (drapeTexture == 0) {
            return -1;
        }
        if (!(_terrainRegularGrid && _terrainMode && _terrainTextureProvider)) {
            return -2; // the drape UV needs the grid's tile-local xy, or the sphere's own inversion
        }
        // renderTileSurfaceDrape reads the map: swap the external texture in so both paths share one draw.
        auto it = _drapeTextures.find(targetTileId);
        GLuint previous = (it != _drapeTextures.end() ? it->second : 0);
        bool wasDraped = _drapeTilesThisFrame.count(targetTileId) > 0;
        _drapeTextures[targetTileId] = drapeTexture;
        _drapeTilesThisFrame.insert(targetTileId);
        int surfaces = renderTileSurfaceDrape(targetTileId, uvOffsetX, uvOffsetY, uvScale);
        if (previous != 0) {
            _drapeTextures[targetTileId] = previous;
        } else {
            _drapeTextures.erase(targetTileId);
        }
        if (!wasDraped) {
            _drapeTilesThisFrame.erase(targetTileId);
        }
        return surfaces;
    }

    int GLTileRenderer::renderDrapedSurfaceFill(const TileId& targetTileId, const Color& color) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!(_terrainRegularGrid && _terrainMode && _terrainTextureProvider)) {
            return -2;
        }
        // Stand-in for an unbaked drape: the same mesh in the background colour. It must draw - the
        // surface is the terrain's only depth writer.
        _terrainDrawDepthBias = 0.0f;
        _terrainDrawDepthClipUnits = 0.0f;
        renderTileSurfaceFill(targetTileId, color);
        return 1;
    }

    int GLTileRenderer::blitDrapeTexture(GLuint srcTexture, float dstOffsetX, float dstOffsetY, float dstScale, float uvOffsetX, float uvOffsetY, float uvScale) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (srcTexture == 0) {
            return 0;
        }
        // A flat, unblended copy, on the flat bake's unit quad.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        int draws = 0;
        for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledFlatSurfaces()) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            const ShaderProgram& shaderProgram = buildShaderProgram("drapeblit", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, DRAPE_FLAG);
            useProgram(shaderProgram);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            // unit quad [0,1] -> the destination sub-rect -> clip [-1,1]
            cglib::mat4x4<float> mvpMatrix = cglib::translate4_matrix(cglib::vec3<float>(-1.0f + 2.0f * dstOffsetX, -1.0f + 2.0f * dstOffsetY, 0.0f))
                                           * cglib::scale4_matrix(cglib::vec3<float>(2.0f * dstScale, 2.0f * dstScale, 1.0f));
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, srcTexture);
            glUniform1i(shaderProgram.uniforms[U_DRAPETEXTURE], 0);
            glUniform4f(shaderProgram.uniforms[U_DRAPEUVTRANSFORM], uvOffsetX, uvOffsetY, uvScale, uvScale);
            glUniform4f(shaderProgram.uniforms[U_COLOR], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfBlitDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
            draws++;

            glBindTexture(GL_TEXTURE_2D, 0);
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        glEnable(GL_BLEND);
        checkGLError();
        return draws;
    }

    void GLTileRenderer::deleteDrapeResources() {
        // Otherwise the FBO and cached textures survive context loss as stale names.
        for (auto it = _drapeTextures.begin(); it != _drapeTextures.end(); it++) {
            glDeleteTextures(1, &it->second);
        }
        _drapeTextures.clear();
        _drapeFingerprints.clear();
        for (GLuint texture : _drapeTexturePool) {
            glDeleteTextures(1, &texture);
        }
        _drapeTexturePool.clear();
        for (GLuint texture : _drapeStaleTextures) {
            glDeleteTextures(1, &texture);
        }
        _drapeStaleTextures.clear();
        _drapeTilesThisFrame.clear();
        _externalDrapeTiles.clear();
        _drapeCoverageMasks.clear();
        _drapeCoverageLayerMasks.clear();
        _drapeMaskTexture = 0;
        if (_drapeFBO != 0) {
            glDeleteFramebuffers(1, &_drapeFBO);
            _drapeFBO = 0;
        }
        if (_drapeStencilRB != 0) {
            glDeleteRenderbuffers(1, &_drapeStencilRB);
            _drapeStencilRB = 0;
            _drapeStencilSize = cglib::vec2<int>(0, 0);
        }
    }

    int GLTileRenderer::renderTerrainPaint(const TileId& targetTileId) {
        // A paint is a function of the elevation texture already bound for this tile: ONE quad into the
        // shared drape at this layer's bake slot, nothing fetched or uploaded.
        if (!_lightingShaderNormalMap || _lightingShaderNormalMap->perVertex) {
            return 0; // the lighting shader IS the hillshade algorithm; without it there is no paint
        }
        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(targetTileId);
        if (!resolved.first || resolved.second.textureId == 0 || resolved.second.metersPerTexel <= 0.0f) {
            return 0; // no elevation data for this tile yet - report "nothing baked", not "done"
        }
        const TerrainTexture& terrainTexture = resolved.second;

        // The bake owns its GL state: culling off (no y flip, reversed winding), depth off, blend on.
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);

        int primitives = 0;
        cglib::mat4x4<double> surfaceFrame = calculateTileMatrix(targetTileId, 1.0f);
        for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledFlatSurfaces()) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            const ShaderProgram& shaderProgram = buildShaderProgram("terrainpaint", terrainPaintVsh, terrainPaintFsh, LightingMode::TERRAINPAINT, RasterFilterMode::NONE, TERRAIN_VTF_FLAG);
            useProgram(shaderProgram);
            // The vertex frame is the tile matrix, so the quad's [0,1] xy maps straight onto it.
            setupTerrainUniforms(shaderProgram, targetTileId, surfaceFrame, false);

            // The DEM gradient is metres per texel; hillshade wants the dimensionless slope. The
            // 1/cos(latitude) stretch comes per fragment (vElevCosh).
            float slopeScale = _terrainPaint.heightScale * calculateTerrainPaintReliefBoost(terrainTexture.metersPerTexel) / terrainTexture.metersPerTexel;
            glUniform2f(shaderProgram.uniforms[U_PAINTSLOPESCALE], slopeScale, slopeScale);
            glUniform4f(shaderProgram.uniforms[U_PAINTPARAMS], _terrainPaint.contrast, _terrainPaint.opacity, 0.0f, 0.0f);
            _lightingShaderNormalMap->setupFunc(shaderProgram.program, _viewState);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = calculateDrapeMVPMatrix(targetTileId, targetTileId);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
            primitives++;

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        checkGLError();
        return primitives;
    }

    int GLTileRenderer::renderTerrainPaintSurfaces(bool asGround) {
        // No drape: the paint IS the surface, one draw per tile over the shared grid VBO, as tangram.
        // Under a shared ground it is a layer on that ground and leaves depth alone.
        const std::vector<TileId>& paintTiles = (_terrainSharedGround ? _terrainGroundTiles : _terrainPaintTiles);
        if (!_lightingShaderNormalMap || _lightingShaderNormalMap->perVertex || paintTiles.empty()) {
            return 0;
        }
        if (!(terrainGridSurfaces() && _terrainMode && _terrainTextureProvider)) {
            return 0; // the shared grid surface is what this draws
        }

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        // As the ground it MUST write depth: no fill draws under it.
        glDepthMask(_terrainSharedGround && !asGround ? GL_FALSE : GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);
        // Same grid as the ground pass but another program: clip z differs in the last bits and LEQUAL
        // drops fragments. One delta of clearance; none as the ground.
        _terrainDrawDepthBias = _terrainDepthBias + (_terrainSharedGround && !asGround ? TERRAIN_LAYER_DEPTH_DELTA : 0.0f);
        _terrainDrawDepthClipUnits = 0.0f;
        // This layer's ordinal, or the bottom (0) as the ground, where tangram draws its terrain raster.
        _terrainDrawLayerOffset = (_terrainSharedGround && !asGround ? -static_cast<float>(_terrainLayerOrdinalBase) : 0.0f);

        // The paint covers the ground, so it carries its sun and shadow, or they vanish under it.
        bool litSurface = _terrainSharedGround && _terrainLighting.enabled;
        bool shadowedSurface = litSurface && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        // DERIVATIVES always: the contour block's fwidth() needs it, and contours arrive as a uniform.
        unsigned int lightFlags = (litSurface ? TERRAIN_LIGHT_FLAG : 0) | (shadowedSurface ? surfaceShadowFlags() : DERIVATIVES_FLAG) | (asGround ? GROUND_BASE_FLAG : 0);

        int draws = 0;
        for (std::size_t paintIndex = 0; paintIndex < paintTiles.size(); paintIndex++) {
            const TileId& tileId = paintTiles[paintIndex];
            // Drawn ON the ground, so it takes the ground's proxy push or a stand-in's shading separates.
            if (_terrainSharedGround && paintIndex < _terrainGroundProxyDepths.size()) {
                float paintOrdinal = (asGround ? 0.0f : static_cast<float>(_terrainLayerOrdinalBase));
                _terrainDrawLayerOffset = -paintOrdinal + _terrainGroundProxyDepths[paintIndex] * TERRAIN_RASTER_PROXY_SCALE;
            }
            const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
            if (!resolved.first || resolved.second.textureId == 0 || resolved.second.metersPerTexel <= 0.0f) {
                continue;
            }
            cglib::mat4x4<double> surfaceFrame = calculateTileMatrix(tileId, 1.0f);
            for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTerrainGridSurfaces()) {
                const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
                const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

                const ShaderProgram& shaderProgram = buildShaderProgram("terrainpaintsurface", terrainPaintVsh, terrainPaintFsh, LightingMode::TERRAINPAINT, RasterFilterMode::NONE, TERRAIN_FLAG | TERRAIN_VTF_FLAG | PAINT_SURFACE_FLAG | lightFlags | fogFlag());
                useProgram(shaderProgram);
                setupFogUniforms(shaderProgram);
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
                bool hasElevation = setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, true);
                if (litSurface) {
                    setupTerrainLightingUniforms(shaderProgram, tileId, surfaceFrame);
                }
                if (shadowedSurface) {
                    setupSurfaceShadowUniforms(shaderProgram, surfaceFrame, hasElevation);
                }

                float slopeScale = _terrainPaint.heightScale * calculateTerrainPaintReliefBoost(resolved.second.metersPerTexel) / resolved.second.metersPerTexel;
                glUniform2f(shaderProgram.uniforms[U_PAINTSLOPESCALE], slopeScale, slopeScale);
                glUniform4f(shaderProgram.uniforms[U_PAINTPARAMS], _terrainPaint.contrast, _terrainPaint.opacity, 0.0f, 0.0f);
                if (asGround) {
                    glUniform4f(shaderProgram.uniforms[U_GROUNDCOLOR], _terrainGroundColor[0], _terrainGroundColor[1], _terrainGroundColor[2], _terrainGroundColor[3]);
                }
                _lightingShaderNormalMap->setupFunc(shaderProgram.program, _viewState);

                glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

                cglib::mat4x4<float> mvpMatrix = calculateTileMVPMatrix(tileId, 1.0f);
                glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

                glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
                VT_STAT_INC(surfaceDraws);
                VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
                draws++;

                disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
            }
        }
        glDepthMask(GL_FALSE);
        // Unbind: a bound GL_ARRAY_BUFFER turns a later client-side array pointer (the sky quad) into
        // an offset.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        checkGLError();
        return draws;
    }

    void GLTileRenderer::setTerrainPaintTiles(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainPaintTiles = tileIds;
        updateTerrainCoverTiles();
    }

    float GLTileRenderer::calculateTerrainPaintReliefBoost(float metersPerTexel) const {
        // Verbatim from the normal-map path. The zoom is the SAMPLING density's, not the tile id's: a
        // 512-texel z11 grid is worth a 256 z12 tile.
        if (!(metersPerTexel > 0.0f)) {
            return 1.0f;
        }
        double zoom = std::log2(156543.03392804097 / metersPerTexel);
        if (_terrainPaint.legacyHeightScale) {
            float exaggeration = zoom < 2 ? 0.2f : zoom < 5 ? 0.3f : 0.35f;
            return static_cast<float>(160.0 * std::pow(2.0, -zoom * exaggeration));
        }
        if (_terrainPaint.exaggerateHeightScale && zoom < 15.0) {
            float exaggerationFactor = zoom < 2.0 ? 0.4f : zoom < 4.5 ? 0.35f : 0.3f;
            return static_cast<float>(std::pow(2.0, (15.0 - zoom) * exaggerationFactor));
        }
        return 1.0f;
    }

    void GLTileRenderer::releaseDrapeTexture(GLuint texture) {
        if (texture == 0) {
            return;
        }
        if (_drapeTexturePool.size() < DRAPE_TEXTURE_POOL_SIZE) {
            _drapeTexturePool.push_back(texture);
        } else {
            glDeleteTextures(1, &texture);
        }
    }

    bool GLTileRenderer::isDrapeableGeometry(const std::shared_ptr<TileGeometry>& geometry) const {
        // A span leaves the bake (a baked pixel IS the ground) only once it has a chord; unresolved it
        // stays on the terrain, where live geometry coplanar with the bake dissolves into noise.
        if (_spanResolver.isEnabled() && !geometry->getSpanRecords().empty() && geometry->isBaseResolved()) {
            return false;
        }
        TileGeometry::Type type = geometry->getType();
        // maplibre's drapeable set: fills and lines bake, extrusions and points stay live. Lines are
        // opt-out: they blur where a slope magnifies the texture.
        if (type == TileGeometry::Type::LINE) {
            return _terrainDrapeLines;
        }
        return type == TileGeometry::Type::POLYGON;
    }

    bool GLTileRenderer::isTileDraped(const TileId& targetTileId) const {
        if (!_terrainDrapeFills) {
            return false;
        }
        if (!_externalDrapeTarget) {
            return _drapeTilesThisFrame.count(targetTileId) > 0;
        }
        // Draped = same ground as a drape tile, whichever is coarser. Finer is a zoom out's outgoing
        // generation: drawn directly it paints the previous zoom over the new one.
        for (const TileId& drapeTileId : _externalDrapeTiles) {
            if (tileCovers(targetTileId, drapeTileId) || tileCovers(drapeTileId, targetTileId)) {
                return true;
            }
        }
        return false;
    }

    bool GLTileRenderer::tileCovers(const TileId& tileId, const TileId& targetTileId) const {
        if (tileId.zoom > targetTileId.zoom) {
            return false;
        }
        int deltaZoom = targetTileId.zoom - tileId.zoom;
        return (targetTileId.x >> deltaZoom) == tileId.x && (targetTileId.y >> deltaZoom) == tileId.y;
    }

    cglib::mat4x4<float> GLTileRenderer::calculateDrapeMVPMatrix(const TileId& sourceTileId, const TileId& targetTileId) const {
        // Ortho bake frame: the SOURCE tile's part covering the target, onto the texture's [-1,1]
        // square; otherwise proxy content samples a coarser lattice and sinks. Tile-local y runs north.
        int deltaZoom = targetTileId.zoom - sourceTileId.zoom;
        float n = 1.0f;
        float fx = 0.0f, gy = 0.0f;
        if (deltaZoom > 0) {
            int span = 1 << deltaZoom;
            n = static_cast<float>(span);
            fx = static_cast<float>(targetTileId.x - (sourceTileId.x << deltaZoom));
            gy = static_cast<float>(span - 1 - (targetTileId.y - (sourceTileId.y << deltaZoom)));
        } else if (deltaZoom < 0) {
            // Source FINER than the drape tile (zoom out): it covers a sub-rect, so the transform inverts.
            int span = 1 << (-deltaZoom);
            n = 1.0f / span;
            fx = -static_cast<float>(sourceTileId.x - (targetTileId.x << (-deltaZoom))) / span;
            gy = -static_cast<float>(span - 1 - (sourceTileId.y - (targetTileId.y << (-deltaZoom)))) / span;
        }
        // source-local [0,1] -> target-local [0,1] -> clip [-1,1]
        return cglib::translate4_matrix(cglib::vec3<float>(-1.0f, -1.0f, 0.0f))
             * cglib::scale4_matrix(cglib::vec3<float>(2.0f, 2.0f, 1.0f))
             * cglib::translate4_matrix(cglib::vec3<float>(-fx, -gy, 0.0f))
             * cglib::scale4_matrix(cglib::vec3<float>(n, n, 1.0f));
    }

    std::size_t GLTileRenderer::calculateDrapeFingerprint(const RenderTile& renderTile) const {
        // Identifies what would be baked, so a change marks the texture stale. Zero means nothing to
        // bake, hence the flag: real content can hash to zero.
        std::size_t hash = 0;
        bool anyContent = false;
        auto combine = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };
        // Scene light is baked with the colours, so the sun invalidates drapes; quantised to bound the
        // re-bakes over a day (DRAPE_LIGHT_STEPS).
        for (int i = 0; i < 3; i++) {
            combine(static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _radiance(i))) * DRAPE_LIGHT_STEPS) * (i + 1));
        }
        combine(static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _backgroundEmissive)) * DRAPE_LIGHT_STEPS) * 4);
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            const RenderTileLayer& renderLayer = it->second;
            // Contact shadows are baked in but their extrusions are not drapeable: count them, or a
            // texture baked before the buildings arrived keeps no shadow.
            if (!hasDrapeableContent(renderLayer) && !hasGroundAOContent(renderLayer)) {
                continue;
        // The SDK layer opacity is baked in too; the opaque case adds nothing, so existing fingerprints hold.
        float layerOpacity = std::max(0.0f, std::min(1.0f, calculateDrapeLayerOpacity()));
        if (layerOpacity < 1.0f) {
            combine(static_cast<std::size_t>(layerOpacity * DRAPE_OPACITY_STEPS) * 5 + 1);
        }
            }
            anyContent = true;
            combine(static_cast<std::size_t>(it->first));
            combine(static_cast<std::size_t>(renderLayer.sourceTileId.zoom) * 2654435761u
                  ^ static_cast<std::size_t>(renderLayer.sourceTileId.x) * 40503u
                  ^ static_cast<std::size_t>(renderLayer.sourceTileId.y));
            // By identity, never address: a new TileLayer often reuses a freed one's address and would
            // match a texture baked for the previous zoom.
            combine(std::hash<std::string>()(renderLayer.layer->getLayerName()));
            combine(static_cast<std::size_t>(renderLayer.layer->getLayerIndex()));
            combine(renderLayer.layer->getGeometries().size() * 2654435761u
                  ^ renderLayer.layer->getBitmaps().size() * 40503u
                  ^ renderLayer.layer->getBackgrounds().size());
            // A span leaves the bake as its chord resolves, invisible to the terms above; without this
            // a lifted bridge stays painted on the ground.
            std::size_t resolvedSpans = 0;
            for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                if (!geometry->getSpanRecords().empty()) {
                    resolvedSpans = resolvedSpans * 2 + (geometry->isBaseResolved() ? 1 : 0);
                }
            }
            combine(resolvedSpans);
        }
        if (anyContent && hash == 0) {
            hash = 1;
        }
        return anyContent ? hash : 0;
    }

    float GLTileRenderer::calculateDrapeOpacity(const RenderTileLayer& renderLayer) const {
        // The style layer opacity, passed on screen as element opacity; a comp-op layer needs the
        // overlay buffer the bake lacks, so it keeps full opacity.
        if (!renderLayer.layer || renderLayer.layer->getCompOp()) {
            return calculateDrapeLayerOpacity();
        }
        return (renderLayer.layer->getOpacityFunc())(_viewState) * calculateDrapeLayerOpacity();
    }

    bool GLTileRenderer::hasGroundAOContent(const RenderTileLayer& renderLayer) const {
        // Decided at decode: the search for a contact shadow cannot stop early.
        return renderLayer.layer && renderLayer.layer->hasGroundAOGeometry();
    }

    bool GLTileRenderer::hasDrapeableContent(const RenderTileLayer& renderLayer) const {
        if (!renderLayer.layer || !isLayerDraped(renderLayer.layer)) {
            return false;
        }
        if (!renderLayer.layer->getBackgrounds().empty() || !renderLayer.layer->getBitmaps().empty()) {
            return true;
        }
        for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
            if (isDrapeableGeometry(geometry)) {
                return true;
            }
        }
        return false;
    }

    void GLTileRenderer::renderDrapeTextures(const std::vector<RenderTile>& renderTiles) {
        // Maplibre-style drape: fills baked flat into a texture the surface samples, once per target
        // tile and capped per frame; until baked the content draws as geometry.
        if (_externalDrapeTarget) {
            return; // the owner drives baking across all layers (cross-layer stacks)
        }
        // An integer zoom change invalidates every tile at once: bake enough per frame that the
        // magnified parent bakes last one or two frames.
        static const std::size_t DRAPE_BAKE_BUDGET_PER_FRAME = 24;
        // Textures orphaned by a resolution change: deleted here, on the GL thread.
        for (GLuint texture : _drapeStaleTextures) {
            glDeleteTextures(1, &texture);
        }
        _drapeStaleTextures.clear();
        _drapeTilesThisFrame.clear();
        std::set<TileId> drapeContentTiles;

        std::vector<const RenderTile*> tilesToBake;
        for (const RenderTile& renderTile : renderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            const TileId& targetTileId = renderTile.targetTileId;
            if (drapeContentTiles.count(targetTileId)) {
                continue;
            }
            bool hasContent = false;
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end() && !hasContent; it++) {
                hasContent = hasDrapeableContent(it->second);
            }
            if (!hasContent) {
                continue;
            }
            drapeContentTiles.insert(targetTileId);
            std::size_t fingerprint = calculateDrapeFingerprint(renderTile);
            auto texIt = _drapeTextures.find(targetTileId);
            auto printIt = _drapeFingerprints.find(targetTileId);
            bool baked = texIt != _drapeTextures.end() && printIt != _drapeFingerprints.end() && printIt->second == fingerprint;
            if (baked) {
                _drapeTilesThisFrame.insert(targetTileId); // cached and still current - drape it now
            } else if (tilesToBake.size() < DRAPE_BAKE_BUDGET_PER_FRAME) {
                tilesToBake.push_back(&renderTile); // new or stale - (re)bake this frame, within budget
            } else if (texIt != _drapeTextures.end()) {
                _drapeTilesThisFrame.insert(targetTileId); // stale but budgeted out - keep showing the old bake
            }
        }

        for (auto it = _drapeTextures.begin(); it != _drapeTextures.end(); ) {
            if (!drapeContentTiles.count(it->first)) {
                releaseDrapeTexture(it->second);
                _drapeFingerprints.erase(it->first);
                it = _drapeTextures.erase(it);
            } else {
                it++;
            }
        }

        if (tilesToBake.empty()) {
            return; // all draped tiles cached - no offscreen work this frame
        }

        if (_drapeFBO == 0) {
            glGenFramebuffers(1, &_drapeFBO);
        }
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, _drapeFBO);
        glViewport(0, 0, _drapeTextureSize, _drapeTextureSize);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        setCompOp(CompOp::SRC_OVER);

        cglib::mat4x4<float> drapeOrtho;
        _drapeMVPOverride = &drapeOrtho;

        for (const RenderTile* renderTilePtr : tilesToBake) {
            const RenderTile& renderTile = *renderTilePtr;
            const TileId& targetTileId = renderTile.targetTileId;
            _drapeTilesThisFrame.insert(targetTileId); // baked now - drape it this frame
            _drapeFingerprints[targetTileId] = calculateDrapeFingerprint(renderTile);
            GLuint tex = ensureDrapeTexture(targetTileId);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Full opacity, so the cached texture ignores the momentary fade blend; an ancestor-sourced
            // layer bakes through a sub-rect. Points, extrusions and labels stay live.
            auto bakes = [&](const RenderTileLayer& renderLayer) {
                // Only layers whose own tile covers this one, as in the cross-layer bake.
                return hasDrapeableContent(renderLayer) && tileCovers(renderLayer.targetTileId, targetTileId);
            };
            bakeLayersDrawOnce(renderTile, bakes, [&](const RenderTileLayer& renderLayer) {
                // Backgrounds and rasters draw the target tile's mesh; their uv logic resolves overzoom.
                float geometryOpacity = calculateDrapeOpacity(renderLayer);
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                if (_drawOncePass != DrawOncePass::CORE) {
                    for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                        renderTileBackground(renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, background);
                    }
                    for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                        renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, bitmap);
                    }
                }
                // Geometry is in SOURCE tile coords, so an overzoomed layer needs the sub-rect transform.
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (isDrapeableGeometry(geometry)) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, geometry);
                    }
                }
            });
        }

        _drapeMVPOverride = nullptr;
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(0, 0, _screenWidth, _screenHeight);
        checkGLError();
    }

    int GLTileRenderer::renderTileSurfaceDrape(const TileId& tileId, float uvOffsetX, float uvOffsetY, float uvScale) {
#if MASSIF_VT_RENDER_STATS
        VT_STAT_CLOCK(drapeClock);
        struct DrapeTimer { std::chrono::steady_clock::time_point& c; ~DrapeTimer() { VT_STAT_SPLIT(surfDrapeNs, c); } } drapeTimer { drapeClock };
#endif
        auto texIt = _drapeTextures.find(tileId);
        if (texIt == _drapeTextures.end() || !_drapeTilesThisFrame.count(tileId)) {
            return -3;
        }
        int surfaces = 0;
        bool gridMode = terrainGridSurfaces() && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        for (const std::shared_ptr<TileSurface>& tileSurface : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(tileId))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            bool lit = _terrainLighting.enabled && _terrainMode && static_cast<bool>(_terrainTextureProvider);
            bool shadowed = lit && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
            bool hasElevation = true;
            unsigned int flags = (_terrainMode && _terrainTextureProvider ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0) | DRAPE_FLAG | (lit ? TERRAIN_LIGHT_FLAG : 0) | (shadowed ? surfaceShadowFlags() : 0);
            const ShaderProgram& shaderProgram = buildShaderProgram("tilesurfacedrape", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, flags | fogFlag());
            useProgram(shaderProgram);
            setupFogUniforms(shaderProgram);
            if (flags & TERRAIN_FLAG) {
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
                hasElevation = setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode);
            }
            if (lit) {
                setupTerrainLightingUniforms(shaderProgram, tileId, surfaceFrame);
            }
            if (shadowed) {
                setupSurfaceShadowUniforms(shaderProgram, surfaceFrame, hasElevation);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIt->second);
            glUniform1i(shaderProgram.uniforms[U_DRAPETEXTURE], 0);
            glUniform4f(shaderProgram.uniforms[U_DRAPEUVTRANSFORM], uvOffsetX, uvOffsetY, uvScale, uvScale);
            glUniform4f(shaderProgram.uniforms[U_COLOR], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            GLsizei drawnIndices = drawSurfaceElements(tileId, *tileSurface, gridMode);
            if (gridMode) {
                drawnIndices += drawTerrainSkirts(tileId, shaderProgram);
            }
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfDrapeDraws);
            VT_STAT_ADD(surfaceIndices, drawnIndices);
            surfaces++;

            glBindTexture(GL_TEXTURE_2D, 0);
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            checkGLError();
        }
        return surfaces;
    }

    void GLTileRenderer::renderTileWireframe(const TileId& tileId) {
        // Debug: the surface mesh as red edges, displaced like the rendered surfaces.
        bool gridMode = terrainGridSurfaces() && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        for (const std::shared_ptr<TileSurface>& tileSurface : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(tileId))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            if (compiledTileSurface.wireframeIndicesVBO == 0) {
                const VertexArray<std::uint16_t>& indices = tileSurface->getIndices();
                std::vector<std::uint16_t> lineIndices;
                lineIndices.reserve(indices.size() * 2);
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                    lineIndices.push_back(indices[i + 0]); lineIndices.push_back(indices[i + 1]);
                    lineIndices.push_back(indices[i + 1]); lineIndices.push_back(indices[i + 2]);
                    lineIndices.push_back(indices[i + 2]); lineIndices.push_back(indices[i + 0]);
                }
                glGenBuffers(1, &compiledTileSurface.wireframeIndicesVBO);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.wireframeIndicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, lineIndices.size() * sizeof(std::uint16_t), lineIndices.data(), GL_STATIC_DRAW);
                compiledTileSurface.wireframeIndicesCount = static_cast<GLsizei>(lineIndices.size());
            }
            if (compiledTileSurface.wireframeIndicesCount == 0) {
                continue;
            }

            unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider ? TERRAIN_VTF_FLAG : 0);
            const ShaderProgram& shaderProgram = buildShaderProgram("tilemask", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag);
            useProgram(shaderProgram);
            if (terrainFlag != 0) {
                setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.wireframeIndicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            Color color(1.0f, 0.0f, 0.0f, 1.0f);
            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            glDrawElements(GL_LINES, compiledTileSurface.wireframeIndicesCount, GL_UNSIGNED_SHORT, 0);

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }

    void GLTileRenderer::renderTileBorder(const TileId& tileId, const TileId& sourceTileId) {
        // Debug: the displaced tile outline, colour by zoom and brightness by parity so neighbours
        // differ. Drawn without depth: occluded tiles matter too.
        if (_tileBorderVBO == 0) {
            std::vector<float> border;
            border.reserve((TILE_BORDER_SEGMENTS * 4 + 1) * 3);
            auto push = [&border](float x, float y) { border.push_back(x); border.push_back(y); border.push_back(0.0f); };
            for (int i = 0; i < TILE_BORDER_SEGMENTS; i++) { push(static_cast<float>(i) / TILE_BORDER_SEGMENTS, 0.0f); }
            for (int i = 0; i < TILE_BORDER_SEGMENTS; i++) { push(1.0f, static_cast<float>(i) / TILE_BORDER_SEGMENTS); }
            for (int i = 0; i < TILE_BORDER_SEGMENTS; i++) { push(1.0f - static_cast<float>(i) / TILE_BORDER_SEGMENTS, 1.0f); }
            for (int i = 0; i < TILE_BORDER_SEGMENTS; i++) { push(0.0f, 1.0f - static_cast<float>(i) / TILE_BORDER_SEGMENTS); }
            push(0.0f, 0.0f);
            _tileBorderVertexCount = static_cast<GLsizei>(border.size() / 3);
            glGenBuffers(1, &_tileBorderVBO);
            glBindBuffer(GL_ARRAY_BUFFER, _tileBorderVBO);
            glBufferData(GL_ARRAY_BUFFER, border.size() * sizeof(float), border.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        bool gridMode = terrainGridSurfaces() && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = calculateTileMatrix(tileId, 1.0f);
        unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider ? TERRAIN_VTF_FLAG : 0);
        const ShaderProgram& shaderProgram = buildShaderProgram("tilemask", backgroundVsh, backgroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag);
        useProgram(shaderProgram);
        if (terrainFlag != 0) {
            setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode);
        }

        glBindBuffer(GL_ARRAY_BUFFER, _tileBorderVBO);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), bufferGLOffset(0));

        cglib::mat4x4<float> mvpMatrix = calculateTileMVPMatrix(tileId, 1.0f);
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

        static const float ZOOM_COLORS[6][3] = {
            { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.6f, 0.0f }, { 0.9f, 0.9f, 0.0f },
            { 0.0f, 0.9f, 0.2f }, { 0.0f, 0.6f, 1.0f }, { 0.8f, 0.0f, 1.0f }
        };
        const float* rgb = ZOOM_COLORS[((tileId.zoom % 6) + 6) % 6];
        float shade = ((tileId.x + tileId.y) & 1) != 0 ? 0.55f : 1.0f;
        // A stand-in (data from another tile) at half opacity, so tiles owning their pixels read solid.
        float opacity = (sourceTileId == tileId ? 1.0f : 0.5f);
        Color color(rgb[0] * shade * opacity, rgb[1] * shade * opacity, rgb[2] * shade * opacity, opacity);
        glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
        glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

        glLineWidth(2.0f); // drivers may clamp this to 1, in which case the outline is hairline
        glDrawArrays(GL_LINE_STRIP, 0, _tileBorderVertexCount);
        glLineWidth(1.0f);

        disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        checkGLError();
    }

    void GLTileRenderer::renderTileBackground(const TileId& tileId, float blend, float opacity, float tileSize, const std::shared_ptr<TileBackground>& background) {
        if (blend * opacity <= 0) {
            return;
        }
        Color backgroundColor = background->getColorFunc()(_viewState);
        if (!background->getPattern() && !backgroundColor.value()) {
            return;
        }
        // The background colour is a Map setting no symbolizer grades: graded here by the same rule.
        if (_backgroundEmissive < 1.0f) {
            std::array<float, 4> rgba = backgroundColor.rgba();
            for (int c = 0; c < 3; c++) {
                rgba[c] *= _backgroundEmissive + (1.0f - _backgroundEmissive) * _radiance(c);
            }
            backgroundColor = Color(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
        bool gridMode = terrainGridSurfaces() && terrainVTF;
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        // The bake is flat and orthographic: two triangles reproduce it exactly, where the displaced
        // grid is tens of thousands per layer per tile.
        for (const std::shared_ptr<TileSurface>& tileSurface : (flatDrape ? buildCompiledFlatSurfaces() : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(tileId)))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            // Flat drape pass: bake the background onto the flat [0,1] grid (no displacement).
            unsigned int terrainFlag = flatDrape ? 0 : (terrainVTF ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : (_terrainMode && !_terrainDepthWrite ? TERRAIN_FLAG : 0));
            const ShaderProgram& shaderProgram = buildShaderProgram("tilebackground", backgroundVsh, backgroundFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (background->getPattern() ? PATTERN_FLAG : 0) | terrainFlag | fogFlag() | coverageFlag());
            useProgram(shaderProgram);
            setupFogUniforms(shaderProgram);
            if ((terrainFlag & TERRAIN_FLAG) != 0) {
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
            }
            if (terrainVTF && !flatDrape) {
                setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, gridMode && !flatDrape);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
            if (background->getPattern()) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
            }
            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                } else {
                    setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = flatDrape ? (*_drapeMVPOverride) : (gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame));
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

            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, backgroundColor.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], blend * opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfBackgroundDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());

            if (_lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            }
            if (background->getPattern()) {
                glBindTexture(GL_TEXTURE_2D, 0);

                disableVertexAttrib(shaderProgram.attribs[A_VERTEXUV]);
            }
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

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

        // In the bake the raster renders FLAT with the ortho bake matrix; the uv matrix resolves
        // overzoom, so the bake frame is the plain target square.
        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
        bool gridMode = terrainGridSurfaces() && terrainVTF;
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(targetTileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        // Two triangles for the flat bake; see renderTileBackground.
        for (const std::shared_ptr<TileSurface>& tileSurface : (flatDrape ? buildCompiledFlatSurfaces() : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(targetTileId)))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            unsigned int terrainFlag = flatDrape ? 0 : (terrainVTF ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : (_terrainMode && !_terrainDepthWrite ? TERRAIN_FLAG : 0));
            // A raster drawn here covers the drape surface's ground, so it takes the same sun and
            // shadow, or it flashes unlit at every integer zoom out. Never in the bake.
            bool litBitmap = !flatDrape && !_terrainShadowMaskPass && terrainVTF && _terrainLighting.enabled && bitmap->getType() == TileBitmap::Type::COLORMAP;
            bool shadowedBitmap = litBitmap && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
            unsigned int lightFlags = (litBitmap ? TERRAIN_LIGHT_FLAG : 0) | (shadowedBitmap ? surfaceShadowFlags() : 0);
            const ShaderProgram* shaderProgramPtr = nullptr;
            switch (bitmap->getType()) {
            case TileBitmap::Type::COLORMAP:
                shaderProgramPtr = &buildShaderProgram("tilecolormap", colormapVsh, colormapFsh, LightingMode::GEOMETRY2D, _rasterFilterMode, PATTERN_FLAG | terrainFlag | lightFlags | fogFlag() | coverageFlag());
                break;
            case TileBitmap::Type::NORMALMAP:
                shaderProgramPtr = &buildShaderProgram("tilenormalmap", normalmapVsh, normalmapFsh, LightingMode::NORMALMAP, _rasterFilterMode, PATTERN_FLAG | terrainFlag | fogFlag() | coverageFlag());
                break;
            default:
                return;
            }
            const ShaderProgram& shaderProgram = *shaderProgramPtr;
            useProgram(shaderProgram);
            setupFogUniforms(shaderProgram);
            if ((terrainFlag & TERRAIN_FLAG) != 0) {
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
            }
            bool hasElevation = true;
            if (terrainVTF && !flatDrape) {
                hasElevation = setupTerrainUniforms(shaderProgram, targetTileId, surfaceFrame, gridMode && !flatDrape);
            }
            if (litBitmap) {
                setupTerrainLightingUniforms(shaderProgram, targetTileId, surfaceFrame);
            }
            if (shadowedBitmap) {
                setupSurfaceShadowUniforms(shaderProgram, surfaceFrame, hasElevation);
            }

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
            bindSurfaceSkirtAttrib(shaderProgram, vertexGeomLayoutParams);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
            if (bitmap->getType() == TileBitmap::Type::COLORMAP && _lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                } else {
                    setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
            } else if (bitmap->getType() == TileBitmap::Type::NORMALMAP && _lightingShaderNormalMap) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                } else {
                    setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
                }
                if (vertexGeomLayoutParams.binormalOffset >= 0) {
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL], 3, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.binormalOffset));
                } else {
                    setConstVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL], 0, 1, 0);
                }
                _lightingShaderNormalMap->setupFunc(shaderProgram.program, _viewState);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = flatDrape ? *_drapeMVPOverride : (gridMode ? calculateTileMVPMatrix(targetTileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame));
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            const CompiledBitmap& compiledTileBitmap = buildCompiledTileBitmap(bitmap);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledTileBitmap.texture);
            glUniform1i(shaderProgram.uniforms[U_BITMAP], 0);
            glUniform4f(shaderProgram.uniforms[U_UVSCALE], bitmap->getWidth(), bitmap->getHeight(), 1.0f / bitmap->getWidth(), 1.0f / bitmap->getHeight());

            cglib::mat3x3<float> uvMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(sourceTileId)) * calculateTileMatrix2D(targetTileId));
            uvMatrix = cglib::mat3x3<float>{ { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } * uvMatrix;
            glUniformMatrix3fv(shaderProgram.uniforms[U_UVMATRIX], 1, GL_FALSE, uvMatrix.data());

            glUniform1f(shaderProgram.uniforms[U_OPACITY], blend * opacity);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfBitmapDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());

            glBindTexture(GL_TEXTURE_2D, 0);

            if (bitmap->getType() == TileBitmap::Type::COLORMAP && _lightingShader2D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
            } else if (bitmap->getType() == TileBitmap::Type::NORMALMAP && _lightingShaderNormalMap) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
                if (vertexGeomLayoutParams.binormalOffset >= 0) {
                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL]);
                }
            }
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXUV]);
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

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
        // Buildings scaled to nothing: zero-area walls and a roof z-fighting the ground, so skip.
        if (geometry->getType() == TileGeometry::Type::POLYGON3D
            && buildingHeightScale(blend, !geometry->getSpanRecords().empty()) <= 0.0f) {
            return;
        }

        VT_STAT_CLOCK(statClock);
        VT_STAT_SPLIT(geomProbeNs, statClock);
        bool styleOffsetting = std::count(styleParams.offsetFuncs.begin(), styleParams.offsetFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleGapWidth = std::count(styleParams.gapWidthFuncs.begin(), styleParams.gapWidthFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleBlur = std::count(styleParams.blurFuncs.begin(), styleParams.blurFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleBorder = std::count(styleParams.borderWidthFuncs.begin(), styleParams.borderWidthFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        // The core pass stamps plain lines only; anything else in a draw-once group draws in the rim pass.
        if (_drawOncePass == DrawOncePass::CORE && (geometry->getType() != TileGeometry::Type::LINE || styleBorder)) {
            return;
        }

        // Flat drape pass: no displacement, no depth bias, tile-local ortho MVP from the caller.
        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool sphericalDrape = flatDrape && _transformer && _transformer->isSpherical(); // positions itself in the tile, in the shader
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider && !flatDrape;
        // All tile content in the 3D scene receives shadows, lines and points too (only fills drape).
        bool shadowReceiver = terrainVTF && !_shadowCasterViewProj && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        // ...and takes the sun: this is the only pass lighting undraped content. Extrusions light by
        // their own model.
        bool terrainLit = terrainVTF && !_shadowCasterViewProj && _terrainLighting.enabled && geometry->getType() != TileGeometry::Type::POLYGON3D;
        unsigned int lightFlag = terrainLit ? GEOMETRY_LIGHT_FLAG : 0;
        unsigned int terrainFlag = flatDrape
            ? (sphericalDrape ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0)
            : ((_terrainMode ? TERRAIN_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG : 0));
        const ShaderProgram* shaderProgramPtr = nullptr;
        switch (geometry->getType()) {
        case TileGeometry::Type::POINT:
            shaderProgramPtr = &buildShaderProgram("point", pointVsh, pointFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag());
            break;
        case TileGeometry::Type::LINE:
            shaderProgramPtr = &buildShaderProgram("line", lineVsh, lineFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | (styleGapWidth ? GAPWIDTH_FLAG : 0) | (styleBlur ? BLUR_FLAG : 0) | (_drawOncePass == DrawOncePass::CORE ? DRAW_ONCE_CORE_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag() | coverageFlag() | drapeMaskFlag() | (_spanResolver.isEnabled() && !geometry->getSpanRecords().empty() ? SPAN_FLAG : 0));
            break;
        case TileGeometry::Type::POLYGON:
            shaderProgramPtr = &buildShaderProgram("polygon", polygonVsh, polygonFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag() | coverageFlag() | drapeMaskFlag() | (_spanResolver.isEnabled() && !geometry->getSpanRecords().empty() ? SPAN_FLAG : 0));
            break;
        case TileGeometry::Type::POLYGON3DGROUND:
            // On the ground: terrain displacement and a draped line's clearance, so it sits ON the
            // surface. Unlit - it multiplies ground that is already lit.
            if (_shadowCasterViewProj || !_groundAOMaskPass) {
                return; // casts nothing, and is only ever drawn into its own mask
            }
            shaderProgramPtr = &buildShaderProgram("polygon3dground", polygon3DGroundVsh, polygon3DGroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag);
            break;
        case TileGeometry::Type::POLYGON3D:
            if (_shadowCasterViewProj) {
                // Caster: the same vertex shader, so the extrusion matches the drawn one.
                shaderProgramPtr = &buildShaderProgram("polygon3dshadow", polygon3DVsh, polygon3DShadowCasterFsh, LightingMode::NONE, RasterFilterMode::NONE, (styleParams.translate ? TRANSFORM_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG : 0));
                break;
            }
            // TERRAIN_FLAG: extrusions need draped content's base-clearance slack against the pre-pass.
            // SHADOW_SINGLE_TAP: the screen-space mask holds the ground's shadow, not theirs.
            {
                GLuint spanDrapeTexture = 0;
                cglib::vec4<float> spanDrapeTransform(0, 0, 1, 1);
                bool spanDrape = _spanResolver.isEnabled() && !geometry->getSpanRecords().empty() && resolveSpanDrape(targetTileId, spanDrapeTexture, spanDrapeTransform);
                shaderProgramPtr = &buildShaderProgram("polygon3d", polygon3DVsh, polygon3DFsh, LightingMode::GEOMETRY3D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG | TERRAIN_FLAG : 0) | (shadowReceiver ? shadowReceiverFlags() | SHADOW_SINGLE_TAP_FLAG | SHADOW_RECEIVER_3D_FLAG : 0) | (_spanResolver.isEnabled() && !geometry->getSpanRecords().empty() ? SPAN_FLAG : 0) | (spanDrape ? SPAN_DRAPE_FLAG : 0) | fogFlag());
                _pendingSpanDrape = spanDrape ? spanDrapeTexture : 0;
                _pendingSpanDrapeTransform = spanDrapeTransform;
                _pendingGroundDrape = spanDrape && resolveGroundDrape(targetTileId, _pendingGroundDrape, _pendingGroundDrapeTransform) ? _pendingGroundDrape : 0;
            }
            break;
        default:
            return;
        }
        const ShaderProgram& shaderProgram = *shaderProgramPtr;
        useProgram(shaderProgram);
        if (!_shadowCasterViewProj) {
            setupFogUniforms(shaderProgram);
        }
        VT_STAT_SPLIT(geomProgramNs, statClock);

        setupGeometryCommonUniforms(shaderProgram, sourceTileId, targetTileId, geometry, GeometryDrawMode { flatDrape, sphericalDrape, terrainVTF, shadowReceiver, terrainLit, terrainFlag });
        VT_STAT_SPLIT(geomTerrainNs, statClock);

        // An extrusion may sit out the tile fade: a style ramping its own opacity over zoom then owns
        // the appearance, instead of fading twice.
        float colorBlend = (geometry->getType() == TileGeometry::Type::POLYGON3D && !_buildingFadeOnAppear ? 1.0f : blend);
        // Scene light, one multiply per distinct cached colour: mapbox's mix(apply_lighting_ground(color),
        // color, emissive), a no-op at the default emissive 1.
        auto evaluateStyleColor = [&](const ColorFunction& colorFunc, int i) {
            Color color = Color::fromColorOpacity(evaluateColorFunc(colorFunc) * colorBlend, opacity);
            float emissive = evaluateFloatFunc(styleParams.emissiveFuncs[i]);
            if (emissive < 1.0f) {
                std::array<float, 4> rgba = color.rgba();
                for (int c = 0; c < 3; c++) {
                    rgba[c] *= emissive + (1.0f - emissive) * _radiance(c);
                }
                color = Color(rgba[0], rgba[1], rgba[2], rgba[3]);
            }
            return cglib::vec4<float>(color.rgba());
        };
        std::array<cglib::vec4<float>, TileGeometry::StyleParameters::MAX_PARAMETERS> colors, borderColors;
        for (int i = 0; i < styleParams.parameterCount; i++) {
            colors[i] = evaluateStyleColor(styleParams.colorFuncs[i], i);
        }
        // Lines only, for the border draw below.
        std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> fillWidths, fillGapWidths, borderWidths, borderGapWidths;
        VT_STAT_SPLIT(geomStyleEvalNs, statClock);
        VT_STAT_ADD(styleParameters, styleParams.parameterCount);

        if (geometry->getType() == TileGeometry::Type::POINT) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths, strokeWidths;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                float width = std::max(0.0f, evaluateFloatFunc(styleParams.widthFuncs[i])) * geometry->getGeometryScale() / tileSize;
                if (width <= 0) {
                    colors[i] = cglib::vec4<float>(0, 0, 0, 0);
                }
                widths[i] = width;

                // Text as geometry (text-clip) takes a label's halo units: antialias ramps, pointVsh
                // pushing the ramp centre out by twice its width, as labelFsh does.
                strokeWidths[i] = 2.0f * std::min(evaluateFloatFunc(styleParams.offsetFuncs[i]), MAX_HALO_PIXELS);
            }
            VT_STAT_SPLIT(geomStyleEvalNs, statClock);

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(strokeWidths.begin(), strokeWidths.begin() + styleParams.parameterCount, [](float strokeWidth) { return strokeWidth == 0; })) {
                    VT_STAT_INC(geometrySkips);
                    return;
                }
            }

            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], vertexGeomLayoutParams.coordScale / vertexGeomLayoutParams.binormalScale / std::pow(2.0f, _viewState.zoom - sourceTileId.zoom));
            // The text-as-geometry antialias ramp in encoded field units: renderLabelBatch's rule,
            // against this path's own size.
            glUniform1f(shaderProgram.uniforms[U_SDFSCALE], 2.0f * GLYPH_SDF_UNIT * static_cast<float>(styleParams.glyphRenderSize - GLYPH_RENDER_SPREAD) / _fullResolution);
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, widths.data());
            if (styleOffsetting) {
                glUniform1fv(shaderProgram.uniforms[U_STROKEWIDTHTABLE], styleParams.parameterCount, strokeWidths.data());
            }
        } else if (geometry->getType() == TileGeometry::Type::LINE) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths, offsets, gapWidths, blurs;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                float offset = 0.5f * _fullResolution * evaluateFloatFunc(styleParams.offsetFuncs[i]) * geometry->getGeometryScale() / tileSize;
                offsets[i] = offset;
                // Half the gap, in the half-width units below: the fragment shader cuts everything inside.
                gapWidths[i] = 0.5f * (0.5f * _fullResolution * std::abs(evaluateFloatFunc(styleParams.gapWidthFuncs[i])) * geometry->getGeometryScale() / tileSize);
                // A LENGTH, not a half-width: the ramp spans it whole, so no halving here.
                blurs[i] = 0.5f * _fullResolution * std::abs(evaluateFloatFunc(styleParams.blurFuncs[i])) * geometry->getGeometryScale() / tileSize;

                // Check for 0-width function. This is used only for polygons.
                if (styleParams.widthFuncs[i] == FloatFunction(0)) {
                    widths[i] = -1;
                }
                else {
                    float width = 0.5f * _fullResolution * std::abs(evaluateFloatFunc(styleParams.widthFuncs[i])) * geometry->getGeometryScale() / tileSize;
                    if (width < 1.0f) {
                        colors[i] = colors[i] * width; // should do gamma correction here, but simple implementation gives closer results to Mapnik
                        width = (width > 0.0f ? 1.0f : 0.0f); // normalize width
                    }
                    // A gapped line is mapbox's casing: a FULL width beyond the half-gap on each side,
                    // or the casing comes out half as thick as the browser draws it.
                    widths[i] = gapWidths[i] > 0.0f ? gapWidths[i] + width : width * 0.5f;
                }
                if (styleBorder) {
                    // maplibre's line-border-width: pixels on each side outside the line, and the gap
                    // shrinks by it so a casing keeps both edges. Clamped: negative means none.
                    float borderHalf = 0.5f * _fullResolution * std::max(0.0f, evaluateFloatFunc(styleParams.borderWidthFuncs[i])) * geometry->getGeometryScale() / tileSize;
                    borderWidths[i] = (borderHalf > 0.0f && widths[i] > 0.0f ? widths[i] + borderHalf : 0.0f);
                    borderGapWidths[i] = std::max(0.0f, gapWidths[i] - borderHalf);
                    borderColors[i] = evaluateStyleColor(styleParams.borderColorFuncs[i], i);
                }
            }
            VT_STAT_SPLIT(geomStyleEvalNs, statClock);

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(styleParams.widthFuncs.begin(), styleParams.widthFuncs.begin() + styleParams.parameterCount, [](const FloatFunction& func) { return func != FloatFunction(0); })) { // check that all are proper lines, not polygons
                    VT_STAT_INC(geometrySkips);
                    return;
                }
            }

            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], vertexGeomLayoutParams.coordScale / (_halfResolution * vertexGeomLayoutParams.binormalScale * std::pow(2.0f, _viewState.zoom - sourceTileId.zoom)));
            glUniform1f(shaderProgram.uniforms[U_ANTIALIASSCALE], _lineAntialiasScale);
            if (terrainVTF) {
                // Screen-space extrusion over terrain (lineVsh): the aspect converts NDC x into y's
                // units, and a width unit is 1/halfResolution of NDC height, as on the flat map.
                glUniform2f(shaderProgram.uniforms[U_SCREENSCALE], std::max(0.0001f, _viewState.aspect), 1.0f / std::max(1.0f, _halfResolution));
            }
            // Undoes the int16 binormal packing: line widths per vertex - 1 plain, more for a miter, a
            // round cap corner or an arrow barb. With uHeightScale it caps an inner corner (lineVsh).
            glUniform1f(shaderProgram.uniforms[U_BINORMALUNITSCALE], 1.0f / vertexGeomLayoutParams.binormalScale);
            glUniform1f(shaderProgram.uniforms[U_HEIGHTSCALE], vertexGeomLayoutParams.heightOffset >= 0 ? vertexGeomLayoutParams.coordScale / vertexGeomLayoutParams.heightScale : 0.0f);
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, widths.data());
            if (styleOffsetting) {
                glUniform1fv(shaderProgram.uniforms[U_OFFSETTABLE], styleParams.parameterCount, offsets.data());
            }
            if (styleGapWidth) {
                glUniform1fv(shaderProgram.uniforms[U_GAPWIDTHTABLE], styleParams.parameterCount, gapWidths.data());
            }
            if (styleBlur) {
                glUniform1fv(shaderProgram.uniforms[U_BLURTABLE], styleParams.parameterCount, blurs.data());
            }
            if (styleBorder) {
                fillWidths = widths;
                fillGapWidths = gapWidths;
            }

            if (styleParams.pattern) {
                std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> strokeScales;
                for (int i = 0; i < styleParams.parameterCount; i++) {
                    float strokeScale = (styleParams.strokeScales[i] > 0.0f ? STROKE_UV_SCALE / styleParams.pattern->bitmap->width / styleParams.strokeScales[i] / 127.0f / (_fullResolution / tileSize) : 0.0f);
                    strokeScales[i] = strokeScale * std::pow(2.0f, std::floor(_viewState.zoom) - _viewState.zoom);
                }
                glUniform1fv(shaderProgram.uniforms[U_STROKESCALETABLE], styleParams.parameterCount, strokeScales.data());
            }
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) {
            glUniform1f(shaderProgram.uniforms[U_HEIGHTSCALE], buildingHeightScale(blend) / vertexGeomLayoutParams.heightScale * vertexGeomLayoutParams.coordScale);
            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], 1.0f / vertexGeomLayoutParams.binormalScale);
            glUniform2f(shaderProgram.uniforms[U_GROUNDAOPARAMS], _groundAOIntensity * (_groundAOBakePass ? 1.0f : groundAOZoomFade(_viewState.zoom)), _groundAOAttenuation);
            // Same tile clip the walls get - see polygon3DGroundFsh for why it is not optional.
            glUniform1f(shaderProgram.uniforms[U_UVSCALE], 1.0f / vertexGeomLayoutParams.texCoordScale);
            cglib::mat3x3<float> groundTileMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(targetTileId)) * calculateTileMatrix2D(sourceTileId));
            glUniformMatrix3fv(shaderProgram.uniforms[U_TILEMATRIX], 1, GL_FALSE, groundTileMatrix.data());
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
            float heightUnits = 1.0f / vertexGeomLayoutParams.heightScale * vertexGeomLayoutParams.coordScale;
            bool spanDeck = !geometry->getSpanRecords().empty();
            glUniform1f(shaderProgram.uniforms[U_UVSCALE], 1.0f / vertexGeomLayoutParams.texCoordScale);
            glUniform1f(shaderProgram.uniforms[U_HEIGHTSCALE], buildingHeightScale(blend, spanDeck) * heightUnits);
            glUniform1f(shaderProgram.uniforms[U_SHADOWHEIGHTSCALE], casterHeightScale(blend, spanDeck) * heightUnits);
            glUniform1f(shaderProgram.uniforms[U_FLOATINGBASE], spanDeck ? 1.0f : 0.0f);
            if (_pendingSpanDrape != 0) {
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, _pendingSpanDrape);
                glUniform1i(shaderProgram.uniforms[U_SPANDRAPETEXTURE], 4); // units 0-3 are taken (see renderTileGeometry)
                glUniform4fv(shaderProgram.uniforms[U_SPANDRAPETRANSFORM], 1, _pendingSpanDrapeTransform.data());
                cglib::vec3<float> drapeLight = spanDrapeLight();
                glUniform3fv(shaderProgram.uniforms[U_SPANDRAPELIGHT], 1, drapeLight.data());
                if (_pendingGroundDrape != 0) {
                    // Unit 6: 5 is the elevation node texture; sharing it displaced the deck's vertices.
                    glActiveTexture(GL_TEXTURE6);
                    glBindTexture(GL_TEXTURE_2D, _pendingGroundDrape);
                    glUniform1i(shaderProgram.uniforms[U_GROUNDDRAPETEXTURE], 6);
                    glUniform4fv(shaderProgram.uniforms[U_GROUNDDRAPETRANSFORM], 1, _pendingGroundDrapeTransform.data());
                }
                glUniform1f(shaderProgram.uniforms[U_GROUNDDRAPE], _pendingGroundDrape != 0 ? 1.0f : 0.0f);
                // Overhang height still counted as ground (the quay, not the water), internal z (polygon3DFsh).
                double tileY = 0.5 - (targetTileId.y + 0.5) / (1 << targetTileId.zoom);
                glUniform1f(shaderProgram.uniforms[U_SPANGROUNDTOLERANCE], static_cast<float>(SPAN_GROUND_TOLERANCE_METRES * _metersToInternal * std::cosh(6.283185307179586 * tileY)));
                glActiveTexture(GL_TEXTURE0);
            }
            cglib::mat3x3<float> tileMatrix = cglib::mat3x3<float>::convert(cglib::inverse(calculateTileMatrix2D(targetTileId)) * calculateTileMatrix2D(sourceTileId));
            if (styleParams.translate) {
                float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
                cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
                tileMatrix = tileMatrix * cglib::translate3_matrix(cglib::vec3<float>(translate(0), translate(1), 1));
            }
            glUniformMatrix3fv(shaderProgram.uniforms[U_TILEMATRIX], 1, GL_FALSE, tileMatrix.data());
        }

        auto allTransparent = [&](const std::array<cglib::vec4<float>, TileGeometry::StyleParameters::MAX_PARAMETERS>& table) {
            return std::all_of(table.begin(), table.begin() + styleParams.parameterCount, [](const cglib::vec4<float>& color) {
                return std::all_of(color.cbegin(), color.cend(), [](float val) { return val < 1.0f / 256.0f; });
            });
        };
        if (allTransparent(colors) && (!styleBorder || allTransparent(borderColors))) {
            VT_STAT_SPLIT(geomStyleNs, statClock);
            VT_STAT_INC(geometrySkips);
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
            // Which slots are patterned: one polygon geometry mixes plain and patterned fills.
            glUniform1fv(shaderProgram.uniforms[U_PATTERNTABLE], styleParams.parameterCount, styleParams.patternScales.data());

            const CompiledBitmap& compiledBitmap = buildCompiledBitmap(styleParams.pattern->bitmap, geometry->getType() != TileGeometry::Type::LINE);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glUniform1i(shaderProgram.uniforms[U_PATTERN], 0);
        }
        VT_STAT_SPLIT(geomStyleNs, statClock);

        const CompiledGeometry* compiledGeometryPtr = buildCompiledTileGeometry(geometry);
        if (!compiledGeometryPtr) {
            if (styleParams.pattern) {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return;
        }
        const CompiledGeometry& compiledGeometry = *compiledGeometryPtr;
        VT_STAT_SPLIT(geomCompileNs, statClock);
        bindGeometryVertexLayout(shaderProgram, geometry, compiledGeometry);

        if (geometry->getType() != TileGeometry::Type::POLYGON3D && _lightingShader2D) {
            _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D && _lightingShader3D) {
            _lightingShader3D->setupFunc(shaderProgram.program, _viewState);
            // ...which uploaded the map's building-emissive. The program is shared, so the rule's own
            // value or the map's is written on every draw.
            if (shaderProgram.uniforms[U_EMISSIVE] >= 0) {
                float emissive = (styleParams.polygon3DEmissiveFunc ? evaluateFloatFunc(*styleParams.polygon3DEmissiveFunc) : _buildingEmissive);
                glUniform1f(shaderProgram.uniforms[U_EMISSIVE], emissive);
            }
        }
        VT_STAT_SPLIT(geomBindNs, statClock);

        // The same buffer, extruded wider by the vertex shader. Drawn for the WHOLE batch first,
        // which is mapbox's casing-layer-under-fill-layer order and what keeps a junction clean.
        if (styleBorder) {
            glUniform4fv(shaderProgram.uniforms[U_COLORTABLE], styleParams.parameterCount, borderColors[0].data());
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, borderWidths.data());
            if (styleGapWidth) {
                glUniform1fv(shaderProgram.uniforms[U_GAPWIDTHTABLE], styleParams.parameterCount, borderGapWidths.data());
            }
            glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(geometryDraws);
            VT_STAT_ADD(geometryIndices, geometry->getIndicesCount());

            glUniform4fv(shaderProgram.uniforms[U_COLORTABLE], styleParams.parameterCount, colors[0].data());
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, fillWidths.data());
            if (styleGapWidth) {
                glUniform1fv(shaderProgram.uniforms[U_GAPWIDTHTABLE], styleParams.parameterCount, fillGapWidths.data());
            }
        }

        // mapbox's translucent line (draw_line.ts): the core stamps the stencil, so a pixel a cap or a
        // crossing already covered is not blended twice; the rim pass then adds what is left.
        bool corePass = (_drawOncePass == DrawOncePass::CORE);
        if (corePass) {
            glStencilMask(DRAW_ONCE_STENCIL_BIT);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        }

        glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
        VT_STAT_SPLIT(geomDrawNs, statClock);
        VT_STAT_INC(geometryDraws);
        VT_STAT_ADD(geometryIndices, geometry->getIndicesCount());

        if (corePass) {
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilMask(0);
        }

        unbindGeometryVertexLayout(shaderProgram, geometry, compiledGeometry);

        if (styleParams.pattern) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        checkGLError();
    }

    void GLTileRenderer::setupGeometryCommonUniforms(const ShaderProgram& shaderProgram, const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry, const GeometryDrawMode& mode) {
        const TileGeometry::StyleParameters& styleParams = geometry->getStyleParameters();
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();

        cglib::mat4x4<float> mvpMatrix;
        if (_shadowCasterViewProj) {
            mvpMatrix = cglib::mat4x4<float>::convert((*_shadowCasterViewProj) * calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        } else if (mode.flatDrape) {
            // coords / coordScale = tile-local [0,1], which the override maps to clip. On a sphere the
            // shader supplies the tile unit (drapeBakeClip), so the override is the whole matrix.
            cglib::mat4x4<float> local = cglib::scale4_matrix(cglib::vec3<float>(1.0f / vertexGeomLayoutParams.coordScale, 1.0f / vertexGeomLayoutParams.coordScale, 1.0f));
            mvpMatrix = mode.sphericalDrape ? *_drapeMVPOverride : (*_drapeMVPOverride) * local;
        } else {
            mvpMatrix = calculateTileMVPMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
        }
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        if (mode.terrainFlag != 0 && geometry->getType() != TileGeometry::Type::POLYGON3D) {
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], mode.terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
        } else if (mode.terrainVTF && !_shadowCasterViewProj && geometry->getType() == TileGeometry::Type::POLYGON3D) {
            // Only VTF has a surface to clear; the caster renders from the light, never biased to the camera.
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
        }
        if (mode.terrainVTF || mode.sphericalDrape) {
            // Elevation from the TARGET tile (the ground under the content), vertex frame from the
            // SOURCE tile; swapped, content sat on another DEM level and slid during a pan.
            setupTerrainUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        } else {
            // A globe curves vertices without terrain, and the tile clip must follow (18-globe.md).
            setupSphericalUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        }
        if (mode.shadowReceiver) {
            cglib::mat4x4<double> shadowFrame = calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
            std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
            for (int i = 0; i < _terrainShadowCascades; i++) {
                shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * shadowFrame);
            }
            glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], _terrainShadowCascades, GL_FALSE, shadowMatrices[0].data());
            setupShadowNormalOffsetUniforms(shaderProgram, shadowFrame);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
            glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
            glActiveTexture(GL_TEXTURE0);
            glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), _terrainShadowStrength, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
            glUniform3f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBias(0), _terrainShadowBias(1), _terrainShadowBias(2));
        glUniform4f(shaderProgram.uniforms[U_SHADOWDEPTHSCALE], _terrainShadowDepthScales[0], _terrainShadowDepthScales[1], _terrainShadowDepthScales[2], _terrainShadowDepthScales[3]);
            setupShadowFadeRangeUniform(shaderProgram);
        }
        // -1 on a program built without DRAPE_MASK: nothing to bind.
        if (_drapeMaskTexture != 0 && shaderProgram.uniforms[U_DRAPEMASKTEXTURE] >= 0) {
            // Unit 3: 0 is the pattern, 1 the elevation texture, 2 the shadow map.
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, _drapeMaskTexture);
            glUniform1i(shaderProgram.uniforms[U_DRAPEMASKTEXTURE], 3);
            glActiveTexture(GL_TEXTURE0);
            glUniform4f(shaderProgram.uniforms[U_DRAPEMASKUVTRANSFORM], _drapeMaskUVTransform(0), _drapeMaskUVTransform(1), _drapeMaskUVTransform(2), _drapeMaskUVTransform(3));
        }
        if (mode.shadowReceiver || mode.terrainLit) {
            // Undraped 2D content takes N.L from the TERRAIN (terrainNdl in commonFsh), so shadow and
            // sun need the slope scale and sun direction. Undeclared uniforms are -1, a no-op.
            setupTerrainLightingUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        }

        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            cglib::mat4x4<float> transformMatrix = _transformer->calculateTileTransform(sourceTileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
            glUniformMatrix4fv(shaderProgram.uniforms[U_TRANSFORMMATRIX], 1, GL_FALSE, transformMatrix.data());
        }
    }

    GLuint GLTileRenderer::findGeometryVAO(const CompiledGeometry& compiledGeometry, GLuint program) {
        for (const std::pair<GLuint, GLuint>& programVAO : compiledGeometry.geometryVAOs) {
            if (programVAO.first == program) {
                return programVAO.second;
            }
        }
        return 0;
    }

    void GLTileRenderer::bindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry) {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        bool lit = _lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D;

        GLuint geometryVAO = findGeometryVAO(compiledGeometry, shaderProgram.program);
        bool freshVAO = false;
        if (geometryVAO == 0) {
            glGenVertexArrays(1, &geometryVAO);
            if (geometryVAO != 0) {
                compiledGeometry.geometryVAOs.emplace_back(shaderProgram.program, geometryVAO);
                freshVAO = true;
            }
        }
        if (geometryVAO != 0) {
            glBindVertexArray(geometryVAO);
        }
        if (geometryVAO == 0 || freshVAO) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);

            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS], 4, GL_BYTE, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.attribsOffset));
            }

            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.texCoordOffset));
            }

            if (lit && vertexGeomLayoutParams.normalOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
            }

            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.binormalOffset));
            }

            if (vertexGeomLayoutParams.heightOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXHEIGHT], 1, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.heightOffset));
            }

            if (vertexGeomLayoutParams.baseOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXBASE], 1, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.baseOffset));
            }

            if (vertexGeomLayoutParams.chordOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXCHORD], 1, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.chordOffset));
            }

        }

        if (!(vertexGeomLayoutParams.attribsOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS], 0, 0, 0, 0);
        }

        if (lit && !(vertexGeomLayoutParams.normalOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
        }

        if (geometry->getType() == TileGeometry::Type::LINE && !(vertexGeomLayoutParams.heightOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXHEIGHT], 0, 0, 0); // no inner-corner caps
        }
    }

    void GLTileRenderer::unbindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry) {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        bool lit = _lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D;

        if (findGeometryVAO(compiledGeometry, shaderProgram.program) != 0) {
            glBindVertexArray(0);
        } else {

            if (vertexGeomLayoutParams.chordOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXCHORD]);
            }

            if (vertexGeomLayoutParams.baseOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXBASE]);
            }

            if (vertexGeomLayoutParams.heightOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXHEIGHT]);
            }

            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL]);
            }

            if (lit && vertexGeomLayoutParams.normalOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
            }

            if (vertexGeomLayoutParams.texCoordOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXUV]);
            }

            if (vertexGeomLayoutParams.attribsOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS]);
            }

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);
        }

        // Always, VAO or not: the compile path binds buffers on VAO 0, and a name left there outlives
        // the geometry for the next renderer drawing from VAO 0.
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

        // fwidth() is core in ESSL 3.00; the 1.00 fallback enables GL_OES_standard_derivatives (commonFsh).
        bool useDerivatives = true;

        const CompiledBitmap& compiledBitmap = buildCompiledBitmap(bitmap, false);
        // Anchors elevated on the GPU by the surface's applyTerrain (mapbox symbol.vertex.glsl); without
        // a texture provider the CPU height is used.
        unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider && labelBatchParams.tileId.zoom >= 0 ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0);
        const ShaderProgram& shaderProgram = buildShaderProgram("labels", labelVsh, labelFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (useDerivatives ? DERIVATIVES_FLAG : 0) | terrainFlag | fogFlag());
        useProgram(shaderProgram);
        setupFogUniforms(shaderProgram);
        if (terrainFlag) {
            // The anchors' own frame, so applyTerrain(aVertexPosition) needs no conversion.
            setupTerrainUniforms(shaderProgram, labelBatchParams.tileId, cglib::translate4_matrix(_viewState.origin), false);
        }

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_viewState.projectionMatrix * labelBatchParams.labelMatrix);
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

        // One screen pixel of antialias ramp in encoded field units: an em is (glyphRenderSize -
        // GLYPH_RENDER_SPREAD) texels over size * scale * _fullResolution / 2 pixels.
        float glyphEmTexels = static_cast<float>(labelBatchParams.glyphRenderSize - GLYPH_RENDER_SPREAD);
        glUniform1f(shaderProgram.uniforms[U_SDFRAMP], 2.0f * GLYPH_SDF_UNIT * glyphEmTexels / (labelBatchParams.scale * _fullResolution));
        // Camera axes for shader-side billboarding (labelVsh); the label matrix is camera-relative.
        glUniform3f(shaderProgram.uniforms[U_LABELAXISX], _viewState.orientation[0](0), _viewState.orientation[0](1), _viewState.orientation[0](2));
        glUniform3f(shaderProgram.uniforms[U_LABELAXISY], _viewState.orientation[1](0), _viewState.orientation[1](1), _viewState.orientation[1](2));
        glUniform4fv(shaderProgram.uniforms[U_COLORTABLE], labelBatchParams.parameterCount, labelBatchParams.colorTable[0].data());
        glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], labelBatchParams.parameterCount, labelBatchParams.widthTable.data());
        glUniform1fv(shaderProgram.uniforms[U_STROKEWIDTHTABLE], labelBatchParams.parameterCount, labelBatchParams.strokeWidthTable.data());
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.verticesVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelVertices.size() * 3 * sizeof(float), _labelVertices.data(), GL_DYNAMIC_DRAW);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, 0, 0);

        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.offsetsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelOffsets.size() * 3 * sizeof(float), _labelOffsets.data(), GL_DYNAMIC_DRAW);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXOFFSET], 3, GL_FLOAT, GL_FALSE, 0, 0);

        if (_lightingShader2D) {
            glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.normalsVBO);
            glBufferData(GL_ARRAY_BUFFER, _labelNormals.size() * 3 * sizeof(float), _labelNormals.data(), GL_DYNAMIC_DRAW);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 3, GL_FLOAT, GL_FALSE, 0, 0);

            _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.texCoordsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelTexCoords.size() * 2 * sizeof(std::int16_t), _labelTexCoords.data(), GL_DYNAMIC_DRAW);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXUV], 2, GL_SHORT, GL_FALSE, 0, 0);

        glBindBuffer(GL_ARRAY_BUFFER, compiledLabelBatch.attribsVBO);
        glBufferData(GL_ARRAY_BUFFER, _labelAttribs.size() * 4 * sizeof(std::int8_t), _labelAttribs.data(), GL_DYNAMIC_DRAW);
        enableVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS], 4, GL_BYTE, GL_FALSE, 0, 0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledLabelBatch.indicesVBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, _labelIndices.size() * sizeof(std::uint16_t), _labelIndices.data(), GL_DYNAMIC_DRAW);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
        glUniform1i(shaderProgram.uniforms[U_BITMAP], 0);
        glUniform2f(shaderProgram.uniforms[U_UVSCALE], 1.0f / bitmap->width, 1.0f / bitmap->height);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_labelIndices.size()), GL_UNSIGNED_SHORT, 0);
        VT_STAT_INC(labelDraws);

        glBindTexture(GL_TEXTURE_2D, 0);

        disableVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS]);
        
        disableVertexAttrib(shaderProgram.attribs[A_VERTEXUV]);

        if (_lightingShader2D) {
            disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
        }
        
        disableVertexAttrib(shaderProgram.attribs[A_VERTEXOFFSET]);

        disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        _labelVertices.clear();
        _labelOffsets.clear();
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

            // Mipmaps only for POT dimensions.
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

    const GLTileRenderer::CompiledGeometry* GLTileRenderer::buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry) {
        // A style change can repoint features at another slot: a byte rewrite, not a decode. Done
        // before the lookup, so a first compile uploads the repointed data.
        tileGeometry->applyStyleState();

        auto it = _compiledTileGeometryMap.find(tileGeometry.get());
        if (it != _compiledTileGeometryMap.end() && it->second.owner.expired()) {
            // The old geometry died and a NEW one got its address: the pointer matches, the VBOs do
            // not. A live owner proves the entry is this geometry's, an expired one proves not.
            VT_STAT_INC(geomCompileStale);
            deleteCompiledGeometry(it->second.geometry);
            _compiledTileGeometryMap.erase(it);
            it = _compiledTileGeometryMap.end();
        }
        if (it == _compiledTileGeometryMap.end()) {
            // The CPU copy is freed once uploaded (one renderer per geometry assumed), but a terrain
            // toggle rebuilds the renderer and keeps the tiles; empty arrays give a storeless buffer.
            if (tileGeometry->getIndicesCount() > 0 && tileGeometry->getIndices().empty()) {
                return nullptr;
            }
            VT_STAT_INC(geomCompileMisses);
            CompiledGeometry compiledGeometry;
            createCompiledGeometry(compiledGeometry);

            glBindBuffer(GL_ARRAY_BUFFER, compiledGeometry.vertexGeometryVBO);
            glBufferData(GL_ARRAY_BUFFER, tileGeometry->getVertexGeometry().size() * sizeof(std::uint8_t), tileGeometry->getVertexGeometry().data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledGeometry.indicesVBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileGeometry->getIndices().size() * sizeof(std::uint16_t), tileGeometry->getIndices().data(), GL_STATIC_DRAW);

            if (!_interactionMode) {
                tileGeometry->releaseVertexArrays(); // if interaction is enabled, we must keep the vertex arrays. Otherwise optimize for lower memory usage
            }
            tileGeometry->clearDirtyVertexBytes(); // the upload above already carries them

            it = _compiledTileGeometryMap.emplace(tileGeometry.get(), OwnedCompiledGeometry { tileGeometry, compiledGeometry }).first;
        }
        else if (const std::optional<std::pair<std::size_t, std::size_t>>& dirtyBytes = tileGeometry->getDirtyVertexBytes()) {
            // A feature was repointed at another style slot: re-upload only the bytes it touched.
            glBindBuffer(GL_ARRAY_BUFFER, it->second.geometry.vertexGeometryVBO);
            glBufferSubData(GL_ARRAY_BUFFER, dirtyBytes->first, dirtyBytes->second - dirtyBytes->first, tileGeometry->getVertexGeometry().data() + dirtyBytes->first);
            tileGeometry->clearDirtyVertexBytes();
        }
        return &it->second.geometry;
    }

    void GLTileRenderer::bindSurfaceSkirtAttrib(const ShaderProgram& shaderProgram, const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams) {
        // Present on a SPHERE only, where a skirt's drop cannot be folded into the vertex z.
        if (vertexGeomLayoutParams.skirtOffset >= 0) {
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXSKIRT], 1, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.skirtOffset));
        } else {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXSKIRT], 0, 0, 0);
        }
    }

    const GLTileRenderer::ShaderProgram& GLTileRenderer::buildShaderProgram(const char* id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags) {
        // Every program is ESSL 3.00: set here once, before the cache key is built.
        flags |= ESSL3_FLAG;

        // A globe curves every vertex, DEM or not, so this does not wait for the terrain flag.
        if (_transformer && _transformer->isSpherical()) {
            flags |= TERRAIN_SPHERICAL_FLAG;
        }

        // Fast path: the call site's literal pointer + flags, no allocation; a miss builds the string key.
        ShaderProgramKey cacheKey { id, flags, static_cast<int>(lightingMode), static_cast<int>(filterMode) };
        auto cacheIt = _shaderProgramCache.find(cacheKey);
        if (cacheIt != _shaderProgramCache.end()) {
            return *cacheIt->second;
        }

        std::string shaderProgramId = std::string(id) + (flags ? std::to_string(flags) : std::string());
        if (lightingMode != LightingMode::NONE) {
            shaderProgramId += "_l" + std::to_string(static_cast<int>(lightingMode));
        }
        if (filterMode != RasterFilterMode::NONE) {
            shaderProgramId += "_f" + std::to_string(static_cast<int>(filterMode));
        }

        auto it = _shaderProgramMap.find(shaderProgramId);
        if (it == _shaderProgramMap.end()) {
            std::set<std::string> defs;
            for (const std::pair<unsigned int, std::string>& flagDefine : flagDefineMap) {
                if (flags & flagDefine.first) {
                    defs.insert(flagDefine.second);
                }
            }
            // Central, so every terrain program gets it; read once at startup, so the key need not carry it.
            if ((flags & TERRAIN_VTF_FLAG) && _terrainDemTaps <= 1) {
                defs.insert("DEM_HW_FILTER");
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
            else if (lightingMode == LightingMode::TERRAINPAINT && _lightingShaderNormalMap && !_lightingShaderNormalMap->perVertex) {
                defs.insert("LIGHTING_FSH");
                defs.insert("DERIVATIVES");
                // The normal-map lighting shader over a prelude reading the shared DEM, so built-in
                // hillshades and custom shaders (getElevation/getMapZoom) both work.
                lightingFsh = terrainPaintPrelude + _lightingShaderNormalMap->shader;
            }
            else if (lightingMode == LightingMode::NORMALMAP && _lightingShaderNormalMap) {
                defs.insert(_lightingShaderNormalMap->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                // fwidth for antialiased contour lines; harmless when contours are off.
                defs.insert("DERIVATIVES");
                if (_lightingShaderNormalMap->perVertex) {
                    lightingVsh = _lightingShaderNormalMap->shader;
                } else {
                    // The shared DEM prelude provides getElevation()/getMapZoom()/sampleElevation().
                    lightingFsh = normalmapCustomPrelude + _lightingShaderNormalMap->shader;
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

            std::string fogCommonFsh = commonFsh;
            fogCommonFsh.replace(fogCommonFsh.find(FOG_HELPERS_PLACEHOLDER), FOG_HELPERS_PLACEHOLDER.size(), fogHelpersFsh);
            fogCommonFsh.replace(fogCommonFsh.find(FOG_BLEND_PLACEHOLDER), FOG_BLEND_PLACEHOLDER.size(), _fogShaderSource.empty() ? fogBlendFsh : _fogShaderSource);

            ShaderProgram shaderProgram;
            std::string fullVsh = commonVsh + lightingVsh + vsh;
            std::string fullFsh = fogCommonFsh + lightingFsh + filterFsh + fsh;
            try {
                createShaderProgram(shaderProgram, fullVsh, fullFsh, defs, uniformMap, attribMap);
            } catch (const std::exception& ex) {
                // A driver rejecting ESSL 3.00, or a 1.00-only app shader in it, must not take the map
                // down: the 1.00 path is complete, only slower.
                if (defs.count("ESSL3") == 0) {
                    throw;
                }
                _essl3Failed = true; // the owner logs it once; vt has no logger of its own
                std::set<std::string> fallbackDefs = defs;
                fallbackDefs.erase("ESSL3");
                fallbackDefs.erase("SHADOW_HW");
                createShaderProgram(shaderProgram, fullVsh, fullFsh, fallbackDefs, uniformMap, attribMap);
            }

            it = _shaderProgramMap.emplace(shaderProgramId, shaderProgram).first;
        }
        _shaderProgramCache[cacheKey] = &it->second;
        return it->second;
    }

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledFlatSurfaces() {
        if (_terrainFlatSurfaces.empty()) {
            if (std::shared_ptr<TileSurface> surface = _tileSurfaceBuilder.buildRegularGridSurface(1)) {
                _terrainFlatSurfaces.push_back(std::move(surface));
            }
        }
        for (const std::shared_ptr<TileSurface>& tileSurface : _terrainFlatSurfaces) {
            CompiledSurface& compiledSurface = _compiledTileSurfaceMap[tileSurface];
            if (compiledSurface.indicesVBO == 0) {
                createCompiledSurface(compiledSurface);
                glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
                glBufferData(GL_ARRAY_BUFFER, tileSurface->getVertexGeometry().size() * sizeof(std::uint8_t), tileSurface->getVertexGeometry().data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileSurface->getIndices().size() * sizeof(std::uint16_t), tileSurface->getIndices().data(), GL_STATIC_DRAW);
            }
        }
        return _terrainFlatSurfaces;
    }

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledTerrainShadowGridSurfaces() {
        int resolution = std::min(_terrainRegularGridResolution, SHADOW_GRID_MAX_RESOLUTION);
        if (resolution == _terrainRegularGridResolution) {
            return buildCompiledTerrainGridSurfaces();
        }
        if (_terrainShadowGridResolution != resolution) {
            _terrainShadowGridResolution = resolution;
            _terrainShadowGridSurfaces.clear();
        }
        if (_terrainShadowGridSurfaces.empty()) {
            if (std::shared_ptr<TileSurface> surface = _tileSurfaceBuilder.buildRegularGridSurface(resolution)) {
                _terrainShadowGridSurfaces.push_back(std::move(surface));
            }
        }
        for (const std::shared_ptr<TileSurface>& tileSurface : _terrainShadowGridSurfaces) {
            CompiledSurface& compiledSurface = _compiledTileSurfaceMap[tileSurface];
            if (compiledSurface.indicesVBO == 0) {
                createCompiledSurface(compiledSurface);

                glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
                glBufferData(GL_ARRAY_BUFFER, tileSurface->getVertexGeometry().size() * sizeof(std::uint8_t), tileSurface->getVertexGeometry().data(), GL_STATIC_DRAW);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileSurface->getIndices().size() * sizeof(std::uint16_t), tileSurface->getIndices().data(), GL_STATIC_DRAW);
            }
        }
        return _terrainShadowGridSurfaces;
    }

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledTerrainGridSurfaces() {
        // One shared unit-grid surface for every tile (per-tile MVP + terrain uniforms).
        if (_terrainGridSurfaces.empty()) {
            if (std::shared_ptr<TileSurface> surface = _tileSurfaceBuilder.buildRegularGridSurface(_terrainRegularGridResolution)) {
                _terrainGridSurfaces.push_back(std::move(surface));
            }
        }
        for (const std::shared_ptr<TileSurface>& tileSurface : _terrainGridSurfaces) {
            CompiledSurface& compiledSurface = _compiledTileSurfaceMap[tileSurface];
            if (compiledSurface.indicesVBO == 0) {
                createCompiledSurface(compiledSurface);

                glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
                glBufferData(GL_ARRAY_BUFFER, tileSurface->getVertexGeometry().size() * sizeof(std::uint8_t), tileSurface->getVertexGeometry().data(), GL_STATIC_DRAW);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileSurface->getIndices().size() * sizeof(std::uint16_t), tileSurface->getIndices().data(), GL_STATIC_DRAW);
            }
        }
        return _terrainGridSurfaces;
    }

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledTerrainGridSkirtSurfaces() {
        if (_terrainGridSkirtSurfaces.empty()) {
            if (std::shared_ptr<TileSurface> surface = _tileSurfaceBuilder.buildRegularGridSkirtSurface(_terrainRegularGridResolution)) {
                _terrainGridSkirtSurfaces.push_back(std::move(surface));
            }
        }
        for (const std::shared_ptr<TileSurface>& tileSurface : _terrainGridSkirtSurfaces) {
            CompiledSurface& compiledSurface = _compiledTileSurfaceMap[tileSurface];
            if (compiledSurface.indicesVBO == 0) {
                createCompiledSurface(compiledSurface);

                glBindBuffer(GL_ARRAY_BUFFER, compiledSurface.vertexGeometryVBO);
                glBufferData(GL_ARRAY_BUFFER, tileSurface->getVertexGeometry().size() * sizeof(std::uint8_t), tileSurface->getVertexGeometry().data(), GL_STATIC_DRAW);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledSurface.indicesVBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, tileSurface->getIndices().size() * sizeof(std::uint16_t), tileSurface->getIndices().data(), GL_STATIC_DRAW);
            }
        }
        return _terrainGridSkirtSurfaces;
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
        // ESSL 3.00 from one set of sources via a few keyword #defines; 1.00 is the per-program
        // fallback. The glFragColor rename is not required (tangram's #define form) but harmless.
        bool essl3 = defs.count("ESSL3") > 0;
        auto compileShader = [&defs, essl3](GLenum type, const std::string& sh) -> GLuint {
            std::string shaderSourceStr = essl3 ? "#version 300 es\n" : "#version 100\n";
            for (const std::string& def : defs) {
                shaderSourceStr += "#define " + def + "\n";
            }
            if (essl3) {
                shaderSourceStr += "#define texture2D texture\n#define texture2DLod textureLod\n";
                if (type == GL_VERTEX_SHADER) {
                    shaderSourceStr += "#define attribute in\n#define varying out\n";
                } else {
                    shaderSourceStr += "#define varying in\nout mediump vec4 glFragColor;\n";
                }
            } else if (type == GL_FRAGMENT_SHADER) {
                shaderSourceStr += "#define glFragColor gl_FragColor\n";
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
                // The line number is into the concatenated source, which nothing on disk matches: quote it.
                std::size_t colon = msg.find(':');
                std::size_t colon2 = colon == std::string::npos ? std::string::npos : msg.find(':', colon + 1);
                int line = 0;
                if (colon2 != std::string::npos) {
                    line = std::atoi(msg.c_str() + colon2 + 1); // "ERROR: 0:376:" - the line is after the SECOND colon
                }
                if (line > 0) {
                    std::size_t pos = 0;
                    for (int i = 1; i < std::max(1, line - 2) && pos != std::string::npos; i++) {
                        pos = shaderSourceStr.find('\n', pos);
                        if (pos != std::string::npos) {
                            pos++;
                        }
                    }
                    std::size_t end = pos;
                    for (int i = 0; i < 5 && end != std::string::npos; i++) {
                        end = shaderSourceStr.find('\n', end);
                        if (end != std::string::npos) {
                            end++;
                        }
                    }
                    if (pos != std::string::npos) {
                        msg += " | source near line " + std::to_string(line) + ": " + shaderSourceStr.substr(pos, (end == std::string::npos ? shaderSourceStr.size() : end) - pos);
                    }
                }
                std::string defList;
                for (const std::string& def : defs) {
                    defList += " " + def;
                }
                throw std::runtime_error("Shader compiling failed: " + msg + " | defines:" + defList);
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

        if (useDepth && useStencil) {
            GLuint depthStencilRB = 0;
            glGenRenderbuffers(1, &depthStencilRB);
            glBindRenderbuffer(GL_RENDERBUFFER, depthStencilRB);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _screenWidth, _screenHeight);
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
        if (compiledSurface.wireframeIndicesVBO != 0) {
            glDeleteBuffers(1, &compiledSurface.wireframeIndicesVBO);
            compiledSurface.wireframeIndicesVBO = 0;
            compiledSurface.wireframeIndicesCount = 0;
        }
    }

    void GLTileRenderer::createCompiledGeometry(CompiledGeometry& compiledGeometry) {
        glGenBuffers(1, &compiledGeometry.vertexGeometryVBO);
        glGenBuffers(1, &compiledGeometry.indicesVBO);
    }
    
    void GLTileRenderer::deleteCompiledGeometry(CompiledGeometry& compiledGeometry) {
        for (const std::pair<GLuint, GLuint>& programVAO : compiledGeometry.geometryVAOs) {
            GLuint geometryVAO = programVAO.second;
            glDeleteVertexArrays(1, &geometryVAO);
        }
        compiledGeometry.geometryVAOs.clear();
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
        glGenBuffers(1, &compiledLabelBatch.offsetsVBO);
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
        if (compiledLabelBatch.offsetsVBO != 0) {
            glDeleteBuffers(1, &compiledLabelBatch.offsetsVBO);
            compiledLabelBatch.offsetsVBO = 0;
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
}
