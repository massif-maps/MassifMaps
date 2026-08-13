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

    // Convex hull of a small point set (monotone chain), used to turn the endpoints of the visible
    // frustum edges back into the ground polygon they outline. The visible ground is the
    // intersection of convex sets, so its footprint is convex and the hull loses nothing.
    std::vector<cglib::vec2<double> > convexHull2D(std::vector<cglib::vec2<double> > points) {
        if (points.size() < 3) {
            return points;
        }
        std::sort(points.begin(), points.end(), [](const cglib::vec2<double>& a, const cglib::vec2<double>& b) {
            return a(0) != b(0) ? a(0) < b(0) : a(1) < b(1);
        });
        points.erase(std::unique(points.begin(), points.end(), [](const cglib::vec2<double>& a, const cglib::vec2<double>& b) {
            return a(0) == b(0) && a(1) == b(1);
        }), points.end());
        if (points.size() < 3) {
            return points;
        }
        auto cross = [](const cglib::vec2<double>& o, const cglib::vec2<double>& a, const cglib::vec2<double>& b) {
            return (a(0) - o(0)) * (b(1) - o(1)) - (a(1) - o(1)) * (b(0) - o(0));
        };
        std::vector<cglib::vec2<double> > hull(points.size() * 2);
        std::size_t k = 0;
        for (std::size_t i = 0; i < points.size(); i++) {
            while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0) {
                k--;
            }
            hull[k++] = points[i];
        }
        for (std::size_t i = points.size() - 1, lower = k + 1; i > 0; i--) {
            while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0) {
                k--;
            }
            hull[k++] = points[i - 1];
        }
        hull.resize(k > 0 ? k - 1 : 0); // the first point is repeated at the end
        return hull;
    }

    // Sutherland-Hodgman clip of a convex polygon against an axis-aligned rectangle.
    std::vector<cglib::vec2<double> > clipPolygonToRect(const std::vector<cglib::vec2<double> >& polygon, double minX, double minY, double maxX, double maxY) {
        std::vector<cglib::vec2<double> > input = polygon, output;
        for (int side = 0; side < 4 && !input.empty(); side++) {
            output.clear();
            int axis = side & 1;
            bool keepAbove = (side < 2);
            double limit = (side == 0 ? minX : side == 1 ? minY : side == 2 ? maxX : maxY);
            auto inside = [axis, keepAbove, limit](const cglib::vec2<double>& p) {
                return keepAbove ? p(axis) >= limit : p(axis) <= limit;
            };
            for (std::size_t i = 0; i < input.size(); i++) {
                const cglib::vec2<double>& current = input[i];
                const cglib::vec2<double>& previous = input[(i + input.size() - 1) % input.size()];
                bool currentInside = inside(current), previousInside = inside(previous);
                if (currentInside != previousInside) {
                    double denom = current(axis) - previous(axis);
                    double t = (std::abs(denom) > 1.0e-12 ? (limit - previous(axis)) / denom : 0.0);
                    output.emplace_back(previous(0) + (current(0) - previous(0)) * t, previous(1) + (current(1) - previous(1)) * t);
                }
                if (currentInside) {
                    output.push_back(current);
                }
            }
            input = output;
        }
        return input;
    }
}

namespace carto::vt {
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

    void GLTileRenderer::setTerrainPainterOrder(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainPainterOrder = enabled;
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

    void GLTileRenderer::setTerrainShadowMap(GLuint texture, int mapSize, int cascades, const std::array<float, MAX_SHADOW_CASCADES>& depthBiases, float strength, float softness, const std::array<cglib::mat4x4<double>, MAX_SHADOW_CASCADES>& lightViewProjs) {
        std::lock_guard<std::mutex> lock(_mutex);

        _terrainShadowTexture = texture;
        _terrainShadowMapSize = mapSize;
        _terrainShadowCascades = std::max(1, std::min(MAX_SHADOW_CASCADES, cascades));
        _terrainShadowBiases = depthBiases;
        _terrainShadowStrength = strength;
        _terrainShadowSoftness = softness;
        _terrainShadowViewProjs = lightViewProjs;
        // Pages beyond the cascade count do not exist in the atlas. Their matrices are pushed
        // clean out of the light box, so the fragment stage's "inside this cascade?" test always
        // fails for them and it can never sample past the last real page - which with CLAMP_TO_EDGE
        // would silently read the neighbouring cascade's edge column.
        for (int i = _terrainShadowCascades; i < MAX_SHADOW_CASCADES; i++) {
            _terrainShadowViewProjs[i] = cglib::translate4_matrix(cglib::vec3<double>(4, 0, 0)) * _terrainShadowViewProjs[0];
            _terrainShadowBiases[i] = _terrainShadowBiases[0];
        }
    }

    void GLTileRenderer::setFog(const Color& color, float startDistance, float distance) {
        std::lock_guard<std::mutex> lock(_mutex);

        _fogColor = color;
        _fogStartDistance = startDistance;
        _fogDistance = distance;
    }

    bool GLTileRenderer::calculateShadowViewProj(const std::vector<TileId>& tileIds, const std::vector<TileId>& casterTileIds, const cglib::vec3<float>& sunDir, const std::vector<std::pair<double, double> >& tileHeights, double minHeight, double maxHeight, float maxDistanceMeters, int mapSize, int cascade, int cascadeCount, std::vector<TileId>& boxCasterTileIds, double& depthRangeMeters, double& texelMeters, cglib::mat4x4<double>& lightViewProj) const {
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
            texelMeters = -3; // diagnostic: which fit bail-out fired
            return false;
        }
        double minZ = -1000.0 * metersToInternal;
        double maxZ = 9000.0 * metersToInternal;
        if (maxHeight > minHeight) {
            minZ = minHeight;
            maxZ = maxHeight;
        }
        // Clamp the covered ground to what the camera can actually SEE, and to a radius around it.
        // The map has a fixed resolution, so its texel size is the ground it spans divided by that
        // resolution. Bounding the drawn tiles alone spends most of the map on ground that is
        // behind the camera or outside its cone: at a tilt the visible ground is a narrow wedge,
        // and fitting the box to that wedge instead of to its bounding square is worth several
        // times the texel density - which is what a shadow edge and a stretched shadow's interior
        // are limited by. Beyond the radius there are simply no shadows.
        //
        // The wedge is kept as a POLYGON, not only as its bounding rectangle: the box below is
        // fitted in LIGHT space, and a world-axis-aligned rectangle rotated into light space
        // bounds far more ground than the wedge itself - up to twice the extent per axis when the
        // map is rotated diagonally to the sun, which is a factor of two on every texel.
        std::vector<cglib::vec2<double> > groundPolygon;
        {
            double visMinX = 0, visMinY = 0, visMaxX = 0, visMaxY = 0;
            bool visFirst = true;
            groundPolygon.clear();
            cglib::mat4x4<double> invCameraProj = cglib::inverse(_viewState.projectionMatrix * _viewState.cameraMatrix);
            double maxDistance = (maxDistanceMeters > 0 ? maxDistanceMeters * metersToInternal : 0);
            const cglib::vec3<double>& eye = _viewState.origin; // camera position in world units
            // Each frustum edge, reduced to the piece of it that can see shadowed GROUND: the part
            // inside the height slab, in front of the camera and no further than the shadow
            // distance. Everything outside that is sky, or under the terrain.
            //
            // Two quantities are taken from rays sampled across the screen: the DISTANCE RANGE over
            // which the view crosses the slab, and the horizontal opening ANGLE of the frustum. The
            // footprint itself is then built from those rather than from the rays, because a ray
            // only crosses the slab over a short piece of its length: at this camera the five
            // sampled screen heights cross it at 14.5-16.7, 16.6-19.2, 20.7-23.8, 29.1-33.6 and
            // 53.7-62.0 km. Those are disjoint. A cascade whose distance slice lands in one of the
            // gaps intersects NO ray and reports an empty box - which is exactly what the outermost
            // cascade did (its texel size logged as 0.0 m) before the far half of the view lost its
            // shadows. Sampling more rays only makes the gaps smaller, it does not remove them.
            cglib::vec2<double> viewDir(1, 0);
            double halfAngle = 0;
            double groundNear = 0, groundFar = 0;
            bool rangeFirst = true;
            auto sampleRay = [&](double ndcX, double ndcY, cglib::vec3<double>& origin, double& d0, double& length) {
                cglib::vec3<double> p0 = cglib::proj_p(cglib::transform(cglib::vec4<double>(ndcX, ndcY, -1.0, 1.0), invCameraProj));
                cglib::vec3<double> p1 = cglib::proj_p(cglib::transform(cglib::vec4<double>(ndcX, ndcY,  1.0, 1.0), invCameraProj));
                origin = p0;
                d0 = cglib::length(p0 - eye);
                cglib::vec3<double> delta = p1 - p0;
                length = cglib::length(delta);
                return delta;
            };
            {
                cglib::vec3<double> centreOrigin;
                double centreD0 = 0, centreLength = 0;
                cglib::vec3<double> centreDelta = sampleRay(0.0, 0.0, centreOrigin, centreD0, centreLength);
                cglib::vec2<double> centreHorizontal(centreDelta(0), centreDelta(1));
                if (cglib::length(centreHorizontal) > 1.0e-12) {
                    viewDir = cglib::unit(centreHorizontal);
                }
            }
            const int GROUND_SAMPLES = 5; // screen heights sampled per side, bottom to top
            for (int sample = 0; sample < 2 * GROUND_SAMPLES; sample++) {
                double ndcX = (sample & 1 ? 1.0 : -1.0);
                double ndcY = -1.0 + 2.0 * (sample / 2) / (GROUND_SAMPLES - 1);
                cglib::vec3<double> p0;
                double d0 = 0, length = 0;
                cglib::vec3<double> delta = sampleRay(ndcX, ndcY, p0, d0, length);
                if (!(length > 0)) {
                    continue;
                }
                cglib::vec2<double> horizontal(delta(0), delta(1));
                if (cglib::length(horizontal) > 1.0e-12) {
                    cglib::vec2<double> unitHorizontal = cglib::unit(horizontal);
                    double cosAngle = std::min(1.0, std::max(-1.0, cglib::dot_product(unitHorizontal, viewDir)));
                    halfAngle = std::max(halfAngle, std::acos(cosAngle));
                }
                double t0 = 0, t1 = 1;
                if (maxDistance > 0 && length > maxDistance) {
                    t1 = maxDistance / length; // cap the far end at the shadow distance
                }
                if (std::abs(delta(2)) > 1.0e-12) {
                    double ta = (minZ - p0(2)) / delta(2), tb = (maxZ - p0(2)) / delta(2);
                    t0 = std::max(t0, std::min(ta, tb));
                    t1 = std::min(t1, std::max(ta, tb));
                } else if (p0(2) < minZ || p0(2) > maxZ) {
                    continue; // parallel to the slab and outside it
                }
                if (t1 < t0) {
                    continue; // this ray never crosses the shadowed height range
                }
                if (rangeFirst) {
                    groundNear = d0 + t0 * length;
                    groundFar = d0 + t1 * length;
                    rangeFirst = false;
                } else {
                    groundNear = std::min(groundNear, d0 + t0 * length);
                    groundFar = std::max(groundFar, d0 + t1 * length);
                }
            }
            // Looking straight down, the rays fan out into EVERY azimuth and the centre ray's own
            // azimuth is numerical noise. Capping the opening angle there cuts the sector and
            // leaves whatever falls outside it - the top and the bottom of the screen - with no
            // light box over it and therefore no shadow. Past a wide fan the honest footprint is
            // the whole disc around the camera, which the tile-cover rectangle then bounds anyway.
            bool fullCircle = (halfAngle > 1.0);
            if (fullCircle) {
                halfAngle = 3.14159265358979323846;
            }
            // Cap how far the shadowed ground reaches. Left unbounded it runs to wherever the
            // frustum finally leaves the height slab, which at a tilt is the horizon: measured
            // 176 km of box at tilt 39 (172 m texels) against 12 km at tilt 35 (12 m) - the same
            // camera, four degrees apart. That cliff is the distant shadows turning into blocks
            // and then vanishing, and no split can hide it, since the outermost cascade is nested
            // and must reach groundFar.
            //
            // Texel size follows the ground extent almost linearly, so the budget is simply how
            // much ground is worth covering. Its unit is taken from the sun and the relief -
            // relief/tan(altitude), the distance a ridge throws its shadow - because that is the
            // scale over which shadows still carry information: three of them covers the part of
            // the view where shadows read as shadows. (This term also stretches the light box, but
            // along the light's DEPTH axis, which costs bias precision rather than texels.)
            //
            // The budget is measured from groundNear, and groundNear is a distance from the EYE:
            // a camera at zoom 11 is 15 km up, so the nearest ground it sees is already 15 km away
            // and the screen spans out to a hundred. A budget of ONE slab extent therefore ended
            // the shadows just past the bottom of the screen - shadows "disappeared" instead of
            // going blocky. Three slab extents keeps the outermost cascade near 4x the near one
            // and still reaches well past the middle of a tilted view; the shader fades the shadow
            // out at the edge of the last page, so where it does end there is no line.
            if (groundFar > groundNear) {
                cglib::vec3<double> sunUnit = cglib::unit(cglib::vec3<double>(sunDir(0), sunDir(1), sunDir(2)));
                double horizontal = std::sqrt(std::max(0.0, 1.0 - sunUnit(2) * sunUnit(2)));
                double slabExtent = (maxZ - minZ) * horizontal / std::max(0.05, std::abs(sunUnit(2)));
                // The second term is what makes this scale with the VIEW rather than being a fixed
                // number of kilometres. groundNear is the distance to the nearest visible ground,
                // so it grows both as the camera rises and as the view tilts towards the horizon -
                // exactly the two ways the useful shadow range grows. A fixed 8 km floor meant that
                // zooming into a city still spent the map on 8 km of ground while the camera was
                // 400 m up: 8 m texels against buildings 10-30 m wide, which is why building
                // shadows were blobs and got worse the more the view was tilted. At z16 this gives
                // about a kilometre and a metre-scale texel instead.
                double allowedGround = std::min(std::max(3.0 * slabExtent, 4.0 * groundNear), 60000.0 * metersToInternal);
                allowedGround = std::max(allowedGround, 1000.0 * metersToInternal);
                if (maxDistance > 0) {
                    allowedGround = maxDistance; // an explicit distance is the caller's decision
                }
                groundFar = std::min(groundFar, groundNear + allowedGround);
            }
            // Split THAT distance range, not the raw frustum: a camera above a mountain spends the
            // first kilometres of every edge in the air, and a cascade cut from the frustum alone
            // would own only sky and then fall back to covering everything.
            // The split is the usual mix of a geometric and a linear one: a screen pixel covers
            // ground in proportion to its distance, so a geometric split keeps texels about the
            // same size relative to the pixels that read them, while the linear term stops the
            // near cascade from collapsing to nothing.
            // OVERLAPPING, not nested and not disjoint. Fully nested (every cascade starting at
            // groundNear) means the outermost one always spans the WHOLE visible range, so with
            // three cascades the far half of a tilted screen is served by a box as wide as the
            // view - the 176 km box that produced 172 m texels. Fully disjoint is what nesting was
            // introduced to fix: a fragment that just fails the near cascade's test (inside its
            // PCF margin, or above its narrowed height slab) falls through to a cascade that does
            // not contain it either and comes out UNSHADOWED, in patches with straight edges.
            // Overlapping by 40% of the previous cascade's reach gives the density of disjoint
            // slices with a fall-through band orders of magnitude wider than the PCF margin.
            //
            // The split itself is geometric with a token linear part: a screen pixel covers ground
            // in proportion to its distance, so a geometric split keeps texels about the same size
            // relative to the pixels that read them. A larger linear term hands the near cascade a
            // slice measured in kilometres (with 3 cascades, a third of the whole view distance),
            // which is what made near shadows staircase at a tilt.
            double sliceNear = groundNear, sliceFar = groundFar;
            if (cascadeCount > 1 && groundFar > groundNear && groundNear > 0) {
                // The ratio being split is capped at 100. When the camera sits INSIDE the height
                // slab - anywhere near mountains, or at a city zoom where the cover reaches the
                // hills - the frustum rays start already inside it, so groundNear collapses to the
                // near plane, a few metres. A geometric split over a ratio of thousands then gives
                // the near cascade a few tens of METRES of ground and hands everything the user is
                // actually looking at to the coarse ones. Capped, the ladder stays useful: with
                // 3 cascades the first covers about a hundredth of the range, the last all of it.
                double splitNear = std::max(groundNear, groundFar / 100.0);
                auto splitFar = [&](int index) {
                    if (index + 1 >= cascadeCount) {
                        return groundFar; // the last cascade always reaches the end of the range
                    }
                    double part = static_cast<double>(index + 1) / cascadeCount;
                    double logSplit = splitNear * std::pow(groundFar / splitNear, part);
                    double linearSplit = splitNear + (groundFar - splitNear) * part;
                    return 0.95 * logSplit + 0.05 * linearSplit;
                };
                sliceFar = splitFar(cascade);
                if (cascade > 0) {
                    sliceNear = groundNear + (splitFar(cascade - 1) - groundNear) * 0.6;
                }
            }
            // The ground this cascade covers is the piece of the view cone between two distances:
            // an annulus sector, centred on the camera, spanning the frustum's horizontal opening
            // angle. Its inner and outer radii are the horizontal distances at which a slant range
            // meets the slab - taken at BOTH slab heights, since the terrain in between is what
            // receives. Built this way the footprint exists for every slice inside the distance
            // range, which is what the ray-intersection version could not guarantee.
            if (!rangeFirst && sliceFar > 0) {
                // A sector is well approximated by five directions; a full circle sampled five
                // times is a pentagon INSIDE the disc, which would clip the corners off the very
                // footprint this is meant to cover.
                const int ANGLE_SAMPLES = (fullCircle ? 13 : 5);
                for (int level = 0; level < 2; level++) {
                    // CLAMPED at zero: with mountains in the cover the top of the slab can be ABOVE
                    // the camera, and then |height| > distance makes the radius zero - every sample
                    // collapses onto the camera position, the footprint has fewer than three points
                    // and falls back to the WHOLE tile cover. That is how the nearest cascade ended
                    // up with the coarsest box of the three (measured 31 m for cascade 0 against
                    // 2.7 m for cascade 1). A slab face at or above the eye is treated as being at
                    // eye height, which covers the ground conservatively instead of collapsing.
                    double height = std::max(0.0, eye(2) - (level ? maxZ : minZ));
                    for (int end = 0; end < 2; end++) {
                        double distance = (end ? sliceFar : sliceNear);
                        double radius = std::sqrt(std::max(0.0, distance * distance - height * height));
                        for (int i = 0; i < ANGLE_SAMPLES; i++) {
                            double angle = halfAngle * (-1.0 + 2.0 * i / (ANGLE_SAMPLES - 1));
                            double c = std::cos(angle), s = std::sin(angle);
                            cglib::vec2<double> direction(viewDir(0) * c - viewDir(1) * s, viewDir(0) * s + viewDir(1) * c);
                            double x = eye(0) + direction(0) * radius;
                            double y = eye(1) + direction(1) * radius;
                            groundPolygon.emplace_back(x, y);
                            if (visFirst) {
                                visMinX = visMaxX = x;
                                visMinY = visMaxY = y;
                                visFirst = false;
                            } else {
                                visMinX = std::min(visMinX, x); visMaxX = std::max(visMaxX, x);
                                visMinY = std::min(visMinY, y); visMaxY = std::max(visMaxY, y);
                            }
                        }
                    }
                }
            }
            // Narrowing to the visible slice is an OPTIMISATION - the drawn tiles are the ground
            // that can be shadowed at all, and this only trims them to the part this cascade
            // covers. When the trim comes out empty the answer is therefore the tiles, NOT failure:
            // a cascade slice can miss the cover completely (at a high zoom the cover is a handful
            // of tiles around the focus while the near slice sits in front of them), and cascade 0
            // returning false takes every shadow on screen with it. Measured: z16 tilt 50 over
            // Grenoble, 4 drape tiles, "shadows WANTED BUT UNAVAILABLE" and a black-and-white map.
            if (!visFirst) {
                double trimMinX = std::max(minX, visMinX), trimMaxX = std::min(maxX, visMaxX);
                double trimMinY = std::max(minY, visMinY), trimMaxY = std::min(maxY, visMaxY);
                if (trimMaxX > trimMinX && trimMaxY > trimMinY) {
                    minX = trimMinX; maxX = trimMaxX;
                    minY = trimMinY; maxY = trimMaxY;
                }
            } else if (maxDistance > 0) {
                double trimMinX = std::max(minX, eye(0) - maxDistance), trimMaxX = std::min(maxX, eye(0) + maxDistance);
                double trimMinY = std::max(minY, eye(1) - maxDistance), trimMaxY = std::min(maxY, eye(1) + maxDistance);
                if (trimMaxX > trimMinX && trimMaxY > trimMinY) {
                    minX = trimMinX; maxX = trimMaxX;
                    minY = trimMinY; maxY = trimMaxY;
                }
            }
            if (maxX <= minX || maxY <= minY) {
                texelMeters = -5; // the drawn tiles themselves are degenerate: nothing to shadow
                return false;
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

        // Fit the box: the SIDES to the ground that receives shadows, the DEPTH range to the
        // ground that casts them. Fitting the sides to the casters too would mean every extra
        // margin tile widens the box and coarsens every texel in it - which is why raising the
        // caster margin blurred and displaced the building shadows. Extending only the depth
        // range lets an off-screen mountain be rasterised into the same texels at no cost.
        //
        // The sides are fitted to the visible-ground POLYGON clipped to the drawn tiles, not to
        // its bounding rectangle. The rectangle is world-axis-aligned and the fit happens in light
        // space, so it is bounded twice - once by the world axes and once by the light axes - and
        // at a tilt with the sun diagonal to the view that costs a factor of two on every texel.
        std::vector<cglib::vec2<double> > footprint = clipPolygonToRect(convexHull2D(groundPolygon), minX, minY, maxX, maxY);
        if (footprint.size() < 3) {
            footprint.clear(); // no usable wedge (or it misses the tiles entirely): fall back to the rectangle
            footprint.emplace_back(minX, minY);
            footprint.emplace_back(maxX, minY);
            footprint.emplace_back(maxX, maxY);
            footprint.emplace_back(minX, maxY);
        }
        double l = 0, r = 0, b = 0, t = 0, n = 0, f = 0;
        bool fitFirst = true;
        for (const cglib::vec2<double>& corner : footprint) {
            for (int level = 0; level < 2; level++) {
                cglib::vec4<double> p = cglib::transform(cglib::vec4<double>(corner(0), corner(1), level ? maxZ : minZ, 1.0), lightView);
                if (fitFirst) {
                    l = r = p(0); b = t = p(1); n = f = -p(2);
                    fitFirst = false;
                } else {
                    l = std::min(l, p(0)); r = std::max(r, p(0));
                    b = std::min(b, p(1)); t = std::max(t, p(1));
                    n = std::min(n, -p(2)); f = std::max(f, -p(2));
                }
            }
        }
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
            // the box cannot cast into it however tall it is. Skipping those keeps the depth range
            // - and with it the resolution of the normalised bias - tied to what really casts,
            // instead of to the whole tile cover, and it is also the list of tiles the caster pass
            // has to draw for this cascade: a near cascade covers a fraction of the tiles, and
            // drawing the rest into it is pure cost. The margin covers the box growth from the
            // texel snapping below.
            double marginX = (r - l) * 0.2, marginY = (t - b) * 0.2;
            if (tileR < l - marginX || tileL > r + marginX || tileT < b - marginY || tileB > t + marginY) {
                continue;
            }
            n = std::min(n, tileN); f = std::max(f, tileF);
            boxCasterTileIds.push_back(tileId);
        }
        // Snap the box to a world-anchored lattice of whole shadow texels, and quantise its size
        // so the texel size itself only changes in steps. Fitted exactly, the box breathes with
        // every camera movement: the same piece of ground falls in a different texel each frame,
        // so every shadow edge crawls and the interior of a large shadow flickers. Snapped, the
        // matrix is bit-identical while the camera moves inside one step - which both stabilises
        // the image and lets the caller skip the caster pass entirely.
        if (mapSize > 0) {
            for (int axis = 0; axis < 3; axis++) {
                double& lo = (axis == 0 ? l : axis == 1 ? b : n);
                double& hi = (axis == 0 ? r : axis == 1 ? t : f);
                double size = hi - lo;
                if (!(size > 0)) {
                    continue;
                }
                // A ladder of eighths of a power of two: at most 12.5% of the box is wasted, and
                // the step changes rarely enough that the texel size is stable in practice.
                double step = std::pow(2.0, std::floor(std::log2(size)) - 3.0);
                double quantSize = std::ceil(size / step) * step;
                // The depth axis has no texels; quantising it keeps the depth range - and with it
                // the shader's normalised bias - constant while the camera moves.
                double grid = (axis == 2 ? step : quantSize / mapSize);
                if (quantSize - size < grid) {
                    quantSize += step; // snapping moves the low edge down by up to one grid cell
                    grid = (axis == 2 ? step : quantSize / mapSize);
                }
                lo = std::floor(lo / grid) * grid;
                hi = lo + quantSize;
            }
        }
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
                    setupTerrainUniforms(shaderProgram, tileId, surfaceFrame, true);
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
        if (_visibleRenderTiles) {
            // Cast from both faces: culling the front faces stored the far side of the building
            // and detached its shadow from its own footprint. The acne that motivated it is
            // handled by the slope-scaled caster offset, which the tightened light frustum made
            // effective again.
            _shadowCasterViewProj = &lightViewProj;
            for (const RenderTile& renderTile : *_visibleRenderTiles) {
                if (!renderTile.visible) {
                    continue;
                }
                bool covered = false;
                for (const TileId& tileId : tileIds) {
                    if (tileCovers(renderTile.targetTileId, tileId)) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    continue;
                }
                for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                    const RenderTileLayer& renderLayer = it->second;
                    if (!renderLayer.layer) {
                        continue;
                    }
                    for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                        if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                            // The tile's OWN blend, not 1: an extrusion fades in by GROWING - blend
                            // scales its height - so a caster drawn at full height throws the
                            // shadow of a finished building from under a half-grown one, and the
                            // shadow pops out of existence when the tile is finally dropped.
                            renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, renderLayer.blend, 1.0f, renderLayer.tileSize, geometry);
                            draws++;
                        }
                    }
                }
            }
            _shadowCasterViewProj = nullptr;
        }
        return draws;
    }

    float GLTileRenderer::shadowCasterFadeSignature() const {
        std::lock_guard<std::mutex> lock(_mutex);

        // A number that moves exactly as fast as the caster geometry does. An extrusion fades in
        // by GROWING, so its blend IS its height: the owner refreshes the shadow map when this has
        // moved far enough to see, instead of every frame of every fade (which costs a full caster
        // pass each time) or never (which leaves the shadow of a building that is not that shape).
        float signature = 0.0f;
        int count = 0;
        if (!_visibleRenderTiles) {
            return signature;
        }
        for (const RenderTile& renderTile : *_visibleRenderTiles) {
            if (!renderTile.visible) {
                continue;
            }
            for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
                const RenderTileLayer& renderLayer = it->second;
                if (!renderLayer.layer) {
                    continue;
                }
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        signature += renderLayer.blend;
                        count++;
                        break; // one contribution per layer, not per geometry batch
                    }
                }
            }
        }
        // The MEAN, not the sum: twenty tiles fading in together move a sum twenty times as fast
        // as one does, and the map would be redrawn on every frame of exactly the moment this is
        // meant to protect. A single tile fading alone moves the mean by less than the step and
        // rides on the age cap instead.
        return count > 0 ? signature / count : 0.0f;
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
        std::lock_guard<std::mutex> lock(_mutex);

        VT_STAT_INC(visibleTileSetChanges);
        _visibleTileIds = tileIds;
        buildTerrainEdgeCoarsening();
        buildTileSurfaces(tileIds);
        buildLabelMaps(labelTiles);
        buildRenderTiles(tiles);
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
            _bitmapLabelMap[pass] = std::make_shared<BitmapLabelMap>();
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
            _bitmapLabelMap[pass].reset();
            _visibleBitmapLabelMap[pass].reset();
        }
        _labels.clear();
        _layerLabelMap.clear();
    }
    
    bool GLTileRenderer::startFrame(float dt) {
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        std::lock_guard<std::mutex> lock(_mutex);

        resetProgramState(); // another renderer may have bound its own program since the last draw

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
        _visibleBitmapLabelMap = _bitmapLabelMap;
        float dOpacity = (_labelBlendingSpeed > 0.0f ? dt * _labelBlendingSpeed : 1.0f);
        for (int pass = 0; pass < 2; pass++) {
            for (const BitmapLabelsPair& bitmapLabels : *_visibleBitmapLabelMap[pass]) {
                for (const std::shared_ptr<Label>& label : bitmapLabels.second) {
                    refresh = updateLabel(label, dOpacity) || refresh;
                }
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

            // Terrain reference surface pre-pass, for EVERY terrain tile layer.
            // Each tile layer works in its OWN depth domain: the depth buffer is
            // cleared, the displaced tile surfaces are rendered at their TRUE depth,
            // and the 2D surface content (backgrounds/bitmaps) then WRITES its real
            // depth on top. The true-depth surface blocks far-slope fragments exactly:
            // translucent surfaces (hillshade) can not blend both slopes of a ridge in
            // one draw call, and draped geometry passes only within its own small
            // forward slack. It also optionally paints the terrain background color
            // with the same meshes.
            //
            // Skipped entirely under a cross-layer drape: the owner has already baked every
            // layer's content into one texture per tile and drawn the shared surface, which is
            // then the only depth-writing terrain geometry. Running this per-layer pre-pass would
            // glClear(DEPTH) that shared surface away and re-establish the private depth domain
            // the shared drape exists to remove.
            // Content composited onto the shared ground is coincident with it, so it has to pass
            // at EQUAL depth; the ground pass itself ran with GL_LESS. Set before the paint, which
            // is the first such layer.
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
                _terrainDrawDepthClipUnits = _terrainPainterOrder ? -TERRAIN_PAINTER_SURFACE_BACK : 0.0f; // painter-order: the ground surface is pushed back like the backgrounds/bitmaps
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

            // 2D geometry pass. With the true-depth ground occluder, lattice-clamped content
            // sits at the SAME depth as the surface, so test it with GL_LEQUAL and no forward
            // bias: coincident content passes (visible), but content behind a near ridge is at
            // greater depth and fails (occluded) - zero forward pull means zero ridge leak.
            //
            // No stencil tile masks: a full displaced surface draw per tile PER LAYER, and tangram
            // has none. What replaces them is tangram's answer - the ground-shaped content writes
            // depth and a retained (proxy) tile writes pushed back, so the tile that replaces it
            // wins the ground and the proxy's own content fails against it. Without that, the
            // retained tile from the previous zoom paints its whole footprint through every gap in
            // the new tile's content: the same roads twice, one zoom level apart, blinking as the
            // blend runs.
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
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

        VT_STAT_CLOCK(lockClock);
        std::lock_guard<std::mutex> lock(_mutex);
        VT_STAT_SPLIT(mutexWaitNs, lockClock);

        resetProgramState(); // another renderer may have bound its own program since the last draw

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
                for (const BitmapLabelsPair& bitmapLabels : *_visibleBitmapLabelMap[pass]) {
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
        using BitmapLabelsPair = std::pair<std::shared_ptr<const Bitmap>, std::vector<std::shared_ptr<Label>>>;

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

            for (const BitmapLabelsPair& bitmapLabels : *_bitmapLabelMap[pass]) {
                for (const std::shared_ptr<Label>& label : bitmapLabels.second) {
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
        // Fully transparent fog, or a zero range, means the style/app did not ask for any: the
        // programs are then built without it and cost nothing.
        return (_fogColor[3] > 0.0f && _fogDistance > _fogStartDistance ? FOG_FLAG : 0);
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
        glUniform2f(shaderProgram.uniforms[U_FOGPARAMS], _fogStartDistance, 1.0f / std::max(1.0e-9f, _fogDistance - _fogStartDistance));
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
                renderLayer.blend = std::max(0.0f, renderLayer.blend - delta);
                refresh = (renderLayer.blend > 0.0f) || refresh;

                // In case of non-active layers, simply remove the layer when it has become invisible.
                if (renderLayer.blend <= 0.0f) {
                    it = renderTile.renderLayers.erase(it);
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
                int pass = ((label->getStyle()->orientation == LabelOrientation::BILLBOARD_3D || label->getStyle()->orientation == LabelOrientation::LINE_BILLBOARD_3D) ? 1 : 0);

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

        // The stencil tile masks, and when they are worth their cost. They clip a tile's content
        // to its own screen footprint, which is what stops a retained tile painting through the
        // gaps of the tile that replaced it - and tangram has no stencil at all. In a TERRAIN
        // frame a mask is a full displaced grid drawn per tile per stencil reset, two thirds of
        // all the surface geometry a frame submits (device, north pan: 19.5 -> 23.5 fps without
        // them), so they are dropped there. In 2D a mask is a two-triangle quad and costs nothing
        // measurable (40.2 vs 40.7 fps), so 2D keeps them.
        // A layer with a comp-op is the exception in both: it composites through the overlay
        // buffer, which has its own stencil and no depth at all, so its content has nothing else
        // to clip it to its tile.
        // Whether the MASKS run is a separate question from whether the buffer HAS a stencil:
        // the single-blend pass below needs one spare bit and no masks at all, so it must not be
        // switched off with them (under a shared ground it never ran, which is where a translucent
        // line kept blending itself twice at every join).
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
        // Tangram's style-layer order as a small dense index. The term it feeds carries a
        // constant-NDC part (2^-19 per unit) whose eye tolerance grows as distance^2 - which is how
        // rounds 45-56 saw through ridges - but that took 128-256 units, and a style layer count is
        // tens. The base separates one tile layer's style layers from another's, since every tile
        // layer has a renderer of its own.
        // Numbered over every style layer this renderer has EVER drawn, not over the ones present
        // this frame. Tangram's `order` is a property of the scene, fixed before a tile loads
        // (osm-bright.yaml numbers them 1..93); a rank over what happens to be on screen renumbers
        // the whole stack as tiles come and go - measured walking a layer's base 7 -> 5 -> 7 between
        // frames - so the depth relation between two layers is a moving target and content pops in
        // and out from under the layer above it. The set only grows, and a style has a bounded
        // number of layers.
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

            // SINGLE BLEND: a translucent style layer must paint each pixel once. Its geometry
            // overlaps itself in ways no tesselation can avoid - a line whose vertices sit closer
            // together than it is wide folds at every join, a route doubles back within its own
            // width - and every overlap blends again, which reads as darker knots along the line.
            // Marking each pixel in a spare stencil bit as the layer paints it and rejecting the
            // second fragment costs one bit and a masked clear per layer, no extra geometry pass.
            // Only for a layer that IS translucent: an opaque one cannot show the artifact, and a
            // later fragment of it legitimately covers an earlier one. A comp-op layer already
            // composites once through its own buffer, and the low stencil bits carry the per-tile
            // mask values, so this needs the tile count to leave the top bit free.
            bool singleBlend = false;
            if (stencilBits > 0 && !layer->getCompOp() && tileStencilMap.size() < SINGLE_BLEND_STENCIL_BIT) {
                singleBlend = geometryOpacity < 1.0f;
                for (auto layerIt = renderLayers.begin(); layerIt != renderLayers.end() && !singleBlend; layerIt++) {
                    for (const std::shared_ptr<TileGeometry>& geometry : (*layerIt)->layer->getGeometries()) {
                        const TileGeometry::StyleParameters& styleParams = geometry->getStyleParameters();
                        for (int i = 0; i < styleParams.parameterCount && !singleBlend; i++) {
                            if ((styleParams.colorFuncs[i])(_viewState).alpha() < 1.0f) {
                                singleBlend = true;
                            }
                        }
                    }
                }
            }
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
                    // Terrain displacement makes near tiles rise in front of far tiles, so
                    // their screen footprints can overlap. Draw the masks so that the tile that
                    // should be visible owns the overlapping pixels: retained blend-out tiles
                    // (no active layers) first - stale tiles kept only for crossfading must
                    // never steal pixels from live tiles (a retained high-zoom tile far behind
                    // a ridge would otherwise punch through the near ridge during LOD
                    // transitions) - then by zoom level ascending - during LOD/overzoom
                    // transitions parent and child tiles have IDENTICAL footprints and the
                    // child must own them (distances are arbitrary there) - and by descending
                    // camera distance within a zoom level, so that near ridges own pixels
                    // against far tiles behind them (by LOD construction nearer tiles never
                    // have a lower zoom level than farther ones).
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

            // Single blend, GL state. After the mask reset above, which clears every bit and puts
            // the op back to KEEP - setting it before would be undone for the first layer of the
            // frame and for the first one after a comp-op layer.
            if (singleBlend) {
                glEnable(GL_STENCIL_TEST); // the masks may not be running at all
                glStencilMask(SINGLE_BLEND_STENCIL_BIT); // clear the paint bit only
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                // INVERT flips the paint bit on fragments that pass; the GL_EQUAL test then rejects
                // anything landing where this layer already painted. With the masks off there is no
                // per-tile value to match, so the test reads the paint bit alone.
                glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                if (maskStencilBits == 0) {
                    glStencilFunc(GL_EQUAL, 0, SINGLE_BLEND_STENCIL_BIT);
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
                    // Only a single-blend layer compares the paint bit (that comparison IS its
                    // rejection); nothing clears the bit afterwards, so any other layer must
                    // ignore it or it inherits the last translucent layer's shape as a hole.
                    glStencilFunc(GL_EQUAL, stencilValue, singleBlend ? 255 : (255 & ~SINGLE_BLEND_STENCIL_BIT));
                }

                // Terrain GPU draping mode (tangram-style depth model): ALL 2D content
                // writes its real depth. Coplanar content of different style layers
                // shares the same displaced heights, so LEQUAL + painter's order stacks
                // it without any per-layer bias; occlusion between tiles/slopes is
                // decided by the actual drawn geometry. Backgrounds/bitmaps share the
                // reference surface VBOs and carry no bias at all; retained blend-out
                // (proxy) tiles are pushed back one delta so live content always wins
                // their overlaps. CPU fallback mode: mesh tesselations differ between
                // layers, so surfaces are separated with slope-scaled polygon offsets.
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
                        // Tangram's model, whole. Three parts, and none of them does the job on its
                        // own - which is what adopting it piecemeal cost:
                        //  - content WRITES depth, so a live tile beats the proxy it replaces and
                        //    the stencil tile masks are not needed;
                        //  - every style layer is pulled forward by its own ordinal, so coplanar
                        //    content of two layers cannot fight once both write (round 52's washed
                        //    road casings were writes WITHOUT this);
                        //  - the pull is (2^-19*w + depth_shift), and depth_shift is CONSTANT CLIP:
                        //    large near the camera, where content and the ground disagree most and
                        //    fills shred into stripes, and dying as 1/w at range, where a forward
                        //    pull is what leaks over a ridge.
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Backgrounds/bitmaps ARE the terrain occluders: they render the
                        // reference surface meshes and WRITE depth. Retained blend-out
                        // (proxy) tiles are pushed back one delta so live content wins.
                        // They draw at TRUE depth, exactly like the drape surface does: they
                        // render the SAME meshes the reference pre-pass already drew, so any
                        // pushback here puts them at (or behind) the depth the pre-pass wrote
                        // and GL_LESS rejects them. Near the camera they survive on the tiny
                        // numerical difference between the two shader paths; beyond a few
                        // kilometres the two depths quantize to the same value and every
                        // raster (hillshade, imagery) vanishes along a hard horizontal line -
                        // the "far tiles go flat with drape off" report. The geometry pass
                        // gets its clearance from the pre-pass pushback instead, which is
                        // where the twist-clearing slack belongs.
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
                    // Tangram has NO per-tile background mesh at all: the map background is the
                    // framebuffer clear colour, applied once per frame
                    // (core/src/map.cpp, FrameBuffer::apply(..., background.toColorF())). Ours gives
                    // every tile layer a Map{background-color} and drew it as a full grid per cover
                    // tile PER LAYER - with three layers over a 28-tile cover that is ~84 grid
                    // draws a frame for pixels the ground pass already painted.
                    // Under a shared ground the ground pass owns the ground colour, so a patternless
                    // background is skipped whatever its colour. A PATTERN is real content (a
                    // texture over the ground) and still draws; tangram has no equivalent of it.
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
                        // Geometry writes as well (tangram writes for opaque AND translucent) and
                        // carries the same ordinal as the backgrounds and rasters of its own layer.
                        // The ordinal term is tangram's flat per-step shift, and it separates
                        // coplanar style layers - it is NOT a budget to spread over the stack.
                        // Scaling it by the ordinal span put ten times their pull on every layer,
                        // which is what let far content over a near ridge. An un-subdivided AREA
                        // FILL that chords over the ground needs more clearance than one step
                        // gives, and there is no room for it here: measured at zoom 14, a fill
                        // slack of half a step leaves the slivers and two steps hides the ground
                        // paint under the fill. That one is tangram-unreachable (their terrain base
                        // map is a raster inside the ground draw, so no vector fill ever chords
                        // over their terrain) and it is open - do not pay for it out of this term.
                        _terrainDrawDepthBias = _terrainDepthBias;
                        _terrainDrawDepthClipUnits = 0.0f;
                        _terrainDrawLayerOffset = proxyDepth(renderLayer) - layerOrdinal;
                    } else if (terrainVTF) {
                        // Geometry (roads, lines, polygons) is a different piecewise-linear
                        // approximation of the height field than the background/surface
                        // meshes: between vertices its chords deviate by the interpolation
                        // error, so it renders with a small distance-growing clip slack -
                        // otherwise it dips below the written background depth and tears
                        // on slopes. The slack band is the only depth range where far
                        // content can leak over a ridge - it does not grow with the style
                        // layer count.
                        // Painter-order: geometry draws at its REAL depth (no slack) - the
                        // clearance is provided by pushing the surface BACK instead, so
                        // geometry is never pulled forward and can not leak over a ridge.
                        float proxyBias = (renderLayer->active ? 0.0f : 1.0f * TERRAIN_LAYER_DEPTH_DELTA);
                        // Painter-order: lattice-clamped content is coincident with the true-depth
                        // occluder surface and is drawn with GL_LEQUAL, so it needs ZERO forward
                        // bias - it passes at equal depth and is occluded (fails) behind a ridge.
                        // Any forward clip bias here leaks over ridges at range (the contour
                        // see-through), so keep it at zero in painter-order.
                        _terrainDrawDepthBias = (_terrainPainterOrder ? 0.0f : _terrainDepthBias + 1.0f * TERRAIN_LAYER_DEPTH_DELTA) - proxyBias;
                        // Lattice clamp (regular-grid mode) makes draped geometry follow the
                        // reference grid surface within the tiny in-cell bilinear-vs-triangle
                        // twist, so the distance-growing slack collapses to a small margin;
                        // adaptive meshes keep the full calibrated slack.
                        _terrainDrawDepthClipUnits = _terrainPainterOrder ? 0.0f : (_terrainRegularGrid ? 2.0f : 12.0f);
                    } else {
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(-1.0f, -2.0f);
                    }
                }

                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    // Draped fills/lines are baked into the drape texture already - unless the
                    // layer opted out of the bake (setNoDrapeLayerFilter), in which case this pass
                    // is the only one that draws it.
                    if (drapedTile && isDrapeableGeometry(geometry->getType()) && isLayerDraped(renderLayer->layer)) {
                        continue;
                    }
                    if (geometry->getType() != TileGeometry::Type::POLYGON3D) {
                        CompOp geometryCompOp = geometry->getStyleParameters().compOp;
                        if (currentCompOp != geometryCompOp) {
                            setCompOp(geometryCompOp);
                            currentCompOp = geometryCompOp;
                        }
                        // Undraped LINES are a DECAL on the surface, and they need the decal
                        // treatment. A line is a chain of quads: between two vertices it chords
                        // over the surface instead of following it, so unlike a fill it is not
                        // coincident with the true-depth occluder surface, and at zero bias the
                        // sagging half of every segment fails the depth test - the line breaks
                        // into dashes over relief (flat ground shows none). A polygon offset is
                        // the right tool over a constant bias: it scales with the primitive's own
                        // depth slope, which is what the sag scales with, so it stays a hairline
                        // clearance head-on and grows exactly where the surface tilts away.
                        bool lineDecal = terrainVTF && geometry->getType() == TileGeometry::Type::LINE;
                        if (lineDecal) {
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
                        if (lineDecal) {
                            _terrainDrawClearance = _terrainLineClearance;
                        }
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, geometry);
                        _terrainDrawClearance = 0.0f;
                        if (lineDecal) {
                            glDisable(GL_POLYGON_OFFSET_FILL);
                            glPolygonOffset(0.0f, 0.0f);
                        }
                    }
                }

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

            if (singleBlend) {
                glStencilMask(0);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                if (maskStencilBits == 0) {
                    glDisable(GL_STENCIL_TEST); // nothing else in this pass uses it
                }
            }

            // If compositing was enabled for this layer, blend the rendered layer with framebuffer
            if (layer->getCompOp()) {
                if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !_overlayBuffer2D.depthStencilAttachments.empty()) {
                    _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer2D.depthStencilAttachments.size()), _overlayBuffer2D.depthStencilAttachments.data());
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
                    contains3DGeometry = (geometry->getType() == TileGeometry::Type::POLYGON3D) || contains3DGeometry;
                }
                if (contains3DGeometry || (layer->getCompOp() && isEmptyBlendRequired(*layer->getCompOp()))) {
                    if (!_rendererLayerIndexRange || (it->first >= _rendererLayerIndexRange->first && it->first < _rendererLayerIndexRange->second)) {
                        renderLayerMap[it->first].push_back(&it->second);
                    }
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

            // Tangram draws its extrusions straight into the main framebuffer, depth-tested
            // against the ground it stands on - it has no per-layer 3D overlay at all. The
            // overlay here buys exactly two things: comp-op compositing, and 'flatten the whole
            // layer, then blend it once' semantics for a fading layer (overlapping translucent
            // buildings must not blend against each other). Neither applies to an opaque layer
            // with no comp-op, which is the normal case - and the overlay costs it a full-screen
            // clear, a full terrain surface pre-pass (one displaced mesh draw per visible tile,
            // only to re-seed a depth buffer the main framebuffer already holds) and a
            // full-screen composite, every frame.
            // Only when the caller says the extrusions are the last tile content of the frame
            // (buildingOrder 1): drawn inline they WRITE depth into the main framebuffer, which
            // would otherwise occlude the 2D content of the layers drawn after them.
            bool useOverlay = !allowInline || static_cast<bool>(layer->getCompOp()) || geometryOpacity < 1.0f - 1.0f / 255.0f;

            // Prepare the overlay buffer.
            GLint currentFBO = 0;
            if (useOverlay) {
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

                if (_overlayBuffer3D.fbo == 0) {
                    // Ask for the packed depth-stencil buffer where it exists: that is the only
                    // way to get a 24-bit depth buffer here (the plain path is DEPTH_COMPONENT16),
                    // and 16 bits over a terrain-sized near-far range quantises to several metres
                    // at a couple of kilometres - enough to eat the bottom of every extrusion once
                    // the terrain surface is an occluder in this buffer. The stencil half is left
                    // unused. Without the extension the fallback stays 16-bit, and the extrusion
                    // depth slack below is what keeps the bases intact.
                    createFrameBuffer(_overlayBuffer3D, true, true, _glExtensions->GL_OES_packed_depth_stencil_supported());
                }

                glBindFramebuffer(GL_FRAMEBUFFER, _overlayBuffer3D.fbo);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            // Terrain reference surface pre-pass, INSIDE the 3D overlay.
            // The extrusions are drawn into a private framebuffer whose depth buffer was
            // just cleared, and the result is composited back as a flat screen quad with
            // depth testing off - so without this the terrain in the main framebuffer can
            // not occlude anything, and a building behind a ridge paints straight over the
            // ridge. Seeding this depth buffer with the same displaced tile surfaces that
            // the 2D pass uses as its occluder restores the occlusion inside the overlay:
            // fragments behind a crest are simply never painted, so the composite leaves
            // the terrain pixels untouched.
            // Skipped on the inline path: there the extrusions ARE in the main framebuffer,
            // whose depth already holds that same cover - the shared ground, or the last tile
            // layer's own surface pre-pass (with buildingOrder = 1 the extrusions are drawn
            // from onDrawFrame3D, after every layer's 2D pass). Re-seeding it would draw the
            // same meshes a second time for the same result.
            // Only when this layer actually has extrusions to occlude: the map above also
            // collects comp-op layers that need the empty-blend overlay but contain no 3D
            // geometry at all, and the pre-pass is a full terrain surface draw.
            bool terrainOccluders = _terrainMode && static_cast<bool>(_terrainTextureProvider) &&
                std::any_of(renderLayers.begin(), renderLayers.end(), [](const RenderTileLayer* renderLayer) {
                    const std::vector<std::shared_ptr<TileGeometry>>& geometries = renderLayer->layer->getGeometries();
                    return std::any_of(geometries.begin(), geometries.end(), [](const std::shared_ptr<TileGeometry>& geometry) {
                        return geometry->getType() == TileGeometry::Type::POLYGON3D;
                    });
                });
            if (terrainOccluders) {
                if (useOverlay) {
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
                // Clearance for the extrusions themselves. Two separate errors to cover:
                //  - the base ring samples the elevation texture at arbitrary xy while the
                //    surface mesh interpolates it linearly over its cells (the same chord
                //    error the draped 2D geometry carries) -> the clip-slack component;
                //  - a wall stands ON the surface it is tested against, so the two are only
                //    separable to the depth buffer's resolution, which in eye units grows
                //    like distance^2/near -> the constant-NDC component, which follows the
                //    same law. Without it the buffer eats the lower walls from a couple of
                //    kilometres out, taking most of a 40 m building with it.
                // A uniform bias shifts every extrusion equally, so building-vs-building
                // occlusion inside the overlay is unaffected.
                _terrainDrawDepthBias = _terrainDepthBias + TERRAIN_EXTRUSION_DEPTH_DELTAS * TERRAIN_LAYER_DEPTH_DELTA;
                _terrainDrawDepthClipUnits = (_terrainRegularGrid ? 2.0f : 12.0f);
            }

            if (!useOverlay) {
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

            // Render tile layers for this layer
            for (const RenderTileLayer* renderLayer : renderLayers) {
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer->layer->getGeometries()) {
                    if (geometry->getType() == TileGeometry::Type::POLYGON3D) {
                        // NOTE: geometry comp op is not supported for 3D polygons. Blending is disabled, setGLBlendState not needed
                        renderTileGeometry(renderLayer->sourceTileId, renderLayer->targetTileId, renderLayer->blend, geometryOpacity, renderLayer->tileSize, geometry);
                    }
                }
            }

            if (!useOverlay) {
                glDisable(GL_BLEND); // the overlay path below composites instead
            }

            if (terrainOccluders) {
                _terrainDrawDepthBias = _terrainDepthBias;
                _terrainDrawDepthClipUnits = 0.0f;
            }

            // Blend the rendered layer with framebuffer
            if (useOverlay) {
                if (_glExtensions->GL_OES_packed_depth_stencil_supported() && !_overlayBuffer3D.depthStencilAttachments.empty()) {
                    // TODO: for now it crashes. See why
//                    _glExtensions->glDiscardFramebufferEXT(GL_FRAMEBUFFER, static_cast<GLsizei>(_overlayBuffer3D.depthStencilAttachments.size()), _overlayBuffer3D.depthStencilAttachments.data());
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
        // Leader lines first, all of them, then all the text: a line that crossed a neighbouring
        // label's glyphs would read as a strike-through. The extra pass only exists when some
        // label actually has a line to draw.
        bool anyCallout = std::any_of(labels.begin(), labels.end(), [](const std::shared_ptr<Label>& label) {
            return label->getStyle()->orientation == LabelOrientation::CALLOUT && label->getStyle()->calloutLineGlyph;
        });
        if (anyCallout) {
            renderLabelPass(labels, bitmap, Label::DrawPass::CALLOUT_LINE);
        }
        renderLabelPass(labels, bitmap, anyCallout ? Label::DrawPass::TEXT : Label::DrawPass::ALL);
    }

    void GLTileRenderer::renderLabelPass(const std::vector<std::shared_ptr<Label>>& labels, const std::shared_ptr<const Bitmap>& bitmap, Label::DrawPass pass) {
        LabelBatchParameters labelBatchParams;
        std::shared_ptr<const TileLabel::Style> lastLabelStyle;
        int styleIndex = -1;
        int haloStyleIndex = -1;
        LabelPlateIndices plateIndices;
        int secondaryStyleIndex = -1;
        int iconStyleIndex = -1;
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

            if (lastLabelStyle != labelStyle) {
                cglib::vec4<float> color = cglib::vec4<float>(evaluateColorFunc(labelStyle->colorFunc).rgba());
                float size = evaluateFloatFunc(labelStyle->sizeFunc);
                cglib::vec4<float> haloColor = cglib::vec4<float>(evaluateColorFunc(labelStyle->haloColorFunc).rgba());
                float haloRadius = evaluateFloatFunc(labelStyle->haloRadiusFunc) * HALO_RADIUS_SCALE;
                haloRadius = std::min(haloRadius, static_cast<float>(GLYPH_RENDER_SPREAD));
                // In SCREEN PIXELS from here on: labelFsh measures the halo against the same one
                // screen pixel the antialias ramp is, so it no longer depends on which raster size
                // the label landed on. The clamp above stays where it was - it is the point past
                // which the encoded field runs out, and it is what a style's widest halo met before.
                haloRadius *= HALO_PIXELS_PER_UNIT;

                // Up to four plates: a fill and a border behind the text, and the same behind the
                // icon. Each colour is one more slot in the batch, exactly like the halo.
                const TileLabel::Style::Plate* plateList[4] = { &labelStyle->textPlate, &labelStyle->textPlate, &labelStyle->iconPlate, &labelStyle->iconPlate };
                const bool plateIsBorder[4] = { false, true, false, true };
                int plateCount = 0;
                for (int i = 0; i < 4; i++) {
                    if (plateIsBorder[i] ? plateList[i]->drawsBorder() : plateList[i]->drawsFill()) {
                        plateCount++;
                    }
                }
                // The second run of text may have its own colour, which is one more slot in the
                // batch - exactly like the halo and the plate.
                bool hasSecondaryColor = static_cast<bool>(labelStyle->secondaryColorFunc);
                cglib::vec4<float> secondaryColor = hasSecondaryColor ? cglib::vec4<float>(evaluateColorFunc(*labelStyle->secondaryColorFunc).rgba()) : color;
                // And so may the icon run - a font icon in its own colour next to the name.
                bool hasIconColor = static_cast<bool>(labelStyle->iconColorFunc);
                cglib::vec4<float> iconColor = hasIconColor ? cglib::vec4<float>(evaluateColorFunc(*labelStyle->iconColorFunc).rgba()) : color;
                if (labelStyle->transform || (lastLabelStyle && lastLabelStyle->transform) || labelBatchParams.scale != labelStyle->scale || labelBatchParams.glyphRenderSize != labelStyle->glyphRenderSize || labelBatchParams.parameterCount + 2 + plateCount + (hasSecondaryColor ? 1 : 0) + (hasIconColor ? 1 : 0) > LabelBatchParameters::MAX_PARAMETERS) {
                    renderLabelBatch(labelBatchParams, bitmap);
                    labelBatchParams.labelCount = 0;
                    labelBatchParams.parameterCount = 0;
                    labelBatchParams.scale = labelStyle->scale;
                    labelBatchParams.glyphRenderSize = labelStyle->glyphRenderSize;
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
                    plateIndices = LabelPlateIndices();
                    secondaryStyleIndex = -1;
                    iconStyleIndex = -1;
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
                int* plateTargets[4] = { &plateIndices.textFill, &plateIndices.textBorder, &plateIndices.iconFill, &plateIndices.iconBorder };
                for (int i = 0; i < 4; i++) {
                    const TileLabel::Style::Plate& plate = *plateList[i];
                    if (!(plateIsBorder[i] ? plate.drawsBorder() : plate.drawsFill())) {
                        continue;
                    }
                    cglib::vec4<float> plateColor = cglib::vec4<float>((plateIsBorder[i] ? plate.style.borderColor : plate.style.color).rgba());
                    int index = labelBatchParams.parameterCount;
                    for (--index; index >= 0; index--) {
                        if (labelBatchParams.colorTable[index] == plateColor && labelBatchParams.widthTable[index] == size && labelBatchParams.strokeWidthTable[index] == 0) {
                            break;
                        }
                    }
                    if (index < 0 && labelBatchParams.parameterCount < LabelBatchParameters::MAX_PARAMETERS) {
                        index = labelBatchParams.parameterCount++;
                        labelBatchParams.colorTable[index] = plateColor;
                        labelBatchParams.widthTable[index] = size;
                        labelBatchParams.strokeWidthTable[index] = 0;
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

                iconStyleIndex = -1;
                if (hasIconColor) {
                    for (iconStyleIndex = labelBatchParams.parameterCount; --iconStyleIndex >= 0; ) {
                        if (labelBatchParams.colorTable[iconStyleIndex] == iconColor && labelBatchParams.widthTable[iconStyleIndex] == size && labelBatchParams.strokeWidthTable[iconStyleIndex] == 0) {
                            break;
                        }
                    }
                    if (iconStyleIndex < 0) {
                        iconStyleIndex = labelBatchParams.parameterCount++;
                        labelBatchParams.colorTable[iconStyleIndex] = iconColor;
                        labelBatchParams.widthTable[iconStyleIndex] = size;
                        labelBatchParams.strokeWidthTable[iconStyleIndex] = 0;
                    }
                }

                lastLabelStyle = labelStyle;
            }

            VT_STAT_CLOCK(statClock);
            label->calculateVertexData(labelBatchParams.widthTable[styleIndex], _viewState, styleIndex, haloStyleIndex, _labelVertices, _labelOffsets, _labelNormals, _labelTexCoords, _labelAttribs, _labelIndices, pass, pass == Label::DrawPass::CALLOUT_LINE ? LabelPlateIndices() : plateIndices, pass == Label::DrawPass::CALLOUT_LINE ? -1 : secondaryStyleIndex, pass == Label::DrawPass::CALLOUT_LINE ? -1 : iconStyleIndex);
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
        // GPU terrain draping: bind the elevation texture covering the tile and the affine
        // transforms taking vertex xy coordinates (in the axis-aligned frame defined by
        // vertexFrameMatrix - a tile matrix or the tile surface origin translation) to
        // elevation texture uv coordinates and to the mercator latitude argument.
        // Distance-proportional depth slack (tangram's 'depth_shift'): independent of
        // the w-scaled (constant-NDC) bias, a draw can carry a CONSTANT clip-space
        // shift. In eye units a clip-constant shift grows linearly with distance,
        // which matches how the piecewise-linear interpolation error between the
        // reference surface meshes and draped geometry meshes grows with the mesh
        // cell size (coarser tiles at range / lower zooms) - a pure constant-NDC
        // delta becomes vanishingly small relative to that error when zoomed out.
        // Scaled by the tile size (mesh cells scale with it) and the projection depth
        // coefficient |m22| (which grows as the near-far range compresses).
        // NEGATIVE units push the draw AWAY from the viewer: the surface pre-pass is
        // pushed back by the interpolation-error slack so that draped geometry passes
        // over it at its REAL depth - content itself carries no distance-growing
        // slack, which is what previously let far-slope content through at ridges.
        float clipUnits = _terrainDrawDepthClipUnits;
        double tileSize = std::abs(_transformer->calculateTileMatrix(tileId, 1.0f)(0, 0));
        double projScaleZ = std::abs(_viewState.projectionMatrix(2, 2));
        // The mesh interpolation error is curvature limited and scales ~QUADRATICALLY
        // with the cell size, not linearly: a linear-in-tile-size slack calibrated for
        // low zooms overshoots several-fold at high zooms, and all the excess turns
        // into far-slope content bleeding over ridge crests and grazing faces (the
        // slack band is exactly the depth range that ignores occlusion). Anchor the
        // quadratic law at zoom 11 tiles (TERRAIN_DEPTH_CLIP_REF_TILE_SIZE).
        // _terrainSlackScale scales the slack with the actual surface/geometry
        // tesselation resolution (the chord error is quadratic in the cell size, so
        // doubling the mesh resolution allows a 4x tighter slack)
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

        // Cross-LOD edge stitching applies to the shared grid SURFACE only: its vertices are
        // the tile-local unit square, so the edge test in the shader is meaningful. Draped
        // content is drawn in its own frames and must keep the plain lattice (it follows the
        // surface everywhere except within the outermost cell, where the stitch bends it).
        // Cross-LOD edge stitching: the shared edge with a COARSER neighbour is bent onto that
        // neighbour's chords so the two grounds meet. Draped CONTENT takes it too - a road or a
        // contour crossing the seam has to land on the same stitched edge as the ground it lies
        // on, or its two halves meet at different heights (a step that only shows once the camera
        // tilts). Only the outermost cell is affected; everything else keeps the plain lattice.
        cglib::vec4<float> edgeCoarsening(1, 1, 1, 1);
        if (!_terrainEdgeCoarseningMap.empty()) {
            auto edgeIt = _terrainEdgeCoarseningMap.find(tileId);
            if (edgeIt != _terrainEdgeCoarseningMap.end()) {
                edgeCoarsening = edgeIt->second;
            }
        }
        glUniform4f(shaderProgram.uniforms[U_TERRAINEDGECOARSENING], edgeCoarsening(0), edgeCoarsening(1), edgeCoarsening(2), edgeCoarsening(3));
        // Vertex frame units -> TARGET tile units, for the edge test above and for the tile clip
        // in the fragment shaders: 1 for the surface (its vertices are the unit square), the
        // frame's scale over the tile's world size otherwise.
        // The OFFSET is what makes this hold for a STAND-IN, where the vertex frame is the source
        // (an ancestor) tile and the target is one of its descendants: the scale alone puts the
        // source's unit square in [0, 2^dz] and the clip then discards every fragment of it except
        // the one quadrant that happens to land in [0, 1] - which blanks all content served by a
        // stand-in, i.e. every layer for a second or two after each integer zoom step. Measuring
        // the position from the TARGET tile's own origin selects the right part of the ancestor
        // for each target, so the four targets together still paint the whole ancestor, each with
        // the elevation mapping of the surface it stands on.
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
        // Lattice clamp (regular-grid surface mode): the reference surface is a regular
        // grid of _terrainRegularGridResolution cells over the tile, so draped geometry
        // snaps its height to the same grid. The cell size in elevation-uv units is the
        // tile's full uv extent (world tile size / texture internal size) divided by the
        // grid resolution - a property of the tile+texture, so it is identical for the
        // surface and every draped layer regardless of their coordinate frame. Off (0) in
        // adaptive mode: geometry then samples the full DEM detail with the calibrated slack.
        // THE SURFACE ITSELF DOES NOT NEED IT: its vertices ARE the lattice nodes, so the clamp
        // returns the node's own bilinear height and costs four of them (16 texture fetches a
        // vertex) to do it. The surface carries the bulk of the vertex work in a terrain frame -
        // tangram's terrain vertex takes ONE fetch - so it takes the plain sample, which is the
        // identical value. The exception is a tile whose edge is stitched to a coarser
        // neighbour: there the clamp is what bends the outermost cell onto the neighbour's
        // chords, so those tiles keep it.
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
        glUniform2f(shaderProgram.uniforms[U_LIGHTPARAMS], _terrainLighting.sunIntensity, _terrainLighting.ambientIntensity);
    }

    void GLTileRenderer::renderTileMask(const TileId& tileId) {
#if CARTO_VT_RENDER_STATS
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
            unsigned int lightFlags = (litSurface ? TERRAIN_LIGHT_FLAG : 0) | (shadowedSurface ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0);
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
                std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
                for (int i = 0; i < MAX_SHADOW_CASCADES; i++) {
                    shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * surfaceFrame);
                }
                glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], MAX_SHADOW_CASCADES, GL_FALSE, shadowMatrices[0].data());
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
                glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
                glActiveTexture(GL_TEXTURE0);
                // A tile drawn flat because its elevation has not arrived has no relief to shadow,
                // and the terrain around it would shadow every texel of it (see renderTileSurfaceDrape).
                glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), hasElevation ? _terrainShadowStrength : 0.0f, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
                glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
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
        if (!_visibleRenderTiles) {
            return 0;
        }
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

        // Accept render tiles that COVER the terrain tile, not just exact matches: the terrain
        // tile set is one normalized cover shared by every layer, so a layer whose own tiles are
        // coarser (a hillshade limited by its DEM max zoom) contributes through its ancestor tile.
        //
        // Several of this renderer's tiles can cover the same terrain tile at once - during a zoom
        // it holds a proxy parent that is blending out AND the live children. They must be baked
        // COARSEST FIRST, with retained (proxy) tiles before active ones at the same zoom, or a
        // parent's full-tile background paints over a child's content and the tile reverts to bare
        // background colour. _visibleRenderTiles is in no such order.
        // COVERS, strictly: only content whose own tile contains this terrain tile is baked.
        // Baking a FINER render tile into its sub-rect looked like free extra detail, but the
        // content is drawn at its own zoom's scale into a fraction of the texture - hairline
        // roads and fills, minified with no mipmap - and a zoom out, which holds a whole
        // generation of finer tiles while they blend away, turned the drape into white aliasing
        // noise. Finer tiles are the generation being replaced; they stay in the 3D pass and
        // fade out there.
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
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                    renderTileBackground(renderLayer.targetTileId, 1.0f, 1.0f, renderLayer.tileSize, background);
                    bakedPrimitives++;
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                    renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, 1.0f, bitmap);
                    bakedPrimitives++;
                }
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (isDrapeableGeometry(geometry->getType()) && isLayerDraped(renderLayer.layer)) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, 1.0f, renderLayer.tileSize, geometry);
                        bakedPrimitives++;
                    }
                }
            }
        }

        _drapeMVPOverride = nullptr;
        checkGLError();
        return bakedPrimitives;
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
        unsigned int lightFlags = (litSurface ? TERRAIN_LIGHT_FLAG : 0) | (shadowedSurface ? TERRAIN_SHADOW_FLAG : 0) | DERIVATIVES_FLAG | (asGround ? GROUND_BASE_FLAG : 0);

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
                    std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
                    for (int i = 0; i < MAX_SHADOW_CASCADES; i++) {
                        shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * surfaceFrame);
                    }
                    glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], MAX_SHADOW_CASCADES, GL_FALSE, shadowMatrices[0].data());
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
                    glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
                    glActiveTexture(GL_TEXTURE0);
                    glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), hasElevation ? _terrainShadowStrength : 0.0f, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
                    glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
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
        // Verbatim from the normal-map path (HillshadeRasterTileLayer::createVectorTile), so a
        // layer switched to paint mode keeps its relief: MapLibre's low-zoom boost by default,
        // or the legacy formula, which damps the relief by the ABSOLUTE zoom instead.
        //
        // The zoom that goes in is the one the SAMPLING density corresponds to, not the tile id
        // of the elevation grid: the normal-map path multiplied the tile zoom by its bitmap
        // resolution, so a 512-texel grid at z11 was worth a z12 tile of 256 texels - which is
        // exactly what the terrain's elevation grids are. Keyed off the grid's own zoom instead,
        // the paint reads the same data as one level coarser and boosts the relief ~1.5x too far.
        // 156543.03 m/texel is the web-mercator resolution at zoom 0 for 256-texel tiles.
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
        // Under a cross-layer drape the baked tiles are the OWNER's terrain tiles, and
        // bakeDrapeTile takes exactly the render tiles that COVER one. So "draped" means: some
        // drawn terrain tile lies within this tile. A FINER tile is not draped - it is not baked
        // either, so it keeps drawing itself in the 3D pass while it blends away. Claiming it
        // was draped suppressed the only content on that ground during a zoom out, which is the
        // ground going white until the coarse tiles finish loading. (Re-tested: the drape cover
        // is routinely COARSER than the render tiles - the leaves are capped at the camera zoom
        // and a hillshade contributes its DEM-limited zoom - so suppressing finer tiles replaces
        // the map with a stretched coarse drape. This is also why fills can not be decoded at
        // source density under draping: those fall-through tiles must carry real terrain-following
        // geometry. See TileLayer::calculateDrawData.)
        for (const TileId& drapeTileId : _externalDrapeTiles) {
            if (tileCovers(targetTileId, drapeTileId)) {
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
        // Orthographic bake frame: map the part of the SOURCE tile covering the target tile onto
        // the full [-1,1] clip square of the target's drape texture.
        //
        // Handling source != target is the whole point: overzoomed/proxy content (a parent tile
        // standing in while the native tile loads) is exactly what is on screen during a pan or
        // zoom. Left undraped it falls through to the displaced-geometry path, where it samples a
        // coarser lattice than the surface it sits on and sinks into it.
        //
        // Tile-local vertex y runs NORTHWARD (the vertex transformer maps (u,v) -> (u, 1-v) and v
        // grows southward with the XYZ tile y), so the y sub-rect index is mirrored.
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
        for (auto it = renderTile.renderLayers.begin(); it != renderTile.renderLayers.end(); it++) {
            const RenderTileLayer& renderLayer = it->second;
            if (!hasDrapeableContent(renderLayer)) {
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
        // Maplibre-style drape: bake each tile's fills/backgrounds FLAT (no terrain
        // displacement) into a per-tile offscreen texture. The terrain surface then samples
        // it as its color, so fills follow the terrain exactly (no holes/see-through).
        //
        // The bake is CACHED per target tile: a tile's texture is baked ONCE (when its content
        // first appears) at full opacity and reused every frame after. Re-baking every visible
        // tile every frame stalls the render thread during fast zooms (a burst of tiles), which
        // is not present in flat rendering. A cached tile is dropped (texture freed) when it
        // leaves the view; it re-bakes if it returns. Only native (non-overzoomed) content is
        // draped; overzoomed content falls through to the normal geometry pass.
        // A tile is "draped" this frame only once its texture is actually baked; until then
        // its content renders as normal geometry (no gap). New-tile bakes are capped per frame
        // so a fast zoom's burst of tiles is spread over a few frames instead of stalling one.
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
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.targetTileId, targetTileId);
                for (const std::shared_ptr<TileBackground>& background : renderLayer.layer->getBackgrounds()) {
                    renderTileBackground(renderLayer.targetTileId, 1.0f, 1.0f, renderLayer.tileSize, background);
                }
                for (const std::shared_ptr<TileBitmap>& bitmap : renderLayer.layer->getBitmaps()) {
                    renderTileBitmap(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, 1.0f, bitmap);
                }
                // Geometry vertices are in SOURCE tile-local coordinates, so an overzoomed layer
                // needs the sub-rect transform to land on the target tile's texture.
                drapeOrtho = calculateDrapeMVPMatrix(renderLayer.sourceTileId, targetTileId);
                for (const std::shared_ptr<TileGeometry>& geometry : renderLayer.layer->getGeometries()) {
                    if (isDrapeableGeometry(geometry->getType())) {
                        renderTileGeometry(renderLayer.sourceTileId, renderLayer.targetTileId, 1.0f, 1.0f, renderLayer.tileSize, geometry);
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
#if CARTO_VT_RENDER_STATS
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
            unsigned int flags = (_terrainMode && _terrainTextureProvider ? TERRAIN_FLAG | TERRAIN_VTF_FLAG : 0) | DRAPE_FLAG | (lit ? TERRAIN_LIGHT_FLAG : 0) | (shadowed ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0);
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
                std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
                for (int i = 0; i < MAX_SHADOW_CASCADES; i++) {
                    shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * surfaceFrame);
                }
                glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], MAX_SHADOW_CASCADES, GL_FALSE, shadowMatrices[0].data());
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
                glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
                glActiveTexture(GL_TEXTURE0);
                // A tile whose elevation has not arrived is drawn FLAT, at zero. In the mountains
                // that is a kilometre below everything around it, so the surrounding terrain
                // shadows every texel of it and it reads as a solid dark block the exact shape of
                // the tile - most visible far away, where elevation arrives last. It has no relief
                // to shadow anyway, so it takes no shadow until its heights are there.
                glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), hasElevation ? _terrainShadowStrength : 0.0f, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
                glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
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
            const ShaderProgram& shaderProgram = buildShaderProgram("tilebackground", backgroundVsh, backgroundFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (background->getPattern() ? PATTERN_FLAG : 0) | terrainFlag | fogFlag());
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
            const ShaderProgram* shaderProgramPtr = nullptr;
            switch (bitmap->getType()) {
            case TileBitmap::Type::COLORMAP:
                shaderProgramPtr = &buildShaderProgram("tilecolormap", colormapVsh, colormapFsh, LightingMode::GEOMETRY2D, _rasterFilterMode, PATTERN_FLAG | terrainFlag | fogFlag());
                break;
            case TileBitmap::Type::NORMALMAP:
                shaderProgramPtr = &buildShaderProgram("tilenormalmap", normalmapVsh, normalmapFsh, LightingMode::NORMALMAP, _rasterFilterMode, PATTERN_FLAG | terrainFlag | fogFlag());
                break;
            default:
                return;
            }
            const ShaderProgram& shaderProgram = *shaderProgramPtr;
            useProgram(shaderProgram);
            if ((terrainFlag & TERRAIN_FLAG) != 0) {
                glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
            }
            if (terrainVTF && !flatDrape) {
                setupTerrainUniforms(shaderProgram, targetTileId, surfaceFrame, gridMode && !flatDrape);
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

        // Flat drape pass: draw the fill into the per-tile drape texture with NO terrain
        // displacement, NO depth bias, and a tile-local orthographic MVP (set by the caller).
        bool flatDrape = (_drapeMVPOverride != nullptr);
        bool terrainVTF = _terrainMode && (bool) _terrainTextureProvider && !flatDrape;
        // Every piece of tile content drawn in the 3D scene receives shadows. That used to mean
        // the 3D extrusions alone, because everything 2D was baked into the drape texture and was
        // shadowed by the surface it was painted on; with only the FILLS draped, the lines and
        // points are in the scene and stayed lit while the slope under them went dark.
        bool shadowReceiver = terrainVTF && !_shadowCasterViewProj && _terrainShadowTexture != 0 && _terrainShadowStrength > 0.0f;
        unsigned int terrainFlag = flatDrape ? 0 : ((_terrainMode ? TERRAIN_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG : 0));
        const ShaderProgram* shaderProgramPtr = nullptr;
        switch (geometry->getType()) {
        case TileGeometry::Type::POINT:
            shaderProgramPtr = &buildShaderProgram("point", pointVsh, pointFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | terrainFlag | (shadowReceiver ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0) | fogFlag());
            break;
        case TileGeometry::Type::LINE:
            shaderProgramPtr = &buildShaderProgram("line", lineVsh, lineFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (styleOffsetting ? OFFSET_FLAG : 0) | terrainFlag | (shadowReceiver ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0) | fogFlag());
            break;
        case TileGeometry::Type::POLYGON:
            shaderProgramPtr = &buildShaderProgram("polygon", polygonVsh, polygonFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | terrainFlag | (shadowReceiver ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0) | fogFlag());
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
            shaderProgramPtr = &buildShaderProgram("polygon3d", polygon3DVsh, polygon3DFsh, LightingMode::GEOMETRY3D, RasterFilterMode::NONE, (styleParams.pattern ? PATTERN_FLAG : 0) | (styleParams.translate ? TRANSFORM_FLAG : 0) | (terrainVTF ? TERRAIN_VTF_FLAG | TERRAIN_FLAG : 0) | (shadowReceiver ? TERRAIN_SHADOW_FLAG | DERIVATIVES_FLAG : 0) | fogFlag());
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

        cglib::mat4x4<float> mvpMatrix;
        if (_shadowCasterViewProj) {
            mvpMatrix = cglib::mat4x4<float>::convert((*_shadowCasterViewProj) * calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        } else if (flatDrape) {
            // fill coords * (1/coordScale) = tile-local [0,1]; the override maps [0,1] -> clip.
            cglib::mat4x4<float> local = cglib::scale4_matrix(cglib::vec3<float>(1.0f / vertexGeomLayoutParams.coordScale, 1.0f / vertexGeomLayoutParams.coordScale, 1.0f));
            mvpMatrix = (*_drapeMVPOverride) * local;
        } else {
            mvpMatrix = calculateTileMVPMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
        }
        glUniformMatrix4fv(shaderProgram.uniforms[U_MVPMATRIX], 1, GL_FALSE, mvpMatrix.data());
        if (terrainFlag != 0 && geometry->getType() != TileGeometry::Type::POLYGON3D) {
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], terrainVTF ? _terrainDrawDepthBias : _terrainDepthBias);
        } else if (terrainVTF && !_shadowCasterViewProj && geometry->getType() == TileGeometry::Type::POLYGON3D) {
            // Extrusions: only the VTF path has a terrain surface to clear (the caster pass
            // renders depth from the light and must not be biased towards the camera).
            glUniform1f(shaderProgram.uniforms[U_DEPTHBIAS], _terrainDrawDepthBias);
        }
        if (terrainVTF) {
            // Two separate things here, and they had swapped roles.
            // The elevation TEXTURE must be the TARGET tile's: that is the tile whose surface this
            // content stands on, and the terrain surface is drawn per target tile. Sampling the
            // source tile's texture picks a different DEM level, so content sat at a different
            // height than the ground beneath it and slid during a pan while tiles streamed in,
            // settling only once source and target became the same tile again.
            // The vertex FRAME must be the SOURCE tile's: the vertices are source-tile-local, and
            // this matrix is what maps them to world for the uv. (An earlier attempt to change
            // only the frame looked like it broke the raster stack; that comparison was against a
            // demo that had silently switched to a different style, so it proved nothing.)
            setupTerrainUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        }
        if (shadowReceiver) {
            cglib::mat4x4<double> shadowFrame = calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale);
            std::array<cglib::mat4x4<float>, MAX_SHADOW_CASCADES> shadowMatrices;
            for (int i = 0; i < MAX_SHADOW_CASCADES; i++) {
                shadowMatrices[i] = cglib::mat4x4<float>::convert(_terrainShadowViewProjs[i] * shadowFrame);
            }
            glUniformMatrix4fv(shaderProgram.uniforms[U_SHADOWMATRIX], MAX_SHADOW_CASCADES, GL_FALSE, shadowMatrices[0].data());
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, _terrainShadowTexture);
            glUniform1i(shaderProgram.uniforms[U_SHADOWTEXTURE], 2);
            glActiveTexture(GL_TEXTURE0);
            glUniform4f(shaderProgram.uniforms[U_SHADOWPARAMS], 1.0f / std::max(1, _terrainShadowMapSize), _terrainShadowStrength, _terrainShadowSoftness, 1.0f / _terrainShadowCascades);
            glUniform4f(shaderProgram.uniforms[U_SHADOWBIAS], _terrainShadowBiases[0], _terrainShadowBiases[1], _terrainShadowBiases[2], _terrainShadowBiases[3]);
            // Draped 2D content takes its N.L from the TERRAIN, not from its own (meaningless)
            // normal - see terrainNdl in commonFsh - so it needs the slope scale and the sun
            // direction even though it carries no lighting of its own. The uniforms this also
            // sets that such a program does not declare resolve to -1, where glUniform is a no-op.
            setupTerrainLightingUniforms(shaderProgram, targetTileId, calculateTileMatrix(sourceTileId, 1.0f / vertexGeomLayoutParams.coordScale));
        }
        
        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, sourceTileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            cglib::mat4x4<float> transformMatrix = _transformer->calculateTileTransform(sourceTileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
            glUniformMatrix4fv(shaderProgram.uniforms[U_TRANSFORMMATRIX], 1, GL_FALSE, transformMatrix.data());
        }
        VT_STAT_SPLIT(geomTerrainNs, statClock);

        std::array<cglib::vec4<float>, TileGeometry::StyleParameters::MAX_PARAMETERS> colors;
        for (int i = 0; i < styleParams.parameterCount; i++) {
            Color color = Color::fromColorOpacity(evaluateColorFunc(styleParams.colorFuncs[i]) * blend, opacity);
            colors[i] = cglib::vec4<float>(color.rgba());
        }
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

                float strokeWidth = evaluateFloatFunc(styleParams.offsetFuncs[i]) * HALO_RADIUS_SCALE;
                strokeWidths[i] = strokeWidth;
            }
            VT_STAT_SPLIT(geomStyleEvalNs, statClock);

            if (std::all_of(widths.begin(), widths.begin() + styleParams.parameterCount, [](float width) { return width == 0; })) {
                if (std::all_of(strokeWidths.begin(), strokeWidths.begin() + styleParams.parameterCount, [](float strokeWidth) { return strokeWidth == 0; })) {
                    VT_STAT_INC(geometrySkips);
                    return;
                }
            }

            glUniform1f(shaderProgram.uniforms[U_BINORMALSCALE], vertexGeomLayoutParams.coordScale / vertexGeomLayoutParams.binormalScale / std::pow(2.0f, _viewState.zoom - sourceTileId.zoom));
            glUniform1f(shaderProgram.uniforms[U_SDFSCALE], styleParams.glyphRenderSize / _fullResolution / BITMAP_SDF_SCALE);
            glUniform1fv(shaderProgram.uniforms[U_WIDTHTABLE], styleParams.parameterCount, widths.data());
            if (styleOffsetting) {
                glUniform1fv(shaderProgram.uniforms[U_STROKEWIDTHTABLE], styleParams.parameterCount, strokeWidths.data());
            }
        } else if (geometry->getType() == TileGeometry::Type::LINE) {
            std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> widths, offsets;
            for (int i = 0; i < styleParams.parameterCount; i++) {
                float offset = 0.5f * _fullResolution * evaluateFloatFunc(styleParams.offsetFuncs[i]) * geometry->getGeometryScale() / tileSize;
                offsets[i] = offset;

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
                    widths[i] = width * 0.5f;
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

            if (styleParams.pattern) {
                std::array<float, TileGeometry::StyleParameters::MAX_PARAMETERS> strokeScales;
                for (int i = 0; i < styleParams.parameterCount; i++) {
                    float strokeScale = (styleParams.strokeScales[i] > 0.0f ? STROKE_UV_SCALE / styleParams.pattern->bitmap->width / styleParams.strokeScales[i] / 127.0f / (_fullResolution / tileSize) : 0.0f);
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

            const CompiledBitmap& compiledBitmap = buildCompiledBitmap(styleParams.pattern->bitmap, geometry->getType() != TileGeometry::Type::LINE);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, compiledBitmap.texture);
            glUniform1i(shaderProgram.uniforms[U_PATTERN], 0);
        }
        VT_STAT_SPLIT(geomStyleNs, statClock);

        const CompiledGeometry& compiledGeometry = buildCompiledTileGeometry(geometry);
        VT_STAT_SPLIT(geomCompileNs, statClock);
        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(compiledGeometry.geometryVAO);
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
            
            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    enableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_TRUE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.normalOffset));
                }
            }

            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL], vertexGeomLayoutParams.dimensions, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.binormalOffset));
            }
            
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                enableVertexAttrib(shaderProgram.attribs[A_VERTEXHEIGHT], 1, GL_SHORT, GL_FALSE, vertexGeomLayoutParams.vertexSize, bufferGLOffset(vertexGeomLayoutParams.heightOffset));
            }
        }

        if (!(vertexGeomLayoutParams.attribsOffset >= 0)) {
            setConstVertexAttrib(shaderProgram.attribs[A_VERTEXATTRIBS], 0, 0, 0, 0);
        }

        if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
            if (!(vertexGeomLayoutParams.normalOffset >= 0)) {
                setConstVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL], 0, 0, 1);
            }
        }

        if (geometry->getType() != TileGeometry::Type::POLYGON3D && _lightingShader2D) {
            _lightingShader2D->setupFunc(shaderProgram.program, _viewState);
        } else if (geometry->getType() == TileGeometry::Type::POLYGON3D && _lightingShader3D) {
            _lightingShader3D->setupFunc(shaderProgram.program, _viewState);
        }
        VT_STAT_SPLIT(geomBindNs, statClock);

        glDrawElements(GL_TRIANGLES, geometry->getIndicesCount(), GL_UNSIGNED_SHORT, 0);
        VT_STAT_SPLIT(geomDrawNs, statClock);
        VT_STAT_INC(geometryDraws);
        VT_STAT_ADD(geometryIndices, geometry->getIndicesCount());

        if (compiledGeometry.geometryVAO != 0) {
            _glExtensions->glBindVertexArrayOES(0);
        } else {
            if (vertexGeomLayoutParams.heightOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXHEIGHT]);
            }
            
            if (vertexGeomLayoutParams.binormalOffset >= 0) {
                disableVertexAttrib(shaderProgram.attribs[A_VERTEXBINORMAL]);
            }

            if (_lightingShader2D || geometry->getType() == TileGeometry::Type::POLYGON3D) {
                if (vertexGeomLayoutParams.normalOffset >= 0) {
                    disableVertexAttrib(shaderProgram.attribs[A_VERTEXNORMAL]);
                }
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
        const ShaderProgram& shaderProgram = buildShaderProgram("labels", labelVsh, labelFsh, LightingMode::GEOMETRY2D, RasterFilterMode::NONE, useDerivatives ? DERIVATIVES_FLAG : 0);
        useProgram(shaderProgram);

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

    const GLTileRenderer::CompiledGeometry& GLTileRenderer::buildCompiledTileGeometry(const std::shared_ptr<TileGeometry>& tileGeometry) {
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
        return it->second.geometry;
    }

    const GLTileRenderer::ShaderProgram& GLTileRenderer::buildShaderProgram(const char* id, const std::string& vsh, const std::string& fsh, LightingMode lightingMode, RasterFilterMode filterMode, unsigned int flags) {
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

            ShaderProgram shaderProgram;
            createShaderProgram(shaderProgram, commonVsh + lightingVsh + vsh, commonFsh + lightingFsh + filterFsh + fsh, defs, uniformMap, attribMap);
            
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
        if (compiledSurface.wireframeIndicesVBO != 0) {
            glDeleteBuffers(1, &compiledSurface.wireframeIndicesVBO);
            compiledSurface.wireframeIndicesVBO = 0;
            compiledSurface.wireframeIndicesCount = 0;
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
