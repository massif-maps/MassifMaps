#include "GLTileRenderer.h"
#include "GLTileRendererShaders.h"
#include "Color.h"
#include "TileGeometryIterator.h"
#include "TileSurfaceBuilder.h"
#include "BitmapManager.h"
#include "LabelCuller.h"
#include "RenderStats.h"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

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

    // Attribute setup that tolerates an attribute the linker dropped. glGetAttribLocation returns
    // -1 for those, and unlike a uniform location of -1 (legal, ignored) a vertex attribute index
    // of -1 is GL_INVALID_VALUE - which on a translated GL shows up as
    // "GL error 0x501 condition [indx >= CODEC_MAX_VERTEX_ATTRIBUTES]" and leaves the draw's
    // attribute state half configured. The shadow caster programs are exactly this case: their
    // fragment shader only writes depth, so uv/normal/attribs are optimised out of the program.
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
    // How far shadows reach, as a multiple of the camera-to-focus distance. mapbox's model verbatim
    // (3d-style/render/shadow_renderer.ts: cameraToCenterDistance * 1.5 * 3.0). A METRIC radius
    // cannot hold at two zooms - the budget this replaces was 10 m x mapSize, i.e. ~10 km at every
    // camera, which ended a mountain's shadow one screen away at z12.
    static constexpr double SHADOW_CUTOUT_DISTANCE_FACTOR = 4.5;
    // The cascade ladder steps by this between pages, anchored on the cutout, so two cascades split
    // at cutout/3 - mapbox's cascadeSplitDist = cameraToCenterDistance * 1.5 against a cutout of
    // 4.5x, exactly.
    static constexpr double SHADOW_CASCADE_STEP = 3.0;
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
        } else if (resolution != _terrainRegularGridResolution) {
            _terrainRegularGridResolution = resolution;
            _terrainGridSurfaces.clear(); // rebuilt lazily; the old compiled VBO is released in endFrame
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

    void GLTileRenderer::setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const std::array<float, MAX_SHADOW_CASCADES>& depthBiases, float strength, float softness, bool depthTexture, bool hardwarePCF, float normalOffset, const cglib::vec3<float>& sunDir, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainShadowTexture = texture;
        _terrainShadowMapSize = mapSize;
        _terrainShadowCascades = std::max(1, std::min(MAX_SHADOW_CASCADES, cascades));
        _terrainShadowBiases = depthBiases;
        _terrainShadowStrength = strength;
        _terrainShadowSoftness = softness;
        _terrainShadowDepthTexture = depthTexture;
        _terrainShadowHardwarePCF = hardwarePCF;
        _terrainShadowNormalOffset = normalOffset;
        _terrainShadowSunDir = sunDir;
        _terrainShadowViewProjs = lightViewProjs;
        // Pages beyond the cascade count do not exist in the atlas, and the receiver lookup is
        // compiled for the count (see shadowReceiverFlags), so those slots are never uploaded and
        // never sampled - which is what keeps CLAMP_TO_EDGE from reading a neighbouring page.
    }

    void GLTileRenderer::setFog(const Color& color, float startDistance, float distance, float rangeScale, float horizonBlend) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogColor = color;
        _fogRangeScale = std::max(1.0e-9f, rangeScale);
        _fogStartDistance = startDistance / _fogRangeScale;
        _fogDistance = distance / _fogRangeScale;
        // The term divides by it, and 0 would mean "no fog in the sky at all" rather than a sharp
        // edge at the horizon - mapbox's own floor.
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
        // The blend is compiled into every program, so they all have to go. Called from the GL
        // thread (TileRenderer::onDrawFrame), which is what makes deleting them here safe.
        for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); it++) {
            deleteShaderProgram(it->second);
        }
        _shaderProgramMap.clear();
        _shaderProgramCache.clear();
        resetProgramState();
    }

    bool GLTileRenderer::calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float distanceFactor, double cameraDistance, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (tileIds.empty()) {
            texelMeters = -1; // diagnostic: which fit bail-out fired
            return false;
        }
        // World-space box of the shadowed ground. The height range comes from the elevation
        // texture's metres-to-internal factor: a fixed metric slab is meaningless in internal
        // units, which change scale with the projection.
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool first = true;
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
        // ANY tile with an elevation texture will do - the metres-to-internal factor is a property
        // of the projection, not of the tile. Asking only tileIds.front() made the whole shadow
        // pass fail whenever that one tile happened to have no decoded DEM yet: the provider is
        // CACHED_ONLY, so a far tile that has not been decoded returns nothing, and the result was
        // every shadow on screen disappearing at once. The more the view is tilted the more distant
        // tiles are in the cover, so the more often it happened.
        double metersToInternal = 0;
        for (const TileId& tileId : tileIds) {
            const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
            if (resolved.first && resolved.second.metersToInternal > 0) {
                metersToInternal = resolved.second.metersToInternal;
                break;
            }
        }
        if (metersToInternal <= 0) {
            // No DEM anywhere - a flat 2D map. The factor is the projection's, not the tile's, so
            // the caller states it and the whole fit works with no elevation: buildings then cast
            // on a flat map exactly as they do on terrain.
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
        // The box for this cascade is the MINIMUM BOUNDING SPHERE of its slice of the view frustum,
        // which is mapbox's model verbatim (3d-style/render/shadow_renderer.ts createLightMatrix,
        // "rotation invariant shadow volume"). Its radius is a function of the slice distances and
        // the field of view ALONE - not of the pitch, the bearing or the sun azimuth - so one texel
        // is the same size at every camera. That is the whole point, and it is why mapbox's shadows
        // look identical at a low tilt and straight down where a ground-footprint fit does not: a
        // wedge of visible ground stretches towards the horizon as the view flattens, and its extent
        // in LIGHT space also swings with the sun, so the texel size followed both.
        double sphereRadius = 0;
        cglib::vec3<double> sphereCenter(0, 0, 0);
        {
            // The cutout is a distance from the CAMERA, in multiples of the camera-to-focus
            // distance. That distance follows the zoom alone, so the same factor holds from a city
            // to a massif.
            // The camera-to-focus distance is passed IN, not read from _viewState: that field is
            // filled by TileRenderer::onDrawFrame, which runs after the shadow pass, so the fit
            // would be reading the previous frame's value or none at all.
            double cutout = (cameraDistance > 0 ? (distanceFactor > 0 ? distanceFactor : SHADOW_CUTOUT_DISTANCE_FACTOR) * cameraDistance : 0);
            if (!(cutout > 0)) {
                texelMeters = -4; // no camera-to-focus distance: nothing to fit a slice to
                return false;
            }
            // Slices step by SHADOW_CASCADE_STEP from the cutout, so two cascades split at
            // cutout/3 - mapbox's 1.5x / 4.5x of the camera distance exactly.
            auto sliceFarAt = [&](int index) {
                double f = cutout;
                for (int i = index; i + 1 < cascadeCount; i++) {
                    f /= SHADOW_CASCADE_STEP;
                }
                return f;
            };
            double sliceNear = (cascade > 0 ? sliceFarAt(cascade - 1) : 0.0);
            double sliceFar = sliceFarAt(cascade);
            // k is the tangent of the half-angle to a frustum CORNER, taken from the projection
            // matrix so it needs no field-of-view plumbing: m(0,0) = 1/tan(fovX/2), m(1,1) = 1/tan(fovY/2).
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
            // The slab narrowing and the caster prefilter below work on a world rectangle; the
            // sphere's own bounds are that rectangle, intersected with the drawn tiles.
            double trimMinX = std::max(minX, sphereCenter(0) - sphereRadius), trimMaxX = std::min(maxX, sphereCenter(0) + sphereRadius);
            double trimMinY = std::max(minY, sphereCenter(1) - sphereRadius), trimMaxY = std::min(maxY, sphereCenter(1) + sphereRadius);
            if (trimMaxX > trimMinX && trimMaxY > trimMinY) {
                minX = trimMinX; maxX = trimMaxX;
                minY = trimMinY; maxY = trimMaxY;
            }
        }

        // Narrow the height slab to the ground THIS cascade covers. The slab is what the light
        // box has to span along its vertical axis - at a low sun the whole scene's relief, three
        // kilometres of it here, sets the box size no matter how small the near cascade's ground
        // footprint is, and every cascade then ends up with the same coarse texels.
        double casterMinZ = minZ, casterMaxZ = maxZ; // the slab the CASTERS live in, before it is
                                                     // narrowed to this cascade's own ground
        if (tileHeights.size() == tileIds.size()) {
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
            // Never narrower than a fraction of the whole slab: a tile whose elevation has not
            // loaded reports an empty range, and collapsing the box onto that would put it at a
            // height the terrain is not at. The floor is ENFORCED by widening the local range,
            // not by refusing to narrow at all: refusing meant a near cascade sitting in a valley
            // kept the whole mountain range's slab, and since a low sun stretches the box by that
            // slab divided by tan(altitude), that one condition set the texel size for every
            // cascade. Ground that still falls outside a narrowed box is not lost - the cascades
            // are nested, so it is shadowed by the next one out.
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
        // Things STAND on the terrain. The slab so far is the DEM's, and 3D extrusions reach well
        // above it: a building whose roof is above maxZ lands in front of the light box's near
        // plane, so it is clipped out of the caster pass (it throws no shadow) and, as a receiver,
        // its own fragments fall outside every cascade page and come out unshadowed. Both symptoms
        // read as "shadows stopped applying to buildings". The headroom that used to hide this was
        // a PERCENTAGE of the relief - fine over a 2 km massif, nothing over flat ground - so it is
        // metric here. The lateral fit needs it too, not just the depth range: at a low sun a
        // receiver's light-space position is displaced sideways by its own height.
        {
            double standingHeadroom = 200.0 * metersToInternal;
            maxZ += standingHeadroom;
            casterMaxZ += standingHeadroom;
        }

        cglib::vec3<double> dir = cglib::unit(cglib::vec3<double>(sunDir(0), sunDir(1), sunDir(2)));
        if (dir(2) < 0.05) {
            texelMeters = -6; // diagnostic: which fit bail-out fired
            return false; // sun at or below the horizon: nothing is meaningfully lit
        }
        cglib::vec3<double> up = std::abs(dir(2)) > 0.99 ? cglib::vec3<double>(0, 1, 0) : cglib::vec3<double>(0, 0, 1);
        // The light view is a pure ROTATION about the world origin, not a look-at aimed at the
        // current box centre. A view anchored on the box would move with the camera, and the whole
        // light-space grid would slide under the terrain every frame - shadow edges then crawl and
        // shimmer during a pan, and no two frames share a matrix, so the caster pass can never be
        // reused. Anchored to the world, the texel lattice below is absolute.
        cglib::mat4x4<double> lightView = cglib::lookat4_matrix(dir, cglib::vec3<double>(0, 0, 0), up);

        // SIDES are the bounding sphere, DEPTH comes from the ground that CASTS: a sphere projects
        // to the same square from every direction, which is what makes the texel size independent
        // of the pitch, the bearing and the sun azimuth. Fitting the sides to the casters as well
        // would let every margin tile coarsen every texel.
        cglib::vec4<double> lightCenter = cglib::transform(cglib::vec4<double>(sphereCenter(0), sphereCenter(1), sphereCenter(2), 1.0), lightView);
        double l = lightCenter(0) - sphereRadius, r = lightCenter(0) + sphereRadius;
        double b = lightCenter(1) - sphereRadius, t = lightCenter(1) + sphereRadius;
        double n = 0, f = 0;
        {
            // The depth range still comes from the slab the receivers live in; the caster loop below
            // widens it to whatever actually casts into this box.
            bool fitFirst = true;
            for (int corner = 0; corner < 4; corner++) {
                for (int level = 0; level < 2; level++) {
                    double x = (corner & 1 ? maxX : minX), y = (corner & 2 ? maxY : minY);
                    cglib::vec4<double> p = cglib::transform(cglib::vec4<double>(x, y, level ? maxZ : minZ, 1.0), lightView);
                    if (fitFirst) {
                        n = f = -p(2);
                        fitFirst = false;
                    } else {
                        n = std::min(n, -p(2)); f = std::max(f, -p(2));
                    }
                }
            }
        }
        // Snap the box to a world-anchored lattice of whole shadow texels, and quantise its size so
        // the texel size itself only changes in steps. Fitted exactly, the box breathes with every
        // camera movement: the same piece of ground falls in a different texel each frame, so every
        // shadow edge crawls and the interior of a large shadow flickers. Snapped, the matrix is
        // bit-identical while the camera moves inside one step - which both stabilises the image and
        // lets the caller skip the caster pass entirely.
        auto snapAxis = [mapSize](double& lo, double& hi, bool depthAxis) {
            double size = hi - lo;
            if (!(mapSize > 0) || !(size > 0)) {
                return;
            }
            // A ladder of eighths of a power of two: at most 12.5% of the box is wasted, and the
            // step changes rarely enough that the texel size is stable in practice.
            double step = std::pow(2.0, std::floor(std::log2(size)) - 3.0);
            double quantSize = std::ceil(size / step) * step;
            // The depth axis has no texels; quantising it keeps the depth range - and with it the
            // shader's normalised bias - constant while the camera moves.
            double grid = (depthAxis ? step : quantSize / mapSize);
            if (quantSize - size < grid) {
                quantSize += step; // snapping moves the low edge down by up to one grid cell
                grid = (depthAxis ? step : quantSize / mapSize);
            }
            lo = std::floor(lo / grid) * grid;
            hi = lo + quantSize;
        };
        // The SIDES are snapped before the casters are culled against them, so the cull can use the
        // final box and a one-texel margin. Culling against the unsnapped box needed a slop of 20%
        // of its width to cover the growth, which on the outer cascade is kilometres of ground and
        // dozens of tiles drawn into a page they cannot reach.
        snapAxis(l, r, false);
        snapAxis(b, t, false);
        double marginX = (r - l) / std::max(1, mapSize), marginY = (t - b) / std::max(1, mapSize);
        for (const TileId& tileId : casterTileIds) {
            cglib::mat4x4<double> tileMatrix = calculateTileMatrix(tileId, 1.0f);
            double tileL = 0, tileR = 0, tileB = 0, tileT = 0, tileN = 0, tileF = 0;
            for (int corner = 0; corner < 8; corner++) {
                cglib::vec4<double> local(corner & 1 ? 1.0 : 0.0, corner & 2 ? 1.0 : 0.0, 0.0, 1.0);
                cglib::vec4<double> world = cglib::transform(local, tileMatrix);
                // The CASTER slab, not this cascade's narrowed one: a mountain outside the
                // cascade's own ground still casts into it, and measuring it against a slab it
                // does not reach leaves the box's near plane in front of it - the caster is then
                // clipped away and its shadow is missing over the whole cascade.
                world(2) = (corner & 4 ? casterMaxZ : casterMinZ);
                cglib::vec4<double> p = cglib::transform(world, lightView);
                if (corner == 0) {
                    tileL = tileR = p(0); tileB = tileT = p(1); tileN = tileF = -p(2);
                } else {
                    tileL = std::min(tileL, p(0)); tileR = std::max(tileR, p(0));
                    tileB = std::min(tileB, p(1)); tileT = std::max(tileT, p(1));
                    tileN = std::min(tileN, -p(2)); tileF = std::max(tileF, -p(2));
                }
            }
            // Light-space xy is constant along a light ray, so a tile whose xy does not overlap
            // the box cannot cast into it however tall it is - the tile's own box is taken over the
            // CASTER slab, so the throw of a distant mountain is already in it. Skipping the rest
            // keeps the depth range - and with it the resolution of the normalised bias - tied to
            // what really casts, and it is also the list of tiles the caster pass has to draw for
            // this cascade: a near cascade covers a fraction of the tiles, and drawing the rest
            // into it is pure cost.
            if (tileR < l - marginX || tileL > r + marginX || tileT < b - marginY || tileB > t + marginY) {
                continue;
            }
            n = std::min(n, tileN); f = std::max(f, tileF);
            boxCasterTileIds.push_back(tileId);
        }
        snapAxis(n, f, true);
        lightViewProj = cglib::ortho4_matrix(l, r, b, t, n, f) * lightView;
        // The depth the box spans, in metres. The shader's bias is a fraction of the normalised
        // depth, so a bias that is constant there grows in WORLD terms as the box grows - which is
        // why a shadow drifted away from its own building as the view zoomed out or the caster
        // margin widened the box. The caller divides its metric bias by this to cancel that.
        depthRangeMeters = (f - n) / metersToInternal;
        // The ground one shadow texel covers, in metres: the number that decides whether a shadow
        // edge reads as an edge or as a staircase. Reported so the caller can log it instead of
        // guessing from the picture.
        texelMeters = std::max(r - l, t - b) / std::max(1, mapSize) / metersToInternal;
        return true;
    }

    template <typename Func>
    void GLTileRenderer::forEachVisibleExtrusion(const std::vector<TileId>* coveredBy, Func&& func) const {
        if (!_visibleRenderTiles) {
            return;
        }
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            if (coveredBy) {
                bool covered = false;
                for (const TileId& tileId : *coveredBy) {
                    if (tileCovers(renderTile.targetTileId, tileId)) {
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

    int GLTileRenderer::renderShadowCasters(const std::vector<TileId>& tileIds, const cglib::mat4x4<double>& lightViewProj, bool castGround) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!(_terrainRegularGrid && _terrainMode && _terrainTextureProvider)) {
            return 0;
        }
        int draws = 0;
        // ONE program and ONE vertex buffer for every tile's ground: the grid surface is shared,
        // so binding it per tile was a hundred redundant state changes per pass - and on a
        // translated GL (emulator, ANGLE) the call count, not the triangle count, is the cost.
        if (castGround) {
            for (const std::shared_ptr<TileSurface>& tileSurface : buildCompiledTerrainGridSurfaces()) {
                const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
                const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

                const ShaderProgram& shaderProgram = buildShaderProgram("shadowcaster", backgroundVsh, shadowCasterFsh, LightingMode::NONE, RasterFilterMode::NONE, TERRAIN_VTF_FLAG);
                useProgram(shaderProgram);
                glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

                for (const TileId& tileId : tileIds) {
                    cglib::mat4x4<double> surfaceFrame = calculateTileMatrix(tileId, 1.0f);
                    cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(lightViewProj * surfaceFrame);
                    // No elevation yet: the tile would be drawn as a flat plane at sea level, which
                    // is not the terrain it stands for and is not what the receiver uses either (a
                    // tile without elevation receives no shadow at all).
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

        // 3D extrusions cast too: buildings on the terrain, and on each other. They are the one
        // kind of tile content that is real 3D rather than a skin on the ground, so they are
        // exactly what the drape cannot represent and what a shadow map is for.
        // Cast from both faces: culling the front faces stored the far side of the building
        // and detached its shadow from its own footprint. The acne that motivated it is
        // handled by the slope-scaled caster offset, which the tightened light frustum made
        // effective again.
        _shadowCasterViewProj = &lightViewProj;
        _shadowCasterSun = true;
        forEachVisibleExtrusion(&tileIds, [this, &draws](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>& geometry) {
            renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
            draws++;
            return true;
        });
        _shadowCasterSun = false;
        _shadowCasterViewProj = nullptr;
        return draws;
    }

    float GLTileRenderer::shadowCasterFadeSignature() const {
        std::lock_guard<std::mutex> lock(_mutex);

        // A number that moves exactly as fast as the caster SET does: a tile's blend is how far it
        // has arrived, so this tracks extrusions appearing and leaving. The owner refreshes the
        // shadow map when it has moved far enough to see, instead of every frame of every fade
        // (a full caster pass each time) or never (the shadow of a building that is not there).
        // The caster's HEIGHT no longer follows the blend - see buildingHeightScale.
        float signature = 0.0f;
        int count = 0;
        forEachVisibleExtrusion(nullptr, [&signature, &count](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>&) {
            signature += renderLayer.blend;
            count++;
            return false; // one contribution per layer, not per geometry batch
        });
        // The MEAN, not the sum: twenty tiles fading in together move a sum twenty times as fast
        // as one does, and the map would be redrawn on every frame of exactly the moment this is
        // meant to protect. A single tile fading alone moves the mean by less than the step and
        // rides on the age cap instead.
        //
        // Times the caster's own height scale: a style that ramps its extrusions to nothing over
        // zoom (Standard, 0 at z15) changes the caster geometry without touching a single tile
        // blend, so the map was never refreshed and the buildings' shadows stayed on an empty map
        // after they had gone.
        return count > 0 ? _buildingHeightScale * signature / count : 0.0f;
    }

    float GLTileRenderer::groundAOZoomFade(float zoom) {
        // A contact shadow is a couple of metres wide, so below zoom 17 it is a sub-pixel rim on a
        // view holding the most buildings - all cost, nothing visible. Faded rather than switched,
        // or a whole city's shadows appear between one frame and the next.
        return std::max(0.0f, std::min(1.0f, zoom - GROUND_AO_MIN_ZOOM));
    }

    void GLTileRenderer::setGroundAO(float intensity, float attenuation) {
        std::lock_guard<std::mutex> lock(_mutex);

        _groundAOIntensity = intensity;
        _groundAOAttenuation = attenuation;
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

    bool GLTileRenderer::isGroundAOBakeable() const {
        std::lock_guard<std::mutex> lock(_mutex);

        // NO zoom fade, unlike the screen-space pass: a bake is cached and only redone when the
        // tile's CONTENT changes, so anything the camera moves must stay out of it. Faded, the
        // tiles baked while a launch animation was still below the fade's own zoom kept no shadow
        // at all - for as long as they stayed cached, which is until a zoom rebuilds them.
        return hasGroundAOTiles(1.0f);
    }

    bool GLTileRenderer::hasGroundAOTiles(float fade) const {
        if (!_visibleRenderTiles || !(_groundAOIntensity * fade > 0.0f)) {
            return false;
        }
        // ...and something to actually draw. Binding the mask target and clearing it costs 1.4 ms
        // on an Adreno 610 whether or not a capsule follows, which is what a style with an AO
        // intensity but no ground radius - or a camera above the buildings - was paying for.
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

        // MIN, into a mask cleared to white. Where two capsules meet - a corner, a building and its
        // building:part, two neighbours - the pixel takes the darkest of them rather than their
        // product. Resolving it here is the whole reason the pass exists: multiplied straight into
        // the frame, every one of those overlaps compounds towards black.
        glDisable(GL_CULL_FACE); // a capsule quad's winding follows its edge; both sides count

        // Seed the mask's own depth with the terrain cover, as the 3D overlay does. Without it the
        // capsule of a building hidden behind a ridge still reached the mask, and the screen
        // multiply then laid that shadow on the slope IN FRONT of it - a second copy of the
        // building's contact shadow, sliding against the ground as the camera pans.
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
        // ...and the EXTRUSIONS, which in a city are the occluder that actually matters: at any
        // tilt most of a building's base rim is hidden behind the building in front of it, and a
        // depth-less mask laid that rim on the front building's wall - the shadow "in the air".
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

        // LEQUAL: a capsule meets its own wall exactly at the base line, and GL_LESS eats the rim
        // right where it is darkest.
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

    void GLTileRenderer::setLabelElevationProvider(std::function<double(const cglib::vec3<double>&)> provider) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelElevationProvider = std::move(provider);
    }

    void GLTileRenderer::invalidateLabelElevation() {
        std::lock_guard<std::mutex> lock(_mutex);

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

    void GLTileRenderer::setExtrusionElevationProvider(std::function<bool(const cglib::vec3<double>&, double&)> provider) {
        std::lock_guard<std::mutex> lock(_mutex);

        _extrusionElevationProvider = std::move(provider);
        invalidateExtrusionBases();
    }

    void GLTileRenderer::invalidateExtrusionBases() {
        // A counter rather than the labels' per-tile list: setVertexBase is a no-op when the height
        // has not moved, so a needless re-resolve costs the elevation queries and uploads nothing.
        _extrusionBaseVersion.fetch_add(1, std::memory_order_relaxed);
    }

    void GLTileRenderer::updateTerrainSkirts() {
        // Tile border skirts are DISABLED: their walls (textured with stretched tile
        // edge/background pixels) rasterize over neighbouring tile content wherever a
        // displaced tile edge leans off-nadir, showing as stable background-colored
        // patches that grow with the tile size (the skirt drop) when zooming out.
        // Same-level tile borders are seam-free via the shared elevation texture
        // borders instead, and residual cross-LOD cracks are far less objectionable
        // than the skirt walls (tangram has no skirts either).
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
    }

    void GLTileRenderer::setRendererLayerIndexRange(const std::optional<std::pair<int, int>>& range) {
        std::lock_guard<std::mutex> lock(_mutex);

        _rendererLayerIndexRange = range;
    }

    void GLTileRenderer::setNoDrapeLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);

        _noDrapeLayerFilter = filter;
    }

    void GLTileRenderer::setClickHandlerLayerFilter(const std::optional<std::regex>& filter) {
        std::lock_guard<std::mutex> lock(_mutex);

        _clickHandlerLayerFilter = filter;
    }

    // Device pixels per line-width unit: line widths are in unscaled-DPI units, so on a dense
    // screen one unit is worth more than one pixel and the antialias ramp - one unit wide - blurs
    // a thin line. The host knows the real pixel size of the viewport; vt only has the normalized
    // resolution. 1 = the old behaviour.
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
        VT_STAT_INC(viewStateChanges);
    }

    float GLTileRenderer::evaluateFloatFunc(const FloatFunction& func) {
        // A constant carries no function object and is already a plain load - only the
        // expression-backed ones are worth a map lookup (see the cache declaration).
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
        VT_STAT_CLOCK(visibleClock);
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(setVisibleTilesLockNs, visibleClock);

        VT_STAT_INC(visibleTileSetChanges);
        _visibleTileIds = tileIds;
        buildTerrainEdgeCoarsening();
        VT_STAT_SPLIT(terrainCoarseningNs, visibleClock);
        buildTileSurfaces(tileIds);
        VT_STAT_SPLIT(tileSurfacesNs, visibleClock);
        buildLabelMaps(labelTiles);
        VT_STAT_SPLIT(labelMapsNs, visibleClock);
        buildRenderTiles(tiles);
        VT_STAT_SPLIT(renderTilesNs, visibleClock);
    }

    const std::set<TileId>& GLTileRenderer::terrainSurfaceTileIds() const {
        // The surfaces are drawn from the cover the owner hands in whenever there is one: under a
        // cross-layer drape the shared surface is drawn for the drape cover (normalised leaves,
        // not any single layer's tiles), and a paint draws itself on the terrain's own cover -
        // it has no tiles of its own at all.
        return (_terrainCoverTileIds.empty() ? _visibleTileIds : _terrainCoverTileIds);
    }

    void GLTileRenderer::buildTerrainEdgeCoarsening() {
        // Per visible tile: how much coarser the neighbour on each edge is. The shared grid
        // surface is drawn for every tile, so a coarser neighbour interpolates the DEM between
        // its own (2^k times wider) lattice nodes; the fine tile must chord across the same
        // nodes on that edge or the shared edge cracks open. The lattices only line up when
        // the resolution is a multiple of the level difference, which caps k.
        _terrainEdgeCoarseningMap.clear();
        if (!(_terrainEdgeStitching && _terrainRegularGrid)) {
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

        // Targeted version of resetTileSurfaces: only the surfaces built over one of the
        // given (elevation) tiles are dropped. A full reset re-tesselates and re-uploads
        // every visible tile surface, which during the initial elevation stream means the
        // whole screen is rebuilt again and again while nothing on it actually changed.
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

        // Release shaders
        for (auto it = _shaderProgramMap.begin(); it != _shaderProgramMap.end(); it++) {
            deleteShaderProgram(it->second);
        }
        _shaderProgramMap.clear();
        _shaderProgramCache.clear();

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
            deleteCompiledGeometry(it->second.geometry);
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

        // Release drape FBO and textures
        deleteDrapeResources();

        // Release tile and screen VBOs
        deleteCompiledQuad(_screenQuad);
        
        _renderTiles.reset();
        _visibleRenderTiles.reset();
        for (int pass = 0; pass < 2; pass++) {
            _passLabels[pass].reset();
            _visiblePassLabels[pass].reset();
        }
        _labels.clear();
        _layerLabelMap.clear();
    }
    
    bool GLTileRenderer::startFrame(float dt) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw
        warmTerrainRasterShader();

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
        VT_STAT_CLOCK(prepClock);
        float dBlend = (_layerBlendingSpeed > 0.0f ? dt * _layerBlendingSpeed : 1.0f);
        for (RenderTile& renderTile : *_visibleRenderTiles) {
            refresh = updateRenderTile(renderTile, dBlend) || refresh;
        }
        VT_STAT_SPLIT(prepTileBlendNs, prepClock);
        
        // Re-anchor labels onto the terrain. Label geometry is built flat when its tile is
        // decoded, so a newly built label is always anchored here; an existing one only when
        // the elevation under one of its tiles changed. Anchoring costs an elevation sample
        // per label vertex - doing it for every label whenever any elevation tile decodes (or
        // whenever the visible tile set changes, which rebuilds the label list) resamples the
        // whole screen several times a second while panning.
        if (_labelElevationProvider) {
            if (_pendingLabelElevationAll || !_pendingLabelElevationTiles.empty()) {
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
            VT_STAT_SPLIT(prepElevDirtyNs, prepClock);
            // Re-anchoring costs one elevation sample per label vertex (~233 us a label) and a
            // whole screen of labels goes dirty at once while elevation tiles stream in, so
            // this loop is 2.5-4.1 ms of a frame. It still has to run to completion: a label
            // left dirty is drawn, and culled, at the height it had before the elevation
            // arrived - which reads as labels popping in at the wrong place and then settling.
            for (const std::shared_ptr<Label>& label : _labels) {
                if (label->isElevationDirty()) {
                    label->updateElevation(_labelElevationProvider);
                    label->setElevationDirty(false);
                    refresh = true;
                }
            }
            VT_STAT_SPLIT(prepElevUpdateNs, prepClock);
        }

        // Update labels
        _visiblePassLabels = _passLabels;
        float dOpacity = (_labelBlendingSpeed > 0.0f ? dt * _labelBlendingSpeed : 1.0f);
        for (int pass = 0; pass < 2; pass++) {
            for (const std::shared_ptr<Label>& label : *_visiblePassLabels[pass]) {
                refresh = updateLabel(label, dOpacity) || refresh;
            }
        }
        VT_STAT_SPLIT(prepLabelBlendNs, prepClock);
        
        // Reset label batch counter
        _labelBatchCounter = 0;

        return refresh;
    }
    
    void GLTileRenderer::renderGeometry(bool geom2D, bool geom3D, bool inline3D) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles) {
            return;
        }

        if (geom2D) {
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


            // Update GL state. In terrain mode 2D geometry is displaced onto the terrain
            // surface and depth-tested (with a small bias towards the viewer) against the
            // terrain depth pre-pass that the host renderer performs before the tile layers.
            // Nothing in the 2D pass writes depth, so painter's order is preserved and
            // co-planar surfaces from different layers can not z-fight.
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            // In terrain mode draped 2D content is depth-tested against the depth-write
            // surfaces so that terrain ridges occlude content behind them. The mesh
            // deviations between the surface and geometry meshes are covered by the
            // two-component depth bias (see setupTerrainUniforms), which stays valid at
            // all tilts and zooms - no tilt gating needed.
            if (_terrainMode) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(GL_FALSE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(0);
            if (_terrainMode) {
                // Terrain-displaced surfaces can face away from the camera near ridge
                // crests (impossible for flat surfaces); culling them would leave holes
                // in both color and depth exactly along ridge silhouettes
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            // Terrain reference surface pre-pass: this tile layer's own depth domain (clear, draw
            // the displaced surfaces at TRUE depth, content writes on top). Skipped under a shared
            // ground or cross-layer drape - the glClear(DEPTH) would throw away the shared surface
            // that exists to remove exactly this per-layer domain. Content composited onto that
            // ground is coincident with it, so it passes at EQUAL depth.
            bool leEqualDepth = _terrainMode && (_terrainDrapeFills || _terrainSharedGround);
            if (leEqualDepth) {
                glDepthFunc(GL_LEQUAL);
            }

            if (_terrainPaint.enabled && !_externalDrapeTarget && !(_terrainPaintOnGround && _terrainSharedGround)) {
                // A paint with no drape draws itself here, in this layer's place in the order -
                // unless it IS the ground, in which case the ground pass already drew it, once per
                // tile, at the bottom of the order.
                renderTerrainPaintSurfaces();
            }
            // A shared ground has already been drawn once for the whole stack, so this layer must
            // not clear the depth buffer and re-establish a private domain - that is exactly what
            // makes a contour or an element leak through the ridge in front of it.
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

                // Drape pass: bake fills flat into per-tile textures, then draw the
                // terrain surface textured with them (at content depth, over the
                // back-pushed pre-pass surface). Fills then hug the terrain exactly.
                if (_terrainDrapeFills) {
                    renderDrapeTextures(*_visibleRenderTiles);
                    // The drape surface IS the terrain grid surface (lattice-exact). Draw it at
                    // TRUE depth and let it WRITE depth: it becomes the real occluder, so a near
                    // ridge blocks the whole far slope (raster/contours/elements) behind it. A
                    // pushed-back / non-writing surface is what let far slopes show through.
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
                // Depth-resolved within the prefill itself (front faces must win over
                // back faces exactly like a correct render would); the depth buffer is
                // cleared afterwards so the real passes start pristine.
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_STENCIL_TEST);
                // Facing-coded: FRONT faces magenta, BACK faces cyan. A 'see-through'
                // spot later showing cyan means literal back faces are visible
                // (winding/mesh problem); magenta means the far slope's front faces
                // painted there (occlusion failure).
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

            // 2D geometry pass: lattice-clamped content is coincident with the true-depth ground,
            // so GL_LEQUAL and no forward bias - zero pull means zero ridge leak. No stencil masks
            // here; content writing depth plus a pushed-back proxy is what replaces them
            // (docs/rendering/05-depth-model.md).
            renderGeometry2D(*_visibleRenderTiles, stencilBits);
            if (leEqualDepth) {
                glDepthFunc(GL_LESS);
            }

            // Debug: outline every tile this layer draws, on the ground.
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

            // Debug: overlay the FINAL stencil buffer contents (stencil-tested fullscreen
            // quads, one distinct color per allocated stencil value, black = unowned 0)
            // and the displaced tile surface meshes as red wireframes
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

            // Restore GL state
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(255);
            glEnable(GL_CULL_FACE);
        }

        if (geom3D) {
            // Update GL state
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_STENCIL_TEST);
            glStencilMask(0);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);

            // 3D polygon pass
            renderGeometry3D(*_visibleRenderTiles, inline3D);

            // Restore GL state
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glEnable(GL_BLEND);
            glStencilMask(255);
        }
    }
    
    void GLTileRenderer::renderLabels(bool labels2D, bool labels3D) {
        VT_STAT_CLOCK(lockClock);
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(mutexWaitNs, lockClock);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visiblePassLabels[0] || !_visiblePassLabels[1]) {
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
                renderLabels(*_visiblePassLabels[pass]);
            }
        }
        
        // Restore GL state
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
            if (it->second.owner.expired()) {
                deleteCompiledGeometry(it->second.geometry);
                it = _compiledTileGeometryMap.erase(it);
            } else {
                it++;
            }
        }

        // Note: we do not release unused label batches. These are unlinkely very big and can be reused later
        VT_STAT_SPLIT(endFrameNs, statClock);
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
                if (!testLayerFilter(renderLayer.layer->getLayerName(), _clickHandlerLayerFilter)) {
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

        // Sort the 3D results by distance, then append to normal results
        std::stable_sort(results3D.begin(), results3D.end(), [](const GeometryIntersectionInfo& result1, const GeometryIntersectionInfo& result2) {
            return result1.rayT > result2.rayT;
        });
        results.insert(results.end(), results3D.begin(), results3D.end());

        return results.size() > initialResultCount;
    }
    
    bool GLTileRenderer::findLabelIntersections(const std::vector<cglib::ray3<double>>& rays, float buffer, bool labels2D, bool labels3D, std::vector<GeometryIntersectionInfo>& results) const {

        std::lock_guard<std::mutex> lock(_mutex);

        // Build set of valid layer indices
        std::set<int> clickHandlerLayerIdxs;
        for (const RenderTile& renderTile : *_renderTiles) {
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (testLayerFilter(renderLayer.layer->getLayerName(), _clickHandlerLayerFilter)) {
                    clickHandlerLayerIdxs.insert(renderLayer.layer->getLayerIndex());
                }
            }
        }

        // Test for label intersections. The ordering may be mixed compared to actual rendering order, but this is non-issue if the labels are non-overlapping.
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
                    // "Is the label facing us" - the anchor's ground normal against the ray. A
                    // CALLOUT is exempt: it is drawn where it is not anchored, its quad is
                    // spanned on the camera axes so it always faces the viewer, and a label
                    // lifted into the SKY is hit by an upward ray - which this test rejects.
                    // That was "peak names below the horizon are clickable, the ones over the
                    // sky are not".
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

    unsigned int GLTileRenderer::fogFlag() const {
        // The drape bake must never fog: it is flat content baked into a texture that is then
        // painted on the terrain surface and fogged there, once. Anything fogged here is BURNT IN
        // and survives the fog being turned off, because the drape texture is cached. It used to
        // fall out of the arithmetic - an orthographic pass has gl_FragCoord.w = 1, a whole world
        // in internal units, which no metric range ever reached - but a camera-relative range does
        // reach it at high zoom, so the bake now says so itself.
        if (_drapeMVPOverride) {
            return 0;
        }
        // Fully transparent fog, or a zero range, means the style/app did not ask for any: the
        // programs are then built without it and cost nothing.
        return (_fogColor[3] > 0.0f && _fogDistance > _fogStartDistance ? FOG_FLAG : 0);
    }

    unsigned int GLTileRenderer::coverageFlag() const {
        return _drapeCoveragePass ? COVERAGE_FLAG : 0;
    }

    unsigned int GLTileRenderer::drapeMaskFlag() const {
        return _drapeMaskTexture != 0 ? DRAPE_MASK_FLAG : 0;
    }

    unsigned int GLTileRenderer::shadowReceiverFlags() const {
        // The cascade count is compiled in: it is a matrix per vertex and a highp varying per
        // fragment each, which is what a shadowed surface actually costs
        // (docs/rendering/08-lighting-sky-fog.md).
        unsigned int cascadeFlag = (_terrainShadowCascades >= 4 ? SHADOW_CASCADES4_FLAG :
                                    _terrainShadowCascades == 3 ? SHADOW_CASCADES3_FLAG :
                                    _terrainShadowCascades == 2 ? SHADOW_CASCADES2_FLAG : 0);
        // Hardware comparison needs a real depth texture to compare against; sampler2DShadow itself
        // is ESSL 3.00 core, which every program now is. No depth texture falls back to manual taps,
        // not to no shadows.
        unsigned int hwFlags = 0;
        if (_terrainShadowDepthTexture) {
            hwFlags = SHADOW_DEPTH_TEXTURE_FLAG | (_terrainShadowHardwarePCF ? SHADOW_HW_FLAG : 0);
        }
        return TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG | cascadeFlag | hwFlags;
    }

    void GLTileRenderer::warmTerrainRasterShader() {
        // The lit raster program is only ever ASKED for at an integer zoom out - the one moment a
        // raster draws outside the drape (see renderTileBitmap) - so building it lazily put a full
        // compile and link of the largest colormap variant (DEM taps, PCF, cascades) inside that
        // gesture. Build it on an ordinary frame instead. The flag set is re-derived every frame
        // and only a change rebuilds, so a shadow or cascade config change warms itself too.
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
        // The terrain surface is drawn over the whole screen - as the drape, and again as the paint
        // over it - so its shadow is resolved once into a screen-space mask and sampled from there.
        // The pass that PRODUCES the mask is the one draw that still computes it analytically.
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

    cglib::vec4<float> GLTileRenderer::calculateShadowNormalOffsets(const cglib::mat4x4<double>& tileFrame) const {
        // The normal offset is a number of shadow-map TEXELS, and the shader adds it to a
        // tile-local position. One texel is (box width / mapSize) in world units, and the box width
        // is read straight off the light matrix: its ortho part scales x by 2/(r-l), and the view
        // part is a pure rotation, so the first row's LENGTH is that scale. Reading it back here
        // instead of threading it through the caller keeps the two in step by construction.
        cglib::vec4<float> offsets(0.0f, 0.0f, 0.0f, 0.0f);
        double tileScale = cglib::length(cglib::vec3<double>(tileFrame(0, 0), tileFrame(0, 1), tileFrame(0, 2)));
        if (!(tileScale > 0) || !(_terrainShadowNormalOffset > 0) || _terrainShadowMapSize <= 0) {
            return offsets;
        }
        // EACH cascade gets its OWN texel, which is mapbox's model (shadow_renderer setupShadows:
        // offset0 from cascade 0's radius, offset1 from the last one's). Clamped to the near
        // cascade's instead, an outer page whose texel is metres of ground was offset by a
        // near-page texel of centimetres - far too little to lift a wall off its own depth - so
        // every building that fell out of the near pages wore a grey patch of its own acne. That is
        // why one cascade was clean and three were not: with one page there is no other to clamp to.
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
        // A tile whose elevation has not arrived is drawn FLAT, at zero. In the mountains that is a
        // kilometre below everything around it, so the surrounding terrain shadows every texel of it
        // and it reads as a solid dark block the exact shape of the tile. It has no relief to shadow
        // anyway, so it takes no shadow until its heights are there.
        glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), hasElevation ? _terrainShadowStrength : 0.0f, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
        glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
    }

    void GLTileRenderer::setTerrainShadowMask(GLuint texture, float invScreenWidth, float invScreenHeight) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainShadowMaskTexture = texture;
        _terrainShadowMaskScale = cglib::vec2<float>(invScreenWidth, invScreenHeight);
    }

    int GLTileRenderer::renderTerrainShadowMask(const std::vector<TileId>& tileIds) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!(_terrainRegularGrid && _terrainMode && _terrainTextureProvider)) {
            return 0;
        }
        if (_terrainShadowTexture == 0 || _terrainShadowStrength <= 0.0f || !_terrainLighting.enabled) {
            return 0;
        }
        // The same geometry, elevation and normal the surface will be drawn with - the fill path IS
        // that draw, with a fragment shader that stops at the shadow value.
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
        // glUseProgram is one of the most expensive state changes on a tiler, and the draw
        // loop is style-layer-major: every tile of a layer draws with the same program, so
        // the call is redundant for all but the first. Measured per-draw setup (everything
        // before glDrawElements) is 24-31 us against 10-12 us for the draw itself, at
        // 250-560 draws a frame. The tracked value is reset whenever another renderer can
        // have bound a program of its own (see resetProgramState).
        if (_lastUsedProgram != shaderProgram.program) {
            _lastUsedProgram = shaderProgram.program;
            glUseProgram(shaderProgram.program);
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
        return !std::regex_match(layer->getLayerName(), *_noDrapeLayerFilter);
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
        // Apply 'root shift' to source tile id. Adjust target tile id, if needed.
        TileId rootTileId = targetTileId;
        while (rootTileId.zoom > 0) {
            rootTileId = rootTileId.getParent();
        }
        TileId sourceTileId = tile->getTileId().getTeleported(rootTileId.x, rootTileId.y);
        if (sourceTileId.zoom > targetTileId.zoom) {
            targetTileId = sourceTileId;
        }

        // Initialize new tile
        renderTile.targetTileId = targetTileId;
        renderTile.tile = tile;
        renderTile.visible = false;
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

        // Check if the existing tile is already covered by some tile in the new list.
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

        // No, the tile is missing. Add it in non-active state.
        RenderTile renderTile = existingRenderTile;
        renderTile.targetTileId = targetTileId;
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            RenderTileLayer& renderLayer = it->second;
            renderLayer.targetTileId = (targetTileId.zoom > renderLayer.targetTileId.zoom ? targetTileId : renderLayer.targetTileId);
            renderLayer.active = false;
        }
        renderTiles.push_back(renderTile);
    }

    bool GLTileRenderer::updateRenderTile(RenderTile& renderTile, float dBlend) const {
        renderTile.visible = isTileVisible(renderTile.targetTileId);

        // Update each layer blend state depending whether the layer is active or not
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
                // An inactive layer that a still-fading ACTIVE layer covers is the only thing
                // painting that area until the replacement is opaque, so it HOLDS instead of
                // fading. Fading both at once leaves coverage at blend + (1-blend)^2, which is
                // below 1 for the whole transition - on a raster or hillshade layer that reads as
                // a blink at every zoom step. The active branch above erases it once the
                // replacement reaches 1, so nothing here has to fade it out.
                bool replaced = false;
                auto it2 = it;
                for (it2++; it2 != renderTile.renderLayers.end() && it2->first == it->first; it2++) {
                    if (it2->second.active && it2->second.targetTileId.covers(renderLayer.targetTileId)) {
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    renderLayer.blend = std::max(0.0f, renderLayer.blend - delta);
                    refresh = (renderLayer.blend > 0.0f) || refresh;

                    // In case of non-active layers, simply remove the layer when it has become invisible.
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

    void GLTileRenderer::buildLabelMaps(const std::vector<std::shared_ptr<const Tile>>& labelTiles) {
        VT_STAT_INC(labelMapRebuilds);

        // Pass 1: work out which tile geometries each label is built from, WITHOUT building
        // anything. A label is identified by the set of (tile object, local id) pairs
        // contributing to it; the tile object rather than the tile id, because the same tile
        // id can be re-served by a different (re-decoded) tile whose geometry differs.
        // Summing the per-contribution hashes makes the signature independent of the order
        // the tiles are visited in, which follows the visible tile order and is not stable.
        std::map<int, std::unordered_map<long long, std::pair<long long, int>>> newLayerSignatureMap;
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                if (!testLayerFilter(layer->getLayerName(), _rendererLayerFilter)) {
                    continue;
                }

                std::unordered_map<long long, std::pair<long long, int>>& signatureMap = newLayerSignatureMap[layer->getLayerIndex()];
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    std::pair<long long, int>& signature = signatureMap[tileLabel->getGlobalId()];
                    signature.first += calculateLabelGeometryHash(tile.get(), tileLabel->getLocalId());
                    signature.second++;
                }
            }
        }

        // Create label list, merge geometries
        std::map<int, GlobalIdLabelMap> newLayerLabelMap;
        std::map<int, std::unordered_set<long long>> reusedLayerLabelIds;
        for (const std::shared_ptr<const Tile>& tile : labelTiles) {
            cglib::mat4x4<double> tileMatrix = _transformer->calculateTileMatrix(tile->getTileId(), 1.0f);
            std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tile->getTileId());
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                if (!testLayerFilter(layer->getLayerName(), _rendererLayerFilter)) {
                    continue;
                }

                GlobalIdLabelMap& newLabelMap = newLayerLabelMap[layer->getLayerIndex()];
                if (newLabelMap.empty()) {
                    newLabelMap.reserve(_layerLabelMap[layer->getLayerIndex()].size() + 64);
                }
                const GlobalIdLabelMap& oldLabelMap = _layerLabelMap[layer->getLayerIndex()];
                const std::unordered_map<long long, std::pair<long long, int>>& signatureMap = newLayerSignatureMap[layer->getLayerIndex()];
                std::unordered_set<long long>& reusedLabelIds = reusedLayerLabelIds[layer->getLayerIndex()];
                for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                    long long globalId = tileLabel->getGlobalId();
                    std::shared_ptr<Label>& label = newLabelMap[globalId];
                    if (label) {
                        // A reused label already holds every contribution - merging this tile
                        // into it would duplicate the geometry it was reused for.
                        if (reusedLabelIds.count(globalId) > 0) {
                            continue;
                        }
                        Label newLabel(*tileLabel, tile->getTileId(), layer->getLayerIndex(), tileMatrix, transformer);
                        label->mergeGeometries(newLabel);
                        VT_STAT_INC(labelsAllocated); // the merge copy costs the same geometry transform
                        continue;
                    }

                    // Reuse the existing label object when every contribution to it is
                    // unchanged. Rebuilding it transforms the same geometry again, drops its
                    // cached vertex data and forces a re-snap of its anchor - which is what
                    // makes the visible label set churn while tiles stream in.
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

        // Stamp the signature on the freshly built labels, now that every contributing tile
        // has been merged into them. Doing it at construction time would make the label look
        // complete to the merge branch above and swallow its remaining contributions.
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
            const std::unordered_set<long long>& reusedLabelIds = reusedLayerLabelIds[newLayerLabelIt->first];
            for (auto newLabelIt = newLabelMap.begin(); newLabelIt != newLabelMap.end(); newLabelIt++) {
                const std::shared_ptr<Label>& newLabel = newLabelIt->second;
                std::shared_ptr<Label>& label = labelMap[newLabelIt->first];
                // A reused object IS the previous label: its placement, visibility and
                // opacity are already the current ones. Note this can not be decided by
                // comparing against the map entry - the release pass above erases entries
                // whose label has faded out, and a reused label may be one of them.
                if (reusedLabelIds.count(newLabelIt->first) > 0) {
                    // nothing to carry over
                }
                else if (label) {
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

        // Build the final label lists: ONE list per pass, in draw order. The order is the style's
        // (priority, then layer, then id) and nothing else - grouping by glyph atlas first made the
        // order of two labels in different atlases a pointer hash, so a label small enough to be
        // rastered at another size than the icon it sits on could be drawn under it.
        std::vector<std::shared_ptr<Label>> labels;
        labels.reserve(_labels.size() + 64);
        std::array<std::shared_ptr<PassLabels>, 2> passLabels;
        bool styledOcclusion = false;
        for (int pass = 0; pass < 2; pass++) {
            passLabels[pass] = std::make_shared<PassLabels>();
            passLabels[pass]->reserve(_passLabels[pass] ? _passLabels[pass]->size() + 64 : 64);
        }
        for (auto layerLabelIt = _layerLabelMap.begin(); layerLabelIt != _layerLabelMap.end(); layerLabelIt++) {
            const GlobalIdLabelMap& labelMap = layerLabelIt->second;
            for (auto labelIt = labelMap.begin(); labelIt != labelMap.end(); labelIt++) {
                const std::shared_ptr<Label>& label = labelIt->second;
                int pass = ((label->getStyle()->orientation == LabelOrientation::BILLBOARD_3D || label->getStyle()->orientation == LabelOrientation::LINE_BILLBOARD_3D) ? 1 : 0);
                // Whether any style layer asks to be occluded, which the owner needs BEFORE the
                // layer passes to decide whether to render the occluder buffer at all. Noted here
                // because this loop already touches every label once.
                styledOcclusion = styledOcclusion || label->getStyle()->occlusionOpacity.value_or(1.0f) < 1.0f;
                passLabels[pass]->push_back(label);
                labels.push_back(label);
            }
        }
        for (int pass = 0; pass < 2; pass++) {
            // DRAW order, so the list runs bottom to top: what is drawn last is on top. The
            // culler's order is the opposite - it places the most important label FIRST, to claim
            // its slot (LabelCuller::process) - and this list used to copy it, which put the LEAST
            // important of an overlapping pair on top. A road name covering the town name it
            // crosses was that, and the layerIndex tie-break right below already ran this way.
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

        // Update built label lists and maps
        _labels = std::move(labels);
        _passLabels = std::move(passLabels);
        _labelOcclusionStyled = styledOcclusion;
        VT_STAT_SET(labelsLive, static_cast<long long>(_labels.size()));
    }

    bool GLTileRenderer::updateLabel(const std::shared_ptr<Label>& label, float dOpacity) const {
        bool refresh = false;
        if (label->isValid()) {
            bool occluded = false;
            if (_labelOcclusionTest && label->isVisible() && label->isActive()) {
                cglib::vec3<double> center(0, 0, 0);
                if (label->calculateCenter(center)) {
                    occluded = _labelOcclusionTest(center);
                }
            }
            if (label->isVisible() && label->isActive() && !occluded) {
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
        // Extract layer tiles for each layers
        std::map<int, std::vector<const RenderTileLayer*>> renderLayerMap;
        // Tangram's maxVisS: the deepest level on screen, which is what a proxy tile's depth is
        // measured against (tileManager.cpp, setProxyDepth).
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

        // Tangram's proxy depth, their formula (tileManager.cpp, setProxyDepth):
        //     max(maxVisS - tileId.s, 1) while the tile stands in, 0 once it is live.
        // How many levels COARSER the drawn tile is than the deepest level on screen - not a flat
        // one. A tile standing in two levels up is a different height field twice over, and the
        // push has to say so; a flat 1 pushes it exactly as far as a tile that is one level off.
        auto proxyDepth = [maxVisibleZoom](const RenderTileLayer* renderLayer) -> float {
            if (renderLayer->active) {
                return 0.0f;
            }
            return std::max(static_cast<float>(maxVisibleZoom - renderLayer->targetTileId.zoom), TERRAIN_PROXY_DEPTH_UNITS);
        };

        // Stencil tile masks clip a tile's content to its footprint. In a TERRAIN frame a mask is a
        // full displaced grid per tile per reset - two thirds of the surface geometry, 19.5 -> 23.5
        // fps without them - so they are dropped there; in 2D a mask is a quad and costs nothing.
        // A comp-op layer is the exception in both: its overlay buffer has no depth to clip it.
        // Whether the MASKS run is separate from whether the buffer HAS a stencil - the single-blend
        // pass below needs one spare bit and no masks.
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

        // Allocate stencil value for each target tile
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
        
        // In terrain mode, draw the tiles of each style layer NEAR-TO-FAR: content
        // writes depth (tangram-style), so near tiles occlude far tiles by real
        // geometry, and translucent content of a far tile can not blend under
        // already-drawn near content.
        if (_terrainMode && _terrainTextureProvider) {
            // One distance per tile, not one per comparison. On the terrain transformer
            // calculateTileBBox samples the elevation manager for the tile's min/max height and
            // transforms the box in double precision, so calling it from the comparator made the
            // sort the single most expensive thing on the render thread (measured on the
            // Crosscall, north pan: 21% of it, with calculateTileBBox at 22% inclusive).
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
                    // Retained blend-out (proxy) tiles first: without the stencil masks nothing
                    // else stops a stale tile kept for the crossfade from painting over the live
                    // tile that replaced it, and the two overlap exactly during a LOD change.
                    if (layer1->active != layer2->active) {
                        return layer2->active;
                    }
                    return tileDistance(layer1->targetTileId) < tileDistance(layer2->targetTileId);
                });
            }
        }

        VT_STAT_ADD(styleLayersDrawn, static_cast<long long>(renderLayerMap.size()));
        VT_STAT_ADD(renderTilesDrawn, static_cast<long long>(renderTiles.size()));

        // Render tile layers in correct order
        bool resetStencil = true;
        std::optional<CompOp> currentCompOp;
        // Tangram's style-layer order as a small dense index (docs/rendering/05-depth-model.md).
        // Numbered over every style layer this renderer has EVER drawn, not the ones on screen:
        // their `order` is a scene property fixed before a tile loads, and a rank over what happens
        // to be present renumbers the stack as tiles come and go (measured 7 -> 5 -> 7 between
        // frames), so content pops in and out from under the layer above it.
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            _terrainStyleLayerIndices.insert(it->first);
        }
        _terrainStyleLayersDrawn = static_cast<int>(_terrainStyleLayerIndices.size()); // the owner numbers the next renderer from here
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            int layerOrdinal = _terrainLayerOrdinalBase + static_cast<int>(std::distance(_terrainStyleLayerIndices.begin(), _terrainStyleLayerIndices.find(it->first)));
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
                    createFrameBuffer(_overlayBuffer2D, true, false, maskStencilBits > 0);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer2D.fbo);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);

                resetStencil = true;
            }

            // If needed, initialize the stencil buffer with target tile masks.
            // The masks implement screen-space tile clipping and must not be depth-tested
            // against the terrain depth pre-pass (the mask surfaces carry no depth bias).
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
                    // Displaced tiles overlap on screen, so the mask order decides who owns a pixel:
                    // retained blend-out tiles first (they must never steal from live ones), then
                    // zoom ascending (parent and child have identical footprints during an LOD
                    // transition and the child must win), then camera distance descending.
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
                // Restore the terrain depth test disabled for the mask stamping above
                if (_terrainMode) {
                    glEnable(GL_DEPTH_TEST);
                }
            }

            // Render tile layers for this layer
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

                // GPU draping: ALL 2D content writes real depth, LEQUAL + painter's order stacks
                // coplanar layers with no per-layer bias, proxy tiles are pushed back one delta.
                // CPU fallback: tesselations differ per layer, so slope-scaled polygon offsets.
                bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
                // Under a shared ground NOTHING but the ground pass writes depth: backgrounds and
                // rasters are drawn on the cover tiles, coincident with it, so a write would only
                // repeat what is there - and the proxy pushback below would put them BEHIND it and
                // make every retained tile's raster disappear.
                bool contentDepthWrite = _terrainMode && !layer->getCompOp() && (terrainVTF || _terrainDepthWrite);
                if (_terrainMode) {
                    if (contentDepthWrite) {
                        glDepthMask(GL_TRUE);
                    }
                    if (_terrainSharedGround) {
                        // Tangram's model, whole - writes + per-layer ordinal + a constant-CLIP
                        // depth_shift. None of the three works alone; adopting it piecemeal cost two
                        // rounds of artifacts. docs/rendering/05-depth-model.md.
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Backgrounds/bitmaps ARE the terrain occluders and draw at TRUE depth: they
                        // render the SAME meshes the pre-pass drew, so any pushback here is rejected
                        // by GL_LESS once the two depths quantize together - every raster vanishing
                        // along a hard horizontal line a few km out. Clearance comes from the
                        // pre-pass pushback instead. Proxy tiles are pushed back one delta.
                        float proxyBias = (renderLayer->active ? 0.0f : 1.0f * TERRAIN_LAYER_DEPTH_DELTA);
                        _terrainDrawDepthBias = _terrainDepthBias - proxyBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                    } else {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        if (_terrainDepthWrite && !layer->getCompOp()) {
                            // Push the WRITTEN depth slightly back by a small constant only. A
                            // slope-scaled factor here leaks along tall steep mountain faces (the
                            // projected face spans a few pixels with an enormous per-pixel depth
                            // gradient, so the whole strip gets pushed far back and geometry behind
                            // the ridge shows through). Geometry-below-surface dips are instead
                            // prevented exactly by the surface-fan height clamp in the transformer.
                            glPolygonOffset(0.0f, 2.0f);
                        } else {
                            glPolygonOffset(-1.0f, -2.0f);
                        }
                    }
                }

                // Draped content lives in the tile's drape texture and must NOT also be drawn as
                // displaced geometry. Overzoomed/proxy layers are draped too (through the
                // sub-rect bake), so this no longer requires sourceTileId == targetTileId.
                bool drapedTile = isTileDraped(renderLayer->targetTileId);
                // Ground-shaped content - the style's tile background and rasters - is a second
                // tesselation of the same height field as the ground, and two tesselations do not
                // agree: under a shared ground it is drawn on the COVER tiles instead of on the
                // layer's own, so it is coincident with the ground to the bit. A layer coarser
                // than the cover therefore draws once per leaf, with the source uv sub-rect the
                // overzoom path already computes for a coarse source over a fine target.
                const std::vector<TileId>& groundTiles = collectGroundLeaves(renderLayer->targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer->layer->getBackgrounds()) {
                    // Draped native backgrounds are baked into the surface texture already.
                    if (drapedTile) {
                        continue;
                    }
                    CompOp backgroundCompOp = CompOp::SRC_OVER;
                    if (currentCompOp != backgroundCompOp) {
                        setCompOp(backgroundCompOp);
                        currentCompOp = backgroundCompOp;
                    }
                    // Under a shared ground the ground pass owns the ground colour, so a patternless
                    // background is skipped - it was ~84 grid draws a frame (3 layers, 28 tiles) for
                    // pixels already painted. Tangram has no per-tile background mesh at all. A
                    // PATTERN is real content and still draws.
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
                    if (drapedTile) {
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
                    // Geometry does NOT write depth: it stacks by painter's order over
                    // the backgrounds (coplanar same-displacement content needs no
                    // per-layer bias), so road casings/fills from different style layers
                    // can not z-fight each other, and vector elements drawn after the
                    // tile layers stay in front of all tile content.
                    if (contentDepthWrite && !_terrainSharedGround) {
                        glDepthMask(GL_FALSE);
                    }
                    if (_terrainSharedGround) {
                        // Geometry writes too (tangram writes for opaque AND translucent) and carries
                        // its layer's ordinal. That term separates COPLANAR style layers, one step
                        // each - not a budget to spread over the stack. An un-subdivided fill needs
                        // more than one step and there is no room for it here (half a step leaves
                        // slivers at z14, two steps hides the ground paint): still open, and not to
                        // be paid out of this term.
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Geometry draws at its REAL depth: the clearance comes from pushing the
                        // SURFACE back, so nothing is ever pulled forward and nothing leaks over a
                        // ridge. (The adaptive fallback keeps the old distance-growing slack, which
                        // is the only depth range far content can leak through.)
                        float proxyBias = (renderLayer->active ? 0.0f : 1.0f * TERRAIN_LAYER_DEPTH_DELTA);
                        // Painter-order: lattice-clamped content is coincident with the true-depth
                        // occluder surface and is drawn with GL_LEQUAL, so it needs ZERO forward
                        // bias - it passes at equal depth and is occluded (fails) behind a ridge.
                        // Any forward clip bias here leaks over ridges at range (the contour
                        // see-through), so keep it at zero in painter-order.
                        _terrainDrawDepthBias = (_terrainRegularGrid ? 0.0f : _terrainDepthBias + 1.0f * TERRAIN_LAYER_DEPTH_DELTA) - proxyBias;
                        // Lattice clamp (regular-grid mode) makes draped geometry follow the
                        // reference grid surface within the tiny in-cell bilinear-vs-triangle
                        // twist, so the distance-growing slack collapses to a small margin;
                        // adaptive meshes keep the full calibrated slack.
                        _terrainDrawDepthClipUnits = _terrainRegularGrid ? 0.0f : 12.0f;
                    } else {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(-1.0f, -2.0f);
                    }
                }

                // A no-drape layer is drawn after the WHOLE drape composite, so without this it can
                // only land on top of it. The mask is the coverage of the draped layers that come
                // after it in the style order; where they paint, they win (#175).
                _drapeMaskTexture = 0;
                if (drapedTile && !isLayerDraped(renderLayer->layer)) {
                    if (!resolveDrapeCoverageMask(renderLayer->targetTileId, renderLayer->layer->getLayerIndex(), _drapeMaskTexture, _drapeMaskUVTransform)) {
                        _drapeMaskTexture = 0;
                    }
                }

                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    // Draped fills/lines are baked into the drape texture already - unless the
                    // layer opted out of the bake (setNoDrapeLayerFilter), in which case this pass
                    // is the only one that draws it.
                    if (drapedTile && isDrapeableGeometry(geometry->getType()) && isLayerDraped(renderLayer->layer)) {
                        continue;
                    }
                    // POLYGON3DGROUND is a contact shadow for the 3D pass, not 2D content.
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D && geometry->getType() != TileGeometry::Type::POLYGON3DGROUND) {
                        CompOp geometryCompOp = geometry->getStyleParameters().compOp;
                        if (currentCompOp != geometryCompOp) {
                            setCompOp(geometryCompOp);
                            currentCompOp = geometryCompOp;
                        }
                        // Undraped LINES and POINTS are DECALS: both are flat quads over a curved
                        // surface, so unlike a fill neither is coincident with it and at zero bias
                        // the sagging half of every quad is cut away over relief. A polygon offset
                        // scales with the primitive's own depth slope, which is what the sag scales
                        // with. A point quad chords MORE than a line's cross-section, not less - a
                        // glyph of clipped text is tens of metres wide - and it was left out: whole
                        // letters disappeared over 3D terrain, only where the ground is draped
                        // (a draped tile draws its surface at TRUE depth and writes it).
                        bool decal = terrainVTF && (geometry->getType() == TileGeometry::Type::LINE || geometry->getType() == TileGeometry::Type::POINT);
                        if (decal) {
                            glEnable(GL_POLYGON_OFFSET_FILL);
                            glPolygonOffset(-2.0f, -8.0f);
                        }
                        // A line is a chain of quads: between two vertices it chords over the
                        // relief the ground follows, so under a depth-writing ground it is cut into
                        // fragments wherever it sags. The clearance is a fixed number of METRES -
                        // the sag does not care how far away the line is - which neither the
                        // ordinal pull (clip-constant, worth distance/near) nor a depth bias
                        // (ndc-constant, worth distance^2/near) can express without leaking through
                        // ridges at range.
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

            // If compositing was enabled for this layer, blend the rendered layer with framebuffer
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
    }
    
    void GLTileRenderer::renderGeometry3D(const std::vector<RenderTile>& renderTiles, bool allowInline) {
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
                    contains3DGeometry = (geometry->getType() == TileGeometry::Type::POLYGON3D || geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) || contains3DGeometry;
                }
                if (contains3DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    if (!_rendererLayerIndexRange || (it->first >= _rendererLayerIndexRange->first && it->first < _rendererLayerIndexRange->second)) {
                        renderLayerMap[it->first].push_back(&it->second);
                    }
                }
            }
        }

        // The extrusions take part in the SAME dense style-layer numbering as the 2D content
        // (see renderGeometry2D). Left out of it they drew at ordinal 0 - the ground's own level -
        // while every 2D layer is pulled forward by its ordinal, so every road and every clipped
        // text run won the depth test against buildings shorter than that pull. Registered here
        // for the same reason the 2D pass registers its own: over every layer ever drawn, not the
        // ones on screen.
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

        // Render tile layers in correct order
        for (auto it = renderLayerMap.begin(); it != renderLayerMap.end(); it++) {
            const std::vector<const RenderTileLayer*>& renderLayers = it->second;
            if (renderLayers.empty()) {
                continue;
            }
            int layerOrdinal = _terrainLayerOrdinalBase + static_cast<int>(std::distance(_terrainStyleLayerIndices.begin(), _terrainStyleLayerIndices.find(it->first)));
            Pass3DState pass = begin3DPass(renderLayers, renderTiles, allowInline);

            // Render tile layers for this layer
            for (const RenderTileLayer* renderLayer : renderLayers) {
                // Only under the shared ground, which is where the ordinal model applies at all;
                // the other paths take their clearance from pushing the surface back instead.
                if (_terrainSharedGround) {
                    _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        // Always drawn: an extrusion whose ground has not resolved yet keeps the
                        // sentinel and the shader falls back to the ground under each vertex.
                        resolveExtrusionBases(renderLayer->sourceTileId, renderLayer->targetTileId, geometry);
                        // NOTE: geometry comp op is not supported for 3D polygons. Blending is disabled, setGLBlendState not needed
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, pass.geometryOpacity, renderLayer->tileSize, geometry);
                    }
                }
                _terrainDrawLayerOffset = 0.0f;
            }

            end3DPass(pass);
        }
    }

    GLTileRenderer::Pass3DState GLTileRenderer::begin3DPass(const std::vector<const RenderTileLayer*>& renderLayers, const std::vector<RenderTile>& renderTiles, bool allowInline) {
        Pass3DState state;
        const std::shared_ptr<const TileLayer>& layer = renderLayers.front()->layer;

        // Layer settings
        state.layerOpacity = (layer->getOpacityFunc())(_viewState);
        if (!layer->getCompOp()) { // use the hack to conform with normal '2D' layers
            std::swap(state.layerOpacity, state.geometryOpacity);
        }
        state.layerCompOp = (layer->getCompOp() ? *layer->getCompOp() : CompOp::SRC_OVER);

        // The 3D overlay buys comp-op compositing and 'flatten then blend once' for a fading
        // layer; neither applies to the normal opaque case, which it costs a full-screen clear,
        // a terrain surface pre-pass and a composite every frame. Tangram has no overlay at all.
        // Inline only when the extrusions are the frame's last tile content (buildingOrder 1) -
        // they write depth and would otherwise occlude the 2D content drawn after them.
        state.useOverlay = !allowInline || static_cast<bool>(layer->getCompOp()) || state.geometryOpacity < 1.0f - 1.0f / 255.0f;

        // Prepare the overlay buffer.
        if (state.useOverlay) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.previousFBO);

            if (_overlayBuffer3D.fbo == 0) {
                // Packed depth-stencil, which is the only way to get a 24-bit depth buffer
                // here: 16 bits over a terrain-sized near-far range quantises to several metres
                // at a couple of kilometres, enough to eat the bottom of every extrusion once
                // the terrain surface is an occluder in this buffer. The stencil half is unused.
                createFrameBuffer(_overlayBuffer3D, true, true, true);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer3D.fbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        // Seed the OVERLAY's own depth buffer with the terrain surfaces: it is composited back
        // as a flat quad with depth testing off, so without this a building behind a ridge
        // paints straight over it. Skipped inline (the main framebuffer already holds that
        // cover) and skipped for a comp-op layer with no extrusions - this is a full surface draw.
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
                // The overlay has its own depth buffer, so it needs its own seeding even under a
                // shared ground - but from the SAME cover, so an extrusion is occluded by exactly
                // the ridge that occludes it in the main framebuffer.
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
            // Extrusion clearance, two errors: the base ring samples elevation at arbitrary xy
            // while the mesh interpolates over cells (clip slack), and a wall standing ON the
            // surface is only separable to the depth buffer's resolution, which grows as
            // distance^2/near (constant-NDC) - without it the buffer eats most of a 40 m
            // building from a couple of km out. Uniform, so building-vs-building is unaffected.
            _terrainDrawDepthBias = _terrainDepthBias + TERRAIN_EXTRUSION_DEPTH_DELTAS * TERRAIN_LAYER_DEPTH_DELTA;
            _terrainDrawDepthClipUnits = (_terrainRegularGrid ? 2.0f : 12.0f);
        }

        if (!state.useOverlay) {
            // Same depth state the overlay's pre-pass leaves behind, so an extrusion is
            // resolved against the ground and against other extrusions exactly as before.
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            // A fading tile's extrusions are PREMULTIPLIED by their blend - colour and alpha
            // both - and the overlay path resolved that in its composite. Drawn inline there is
            // no composite, so they have to blend here or a half-faded building is written to
            // the framebuffer as near-black: the dark flash at the start of a fade in and the
            // end of a fade out. It also makes the degenerate first frames invisible rather
            // than a black footprint, since an extrusion fades in by GROWING from zero height.
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

        // Blend the rendered layer with framebuffer
        if (state.useOverlay) {
            if (!_overlayBuffer3D.depthStencilAttachments.empty()) {
                // TODO: for now it crashes. See why
//                glInvalidateFramebuffer(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer3D.depthStencilAttachments.size()), _overlayBuffer3D.depthStencilAttachments.data());
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
        // Leader lines first, all of them, then all the text: a line that crossed a neighbouring
        // label's glyphs would read as a strike-through. The extra pass only exists when some
        // label actually has a line to draw.
        bool anyCallout = std::any_of(labels.begin(), labels.end(), [](const std::shared_ptr<Label>& label) {
            return label->getStyle()->orientation == LabelOrientation::CALLOUT && label->getStyle()->calloutLineGlyph;
        });
        if (anyCallout) {
            renderLabelPass(labels, Label::DrawPass::CALLOUT_LINE);
        }
        renderLabelPass(labels, anyCallout ? Label::DrawPass::TEXT : Label::DrawPass::ALL);
    }

    void GLTileRenderer::renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, Label::DrawPass pass) {
        LabelBatchParameters labelBatchParams;
        // The atlas the batch being built draws from. A batch samples ONE texture, so a label from
        // another atlas ends the batch - the list is in draw order and stays that way.
        std::shared_ptr<const Bitmap> bitmap;
        std::shared_ptr<const TileLabel::Style> lastLabelStyle;
        int styleIndex = -1;
        int haloStyleIndex = -1;
        LabelPlateIndices plateIndices;
        int secondaryStyleIndex = -1;
        int iconStyleIndex = -1;
        int iconHaloStyleIndex = -1;
        for (const std::shared_ptr<Label>& label : labels) {
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
            std::shared_ptr<const BitmapPattern> labelPattern = labelStyle->glyphMap->getBitmapPattern();
            const std::shared_ptr<const Bitmap>& labelBitmap = labelPattern->bitmap;
            if (lastLabelStyle != labelStyle) {
                // The scene light, as far as the label's emissive lets it through. mapbox lights a
                // label like any other surface, but defaults text and icon to 1 - a no-op here -
                // which is what keeps a name legible over a night map. One factor for every colour
                // the style carries, evaluated once per label STYLE.
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
                // The halo may be lit HARDER than the ink it outlines: a name kept legible by a high
                // emissive over a map that darkens still wants its outline to go dark with the map,
                // or the two meet at the same grey and the outline stops separating anything.
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
                // Already in SCREEN PIXELS: the symbolizer scaled the style's radius by the pixel
                // scale, and labelFsh measures the halo against the same one screen pixel the
                // antialias ramp is. The cap is where the encoded field runs out.
                float haloRadius = std::min(evaluateFloatFunc(labelStyle->haloRadiusFunc), MAX_HALO_PIXELS);

                // Two plates - one behind the text, one behind the icon - and each colour is one
                // more slot in the batch, exactly like the halo. A plate with a border needs two,
                // and they must be CONSECUTIVE: one quad draws both (see LabelPlateIndices).
                const TileLabel::Style::Plate* plateList[2] = { &labelStyle->textPlate, &labelStyle->iconPlate };
                int plateCount = 0;
                for (int i = 0; i < 2; i++) {
                    plateCount += (plateList[i]->draws() ? (plateList[i]->drawsBorder() ? 2 : 1) : 0);
                }
                // The second run of text may have its own colour, which is one more slot in the
                // batch - exactly like the halo and the plate.
                bool hasSecondaryColor = static_cast<bool>(labelStyle->secondaryColorFunc);
                cglib::vec4<float> secondaryColor = hasSecondaryColor ? lit(cglib::vec4<float>(evaluateColorFunc(*labelStyle->secondaryColorFunc).rgba())) : color;
                // And so may the icon run - a font icon in its own colour next to the name.
                bool hasIconColor = static_cast<bool>(labelStyle->iconColorFunc);
                cglib::vec4<float> iconColor = hasIconColor ? lit(cglib::vec4<float>(evaluateColorFunc(*labelStyle->iconColorFunc).rgba())) : color;
                // And its own halo - an icon never takes the text's (see Label::appendLabelPlates).
                float iconHaloRadius = labelStyle->iconHaloRadiusFunc ? std::min(evaluateFloatFunc(*labelStyle->iconHaloRadiusFunc), MAX_ICON_HALO_PIXELS) : 0.0f;
                bool hasIconHalo = iconHaloRadius > 0.0f && labelStyle->iconHaloColorFunc;
                cglib::vec4<float> iconHaloColor = hasIconHalo ? litHalo(cglib::vec4<float>(evaluateColorFunc(*labelStyle->iconHaloColorFunc).rgba())) : color;
                // The em size the ICON RUN is drawn at, which is not the text's: the run keeps a
                // fixed pixel size of its own (Label::calculateVertexData scales it by
                // iconRefSize/size, then by its own ramp). The width table is what labelVsh divides
                // uSDFRamp by, so an icon sharing the text's slot got the text's antialias ramp -
                // 20 px of icon smoothed as if it were 13 px of text, which fattened every thin
                // stroke until it closed: a fork lost the gaps between its tines.
                float iconSize = labelStyle->iconRefSize > 0.0f ? labelStyle->iconRefSize : size;
                if (labelStyle->iconScaleFunc && labelStyle->iconRefScale > 0.0f) {
                    iconSize *= evaluateFloatFunc(*labelStyle->iconScaleFunc) / labelStyle->iconRefScale;
                }
                // A slot of its own whenever that ramp would differ, not only for a coloured icon.
                bool hasIconRun = hasIconColor || iconSize != size;
                // The style layer's own occluded opacity, or this layer's default where it sets
                // none - and a batch carries one, so a change ends it like a change of atlas does.
                float labelOcclusionOpacity = labelStyle->occlusionOpacity.value_or(_labelOcclusionOpacity);
                if (bitmap != labelBitmap || labelBatchParams.occlusionOpacity != labelOcclusionOpacity || labelBatchParams.scale != labelStyle->scale || labelBatchParams.glyphRenderSize != labelStyle->glyphRenderSize || labelBatchParams.parameterCount + 2 + plateCount + (hasSecondaryColor ? 1 : 0) + (hasIconRun ? 1 : 0) + (hasIconHalo ? 1 : 0) > LabelBatchParameters::MAX_PARAMETERS) {
                    renderLabelBatch(labelBatchParams, bitmap);
                    bitmap = labelBitmap;
                    labelBatchParams.labelCount = 0;
                    labelBatchParams.parameterCount = 0;
                    labelBatchParams.scale = labelStyle->scale;
                    labelBatchParams.glyphRenderSize = labelStyle->glyphRenderSize;
                    labelBatchParams.occlusionOpacity = labelOcclusionOpacity;
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
                // Every plate colour needs its own slot in the batch - exactly like the halo.
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
                    // The icon's plate is its background, so icon-opacity fades it with the glyph
                    // on it - LIVE, because that opacity is a zoom ramp: baked at decode, a POI
                    // whose icon a zoom step hides kept its disc until the tile was decoded again,
                    // which is the coloured square that flashed while zooming.
                    float plateOpacity = (i == 1 && labelStyle->iconOpacityFunc ? evaluateFloatFunc(*labelStyle->iconOpacityFunc) : 1.0f);
                    cglib::vec4<float> fillColor = lit(cglib::vec4<float>(plate.style.color.rgba())) * plateOpacity;
                    cglib::vec4<float> borderColor = lit(cglib::vec4<float>(plate.style.borderColor.rgba())) * plateOpacity;
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

            VT_STAT_CLOCK(statClock);
            std::size_t labelVertexOffset = _labelVertices.size();
            label->calculateVertexData(labelBatchParams.widthTable[styleIndex], _viewState, styleIndex, haloStyleIndex, _labelVertices, _labelOffsets, _labelNormals, _labelTexCoords, _labelAttribs, _labelIndices, pass, pass == Label::DrawPass::CALLOUT_LINE ? LabelPlateIndices() : plateIndices, pass == Label::DrawPass::CALLOUT_LINE ? -1 : secondaryStyleIndex, pass == Label::DrawPass::CALLOUT_LINE ? -1 : iconStyleIndex, pass == Label::DrawPass::CALLOUT_LINE ? -1 : iconHaloStyleIndex);
            if (labelStyle->transform) {
                // Conjugated by the tile matrix the style's translate is a pure world translation, so
                // it rides on the vertices - as a BATCH matrix it made every such label its own draw.
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

        // Screen-space composite quad; must never be depth-tested (in terrain mode the
        // depth buffer contains the terrain depth pre-pass, which would clip the quad)
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
        // Memoised for the frame: setupTerrainUniforms runs per DRAW and the answer cannot
        // change within a frame. The provider call doubles as the LRU touch in the SDK's
        // elevation texture cache, but that cache only needs the entry marked used ONCE in the
        // current frame to protect it from eviction, which one call per tile still does.
        auto it = _terrainTextureCache.find(tileId);
        if (it != _terrainTextureCache.end()) {
            return it->second;
        }
        std::pair<bool, TerrainTexture> resolved(false, TerrainTexture());
        resolved.first = _terrainTextureProvider && _terrainTextureProvider(tileId, resolved.second);
        return _terrainTextureCache.emplace(tileId, resolved).first->second;
    }

    bool GLTileRenderer::setupTerrainUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix, bool gridSurface) {
        // GPU draping: bind the tile's elevation texture and the transforms taking vertex xy (in
        // vertexFrameMatrix's frame) to elevation uv and to the mercator latitude argument.
        // The clip-constant slack grows linearly with distance in eye units, matching how the
        // surface-vs-geometry interpolation error grows with the mesh cell. NEGATIVE units push the
        // draw AWAY: the surface pre-pass takes the slack so content keeps its REAL depth.
        float clipUnits = _terrainDrawDepthClipUnits;
        double tileSize = std::abs(_transformer->calculateTileMatrix(tileId, 1.0f)(0, 0));
        double projScaleZ = std::abs(_viewState.projectionMatrix(2, 2));
        // The interpolation error is curvature limited and QUADRATIC in the cell size: a linear
        // slack calibrated at low zoom overshoots several-fold at high zoom, and the excess is
        // exactly the depth range that ignores occlusion. Anchored at zoom 11 tiles, and scaled by
        // the mesh resolution (doubling it allows a 4x tighter slack).
        double slackScale = tileSize * std::min(4.0, tileSize / TERRAIN_DEPTH_CLIP_REF_TILE_SIZE) * _terrainSlackScale;
        // The clip slack magnitude is the same proven, twist-clearing value in both models;
        // the sign / which draw carries it differs (see the loop). The painter-order per-layer
        // delta uniforms are unused - painter-order is expressed purely as a surface back-push.
        // Tangram's depth_shift rides on top of the geometric slack and is deliberately NOT scaled
        // by the tile size: it is a fixed clip-space pull whose NDC effect dies off as 1/w.
        // In the shared-ground (tangram) model the shift rides in the PER-LAYER term below, not in
        // the slack: adding it here as well would apply it twice.
        double contentShift = (gridSurface || _terrainSharedGround ? 0.0 : _terrainContentDepthShift * projScaleZ);
        glUniform1f(shaderProgram.uniforms[U_DEPTHBIASCLIP], static_cast<float>(clipUnits * TERRAIN_DEPTH_CLIP_SLACK * slackScale * projScaleZ + contentShift));
        // Tangram's per-layer term, (proxy - layer) * (2^-19 * w + depth_shift). It is what lets
        // content WRITE depth without style layers z-fighting each other, and what makes a live
        // tile beat the proxy it replaces - which is the job the stencil tile masks were doing.
        // Zero for the surface itself, which is the bottom of the stack.
        // Surfaces take it too: a terrain paint IS a surface and still has to sit at its own place
        // in the stack's depth order, or the fills of the layer under it are pulled in front of it.
        // The ground pass sets the offset to 0 itself, which is where the stack starts.
        glUniform1f(shaderProgram.uniforms[U_LAYERDEPTHOFFSET], _terrainDrawLayerOffset);
        // depth_shift, tangram's, verbatim from res/scenes/terrain-3d.yaml:
        //     depth_shift = -0.02*u_proj[2][3];
        // glm::perspective puts -1 in [2][3], so it is a FLAT 0.02 - not scaled by the projection
        // at all. Their comment says what it is for: "use larger depth delta near camera to prevent
        // terrain from covering geometry", which is exactly what un-subdivided content needs, since
        // it chords across the terrain between its own vertices. Constant CLIP, so it is large near
        // the camera - where the chord error is - and dies as 1/w at range, where a forward pull
        // would leak over a ridge.
        double depthShift = (_terrainSharedGround ? _terrainContentDepthShift : 0.0);
        glUniform1f(shaderProgram.uniforms[U_DEPTHSHIFT], static_cast<float>(depthShift));

        // Metre-constant clearance (see applyDepthBias). proj[2][3] is -2*far*near/(far-near), the
        // term that turns an eye distance into ndc, so multiplying the clearance in world units by
        // it gives the clip offset that the shader divides by w. Set per draw and zero for
        // everything that does not chord over the ground - the surfaces, the backgrounds and the
        // rasters ARE the ground.
        glUniform1f(shaderProgram.uniforms[U_DEPTHCLEARANCE], static_cast<float>(_terrainDrawClearance * _viewState.projectionMatrix(2, 3)));

        // Cross-LOD edge stitching bends the edge shared with a COARSER neighbour onto that
        // neighbour's chords. Draped CONTENT takes it too - a road crossing the seam must land on
        // the same stitched edge as the ground it lies on, or its halves meet at different heights
        // (invisible from straight down, a step as soon as the camera tilts).
        cglib::vec4<float> edgeCoarsening(1, 1, 1, 1);
        if (!_terrainEdgeCoarseningMap.empty()) {
            auto edgeIt = _terrainEdgeCoarseningMap.find(tileId);
            if (edgeIt != _terrainEdgeCoarseningMap.end()) {
                edgeCoarsening = edgeIt->second;
            }
        }
        glUniform4f(shaderProgram.uniforms[U_TERRAINEDGECOARSENING], edgeCoarsening(0), edgeCoarsening(1), edgeCoarsening(2), edgeCoarsening(3));
        // Vertex frame units -> TARGET tile units, for the edge test above and the fragment tile
        // clip. The OFFSET is what makes it hold for a STAND-IN: with the scale alone the source
        // ancestor's unit square lands in [0, 2^dz] and the clip keeps only the one quadrant inside
        // [0, 1], blanking every layer served by a stand-in for a second after each zoom step.
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
            glUniform2f(shaderProgram.uniforms[U_TILEUNITSCALE], 0.0f, 0.0f); // no tile clipping without elevation
            glUniform2f(shaderProgram.uniforms[U_TILEUNITOFFSET], 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_LAYERDEPTHOFFSET], 0.0f);
            glUniform1f(shaderProgram.uniforms[U_DEPTHSHIFT], 0.0f);
            return false;
        }

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, terrainTexture.textureId);
        glUniform1i(shaderProgram.uniforms[U_ELEVATIONTEXTURE], 1);
        glActiveTexture(GL_TEXTURE0);

        cglib::vec2<double> frameOrigin(vertexFrameMatrix(0, 3), vertexFrameMatrix(1, 3));
        cglib::vec2<double> frameScale(vertexFrameMatrix(0, 0), vertexFrameMatrix(1, 1));
        double invSizeX = 1.0 / terrainTexture.internalSize(0);
        double invSizeY = 1.0 / terrainTexture.internalSize(1);
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
        // Lattice clamp: draped geometry snaps its height to the same regular grid the surface is
        // built from. The cell size is a property of the tile+texture, identical for the surface and
        // every draped layer whatever their frame; 0 in adaptive mode.
        // THE SURFACE DOES NOT NEED IT - its vertices ARE the nodes, so the clamp returns the same
        // height for 16 texture fetches a vertex instead of one. Except on a stitched edge, where
        // the clamp is what bends the outermost cell onto the coarse neighbour's chords.
        bool latticeNodes = gridSurface && edgeCoarsening == cglib::vec4<float>(1, 1, 1, 1);
        if (_terrainRegularGrid && _terrainRegularGridResolution > 0 && _terrainDemTaps >= 16 && !latticeNodes) {
            double worldTileSize = std::abs(_transformer->calculateTileMatrix(tileId, 1.0f)(0, 0));
            float latticeCellX = static_cast<float>(worldTileSize * invSizeX / _terrainRegularGridResolution);
            float latticeCellY = static_cast<float>(worldTileSize * invSizeY / _terrainRegularGridResolution);
            glUniform2f(shaderProgram.uniforms[U_ELEVATIONLATTICECELL], latticeCellX, latticeCellY);
        } else {
            glUniform2f(shaderProgram.uniforms[U_ELEVATIONLATTICECELL], 0.0f, 0.0f);
        }
        double frameScaleZ = (vertexFrameMatrix(2, 2) != 0 ? vertexFrameMatrix(2, 2) : 1.0);
        // An extrusion's CPU base is already in INTERNAL z units - getDisplayHeight applied the
        // exaggeration and the mercator stretch - so it owes only the frame scale, the same
        // 1/frameScaleZ folded into uElevationScale.x for heights that come from the texture.
        glUniform1f(shaderProgram.uniforms[U_BASESCALE], static_cast<float>(1.0 / frameScaleZ));
        glUniform4f(shaderProgram.uniforms[U_ELEVATIONSCALE],
            static_cast<float>(terrainTexture.metersToInternal / frameScaleZ),
            static_cast<float>(frameOrigin(1) * terrainTexture.mercatorYScale),
            static_cast<float>(frameScale(1) * terrainTexture.mercatorYScale),
            static_cast<float>(-vertexFrameMatrix(2, 3) / frameScaleZ)); // tile surface frames are origin-relative, with a non-zero origin z in terrain mode
        return true;
    }

    void GLTileRenderer::setupTerrainLightingUniforms(const ShaderProgram& shaderProgram, const TileId& tileId, const cglib::mat4x4<double>& vertexFrameMatrix) {
        // The slope scale converts a height difference in metres into world units per unit of
        // elevation-uv, so the fragment shader's central difference reproduces the slope of the
        // surface that the vertex stage actually displaced (exaggeration included, because it is
        // baked into metersToInternal). The mercator 1/cos(latitude) stretch is applied per
        // fragment through vElevCosh.
        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(tileId);
        bool valid = resolved.first;
        const TerrainTexture& terrainTexture = resolved.second;
        float slopeX = 0.0f, slopeY = 0.0f;
        if (valid && terrainTexture.internalSize(0) > 0 && terrainTexture.internalSize(1) > 0) {
            slopeX = static_cast<float>(terrainTexture.metersToInternal / terrainTexture.internalSize(0));
            slopeY = static_cast<float>(terrainTexture.metersToInternal / terrainTexture.internalSize(1));
        }
        glUniform2f(shaderProgram.uniforms[U_TERRAINSLOPESCALE], slopeX, slopeY);
        glUniform3f(shaderProgram.uniforms[U_SUNDIR], _terrainLighting.sunDir(0), _terrainLighting.sunDir(1), _terrainLighting.sunDir(2));
        glUniform4f(shaderProgram.uniforms[U_SUNCOLOR], _terrainLighting.sunColor(0), _terrainLighting.sunColor(1), _terrainLighting.sunColor(2), 1.0f);
        glUniform4f(shaderProgram.uniforms[U_AMBIENTCOLOR], _terrainLighting.ambientColor(0), _terrainLighting.ambientColor(1), _terrainLighting.ambientColor(2), 1.0f);
        glUniform2f(shaderProgram.uniforms[U_LIGHTPARAMS], _terrainLighting.sunIntensity, _terrainLighting.ambientIntensity);
    }

    bool GLTileRenderer::resolveExtrusionBases(const TileId& sourceTileId, const TileId& targetTileId, const std::shared_ptr<TileGeometry>& geometry) const {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry->getVertexGeometryLayoutParameters();
        if (params.baseOffset < 0 || params.texCoordOffset < 0 || !_extrusionElevationProvider) {
            return true; // not an extrusion, or no elevation at all - the ground is the base
        }
        unsigned int version = _extrusionBaseVersion.load(std::memory_order_relaxed);
        if (geometry->isBaseResolved() && geometry->getBaseElevationVersion() == version) {
            return true;
        }
        // Nothing to resolve against yet: leave the sentinel in place and try again next frame.
        // The elevation is asked of the TARGET tile because that is what the draw itself binds
        // from - under overzoom the source tile is a coarser ancestor with no elevation entry.
        // NOT a reason to skip the draw: the shader falls back to the per-vertex ground, which is
        // wrong on a slope but visible, where a base of 0 would bury the building at sea level.
        if (!resolveTerrainTexture(targetTileId).first) {
            return false;
        }
        const VertexArray<std::uint8_t>& vertexGeometry = geometry->getVertexGeometry();
        if (vertexGeometry.empty() || params.vertexSize <= 0) {
            return false;
        }

        // The centroid rides in the texcoord slot, at the coord scale (see packGeometry). Tile
        // matrix and shader agree only on UNFLIPPED tile coords - the transformer flipped y when
        // the centroid was stored, and polygon3DVsh flips it back for exactly this reason.
        cglib::mat3x3<double> tileMatrix = calculateTileMatrix2D(sourceTileId, 1.0f);
        std::size_t vertexCount = vertexGeometry.size() / params.vertexSize;
        // One elevation query per FOOTPRINT, not per vertex: every vertex of a building carries the
        // same centroid and a city tile runs to tens of thousands of vertices over a few hundred
        // buildings. The vertices of one footprint are contiguous, so remembering the last one is
        // the whole cache - the same trick ElevationManager::getGridForInternalPos plays.
        std::int32_t lastU = 0, lastV = 0;
        double lastHeight = 0;
        bool haveLast = false;
        bool haveHeight = false;
        bool allResolved = true;
        for (std::size_t i = 0; i < vertexCount; i++) {
            const std::int16_t* texCoordPtr = reinterpret_cast<const std::int16_t*>(vertexGeometry.data() + i * params.vertexSize + params.texCoordOffset);
            std::int32_t u = texCoordPtr[0];
            std::int32_t v = texCoordPtr[1];
            if (!haveLast || u != lastU || v != lastV) {
                cglib::vec2<double> tilePos(u / static_cast<double>(params.texCoordScale),
                                            1.0 - v / static_cast<double>(params.texCoordScale));
                cglib::vec2<double> internalPos = cglib::transform_point(tilePos, tileMatrix);
                haveHeight = _extrusionElevationProvider(cglib::vec3<double>(internalPos(0), internalPos(1), 0), lastHeight);
                lastU = u;
                lastV = v;
                haveLast = true;
            }
            // A footprint whose DEM has not decoded yet keeps the sentinel and is retried next
            // frame. Writing the provider's 0 instead is what buried every building: a base of 0
            // where the ground is 215 m puts the whole prism under the terrain surface.
            if (haveHeight) {
                geometry->setVertexBase(i, static_cast<float>(lastHeight));
            } else {
                allResolved = false;
            }
        }
        if (!allResolved) {
            return false; // still drawn, on the per-vertex ground, until the elevation lands
        }
        geometry->setBaseResolved(true);
        geometry->setBaseElevationVersion(version);
        return true;
    }

    void GLTileRenderer::renderTileMask(const TileId& tileId) {
#if MASSIF_VT_RENDER_STATS
        VT_STAT_CLOCK(maskClock);
        struct MaskTimer { std::chrono::steady_clock::time_point& c; ~MaskTimer() { VT_STAT_SPLIT(surfMaskNs, c); } } maskTimer { maskClock };
#endif
        bool gridMode = _terrainRegularGrid && _terrainMode && static_cast<bool>(_terrainTextureProvider);
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
        // Debug view: what the stencil buffer ACTUALLY contains at the end of the 2D pass.
        // Each allocated stencil value is shown as a distinct translucent color via a
        // stencil-tested fullscreen quad; value 0 (pixels owned by no tile mask) = black.
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

        // Unowned pixels first (black), then one color per allocated value
        glStencilFunc(GL_EQUAL, 0, 255);
        Color black(0.0f, 0.0f, 0.0f, 1.0f);
        glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, black.rgba().data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        for (const std::pair<TileId, GLint>& tileMask : _debugOrderedTileMasks) {
            int v = tileMask.second;
            glStencilFunc(GL_EQUAL, v, 255);
            // Color keyed by TILE ZOOM: z10-=grey, z11=red, z12=orange, z13=yellow,
            // z14=green, z15=cyan, z16+=magenta
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
        // The displaced tile surface as a solid color (or depth-only when transparent).
        // Drawn UNDER the style content with the per-draw depth bias applied - the
        // terrain pre-pass renders it pushed slightly back so content passes over it
        // at its real depth.
        bool gridMode = _terrainRegularGrid && _terrainMode && static_cast<bool>(_terrainTextureProvider);
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        for (const std::shared_ptr<TileSurface>& tileSurface : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(tileId))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            unsigned int terrainFlag = (_terrainMode && _terrainTextureProvider ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0);
            // The shared ground is the lit terrain surface - the same role the drape surface has
            // when there is a drape - so it takes the sun and the shadow map exactly as that does.
            // The depth pre-passes and the 3D overlay seeding ask for the plain fill: they are
            // colour-masked or invisible, and lighting them is pure shader cost.
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

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glUniform4fv(shaderProgram.uniforms[U_COLOR], 1, color.rgba().data());
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfFillDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());

            disableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION]);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            checkGLError();
        }
    }

    GLuint GLTileRenderer::ensureDrapeTexture(const TileId& tileId) {
        GLuint& tex = _drapeTextures[tileId];
        if (tex == 0) {
            // Recycle through a pool: tiles enter and leave the visible set constantly while
            // panning, and glGenTextures/glTexImage2D per tile per pan is real allocation churn.
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
        // The drape cover wins over a paint's: under a cross-layer drape the shared surface is
        // drawn for the drape leaves, and a paint bakes into that texture flat instead of drawing
        // a surface of its own. Both are handed in every frame, so only rebuild on a real change.
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
            // No leaf of its own: either there is no shared ground at all, or the cover is COARSER
            // here (the split hit its cap). The tile then draws on its own surface, one tesselation
            // finer than the ground it stands on - it hugs the same height field, so it stays
            // within the content slack.
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

        // The ground is the frame's only depth-writing terrain geometry, at its TRUE depth: it
        // blocks the far slope of a ridge exactly, and everything drawn after it tests against it
        // with GL_LEQUAL and no forward pull. Pushing it back instead is what opened the
        // see-through band of rounds 45-56 - do not.
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

        // With the paint AS the ground there is one draw per tile, not two: the paint carries this
        // colour as its base and shades it, which is tangram's terrain raster. The fill is then
        // only needed where the paint cannot draw - a tile whose elevation has not arrived, which
        // the paint skips - or the ground would have a hole showing the flat background plane.
        bool paintIsGround = _terrainPaintOnGround && _terrainPaint.enabled && _lightingShaderNormalMap && !_lightingShaderNormalMap->perVertex && _terrainRegularGrid && _terrainTextureProvider;

        int surfaceDraws = 0;
        for (std::size_t i = 0; i < _terrainGroundTiles.size(); i++) {
            if (paintIsGround) {
                const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(_terrainGroundTiles[i]);
                if (resolved.first && resolved.second.textureId != 0 && resolved.second.metersPerTexel > 0.0f) {
                    continue; // the paint draws this tile, base colour included
                }
            }
            // The bottom of the stack (offset 0) unless this tile is standing in on a coarser
            // level, in which case it is pushed back hard - tangram's `proxy *= 48` for the terrain
            // raster. A stand-in is a DIFFERENT height field: where it rises above the level it
            // replaces it pokes through the content drawn on that level.
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
            // A paint renderer holds no tiles: it cannot extend the cover (it paints whatever the
            // other layers put in it) and it has nothing per-tile to fingerprint. Reporting the
            // previous frame's cover instead makes every tile that has just entered it look
            // incomplete, and the owner bakes it a second time. Its appearance is watched through
            // the drape STACK signature (TileLayer::drapeStackSignature) instead.
            return;
        }
        if (!_visibleRenderTiles) {
            return;
        }
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            // EVERY visible tile is reported, including ones whose content has not loaded yet.
            // Taking over the surface means the per-layer pre-pass no longer draws one, so a tile
            // omitted here gets no terrain surface at all and the global terrain background shows
            // through it - which is what the pre-pass used to cover unconditionally. Its drape
            // texture is simply empty until content arrives.
            // Combined, so a target tile covered by several render tiles of this renderer gets a
            // fingerprint reflecting all of them.
            std::size_t& fingerprint = drapeTiles[renderTile.targetTileId];
            std::size_t contribution = calculateDrapeFingerprint(renderTile);
            if (contribution == 0) {
                continue; // reported for the cover, but nothing here to bake: the entry stays 0
            }
            // Mixing in a zero contribution would still leave a non-zero fingerprint, and the owner
            // reads a non-zero fingerprint as "this layer HAS something for that tile". It then
            // waits for a bake that can never deliver it, keeps the tile marked incomplete and goes
            // on drawing the previous, finer generation's textures over it - a patch of stale map
            // that survives a zoom out for as long as those textures stay cached.
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

    int GLTileRenderer::bakeDrapeCoverage(const TileId& targetTileId, int fromStyleLayerIdx) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState();

        if (_terrainPaint.enabled) {
            // A paint SHADES the ground the other layers put in the drape; treating it as an
            // occluder would hide every live layer under it outright. It contributes no coverage,
            // so a contour below a paint stays visible - which is what it did before #175.
            return 0;
        }
        _drapeCoveragePass = true;
        int baked = bakeDrapeUnits(targetTileId, fromStyleLayerIdx);
        _drapeCoveragePass = false;
        return baked;
    }

    int GLTileRenderer::bakeDrapeUnits(const TileId& targetTileId, int fromStyleLayerIdx) {
        if (!_visibleRenderTiles) {
            return 0;
        }
        _drapeMaskTexture = 0; // nothing baked is ever masked - the mask is what the bake produces
        int bakedPrimitives = 0;
        cglib::mat4x4<float> drapeOrtho;
        _drapeMVPOverride = &drapeOrtho;

        // The bake owns its GL state. It runs from the owner (MapRenderer) BEFORE any layer's
        // own render pass, so nothing has established the state the per-layer drape path used to
        // inherit. Culling in particular must be OFF: the bake matrix maps tile-local xy straight
        // to clip space with no y flip, while the on-screen matrix goes through a projection that
        // does flip y - so every triangle bakes with the opposite winding and back-face culling
        // silently discards the whole tile's fills.
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);

        // Take render tiles that COVER this terrain tile - a coarser layer contributes through its
        // ancestor - baked COARSEST FIRST, proxies before active tiles at the same zoom, or a
        // parent's background paints over a child's content. _visibleRenderTiles is in no such order.
        // COVERS strictly: a FINER tile baked into its sub-rect is minified with no mipmap, and a
        // zoom out (which holds a whole finer generation) turns the drape into white aliasing noise.
        // Those tiles are the generation being replaced and fade out in the 3D pass instead.
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
            const RenderTile& renderTile = *renderTilePtr;
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!hasDrapeableContent(renderLayer)) {
                    continue;
                }
                // A coverage bake starts part-way up the style stack: only the units ABOVE the live
                // layer being masked occlude it.
                if (renderLayer.layer->getLayerIndex() < fromStyleLayerIdx) {
                    continue;
                }
                // A render layer can be finer than the render tile that holds it (retained
                // children blending out). Such a layer may sit entirely OUTSIDE this terrain tile
                // - baking it anyway painted a neighbouring tile's content over this one - and
                // even when it is inside, it belongs to the generation being replaced, not to
                // this tile. Same rule as above: it has to COVER the terrain tile.
                if (!tileCovers(renderLayer.targetTileId, targetTileId)) {
                    continue;
                }
                // Backgrounds/rasters draw their own target tile's surface mesh (their uv logic
                // resolves source-vs-target overzoom); geometry is in source tile coordinates.
                // Either may be coarser OR finer than the terrain tile, hence the sub-rect in
                // each case.
                float geometryOpacity = calculateDrapeOpacity(renderLayer);
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                    renderTileBackground(renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, background);
                    bakedPrimitives++;
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                    renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, bitmap);
                    bakedPrimitives++;
                }
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (isDrapeableGeometry(geometry->getType()) && isLayerDraped(renderLayer.layer)) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, geometry);
                        bakedPrimitives++;
                    }
                }
            }
        }

        _drapeMVPOverride = nullptr;
        checkGLError();
        return bakedPrimitives;
    }

    void GLTileRenderer::collectDrapeStackOrder(std::vector<std::pair<int, bool> >& units) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_terrainPaint.enabled || !_visibleRenderTiles) {
            return; // a paint has no style layers of its own; see bakeDrapeCoverage
        }
        // Layer INDEX order, which is the order the live pass and the bake both draw in. One entry
        // per style layer, whatever how many tiles carry it: the cut is a property of the stack.
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
                    drapeable = drapeable || isDrapeableGeometry(geometry->getType());
                }
                if (drapeable) {
                    drapedByLayer[renderLayer.layer->getLayerIndex()] = draped;
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
            // COARSER or equal only: a mask finer than the tile being drawn would need several
            // textures in one draw. That draw keeps the pre-#175 behaviour and is drawn on top.
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

    void GLTileRenderer::setLabelOcclusionDepth(GLuint depthTexture, float occluderSize) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelOcclusionTexture = depthTexture;
        _labelOcclusionSize = occluderSize;
    }

    bool GLTileRenderer::hasStyledLabelOcclusion() const {
        std::lock_guard<std::mutex> lock(_mutex);

        return _labelOcclusionStyled;
    }

    void GLTileRenderer::setLabelOcclusionOpacity(float occludedOpacity) {
        std::lock_guard<std::mutex> lock(_mutex);

        _labelOcclusionOpacity = occludedOpacity;
    }

    int GLTileRenderer::renderLabelOcclusionDepth() {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles) {
            return 0;
        }
        // The shadow CASTER path, pointed at the camera instead of the sun: the extrusions as they
        // are drawn on screen - same shader, same terrain anchoring - with their window depth
        // packed into the colour channels. A depth-texture target would say the same thing more
        // directly, but sampling one from a vertex shader is not something every driver here does,
        // and this path is already proven on all of them.
        int drawn = 0;
        cglib::mat4x4<double> cameraViewProj = _viewState.projectionMatrix * _viewState.cameraMatrix;
        _shadowCasterViewProj = &cameraViewProj;
        forEachVisibleExtrusion(nullptr, [this, &drawn](const RenderTileLayer& renderLayer, const std::shared_ptr<TileGeometry>& geometry) {
            // The tile's own blend, as the shadow caster uses: an extrusion fades in by GROWING,
            // so a full-height occluder hides labels behind a building that is not there yet.
            renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
            drawn++;
            return true;
        });
        _shadowCasterViewProj = nullptr;
        checkGLError();
        return drawn;
    }

    int GLTileRenderer::bakeGroundAOMask(const TileId& targetTileId) {
        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

        if (!_visibleRenderTiles || !(_groundAOIntensity > 0.0f)) {
            return 0;
        }
        // STATE-NEUTRAL on purpose: this runs inside the drape bake, which has already established
        // its own (culling off above all - the bake matrix does not flip y, so a stray glEnable
        // there empties every tile baked afterwards). The caller owns blend, cull, depth and the
        // framebuffer; this only picks the frame and the pass.
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
                        // Blend 1, like every other bake: the tile's fade-in is a per-frame value
                        // and this picture is cached. A tile baked in the frame its extrusions
                        // arrived - which is exactly when the shadow first has anything to bake -
                        // froze the shadow at the fade's starting strength, i.e. at nothing.
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
            return -2; // the shared grid the drape UV depends on is not active
        }
        // renderTileSurfaceDrape reads the texture from the map; swap the external one in for the
        // duration of the draw so the two paths share one surface implementation.
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
        // Stand-in for a tile whose drape texture is not baked yet: the SAME surface mesh, in the
        // terrain background colour. Drawing it matters more than its colour does - the surface is
        // the terrain's only depth writer, and a tile skipped here leaves a depth hole that vector
        // elements and billboards behind the terrain immediately show through.
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
        // Flat and unblended: this is a copy, not a draw over something. The unit quad is the
        // same one the flat bake uses, so no geometry is built for it.
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
        // Called on renderer reset/deinit. Without this the FBO and every cached drape texture
        // survive GL context loss as stale names - a leak, and a source of draws against
        // handles that no longer exist.
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
    }

    int GLTileRenderer::renderTerrainPaint(const TileId& targetTileId) {
        // The paint has no tiles of its own: it is a function of the elevation texture the
        // terrain already binds for this tile, drawn as ONE quad into the shared drape texture
        // at this layer's place in the bake order. Nothing is fetched, decoded or uploaded.
        if (!_lightingShaderNormalMap || _lightingShaderNormalMap->perVertex) {
            return 0; // the lighting shader IS the hillshade algorithm; without it there is no paint
        }
        const std::pair<bool, TerrainTexture>& resolved = resolveTerrainTexture(targetTileId);
        if (!resolved.first || resolved.second.textureId == 0 || resolved.second.metersPerTexel <= 0.0f) {
            return 0; // no elevation data for this tile yet - report "nothing baked", not "done"
        }
        const TerrainTexture& terrainTexture = resolved.second;

        // The bake owns its GL state: it runs before any layer's own pass, so nothing has
        // established one. Culling off (the bake matrix does not flip y, so the winding is
        // reversed), depth off, blend on - the paint composites over what earlier layers baked.
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
            // Binds the elevation texture and the tile-local -> elevation uv transform. The
            // vertex frame is the tile matrix, so the quad's [0,1] xy maps straight onto it.
            setupTerrainUniforms(shaderProgram, targetTileId, surfaceFrame, false);

            // The DEM gradient is in metres per texel; the hillshade algorithms want the
            // dimensionless slope, scaled by the layer's height scale. The mercator
            // 1/cos(latitude) stretch is applied per fragment (vElevCosh).
            float slopeScale = _terrainPaint.heightScale * calculateTerrainPaintReliefBoost(terrainTexture.metersPerTexel) / terrainTexture.metersPerTexel;
            glUniform2f(shaderProgram.uniforms[U_PAINTSLOPESCALE], slopeScale, slopeScale);
            glUniform4f(shaderProgram.uniforms[U_PAINTPARAMS], _terrainPaint.contrast, _terrainPaint.opacity, 0.0f, 0.0f);
            _lightingShaderNormalMap->setupFunc(shaderProgram.program, _viewState);

            glBindBuffer(GL_ARRAY_BUFFER, compiledTileSurface.vertexGeometryVBO);
            enableVertexAttrib(shaderProgram.attribs[A_VERTEXPOSITION], 3, GL_FLOAT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.coordOffset));
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
        // No drape to bake into: draw the paint as the terrain surface itself, one draw per tile
        // over the shared grid VBO - which is what tangram does (the hillshade is a raster style
        // drawn on the tile's terrain mesh). The tiles come from the owner, because a paint has no
        // tile set of its own.
        // Under a shared ground the paint is one of the layers composited onto it, so it draws the
        // cover the ground was drawn from - the same tiles, the same lattice, coincident to the
        // bit - and leaves the depth alone. On its own it IS the surface, and writes depth.
        const std::vector<TileId>& paintTiles = (_terrainSharedGround ? _terrainGroundTiles : _terrainPaintTiles);
        if (!_lightingShaderNormalMap || _lightingShaderNormalMap->perVertex || paintTiles.empty()) {
            return 0;
        }
        if (!(_terrainRegularGrid && _terrainMode && _terrainTextureProvider)) {
            return 0; // the shared grid surface is what this draws
        }

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        // Writes depth unless a ground pass already established it. As the ground it MUST write:
        // there is no fill draw underneath it any more.
        glDepthMask(_terrainSharedGround && !asGround ? GL_FALSE : GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        setCompOp(CompOp::SRC_OVER);
        // Under a shared ground the paint draws the SAME grid, displaced by the same DEM, as the
        // ground pass already drew - but from a different program, so the two clip z values differ
        // in the last float bits and GL_LEQUAL drops a scatter of fragments, showing the bare
        // ground colour through the shading as white speckles. One delta of clearance (the value
        // backgrounds carry over the surface they share) is all it takes.
        // As the ground there is no second copy of the surface to clear, so no delta is needed -
        // and none is wanted, since the delta exists to lift the paint off a fill it no longer has.
        _terrainDrawDepthBias = _terrainDepthBias + (_terrainSharedGround && !asGround ? TERRAIN_LAYER_DEPTH_DELTA : 0.0f);
        _terrainDrawDepthClipUnits = 0.0f;
        // At this layer's place in the stack's depth order, like any other content of it - or at
        // the BOTTOM of it (ordinal 0) when the paint is the ground, which is where tangram draws
        // the terrain raster (`order: global.earth_order`, 0 in their demo scene).
        _terrainDrawLayerOffset = (_terrainSharedGround && !asGround ? -static_cast<float>(_terrainLayerOrdinalBase) : 0.0f);

        // The paint COVERS the ground it is drawn on, so it has to carry the ground's sun and
        // shadow too: lighting only the surface underneath leaves the shading over it unlit and
        // the shadows invisible wherever the paint is opaque.
        bool litSurface = _terrainSharedGround && _terrainLighting.enabled;
        bool shadowedSurface = litSurface && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        // DERIVATIVES unconditionally: the contour block's fwidth() needs it, and the paint has no
        // way to know whether the layer asked for contours (the interval arrives as a uniform, set
        // by the shared normal-map setup func). The NORMALMAP path does the same for the same
        // reason - harmless when contours are off, since the branch is not taken.
        unsigned int lightFlags = (litSurface ? TERRAIN_LIGHT_FLAG : 0) | (shadowedSurface ? surfaceShadowFlags() : DERIVATIVES_FLAG) | (asGround ? GROUND_BASE_FLAG : 0);

        int draws = 0;
        for (std::size_t paintIndex = 0; paintIndex < paintTiles.size(); paintIndex++) {
            const TileId& tileId = paintTiles[paintIndex];
            // The paint is drawn ON the ground, so it carries the ground's proxy push as well -
            // otherwise a stand-in tile's shading separates from the surface it shades.
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
        // Leaving the surface VBOs bound corrupts every later draw that feeds a CLIENT-SIDE array:
        // a bound GL_ARRAY_BUFFER turns the pointer into an offset into it. The sky is exactly
        // that (SkyRenderer draws its quad from a static array), and its quad flew off screen -
        // the terrain rendered while the sky went black.
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
        // Verbatim from the normal-map path, so a layer switched to paint mode keeps its relief.
        // The zoom that goes in is the SAMPLING density's, not the grid's tile id: a 512-texel grid
        // at z11 is worth a z12 tile of 256 texels, and keying off the grid's own zoom boosts the
        // relief ~1.5x too far. 156543.03 is the zoom-0 m/texel for 256-texel tiles.
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

    bool GLTileRenderer::isDrapeableGeometry(TileGeometry::Type type) const {
        // The maplibre drapeable set: backgrounds, fills, lines and rasters go into the texture;
        // 3D extrusions and point symbols stay live in the scene (they are not surface-conformal,
        // so flattening them into the terrain skin would be wrong, not just imprecise).
        // Lines are opt-out (setTerrainDrapeFills includeLines): the bake resolves them at the
        // drape texture's resolution, so a slope that magnifies the texture also blurs them.
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
        // "Draped" = the drape and this tile describe the same ground, whichever is coarser. The
        // drape cover is routinely COARSER than the render tiles, which is also why fills cannot be
        // decoded at source density under draping - see TileLayer::calculateDrawData.
        //
        // The FINER direction is the outgoing generation of a zoom out. It used to be left undraped
        // so it kept drawing itself while it blended away, on the grounds that nothing else covered
        // that ground - but a drape tile that CONTAINS it does cover it, and the direct draw then
        // paints the previous zoom's raster over the new one for the length of the fade.
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
        // Orthographic bake frame: the part of the SOURCE tile covering the target onto the target
        // texture's full [-1,1] clip square. source != target is the point - proxy content is what
        // is on screen during a pan, and left undraped it samples a coarser lattice than the surface
        // it sits on and sinks in. Tile-local vertex y runs NORTHWARD, so the y sub-rect is mirrored.
        int deltaZoom = targetTileId.zoom - sourceTileId.zoom;
        float n = 1.0f;
        float fx = 0.0f, gy = 0.0f;
        if (deltaZoom > 0) {
            int span = 1 << deltaZoom;
            n = static_cast<float>(span);
            fx = static_cast<float>(targetTileId.x - (sourceTileId.x << deltaZoom));
            gy = static_cast<float>(span - 1 - (targetTileId.y - (sourceTileId.y << deltaZoom)));
        } else if (deltaZoom < 0) {
            // Source FINER than the drape tile: it covers only a SUB-RECT of the texture, so the
            // sub-rect transform runs the other way. This is the zoom-out case - a render tile
            // retains the finer tiles it replaces until they blend out, and those render layers
            // keep their own finer target/source tile id (initializeRenderTile), so a coarse
            // render tile really does carry z+1/z+2 content. Left unhandled (n = 1) that content
            // was stretched over the WHOLE drape tile: a quarter of the map painted at 2x scale
            // in the wrong place, for the few frames the finer layer survives.
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
        // Identifies exactly what would be baked. When it changes - a style layer finishes
        // loading, a proxy is replaced by its native tile - the cached texture is stale and must
        // be re-baked, which the original bake-once cache had no way to notice.
        // ZERO MEANS NOTHING TO BAKE, and callers rely on that: a tile with no drapeable content
        // must be distinguishable from one that has some. Hence the explicit flag rather than
        // "hash != 0" - the hash of real content can in principle land on zero.
        std::size_t hash = 0;
        bool anyContent = false;
        auto combine = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };
        // The scene light is baked in with the colours, so moving the sun makes every cached drape
        // stale - without this, changing the hour on a running map moved the buildings, which are
        // lit by a per-frame uniform, and left the ground exactly as it was. QUANTISED to 64 steps
        // per channel so a day cycle re-bakes a few dozen times over its whole range rather than
        // once per frame.
        for (int i = 0; i < 3; i++) {
            combine(static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _radiance(i))) * 64.0f) * (i + 1));
        }
        combine(static_cast<std::size_t>(std::max(0.0f, std::min(1.0f, _backgroundEmissive)) * 64.0f) * 4);
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            const RenderTileLayer& renderLayer = it->second;
            // The contact shadows count too: they are baked INTO the drape, but the extrusions
            // that carry them are not drapeable content, so a layer holding only buildings used to
            // contribute nothing here. Its tiles then decoded without ever changing the
            // fingerprint, no re-bake was asked for, and whichever drape textures had been baked
            // before the buildings arrived kept no shadow at all - for as long as they stayed
            // cached. That is the AO missing from a tile here and there with no pattern to it.
            if (!hasDrapeableContent(renderLayer) && !hasGroundAOContent(renderLayer)) {
                continue;
            }
            anyContent = true;
            combine(static_cast<std::size_t>(it->first));
            combine(static_cast<std::size_t>(renderLayer.sourceTileId.zoom) * 2654435761u
                  ^ static_cast<std::size_t>(renderLayer.sourceTileId.x) * 40503u
                  ^ static_cast<std::size_t>(renderLayer.sourceTileId.y));
            // Identify the layer by what it IS, never by where it lives. A raw pointer says
            // nothing once the object behind it is freed: while zooming, tiles turn over fast
            // and a new TileLayer lands on a dead one's address often enough that the
            // fingerprint matches a texture baked for the PREVIOUS zoom level. The tile then
            // keeps the stale bake, and alternates with correctly re-baked neighbours - which
            // is seen as polygons flashing between the old and the new zoom.
            combine(std::hash<std::string>()(renderLayer.layer->getLayerName()));
            combine(static_cast<std::size_t>(renderLayer.layer->getLayerIndex()));
            combine(renderLayer.layer->getGeometries().size() * 2654435761u
                  ^ renderLayer.layer->getBitmaps().size() * 40503u
                  ^ renderLayer.layer->getBackgrounds().size());
        }
        if (anyContent && hash == 0) {
            hash = 1;
        }
        return anyContent ? hash : 0;
    }

    float GLTileRenderer::calculateDrapeOpacity(const RenderTileLayer& renderLayer) const {
        // The style's own layer opacity, which the on-screen path passes as element opacity when
        // the layer has no comp-op (see renderTileLayers). The bake used to hardcode 1.0 and drew
        // every draped layer fully opaque. A comp-op layer needs the overlay buffer the bake has
        // no equivalent of, so it keeps its current full-opacity behaviour.
        if (!renderLayer.layer || renderLayer.layer->getCompOp()) {
            return 1.0f;
        }
        return (renderLayer.layer->getOpacityFunc())(_viewState);
    }

    bool GLTileRenderer::hasGroundAOContent(const RenderTileLayer& renderLayer) const {
        if (!renderLayer.layer) {
            return false;
        }
        for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
            if (geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) {
                return true;
            }
        }
        return false;
    }

    bool GLTileRenderer::hasDrapeableContent(const RenderTileLayer& renderLayer) const {
        if (!renderLayer.layer || !isLayerDraped(renderLayer.layer)) {
            return false;
        }
        if (!renderLayer.layer->getBackgrounds().empty() || !renderLayer.layer->getBitmaps().empty()) {
            return true;
        }
        for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
            if (isDrapeableGeometry(geometry->getType())) {
                return true;
            }
        }
        return false;
    }

    void GLTileRenderer::renderDrapeTextures(const std::vector<RenderTile>& renderTiles) {
        // Maplibre-style drape: bake each tile's fills/backgrounds FLAT into an offscreen texture
        // the surface then samples as its colour, so fills follow the terrain exactly. Baked ONCE
        // per target tile and reused, dropped when it leaves the view; new bakes are capped per
        // frame so a fast zoom's burst is spread out. A tile counts as draped only once its texture
        // exists - until then its content renders as normal geometry, so there is no gap.
        if (_externalDrapeTarget) {
            return; // the owner drives baking across all layers (cross-layer stacks)
        }
        // An integer zoom change invalidates the whole visible set at once. With a small budget
        // most tiles then spend several frames showing a texture baked from an overzoomed parent -
        // magnified content that pops when the native bake lands, because the bake is deliberately
        // unblended (full opacity, so the cached texture is stable). Bake enough per frame that
        // the window is one or two frames rather than four or five.
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

        // Recycle textures for tiles no longer needing a drape.
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

            // Bake backgrounds, rasters and fills/lines in layer order at FULL opacity, so the
            // cached texture is stable regardless of the fade-in blend of the moment. Layers
            // whose source tile is an ancestor of the target are baked through a sub-rect
            // transform, so proxy content drapes correctly instead of falling back to the
            // displaced-geometry path. Points, 3D extrusions and labels stay live on top.
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!hasDrapeableContent(renderLayer)) {
                    continue;
                }
                // Only layers whose own tile covers this one, for the same reason as the
                // cross-layer bake: a finer retained layer is the generation being replaced, and
                // baking it minified into a sub-rect aliases into noise.
                if (!tileCovers(renderLayer.targetTileId, targetTileId)) {
                    continue;
                }
                // Backgrounds and rasters draw the target tile's surface mesh (their own uv logic
                // already resolves overzoom).
                float geometryOpacity = calculateDrapeOpacity(renderLayer);
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                    renderTileBackground(renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, background);
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                    renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, bitmap);
                }
                // Geometry vertices are in SOURCE tile-local coordinates, so an overzoomed layer
                // needs the sub-rect transform to land on the target tile's texture.
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (isDrapeableGeometry(geometry->getType())) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, geometryOpacity, renderLayer.tileSize, geometry);
                    }
                }
            }
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
        bool gridMode = _terrainRegularGrid && _terrainMode && static_cast<bool>(_terrainTextureProvider);
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

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiledTileSurface.indicesVBO);

            cglib::mat4x4<float> mvpMatrix = gridMode ? calculateTileMVPMatrix(tileId, 1.0f) : cglib::mat4x4<float>::convert(_cameraProjMatrix * surfaceFrame);
            glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIt->second);
            glUniform1i(shaderProgram.uniforms[U_DRAPETEXTURE], 0);
            glUniform4f(shaderProgram.uniforms[U_DRAPEUVTRANSFORM], uvOffsetX, uvOffsetY, uvScale, uvScale);
            glUniform4f(shaderProgram.uniforms[U_COLOR], 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform1f(shaderProgram.uniforms[U_OPACITY], 1.0f);

            glDrawElements(GL_TRIANGLES, tileSurface->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
            VT_STAT_INC(surfaceDraws);
            VT_STAT_INC(surfDrapeDraws);
            VT_STAT_ADD(surfaceIndices, tileSurface->getIndicesCount());
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
        // Debug view: the tile surface triangle mesh as red edges, displaced exactly like
        // the rendered surfaces (same vertex buffers + terrain uniforms as the mask/background).
        bool gridMode = _terrainRegularGrid && _terrainMode && static_cast<bool>(_terrainTextureProvider);
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
        // Debug view: the outline of the tile as it is actually drawn - displaced by the terrain,
        // so the line lies ON the ground and a tile whose footprint overlaps its neighbour's is
        // visible as such. The colour comes from the tile's own zoom, so a layer drawing a coarser
        // tile set than the one under it stands out, and the brightness alternates with the tile
        // parity so two tiles of the same zoom never share an edge colour. Drawn without depth,
        // because the point is to see where the tiles ARE, including the ones being occluded.
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

        bool gridMode = _terrainRegularGrid && _terrainMode && static_cast<bool>(_terrainTextureProvider);
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
        // A tile whose DATA comes from another tile is a stand-in (an overzoomed ancestor, or a
        // retained tile from another zoom): halve its opacity, so the tiles that legitimately own
        // their pixels read as the solid ones.
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
        // The map's background is the largest surface on screen and its colour is a Map setting, so
        // it never passes through a symbolizer's grade - graded here by the same rule.
        if (_backgroundEmissive < 1.0f) {
            std::array<float, 4> rgba = backgroundColor.rgba();
            for (int c = 0; c < 3; c++) {
                rgba[c] *= _backgroundEmissive + (1.0f - _backgroundEmissive) * _radiance(c);
            }
            backgroundColor = Color(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
        bool gridMode = _terrainRegularGrid && terrainVTF;
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(tileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        // The bake is flat and orthographic: two triangles reproduce it exactly, and drawing the
        // displaced grid instead means tens of thousands of triangles per layer per tile - which
        // is what made a zoom step cost hundreds of milliseconds.
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

        // In the drape bake the raster is rendered FLAT into the tile's texture: the same grid
        // surface mesh, but with terrain displacement off and the orthographic bake matrix, so
        // the [0,1] tile-local mesh maps onto the texture. The uv matrix below already resolves
        // source-vs-target overzoom, so the bake frame is the plain target-tile square.
        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider;
        bool gridMode = _terrainRegularGrid && terrainVTF;
        cglib::mat4x4<double> surfaceFrame = gridMode ? calculateTileMatrix(targetTileId, 1.0f) : cglib::translate4_matrix(_tileSurfaceBuilderOrigin);
        // Two triangles for the flat bake; see renderTileBackground.
        for (const std::shared_ptr<TileSurface>& tileSurface : (flatDrape ? buildCompiledFlatSurfaces() : (gridMode ? buildCompiledTerrainGridSurfaces() : buildCompiledTileSurfaces(targetTileId)))) {
            const TileSurface::VertexGeometryLayoutParameters& vertexGeomLayoutParams = tileSurface->getVertexGeometryLayoutParameters();
            const CompiledSurface& compiledTileSurface = _compiledTileSurfaceMap[tileSurface];

            unsigned int terrainFlag = flatDrape ? 0 : (terrainVTF ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : (_terrainMode && !_terrainDepthWrite ? TERRAIN_FLAG : 0));
            // A raster drawn HERE covers the same ground the drape surface does, so it takes the
            // same sun and shadow. It is not draped when it is finer than the drape cover - the
            // outgoing generation of a zoom out - and drawing that unlit next to the lit surface
            // below is the flash at every integer zoom out. Never in the bake: the drape texture is
            // lit once, by the surface that samples it. NORMALMAP has its own lighting model.
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
            // The program is built with fogFlag() like every other draw, but this was the one path
            // that never set the uniforms - so a raster drawn outside the drape kept whatever fog
            // state the program object last held, i.e. none.
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

        VT_STAT_CLOCK(statClock);
        VT_STAT_SPLIT(geomProbeNs, statClock);
        bool styleOffsetting = std::count(styleParams.offsetFuncs.begin(), styleParams.offsetFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleGapWidth = std::count(styleParams.gapWidthFuncs.begin(), styleParams.gapWidthFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleBlur = std::count(styleParams.blurFuncs.begin(), styleParams.blurFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;
        bool styleBorder = std::count(styleParams.borderWidthFuncs.begin(), styleParams.borderWidthFuncs.begin() + styleParams.parameterCount, FloatFunction(0)) != styleParams.parameterCount;

        // Flat drape pass: draw the fill into the per-tile drape texture with NO terrain
        // displacement, NO depth bias, and a tile-local orthographic MVP (set by the caller).
        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider && !flatDrape;
        // Every piece of tile content drawn in the 3D scene receives shadows. That used to mean
        // the 3D extrusions alone, because everything 2D was baked into the drape texture and was
        // shadowed by the surface it was painted on; with only the FILLS draped, the lines and
        // points are in the scene and stayed lit while the slope under them went dark.
        bool shadowReceiver = terrainVTF && !_shadowCasterViewProj && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        // ...and takes the sun the same way. Whatever reaches this call in the 3D scene is content
        // the drape did NOT bake - a no-drape layer (contours), or everything when the drape is
        // off - so it is the only pass that can light it, and without this it kept its full style
        // colour beside a ground that is lit and shadowed. Extrusions light by their own model.
        bool terrainLit = terrainVTF && !_shadowCasterViewProj && _terrainLighting.enabled && geometry->getType() != TileGeometry::Type::POLYGON3D;
        unsigned int lightFlag = terrainLit ? GEOMETRY_LIGHT_FLAG : 0;
        unsigned int terrainFlag = flatDrape ? 0 : ((_terrainMode ? TERRAIN_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG : 0));
        const ShaderProgram* shaderProgramPtr = nullptr;
        switch (geometry->getType()) {
        case TileGeometry::Type::POINT:
            shaderProgramPtr = &buildShaderProgram("point", pointVsh, pointFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag());
            break;
        case TileGeometry::Type::LINE:
            shaderProgramPtr = &buildShaderProgram("line", lineVsh, lineFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | (styleGapWidth ? GAPWIDTH_FLAG : 0) | (styleBlur ? BLUR_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag() | coverageFlag() | drapeMaskFlag());
            break;
        case TileGeometry::Type::POLYGON:
            shaderProgramPtr = &buildShaderProgram("polygon", polygonVsh, polygonFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | terrainFlag | (shadowReceiver ? shadowReceiverFlags() : 0) | lightFlag | fogFlag() | coverageFlag() | drapeMaskFlag());
            break;
        case TileGeometry::Type::POLYGON3DGROUND:
            // Flat on the ground, so it takes the terrain displacement and the same clearance a
            // draped line does - it must sit ON the surface, not inside it. No lighting: it is a
            // multiplier over ground that is already lit.
            if (_shadowCasterViewProj || !_groundAOMaskPass) {
                return; // casts nothing, and is only ever drawn into its own mask
            }
            shaderProgramPtr = &buildShaderProgram("polygon3dground", polygon3DGroundVsh, polygon3DGroundFsh, LightingMode::NONE, RasterFilterMode::NONE, terrainFlag);
            break;
        case TileGeometry::Type::POLYGON3D:
            if (_shadowCasterViewProj) {
                // Caster pass: same vertex shader (so the extrusion is identical to the drawn
                // one), depth-packing fragment shader, no lighting.
                shaderProgramPtr = &buildShaderProgram("polygon3dshadow", polygon3DVsh, shadowCasterFsh, LightingMode::NONE, RasterFilterMode::NONE, (styleParams.translate ? TRANSFORM_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG : 0));
                break;
            }
            // TERRAIN_FLAG (depth bias) too: in terrain mode the extrusions are depth-tested
            // against a terrain surface pre-pass (renderGeometry3D seeds the 3D overlay's
            // depth buffer with it), so they need the same base-clearance slack as draped
            // 2D geometry - otherwise the lower walls are clipped by the ground on slopes.
            // SHADOW_SINGLE_TAP: an extrusion cannot use the terrain's screen-space mask - that holds
            // the ground's shadow, not its own - so it is the one receiver still running the kernel
            // per fragment, over a wall that is shadowed or lit almost in one piece.
            shaderProgramPtr = &buildShaderProgram("polygon3d", polygon3DVsh, polygon3DFsh, LightingMode::GEOMETRY3D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG | TERRAIN_FLAG : 0) | (shadowReceiver ? shadowReceiverFlags() | SHADOW_SINGLE_TAP_FLAG : 0) | fogFlag());
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

        setupGeometryCommonUniforms(shaderProgram, sourceTileId, targetTileId, geometry, GeometryDrawMode { flatDrape, terrainVTF, shadowReceiver, terrainLit, terrainFlag });
        VT_STAT_SPLIT(geomTerrainNs, statClock);

        // An extrusion may sit out the tile's fade: a style that ramps its own opacity over zoom -
        // Standard goes 0 at z15 to full at z15.3 - then owns the whole appearance, instead of
        // fading a second time on a timer whenever a tile arrives.
        float colorBlend = (geometry->getType() == TileGeometry::Type::POLYGON3D && !_buildingFadeOnAppear ? 1.0f : blend);
        // The scene light, applied where the colour is already evaluated once per frame and
        // cached - so this costs one multiply per DISTINCT colour, not per feature. mapbox's
        // `mix(apply_lighting_ground(color), color, emissive_strength)`: at emissive 1, which is
        // the default and what every style did before this existed, it is exactly a no-op.
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

                // Text drawn as geometry (text-clip) takes the same halo units as a label: measured
                // in antialias ramps, and pointVsh pushes the ramp centre out by twice its width in
                // screen pixels, exactly like labelFsh.
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
            // The antialias ramp of text drawn as geometry, in the texture values the field is
            // encoded in - the same rule the label batch uses (see renderLabelBatch), against this
            // path's own 'size', which already carries the tile scale. The old form was written for
            // the single 27-texel raster and a spread of 4: the raster ladder and the wider spread
            // left it measuring a ramp up to six screen pixels, which is the glyph dissolving into
            // its halo.
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
                // Half the gap, in the same units as the half-widths below: the fragment shader
                // cuts everything inside it.
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
                    // A gapped line is mapbox's casing: a FULL line-width beyond the half-gap on
                    // each side, not a half-width, or the casing comes out half as thick as the
                    // browser draws it and reads as an edge of the road itself.
                    widths[i] = gapWidths[i] > 0.0f ? gapWidths[i] + width : width * 0.5f;
                }
                if (styleBorder) {
                    // maplibre's `line-border-width`: pixels on EACH side, outside the line. The
                    // gap shrinks by the same amount so a casing keeps its border on both edges.
                    // CLAMPED, not abs: a style computing the border from two width ramps goes
                    // negative wherever the casing's ramp has not started yet, and a negative
                    // border means no border - mirrored it would draw one the road never has.
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
                // Screen-space line extrusion over terrain (see lineVsh): the aspect converts an
                // NDC x offset into the same units as y, and one line-width unit is 1/halfResolution
                // of the NDC height - the very scale uBinormalScale is built from, so a line ends
                // up exactly as wide as it is on the flat map.
                glUniform2f(shaderProgram.uniforms[U_SCREENSCALE], std::max(0.0001f, _viewState.aspect), 1.0f / std::max(1.0f, _halfResolution));
                // How many line widths a vertex is extruded by: the binormal ships packed as int16
                // against a per-geometry scale, so only this undoes it - 1 for a plain vertex,
                // more for a miter, a round cap corner or an arrow barb.
                glUniform1f(shaderProgram.uniforms[U_BINORMALUNITSCALE], 1.0f / vertexGeomLayoutParams.binormalScale);
            }
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
            glUniform1f(shaderProgram.uniforms[U_UVSCALE], 1.0f / vertexGeomLayoutParams.texCoordScale);
            glUniform1f(shaderProgram.uniforms[U_HEIGHTSCALE], buildingHeightScale(blend) * heightUnits);
            glUniform1f(shaderProgram.uniforms[U_SHADOWHEIGHTSCALE], casterHeightScale(blend) * heightUnits);
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
            // Which slots of this draw are patterned: a polygon geometry now carries plain fills
            // alongside the patterned ones instead of being split at every alternation.
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

        glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
        VT_STAT_SPLIT(geomDrawNs, statClock);
        VT_STAT_INC(geometryDraws);
        VT_STAT_ADD(geometryIndices, geometry->getIndicesCount());

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
            // fill coords * (1/coordScale) = tile-local [0,1]; the override maps [0,1] -> clip.
            cglib::mat4x4<float> local = cglib::scale4_matrix(cglib::vec3<float>(1.0f / vertexGeomLayoutParams.coordScale, 1.0f / vertexGeomLayoutParams.coordScale, 1.0f));
            mvpMatrix = (*_drapeMVPOverride) * local;
        } else {
            mvpMatrix = calculateTileMVPMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
        }
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        if (mode.terrainFlag != 0 && geometry->getType() != TileGeometry::Type::POLYGON3D) {
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], mode.terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
        } else if (mode.terrainVTF && !_shadowCasterViewProj && geometry->getType() == TileGeometry::Type::POLYGON3D) {
            // Extrusions: only the VTF path has a terrain surface to clear (the caster pass
            // renders depth from the light and must not be biased towards the camera).
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
        }
        if (mode.terrainVTF) {
            // The elevation TEXTURE is the TARGET tile's - that is the surface this content stands
            // on - while the vertex FRAME is the SOURCE tile's, since the vertices are source-local.
            // They were swapped: content then sat at a different DEM level than the ground beneath
            // it and slid during a pan until source and target became the same tile.
            setupTerrainUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
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
            glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
        }
        // The location is -1 on a program built without DRAPE_MASK - a point, or any geometry the
        // mask does not apply to - and binding a texture for it is pure waste.
        if (_drapeMaskTexture != 0 && shaderProgram.uniforms[U_DRAPEMASKTEXTURE] >= 0) {
            // Unit 3: 0 is the pattern, 1 the elevation texture, 2 the shadow map.
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, _drapeMaskTexture);
            glUniform1i(shaderProgram.uniforms[U_DRAPEMASKTEXTURE], 3);
            glActiveTexture(GL_TEXTURE0);
            glUniform4f(shaderProgram.uniforms[U_DRAPEMASKUVTRANSFORM], _drapeMaskUVTransform(0), _drapeMaskUVTransform(1), _drapeMaskUVTransform(2), _drapeMaskUVTransform(3));
        }
        if (mode.shadowReceiver || mode.terrainLit) {
            // Undraped 2D content takes its N.L from the TERRAIN, not from its own (meaningless)
            // normal - see terrainNdl in commonFsh - so both the shadow and the sun need the slope
            // scale and the sun direction. The uniforms this sets that a program does not declare
            // resolve to -1, where glUniform is a no-op.
            setupTerrainLightingUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        }

        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            cglib::mat4x4<float> transformMatrix = _transformer->calculateTileTransform(sourceTileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
            glUniformMatrix4fv(shaderProgram.uniforms[U_TRANSFORMMATRIX], 1, GL_FALSE, transformMatrix.data());
        }
    }

    void GLTileRenderer::bindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry) {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        bool lit = _lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D;

        if (compiledGeometry.geometryVAO != 0) {
            glBindVertexArray(compiledGeometry.geometryVAO);
        }
        if (compiledGeometry.geometryVAO == 0 || compiledGeometry.geometryVAOProgram != shaderProgram.program) {
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
        }

        if (!(vertexGeomLayoutParams.attribsOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS], 0, 0, 0, 0);
        }

        if (lit && !(vertexGeomLayoutParams.normalOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
        }
    }

    void GLTileRenderer::unbindGeometryVertexLayout(const ShaderProgram& shaderProgram, const std::shared_ptr<TileGeometry>& geometry, const CompiledGeometry& compiledGeometry) {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = geometry->getVertexGeometryLayoutParameters();
        bool lit = _lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D;

        if (compiledGeometry.geometryVAO != 0) {
            glBindVertexArray(0);
        } else {
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

        if (compiledGeometry.geometryVAO == 0 || compiledGeometry.geometryVAOProgram != shaderProgram.program) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            compiledGeometry.geometryVAOProgram = (compiledGeometry.geometryVAO != 0 ? shaderProgram.program : 0);
        }
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

        // fwidth() is core on an ES 3.0 context. The shaders are still GLSL ES 1.00 until Phase 3,
        // so commonFsh keeps emitting '#extension GL_OES_standard_derivatives : enable' for them.
        bool useDerivatives = true;

        const CompiledBitmap& compiledBitmap = buildCompiledBitmap(bitmap, false);
        unsigned int occlusionFlag = (_labelOcclusionTexture != 0 && labelBatchParams.occlusionOpacity < 1.0f ? LABEL_OCCLUSION_FLAG : 0);
        const ShaderProgram& shaderProgram = buildShaderProgram("labels", labelVsh, labelFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (useDerivatives ? DERIVATIVES_FLAG : 0) | occlusionFlag | fogFlag());
        useProgram(shaderProgram);
        setupFogUniforms(shaderProgram);
        if (occlusionFlag) {
            // Unit 1: unit 0 is the glyph atlas, bound per batch below.
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, _labelOcclusionTexture);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(shaderProgram.uniforms[U_LABELOCCLUSIONTEX], 1);
            // The occluder square in uv, the depth offset that keeps a label standing ON the
            // ground from reading as behind it, the opacity an occluded label keeps, and the
            // sharpness of the comparison.
            float halfSizeU = 0.5f * _labelOcclusionSize / std::max(1.0f, static_cast<float>(_screenWidth));
            float halfSizeV = 0.5f * _labelOcclusionSize / std::max(1.0f, static_cast<float>(_screenHeight));
            glUniform4f(shaderProgram.uniforms[U_LABELOCCLUSIONPARAMS], 0.5f * (halfSizeU + halfSizeV), LABEL_OCCLUSION_DEPTH_OFFSET, labelBatchParams.occlusionOpacity, 1.0f / LABEL_OCCLUSION_DEPTH_RAMP);
        }

        cglib::mat4x4<float> mvpMatrix = cglib::mat4x4<float>::convert(_viewState.projectionMatrix * labelBatchParams.labelMatrix);
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());

        // The antialias ramp has to be one screen pixel wide, and it is expressed in the texture
        // values the field is encoded in. One em is (glyphRenderSize - GLYPH_RENDER_SPREAD) glyph
        // texels and is drawn over 'size * scale * _fullResolution / 2' screen pixels (vt's
        // resolution is twice what one tile covers), so a screen pixel is that ratio of texels -
        // GLYPH_SDF_UNIT turns texels into texture values. The batch has one scale for the whole
        // label; the DERIVATIVES path in labelFsh measures it per fragment instead.
        float glyphEmTexels = static_cast<float>(labelBatchParams.glyphRenderSize - GLYPH_RENDER_SPREAD);
        glUniform1f(shaderProgram.uniforms[U_SDFRAMP], 2.0f * GLYPH_SDF_UNIT * glyphEmTexels / (labelBatchParams.scale * _fullResolution));
        // Camera axes for the shader-side billboarding (see labelVsh); the label matrix is
        // camera-relative, so these are the plain camera basis vectors.
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

    const GLTileRenderer::CompiledGeometry* GLTileRenderer::buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry) {
        // The style parameter that picks a feature out may have changed since this geometry was
        // built: repointing its features at their other style slot is a byte rewrite here, not a
        // tile decode. Done before the buffers are looked at, so a geometry compiled for the first
        // time uploads the repointed data straight away.
        tileGeometry->applyStyleState();

        auto it = _compiledTileGeometryMap.find(tileGeometry.get());
        if (it != _compiledTileGeometryMap.end() && it->second.owner.expired()) {
            // The geometry this entry was built for is gone and the allocator handed its
            // address to a NEW geometry: the raw pointer matches but the VBOs do not. Two
            // live objects can never share an address, so a live owner is proof the entry
            // belongs to this geometry - an expired one is proof it does not. Without this
            // the renderer draws the previous tile's buffers, which shows up as roads and
            // labels flashing while tiles turn over during a zoom.
            VT_STAT_INC(geomCompileStale);
            deleteCompiledGeometry(it->second.geometry);
            _compiledTileGeometryMap.erase(it);
            it = _compiledTileGeometryMap.end();
        }
        if (it == _compiledTileGeometryMap.end()) {
            // Nothing left to upload. releaseVertexArrays() frees the CPU copy once a geometry is
            // in a VBO, which assumes ONE renderer per geometry - and a renderer is rebuilt from
            // scratch whenever the tile transformer changes (TileRenderer::setTileTransformer, i.e.
            // every terrain toggle), while the tiles themselves are handed straight to it. Uploading
            // the empty arrays gave a buffer with NO data store and a draw of 65535 indices into it:
            // undefined on a device, a null dereference inside the emulator's GL encoder. The tiles
            // are re-decoded for the new transformer anyway, so skipping is a frame or two of
            // missing geometry rather than a crash.
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
            // A feature was repointed at another style slot: re-upload just the bytes it touched
            // instead of decoding the tile again
            glBindBuffer(GL_ARRAY_BUFFER, it->second.geometry.vertexGeometryVBO);
            glBufferSubData(GL_ARRAY_BUFFER, dirtyBytes->first, dirtyBytes->second - dirtyBytes->first, tileGeometry->getVertexGeometry().data() + dirtyBytes->first);
            tileGeometry->clearDirtyVertexBytes();
        }
        return &it->second.geometry;
    }

    const GLTileRenderer::ShaderProgram& GLTileRenderer::buildShaderProgram(const char* id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags) {
        // Every program is ESSL 3.00. Set here rather than at the 20-odd call sites, and before the
        // cache key is built so the key still distinguishes a program that fell back to 1.00.
        flags |= ESSL3_FLAG;

        // Fast path: the call site's literal pointer + the flags, no allocation (see the
        // cache declaration). Only a miss builds the string key below.
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
            // Central, so every terrain program picks it up without threading the flag through
            // each buildShaderProgram call site. Read once at startup, so the program cache key
            // does not have to carry it.
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
                // The same lighting shader the normal-map path uses, over a prelude that reads
                // the shared terrain DEM instead of a per-tile normal map raster - so the built-in
                // hillshade algorithms and custom shaders (getElevation/getMapZoom) both work.
                lightingFsh = terrainPaintPrelude + _lightingShaderNormalMap->shader;
            }
            else if (lightingMode == LightingMode::NORMALMAP && _lightingShaderNormalMap) {
                defs.insert(_lightingShaderNormalMap->perVertex ? "LIGHTING_VSH" : "LIGHTING_FSH");
                // Enable screen-space derivatives (fwidth) for anti-aliased contour lines in the
                // elevation-encoded hillshade path. Harmless when contours are off (branch not taken).
                defs.insert("DERIVATIVES");
                if (_lightingShaderNormalMap->perVertex) {
                    lightingVsh = _lightingShaderNormalMap->shader;
                } else {
                    // Prepend the shared DEM prelude so the custom/built-in shader can call
                    // getElevation()/getMapZoom()/sampleElevation() and read the shared uniforms.
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
                // A driver that will not take the ESSL 3.00 variant - or an application shader
                // concatenated into it that is 1.00-only - must not take the whole map with it.
                // The 1.00 path is a complete implementation, only slower.
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

    const std::vector<std::shared_ptr<TileSurface>>& GLTileRenderer::buildCompiledTerrainGridSurfaces() {
        // The single shared unit-grid surface, built once and reused for every tile
        // (drawn with the tile's own MVP + terrain uniforms). No per-tile tesselation.
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
        // GLSL ES 3.00 from ONE set of shader sources: the differences are a handful of renamed
        // keywords. The 1.00 path below is the per-program fallback, kept one release as a canary.
        //
        // The fragment shaders here write `glFragColor`, a plain rename. That rename is NOT
        // required - `#define gl_FragColor ...` compiles and is applied, measured through ANGLE's
        // translator (docs/internals/rendering/16-graphics-api-migration.md); ESSL reserves the GL_
        // prefix for MACRO names, and gl_FragColor is a built-in variable ESSL 3.00 does not
        // declare. all/native's Shader.cpp takes tangram's #define form so that application GLSL
        // needs no migration. The rename is harmless, so it stays.
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
                // The reported line number is into the CONCATENATED source, which nothing on disk
                // matches - quote it, or every compile error costs a round of guessing.
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
        glGenVertexArrays(1, &compiledGeometry.geometryVAO);
        glGenBuffers(1, &compiledGeometry.vertexGeometryVBO);
        glGenBuffers(1, &compiledGeometry.indicesVBO);
    }
    
    void GLTileRenderer::deleteCompiledGeometry(CompiledGeometry& compiledGeometry) {
        if (compiledGeometry.geometryVAO != 0) {
            glDeleteVertexArrays(1, &compiledGeometry.geometryVAO);
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
