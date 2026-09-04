#include "TerrainTileTransformer.h"
#include "core/MapTile.h"
#include "terrain/ElevationManager.h"
#include "terrain/ElevationTileGrid.h"
#include "terrain/TesselationBounds.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace massif {

    // Surface cells a fill subdivides to: indices fall as 1/N^2, chord error grows as N^2, and the
    // usable value is whatever the depth budget still clears - a measurement, not a derivation
    // (the ladder is in docs/internals/rendering/02-tiles.md).
    //   adb shell setprop debug.massif.areathreshold 4
    static constexpr float AREA_THRESHOLD_CELLS = 2.0f;

    // How far a draped line may chord away from the terrain, in METRES. Chosen for margin, not for
    // speed: 0.5-4 m all measure the same, and a draped line is lifted 25 m off the surface anyway
    // (DEFAULT_LINE_CLEARANCE_METERS). Numbers in docs/internals/rendering/04-terrain.md.
    static constexpr float DEFAULT_LINE_SAG_METERS = 2.0f;
#ifdef __ANDROID__
    // The same for LINES - the expensive half over a city, since they are drawn as terrain geometry
    // every frame while the fills are baked once.
    //   adb shell setprop debug.massif.linethreshold 4
    // Relief (metres in the tile) under which the LATTICE split is skipped: the cell fold it guards
    // against is a fraction of the relief, so on a valley floor it protects against nothing.
    //   adb shell setprop debug.massif.latticerelief 50
    static float latticeReliefThreshold() {
        static const float relief = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.latticerelief", property) > 0) {
                float value = static_cast<float>(std::atof(property));
                if (value >= 0.0f) {
                    return value;
                }
            }
            return 0.0f;
        }();
        return relief;
    }

    // Maximum chord sag a draped line may keep, in METRES - the same currency as the depth
    // clearance that lifts these lines (uDepthClearance, see 04-terrain.md), so the two agree on
    // what "close enough to the ground" means. 0 goes back to the old lattice / threshold split,
    // which is how the two are A/B'd:
    //   adb shell setprop debug.massif.linesag 0
    static float lineSagToleranceMeters() {
        static const float tolerance = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.linesag", property) > 0) {
                float value = static_cast<float>(std::atof(property));
                if (value >= 0.0f) {
                    return value;
                }
            }
            return DEFAULT_LINE_SAG_METERS;
        }();
        return tolerance;
    }

    static float lineThresholdScale() {
        static const float scale = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.linethreshold", property) > 0) {
                float value = static_cast<float>(std::atof(property));
                if (value > 0.0f) {
                    return value;
                }
            }
            return 1.0f;
        }();
        return scale;
    }

    static float areaThresholdScale() {
        static const float scale = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.areathreshold", property) > 0) {
                float value = static_cast<float>(std::atof(property));
                if (value > 0.0f) {
                    return value;
                }
            }
            return AREA_THRESHOLD_CELLS;
        }();
        return scale;
    }
#else
    static float lineSagToleranceMeters() {
        return DEFAULT_LINE_SAG_METERS;
    }

    static float latticeReliefThreshold() {
        return 0.0f;
    }

    static float lineThresholdScale() {
        return 1.0f;
    }

    static float areaThresholdScale() {
        return AREA_THRESHOLD_CELLS;
    }
#endif

    TerrainTileTransformer::TerrainVertexTransformer::TerrainVertexTransformer(const vt::TileId& tileId, double scale, std::shared_ptr<ElevationTileGrid> grid, float exaggeration, float divideThreshold, float lineDivideThreshold, float latticeCell, float sagToleranceMeters) :
        _tileId(tileId),
        _scale(scale),
        _grid(std::move(grid)),
        _exaggeration(exaggeration),
        _divideThreshold(divideThreshold),
        _lineDivideThreshold(lineDivideThreshold),
        _latticeCell(latticeCell)
    {
        int tileMask = (1 << tileId.zoom) - 1;
        double zoomScale = 1.0 / (1 << tileId.zoom);
        _tileOffsetInternal = cglib::vec2<double>((tileId.x * zoomScale - 0.5) * _scale, ((tileMask - tileId.y) * zoomScale - 0.5) * _scale);
        _tileScaleInternal = zoomScale * _scale;
        _tileScaleMeters = EARTH_CIRCUMFERENCE * zoomScale;
        _localFromInternal = (1 << tileId.zoom) / _scale;

        if (sagToleranceMeters > 0.0f) {
            // The tolerance is given in METRES because that is what the depth clearance lifting
            // these lines is worth (see 04-terrain.md); heights here are tile-local, so convert
            // once at the tile centre - the latitude factor varies by a fraction of a percent
            // across one tile.
            _sagToleranceLocal = calculateHeight(cglib::vec2<float>(0.5f, 0.5f), sagToleranceMeters);
            // The DEM cannot describe relief finer than its own texel, so cutting below it only
            // resamples the same interpolated slope.
            if (_grid && _grid->getWidth() > 1) {
                _sagMinSegmentMeters = static_cast<float>(_tileScaleMeters / _grid->getWidth());
            }
        }
    }

    cglib::vec3<float> TerrainTileTransformer::TerrainVertexTransformer::calculatePoint(const cglib::vec2<float>& pos) const {
        return cglib::vec3<float>(pos(0), 1 - pos(1), static_cast<float>(calculateLocalHeight(pos)));
    }

    cglib::vec3<float> TerrainTileTransformer::TerrainVertexTransformer::calculateNormal(const cglib::vec2<float>& pos) const {
        // Keep 'up' as the normal: it is the extrusion direction for 3D geometry (buildings must
        // stay vertical) and keeps hillshade/lighting behavior identical to the flat planar case.
        return cglib::vec3<float>(0, 0, 1);
    }

    cglib::vec3<float> TerrainTileTransformer::TerrainVertexTransformer::calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const {
        return cglib::vec3<float>(vec(0), -vec(1), 0);
    }

    cglib::vec2<float> TerrainTileTransformer::TerrainVertexTransformer::calculateTilePosition(const cglib::vec3<float>& pos) const {
        return cglib::vec2<float>(pos(0), 1 - pos(1));
    }

    float TerrainTileTransformer::TerrainVertexTransformer::calculateHeight(const cglib::vec2<float>& pos, float height) const {
        double internalY = _tileOffsetInternal(1) + (1 - pos(1)) * _tileScaleInternal;
        double cosLatitude = calculateMercatorCosine(internalY);
        return static_cast<float>(height / cosLatitude * (1 << _tileId.zoom) / EARTH_CIRCUMFERENCE);
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateLineString(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const {
        if (count > 0) {
            tesselatedPoints.append(points[0]);
            for (std::size_t i = 0; i + 1 < count; i++) {
                const cglib::vec2<float>& pos0 = points[i + 0];
                const cglib::vec2<float>& pos1 = points[i + 1];
                // Regular-grid mode: cut the segment exactly where it leaves a surface triangle
                // instead of halving it until it is small enough to hide the error. Every
                // sub-segment then lies IN a triangle of the surface, so it follows the surface
                // exactly rather than approximately - with fewer vertices than the fraction-of-a-cell
                // halving needed to keep the chord sag under the (zero) painter-order depth slack.
                float dist = cglib::length(pos1 - pos0) * static_cast<float>(_tileScaleMeters);
                if (_sagToleranceLocal > 0.0f) {
                    // Cut by the sag the terrain actually has, not by the tile's cell count.
                    tesselateSegmentBySag(pos0, pos1, calculateLocalHeight(pos0), calculateLocalHeight(pos1), dist, 0, tesselatedPoints);
                    continue;
                }
                if (_latticeCell > 0 && tesselateSegmentOnLattice(pos0, pos1, tesselatedPoints)) {
                    continue;
                }
                tesselateSegment(pos0, pos1, dist, _lineDivideThreshold, tesselatedPoints);
            }
        }
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateLabelLineString(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const {
        // A label line is READ, never drawn: the lattice split keeps a DRAWN segment inside one
        // surface triangle, which buys a glyph run nothing, and neither does the finer line
        // threshold - the profile a run follows cannot carry more detail than the surface it is
        // laid on. Halve to the SURFACE cell instead. Every vertex dropped here is an elevation
        // sample dropped from every terrain re-anchor, which is the most expensive thing on the
        // render thread over 3D terrain (docs/internals/rendering/06-labels.mdx). Measured: with no line
        // subdivision at all, 'prepare' goes 154 -> 68 ms on the north pan.
        if (count > 0) {
            tesselatedPoints.append(points[0]);
            for (std::size_t i = 0; i + 1 < count; i++) {
                const cglib::vec2<float>& pos0 = points[i + 0];
                const cglib::vec2<float>& pos1 = points[i + 1];
                float dist = cglib::length(pos1 - pos0) * static_cast<float>(_tileScaleMeters);
                tesselateSegment(pos0, pos1, dist, _divideThreshold, tesselatedPoints);
            }
        }
    }

    bool TerrainTileTransformer::TerrainVertexTransformer::tesselateSegmentOnLattice(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, vt::VertexArray<cglib::vec2<float>>& points) const {
        // The surface is a regular grid of _latticeCell cells, each split into two triangles.
        // The shader folds a cell along fg.x + fg.y = 1 in ELEVATION-UV space; these points are
        // in tile (u, v) space, and the surface builder emits its vertices at y = 1 - v, so the
        // same fold reads as u + v = const here. A segment therefore stays inside one triangle
        // as long as it crosses none of x = k*cell, y = k*cell, x + y = k*cell.
        const cglib::vec2<float> delta = pos1 - pos0;
        const float cell = _latticeCell;
        const float f0[3] = { pos0(0), pos0(1), pos0(0) + pos0(1) };
        const float f1[3] = { pos1(0), pos1(1), pos1(0) + pos1(1) };

        float ts[3 * MAX_LATTICE_SPLITS_PER_SEGMENT];
        std::size_t tCount = 0;
        for (int axis = 0; axis < 3; axis++) {
            float d = f1[axis] - f0[axis];
            if (std::abs(d) < 1.0e-9f) {
                continue;
            }
            float from = std::min(f0[axis], f1[axis]);
            float to = std::max(f0[axis], f1[axis]);
            double firstK = std::floor(from / cell) + 1;
            double lastK = std::ceil(to / cell) - 1;
            if (lastK - firstK + 1 > MAX_LATTICE_SPLITS_PER_SEGMENT) {
                return false; // spans too many cells: not worth enumerating
            }
            for (double k = firstK; k <= lastK; k += 1) {
                float t = (static_cast<float>(k * cell) - f0[axis]) / d;
                if (t > 1.0e-5f && t < 1.0f - 1.0e-5f) {
                    if (tCount >= sizeof(ts) / sizeof(ts[0])) {
                        return false;
                    }
                    ts[tCount++] = t;
                }
            }
        }

        std::sort(ts, ts + tCount);
        float prevT = 0.0f;
        for (std::size_t i = 0; i < tCount; i++) {
            if (ts[i] - prevT < 1.0e-5f) {
                continue; // the segment passes through a lattice node: one point, not three
            }
            points.append(pos0 + delta * ts[i]);
            prevT = ts[i];
        }
        points.append(pos1);
        return true;
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateTriangles(const std::size_t* indices, std::size_t count, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& tesselatedIndices) const {
        for (std::size_t i = 0; i + 2 < count; i += 3) {
            std::size_t i0 = indices[i + 0];
            std::size_t i1 = indices[i + 1];
            std::size_t i2 = indices[i + 2];
            float dist01 = cglib::length(coords[i1] - coords[i0]) * static_cast<float>(_tileScaleMeters);
            float dist02 = cglib::length(coords[i2] - coords[i0]) * static_cast<float>(_tileScaleMeters);
            float dist12 = cglib::length(coords[i2] - coords[i1]) * static_cast<float>(_tileScaleMeters);
            tesselateTriangle(i0, i1, i2, dist01, dist02, dist12, coords, texCoords, tesselatedIndices);
        }
    }

    double TerrainTileTransformer::TerrainVertexTransformer::calculateLocalHeight(const cglib::vec2<float>& pos) const {
        // Tile geometry is built FLAT: the GPU draping shader replaces the z of every
        // draped vertex with the shared elevation texture sample, so sampling heights at
        // build time would be wasted work (this was by far the most expensive part of
        // terrain tile decodes and surface builds). Label anchors get their heights
        // dynamically (GLTileRenderer label elevation provider), and hit test rays are
        // pre-intersected with the terrain by the host renderer.
        return 0.0;
    }

    double TerrainTileTransformer::TerrainVertexTransformer::calculateMercatorCosine(double internalY) const {
        double sin = std::tanh(internalY * 2 * PI / _scale);
        return std::sqrt(std::max(1.0e-6, 1.0 - sin * sin));
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateSegment(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, float dist, float threshold, vt::VertexArray<cglib::vec2<float>>& points) const {
        if (dist > threshold) {
            cglib::vec2<float> posM = (pos0 + pos1) * 0.5f;
            tesselateSegment(pos0, posM, dist * 0.5f, threshold, points);
            tesselateSegment(posM, pos1, dist * 0.5f, threshold, points);
        }
        else {
            points.append(pos1);
        }
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateSegmentBySag(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, double h0, double h1, float dist, int depth, vt::VertexArray<cglib::vec2<float>>& points) const {
        if (depth < MAX_SAG_SPLIT_DEPTH && dist > _sagMinSegmentMeters) {
            cglib::vec2<float> posM = (pos0 + pos1) * 0.5f;
            double hM = calculateLocalHeight(posM);
            // How far the terrain leaves the straight chord at its midpoint. Recursing on both
            // halves keeps this a bound on the WHOLE sub-segment, not only on its centre.
            if (std::abs(hM - (h0 + h1) * 0.5) > _sagToleranceLocal) {
                tesselateSegmentBySag(pos0, posM, h0, hM, dist * 0.5f, depth + 1, points);
                tesselateSegmentBySag(posM, pos1, hM, h1, dist * 0.5f, depth + 1, points);
                return;
            }
        }
        points.append(pos1);
    }

    void TerrainTileTransformer::TerrainVertexTransformer::tesselateTriangle(std::size_t i0, std::size_t i1, std::size_t i2, float dist01, float dist02, float dist12, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& indices) const {
        // Red-green refinement with an EDGE-LOCAL split rule: an edge is split at its
        // midpoint if and only if IT is longer than the threshold. Both triangles sharing
        // an edge therefore always make the same decision and the tesselation contains no
        // T-vertices. This matters because the vertices are displaced (on the GPU) by
        // sampled terrain heights: a T-vertex displaces to its sampled height while the
        // neighbouring triangle's unsplit edge crosses that point at the interpolated
        // height, opening background-colored cracks all over rugged terrain (the
        // long-standing 'white triangles when zooming out' artifact).
        bool split01 = dist01 > _divideThreshold;
        bool split02 = dist02 > _divideThreshold;
        bool split12 = dist12 > _divideThreshold;
        if (!split01 && !split02 && !split12) {
            indices.append(i0, i1, i2);
            return;
        }
        // ... but only over the tile ITSELF. A source tile keeps a buffer around its data, and at
        // overzoom that buffer is scaled with everything else: a z14 source drawn into a z19 target
        // reaches 2.2 tile widths past the border, while the threshold is the z19 one. The polygon
        // gate upstream is an INTERSECTS test, so one triangle touching the tile was refined across
        // its whole extent - measured over Paris at z19, 145 m against a 2.4 m threshold, 4096
        // triangles out of one and ~1000 such triangles in a frame, which is a 2.5 GB kill or a
        // hang. Everything past the border is clipped per fragment anyway, so subdividing it buys
        // nothing; the sub-triangles that still touch the tile are refined exactly as before, so
        // the split rule stays edge-local and the surface keeps no T-vertex where it is drawn.
        cglib::bbox2<float> bounds(coords[i0]);
        bounds.add(coords[i1]);
        bounds.add(coords[i2]);
        if (!TesselationBounds::refines(bounds)) {
            indices.append(i0, i1, i2);
            return;
        }

        auto splitEdge = [&](std::size_t ia, std::size_t ib) -> std::size_t {
            std::size_t iM = coords.size();
            coords.append((coords[ia] + coords[ib]) * 0.5f);
            if (!texCoords.empty()) {
                texCoords.append((texCoords[ia] + texCoords[ib]) * 0.5f);
            }
            return iM;
        };
        auto edgeDist = [&](std::size_t ia, std::size_t ib) -> float {
            return cglib::length(coords[ib] - coords[ia]) * static_cast<float>(_tileScaleMeters);
        };

        if (split01 && split02 && split12) {
            // regular 1-to-4 split; the midsegments are exactly half the opposite edges
            std::size_t m01 = splitEdge(i0, i1);
            std::size_t m02 = splitEdge(i0, i2);
            std::size_t m12 = splitEdge(i1, i2);
            tesselateTriangle(i0, m01, m02, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(m01, i1, m12, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(m02, m12, i2, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(m01, m12, m02, dist02 * 0.5f, dist12 * 0.5f, dist01 * 0.5f, coords, texCoords, indices);
        }
        else if (split01 && split02) {
            std::size_t m01 = splitEdge(i0, i1);
            std::size_t m02 = splitEdge(i0, i2);
            float distM01_2 = edgeDist(m01, i2);
            tesselateTriangle(i0, m01, m02, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(m01, i1, i2, dist01 * 0.5f, distM01_2, dist12, coords, texCoords, indices);
            tesselateTriangle(m01, i2, m02, distM01_2, dist12 * 0.5f, dist02 * 0.5f, coords, texCoords, indices);
        }
        else if (split01 && split12) {
            std::size_t m01 = splitEdge(i0, i1);
            std::size_t m12 = splitEdge(i1, i2);
            float distM01_2 = edgeDist(m01, i2);
            tesselateTriangle(m01, i1, m12, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(i0, m01, i2, dist01 * 0.5f, dist02, distM01_2, coords, texCoords, indices);
            tesselateTriangle(m01, m12, i2, dist02 * 0.5f, distM01_2, dist12 * 0.5f, coords, texCoords, indices);
        }
        else if (split02 && split12) {
            std::size_t m02 = splitEdge(i0, i2);
            std::size_t m12 = splitEdge(i1, i2);
            float distM02_1 = edgeDist(m02, i1);
            tesselateTriangle(i0, i1, m02, dist01, dist02 * 0.5f, distM02_1, coords, texCoords, indices);
            tesselateTriangle(i1, m12, m02, dist12 * 0.5f, distM02_1, dist01 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(m02, m12, i2, dist01 * 0.5f, dist02 * 0.5f, dist12 * 0.5f, coords, texCoords, indices);
        }
        else if (split01) {
            std::size_t m01 = splitEdge(i0, i1);
            float distM = edgeDist(m01, i2);
            tesselateTriangle(i2, i0, m01, dist02, distM, dist01 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(i1, i2, m01, dist12, dist01 * 0.5f, distM, coords, texCoords, indices);
        }
        else if (split02) {
            std::size_t m02 = splitEdge(i0, i2);
            float distM = edgeDist(m02, i1);
            tesselateTriangle(i0, i1, m02, dist01, dist02 * 0.5f, distM, coords, texCoords, indices);
            tesselateTriangle(i1, i2, m02, dist12, distM, dist02 * 0.5f, coords, texCoords, indices);
        }
        else {
            std::size_t m12 = splitEdge(i1, i2);
            float distM = edgeDist(m12, i0);
            tesselateTriangle(i0, i1, m12, dist01, distM, dist12 * 0.5f, coords, texCoords, indices);
            tesselateTriangle(i2, i0, m12, dist02, dist12 * 0.5f, distM, coords, texCoords, indices);
        }
    }

    TerrainTileTransformer::TerrainTileTransformer(float scale, const std::shared_ptr<ElevationManager>& elevationManager, int meshResolution, int minZoom, bool sourceDensity, bool sourceDensityLines) :
        _scale(scale),
        _elevationManager(elevationManager),
        _meshResolution(std::max(1, meshResolution)),
        _minZoom(minZoom),
        _sourceDensity(sourceDensity),
        _sourceDensityLines(sourceDensityLines)
    {
    }

    cglib::vec3<double> TerrainTileTransformer::calculateTileOrigin(const vt::TileId& tileId) const {
        int tileMask = (1 << tileId.zoom) - 1;
        double zoomScale = 1.0 / (1 << tileId.zoom);
        cglib::vec3<double> p;
        p(0) = (tileId.x * zoomScale - 0.5) * _scale;
        p(1) = ((tileMask - tileId.y) * zoomScale - 0.5) * _scale;
        p(2) = 0;
        return p;
    }

    cglib::bbox3<double> TerrainTileTransformer::calculateTileBBox(const vt::TileId& tileId) const {
        cglib::bbox3<double> bbox = cglib::transform_bbox(cglib::bbox3<double>(cglib::vec3<double>(0, 0, 0), cglib::vec3<double>(1, 1, 0)), calculateTileMatrix(tileId, 1.0f));
        if (tileId.zoom >= _minZoom) {
            int tileMask = (1 << tileId.zoom) - 1;
            MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
            double minZ = 0, maxZ = 0;
            _elevationManager->getMinMaxDisplayHeight(mapTile, minZ, maxZ);
            bbox.add(cglib::vec3<double>(bbox.min(0), bbox.min(1), minZ));
            bbox.add(cglib::vec3<double>(bbox.max(0), bbox.max(1), maxZ));
        }
        return bbox;
    }

    cglib::mat4x4<double> TerrainTileTransformer::calculateTileMatrix(const vt::TileId& tileId, float coordScale) const {
        double s = _scale * coordScale / (1 << tileId.zoom);
        cglib::vec3<double> p = calculateTileOrigin(tileId);

        cglib::mat4x4<double> m = cglib::mat4x4<double>::zero();
        m(0, 0) = s;
        m(1, 1) = s;
        m(2, 2) = s;
        m(0, 3) = p(0);
        m(1, 3) = p(1);
        m(2, 3) = p(2);
        m(3, 3) = 1;
        return m;
    }

    cglib::mat4x4<float> TerrainTileTransformer::calculateTileTransform(const vt::TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const {
        return cglib::translate4_matrix(cglib::vec3<float>(translate(0) / coordScale, -translate(1) / coordScale, 0));
    }

    std::shared_ptr<const vt::TileTransformer::VertexTransformer> TerrainTileTransformer::createTileVertexTransformer(const vt::TileId& tileId) const {
        std::shared_ptr<ElevationTileGrid> grid;
        if (tileId.zoom >= _minZoom) {
            int tileMask = (1 << tileId.zoom) - 1;
            MapTile mapTile(tileId.x & tileMask, std::min(std::max(tileId.y, 0), tileMask), tileId.zoom, 0);
            grid = _elevationManager->getTileGrid(mapTile, ElevationManager::LoadMode::CACHED_ONLY);
        }

        float divideThreshold = std::numeric_limits<float>::infinity();
        float lineDivideThreshold = std::numeric_limits<float>::infinity();
        float latticeCell = 0.0f;
        if (grid && grid->getMaxHeight() - grid->getMinHeight() > FLAT_HEIGHT_RANGE_EPSILON) {
            double tileScaleMeters = EARTH_CIRCUMFERENCE / (1 << tileId.zoom);
            double threshold = tileScaleMeters / _meshResolution;

            // The reference surface is the renderer's shared _meshResolution grid, so subdivide to
            // one grid cell: every sub-vertex then lattice-clamps onto it and cannot sag through.
            // Not to the DEM texel size - the grid, not the DEM, is what the depth test compares to.
            // Source-density (tangram) mode drops FILL subdivision only: fills are the expensive
            // side (~meshResolution^2 triangles per tile) and a per-draw slack lifts them, while
            // lines are 1D and must stay on the surface (contours lie exactly on it).
            divideThreshold = _sourceDensity ? std::numeric_limits<float>::infinity() : static_cast<float>(threshold * areaThresholdScale());
            lineDivideThreshold = _sourceDensityLines ? std::numeric_limits<float>::infinity() : static_cast<float>(threshold * lineThresholdScale());
            // The lattice split cuts lines at the cell triangle boundaries, killing the chord sag
            // across a cell's fold. Skipped when the relief cannot fold a cell enough to matter.
            bool latticeWorthIt = (grid->getMaxHeight() - grid->getMinHeight()) >= latticeReliefThreshold();
            latticeCell = (_sourceDensityLines || !latticeWorthIt) ? 0.0f : static_cast<float>(1.0 / _meshResolution);
        }

        return std::make_shared<TerrainVertexTransformer>(tileId, _scale, std::move(grid), _elevationManager->getExaggeration(), divideThreshold, lineDivideThreshold, latticeCell, lineSagToleranceMeters());
    }
}
