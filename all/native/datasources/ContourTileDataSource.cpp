#include "ContourTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "core/MapPos.h"
#include "core/MapBounds.h"
#include "core/Variant.h"
#include "components/Exceptions.h"
#include "graphics/Bitmap.h"
#include "datasources/components/TileBitmap.h"
#include "projections/Projection.h"
#include "components/TerrainOptions.h"
#include "rastertiles/ElevationDecoder.h"
#include "terrain/ElevationManager.h"
#include "terrain/ElevationTileGrid.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "utils/TileUtils.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

#include <mbvtbuilder/MBVTTileBuilder.h>

#include <mapnikvt/mbvtpackage/MBVTPackage.pb.h>

namespace {

    using GridPoint = std::pair<double, double>; // (gx, gy) in grid node coordinates
    using Polyline = std::vector<GridPoint>;

    // Label stubs: tangram's contour label generator, ported from
    // core/src/style/contourTextStyle.cpp (ContourTextStyleBuilder / getContourLine). Their
    // constants, expressed against a 256 pixel tile as they are there.
    const int LABEL_GRID_SIZE = 4;                       // gridSize: seeds per tile side
    const double LABEL_TILE_SIZE = 256.0;
    const double LABEL_MAX_POS_ERR = 0.25 / LABEL_TILE_SIZE;
    const double LABEL_LEN = 32.0 / LABEL_TILE_SIZE;     // labelLen: how long a stub has to be
    const double LABEL_STEP_SIZE = 2.0 / LABEL_TILE_SIZE;
    const int LABEL_MAX_ITER = 12;

    // Bilinear height and its gradient at tile-local (u, v) in [0, 1], in units of elevation per
    // unit of uv - the same quantity tangram's ElevationManager::elevationLerp returns.
    inline double sampleHeightGrad(const std::vector<float>& heights, int W, int H, double u, double v, double& gu, double& gv) {
        double x = std::min(std::max(u, 0.0), 1.0) * (W - 1);
        double y = std::min(std::max(v, 0.0), 1.0) * (H - 1);
        int x0 = std::min(static_cast<int>(x), W - 2);
        int y0 = std::min(static_cast<int>(y), H - 2);
        double fx = x - x0;
        double fy = y - y0;
        double h00 = heights[static_cast<std::size_t>(y0) * W + x0];
        double h10 = heights[static_cast<std::size_t>(y0) * W + x0 + 1];
        double h01 = heights[static_cast<std::size_t>(y0 + 1) * W + x0];
        double h11 = heights[static_cast<std::size_t>(y0 + 1) * W + x0 + 1];
        gu = ((h10 - h00) * (1.0 - fy) + (h11 - h01) * fy) * (W - 1);
        gv = ((h01 - h00) * (1.0 - fx) + (h11 - h10) * fx) * (H - 1);
        return (h00 * (1.0 - fx) + h10 * fx) * (1.0 - fy) + (h01 * (1.0 - fx) + h11 * fx) * fy;
    }

    // Walks from the seed (u, v) down the gradient onto the nearest contour level, then along the
    // contour (the tangent of the gradient) for as long as a label needs. Returns the level, or 0
    // when the seed does not reach one - a flat tile, a zero gradient, or a walk that left the tile.
    // Height and gradient at tile-local (u, v), whatever the heights come from: the resampled
    // grid decoded from a DEM bitmap, or the elevation grid the terrain already holds.
    using HeightSampler = std::function<double(double u, double v, double& gu, double& gv)>;

    double traceLabelStub(const HeightSampler& sampler, double interval,
                          double u, double v, Polyline& line) {
        const std::size_t numLinePts = static_cast<std::size_t>(1.25 * LABEL_LEN / LABEL_STEP_SIZE);
        double level = std::numeric_limits<double>::quiet_NaN();
        while (true) {
            double step = 0, prevElev = 0, lowerElev = 0, upperElev = 0;
            double gu = 0, gv = 0, prevU = 0, prevV = 0, lowerU = 0, lowerV = 0, upperU = 0, upperV = 0;
            bool hasLower = false, hasUpper = false;
            int niter = 0;
            do {
                double elev = sampler(u, v, gu, gv);
                if (std::isnan(level)) {
                    level = std::round(elev / interval) * interval;
                    if (level <= 0.0) {
                        return 0.0; // sea level and below carry no useful label
                    }
                }

                if (elev < level && (!hasLower || elev > lowerElev)) {
                    lowerElev = elev; lowerU = u; lowerV = v; hasLower = true;
                } else if (elev > level && (!hasUpper || elev < upperElev)) {
                    upperElev = elev; upperU = u; upperV = v; hasUpper = true;
                }

                // Zero gradient: fall back to the secant of the previous step, as they do - a flat
                // sample is common enough that giving up on it loses labels.
                if (gu == 0 && gv == 0) {
                    if (niter == 0 || prevElev == elev || (u == prevU && v == prevV)) {
                        return 0.0;
                    }
                    double du = u - prevU, dv = v - prevV;
                    double dr2 = du * du + dv * dv;
                    gu = du * (elev - prevElev) / dr2;
                    gv = dv * (elev - prevElev) / dr2;
                }
                prevElev = elev;
                prevU = u;
                prevV = v;

                double gradLen = std::sqrt(gu * gu + gv * gv);
                step = std::abs(level - elev) / gradLen;
                double signedLen = (level < elev ? -gradLen : gradLen);

                if (!hasLower || !hasUpper) {
                    double toEdge = std::min(std::min(u, v), std::min(1.0 - u, 1.0 - v));
                    double limited = std::min(step, std::max(0.025, toEdge));
                    u += limited * (gu / signedLen);
                    v += limited * (gv / signedLen);
                } else {
                    // The level is bracketed: interpolate straight onto it.
                    double d = upperElev - lowerElev;
                    u = (upperU * (level - lowerElev) + lowerU * (upperElev - level)) / d;
                    v = (upperV * (level - lowerElev) + lowerV * (upperElev - level)) / d;
                }

                if (++niter > LABEL_MAX_ITER || !(u >= 0 && v >= 0 && u <= 1 && v <= 1)) {
                    return 0.0;
                }
            } while (step > LABEL_MAX_POS_ERR);

            line.emplace_back(u, v); // tile-local uv; the caller maps it to the tile's bounds
            if (line.size() >= numLinePts) {
                return level;
            }
            // Along the contour: the tangent of the gradient.
            double tangentLen = std::sqrt(gu * gu + gv * gv);
            if (!(tangentLen > 0)) {
                return 0.0;
            }
            u = std::min(std::max(u + (gv / tangentLen) * LABEL_STEP_SIZE, 0.0), 1.0);
            v = std::min(std::max(v - (gu / tangentLen) * LABEL_STEP_SIZE, 0.0), 1.0);
        }
    }

    // One marching-squares segment: the two cell edges it crosses (used to link segments into
    // polylines without any floating point matching) plus the crossing points themselves.
    struct Segment {
        std::int32_t edgeA, edgeB;
        float ax, ay, bx, by;
    };

    // Reusable scratch for linking one level's segments. Indexed by edge id, so no hashing and
    // no per-level allocation: 'stamp' marks which level an entry belongs to, which is what
    // makes reuse without clearing 2*W*H entries per level possible.
    struct LinkBuffers {
        std::vector<std::int32_t> firstSegment;
        std::vector<std::int32_t> secondSegment;
        std::vector<std::uint32_t> stamp;
        std::uint32_t generation = 0;
        std::vector<char> used;

        void resize(std::size_t edgeCount) {
            firstSegment.assign(edgeCount, -1);
            secondSegment.assign(edgeCount, -1);
            stamp.assign(edgeCount, 0);
            generation = 0;
        }
    };

    // Linear crossing position of 'level' between corner values va (at ta) and vb (at tb).
    inline double lerpT(float va, float vb, double level) {
        double d = static_cast<double>(vb) - static_cast<double>(va);
        if (d == 0.0) {
            return 0.5;
        }
        double t = (level - va) / d;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return t;
    }

    // Marching squares over a WxH height grid (row-major, row 0 = south), for EVERY level in one
    // pass: a cell can only be crossed by the levels between its lowest and highest corner (one
    // or two in practice), so the cost follows the number of crossings instead of grid area x
    // level count - re-scanning the whole grid per level was the dominant cost of a contour tile
    // (a tile spanning 40 levels scanned 96x96 cells 40 times to emit a few thousand segments).
    void marchingSquaresAllLevels(const std::vector<float>& heights, int W, int H,
                                  double interval, long long firstLevel, long long lastLevel,
                                  std::vector<std::vector<Segment>>& segmentsPerLevel) {
        // Edge id: horizontal edge (between (x,y) and (x+1,y)) -> (y*W + x)*2 + 0
        //          vertical   edge (between (x,y) and (x,y+1)) -> (y*W + x)*2 + 1
        auto hEdge = [W](int x, int y) -> std::int32_t { return (y * W + x) * 2 + 0; };
        auto vEdge = [W](int x, int y) -> std::int32_t { return (y * W + x) * 2 + 1; };

        for (int y = 0; y + 1 < H; y++) {
            const float* row0 = &heights[static_cast<std::size_t>(y) * W];
            const float* row1 = &heights[static_cast<std::size_t>(y + 1) * W];
            for (int x = 0; x + 1 < W; x++) {
                float v00 = row0[x];     // SW
                float v10 = row0[x + 1]; // SE
                float v01 = row1[x];     // NW
                float v11 = row1[x + 1]; // NE

                float lo = std::min(std::min(v00, v10), std::min(v01, v11));
                float hi = std::max(std::max(v00, v10), std::max(v01, v11));
                // The corner test below is 'value >= level', so a level crosses this cell when
                // lo < level <= hi.
                long long cellFirst = static_cast<long long>(std::floor(lo / interval)) + 1;
                long long cellLast = static_cast<long long>(std::floor(hi / interval));
                if (cellFirst < firstLevel) cellFirst = firstLevel;
                if (cellLast > lastLevel) cellLast = lastLevel;

                for (long long l = cellFirst; l <= cellLast; l++) {
                    double level = l * interval;

                    int idx = 0;
                    if (v00 >= level) idx |= 1; // SW
                    if (v10 >= level) idx |= 2; // SE
                    if (v11 >= level) idx |= 4; // NE
                    if (v01 >= level) idx |= 8; // NW
                    if (idx == 0 || idx == 15) {
                        continue;
                    }

                    std::vector<Segment>& segments = segmentsPerLevel[static_cast<std::size_t>(l - firstLevel)];
                    auto addSegment = [&](std::int32_t ea, GridPoint pa, std::int32_t eb, GridPoint pb) {
                        Segment segment;
                        segment.edgeA = ea;
                        segment.edgeB = eb;
                        segment.ax = static_cast<float>(pa.first);
                        segment.ay = static_cast<float>(pa.second);
                        segment.bx = static_cast<float>(pb.first);
                        segment.by = static_cast<float>(pb.second);
                        segments.push_back(segment);
                    };

                    // Crossing points on the 4 cell edges (only some are used per case).
                    std::int32_t eB = hEdge(x, y);       GridPoint pB(x + lerpT(v00, v10, level), y);           // bottom (S)
                    std::int32_t eT = hEdge(x, y + 1);   GridPoint pT(x + lerpT(v01, v11, level), y + 1);       // top (N)
                    std::int32_t eL = vEdge(x, y);       GridPoint pL(x, y + lerpT(v00, v01, level));           // left (W)
                    std::int32_t eR = vEdge(x + 1, y);   GridPoint pR(x + 1, y + lerpT(v10, v11, level));       // right (E)

                    switch (idx) {
                        case 1:  case 14: addSegment(eL, pL, eB, pB); break;
                        case 2:  case 13: addSegment(eB, pB, eR, pR); break;
                        case 3:  case 12: addSegment(eL, pL, eR, pR); break;
                        case 4:  case 11: addSegment(eR, pR, eT, pT); break;
                        case 6:  case 9:  addSegment(eB, pB, eT, pT); break;
                        case 7:  case 8:  addSegment(eL, pL, eT, pT); break;
                        case 5: // saddle: two segments (consistent resolution)
                            addSegment(eL, pL, eT, pT);
                            addSegment(eB, pB, eR, pR);
                            break;
                        case 10: // saddle
                            addSegment(eL, pL, eB, pB);
                            addSegment(eR, pR, eT, pT);
                            break;
                        default: break;
                    }
                }
            }
        }
    }

    // Links one level's segments into polylines through their shared cell-edge ids (no floating
    // point matching). A grid edge carries at most one crossing point per cell, so at most two
    // segments meet on it - which is what lets the adjacency live in two flat arrays.
    std::vector<Polyline> linkSegments(const std::vector<Segment>& segments, LinkBuffers& buffers) {
        std::vector<Polyline> polylines;
        if (segments.empty()) {
            return polylines;
        }

        buffers.generation++;
        std::uint32_t generation = buffers.generation;
        auto attach = [&](std::int32_t edge, std::int32_t segmentIndex) {
            if (buffers.stamp[edge] != generation) {
                buffers.stamp[edge] = generation;
                buffers.firstSegment[edge] = segmentIndex;
                buffers.secondSegment[edge] = -1;
            } else if (buffers.secondSegment[edge] < 0) {
                buffers.secondSegment[edge] = segmentIndex;
            }
        };
        for (std::size_t i = 0; i < segments.size(); i++) {
            attach(segments[i].edgeA, static_cast<std::int32_t>(i));
            attach(segments[i].edgeB, static_cast<std::int32_t>(i));
        }

        buffers.used.assign(segments.size(), 0);
        auto takeSegment = [&](std::int32_t edge) -> std::int32_t {
            std::int32_t first = buffers.firstSegment[edge];
            if (first >= 0 && !buffers.used[first]) {
                buffers.used[first] = 1;
                return first;
            }
            std::int32_t second = buffers.secondSegment[edge];
            if (second >= 0 && !buffers.used[second]) {
                buffers.used[second] = 1;
                return second;
            }
            return -1;
        };

        // From a starting edge, follow unused segments as far as they go. A closed ring comes
        // back to its first edge and ends there with the first point repeated, i.e. closed.
        auto buildChain = [&](std::int32_t start) {
            Polyline line;
            std::int32_t current = start;
            bool first = true;
            while (true) {
                std::int32_t segmentIndex = takeSegment(current);
                if (segmentIndex < 0) {
                    break;
                }
                const Segment& segment = segments[segmentIndex];
                bool forward = (segment.edgeA == current);
                if (first) {
                    line.emplace_back(forward ? segment.ax : segment.bx, forward ? segment.ay : segment.by);
                    first = false;
                }
                line.emplace_back(forward ? segment.bx : segment.ax, forward ? segment.by : segment.ay);
                current = forward ? segment.edgeB : segment.edgeA;
            }
            return line;
        };

        // Open chains first: their endpoints (on the tile border) carry a single segment, so
        // starting anywhere else would cut them in two.
        for (const Segment& segment : segments) {
            for (std::int32_t edge : { segment.edgeA, segment.edgeB }) {
                if (buffers.secondSegment[edge] < 0) {
                    Polyline line = buildChain(edge);
                    if (line.size() >= 2) polylines.push_back(std::move(line));
                }
            }
        }
        // Remaining closed loops.
        for (std::size_t i = 0; i < segments.size(); i++) {
            if (buffers.used[i]) {
                continue;
            }
            Polyline line = buildChain(segments[i].edgeA);
            if (line.size() >= 2) polylines.push_back(std::move(line));
        }

        return polylines;
    }

}

namespace massif {

    ContourTileDataSource::ContourTileDataSource(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder) :
        TileDataSource(),
        _dataSource(dataSource),
        _elevationDecoder(elevationDecoder),
        _baseInterval(10.0f),
        _simplifyTolerance(1.5f),
        _resolution(128),
        // 5, not 12: contours belong on a regional view too, and the interval ladder below already
        // coarsens them there.
        _minVisibleZoom(5),
        // ON. Without it a traced line stops dead at every tile border, which is the first thing
        // anyone notices - it costs up to 3 extra DEM fetches per tile.
        _seamlessEdges(true),
        _labelStubs(false),
        _labelInterval(0.0f),
        _layerName("contour"),
        _mutex(),
        _dataSourceListener()
    {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }
        // Starting point only - see setIntervalMultiplier. Nested (10 | 50 | 100 | 500 for a 10m
        // base) so lines meet across tiles of different zoom, and no finer than a style is likely to
        // draw at the camera zoom where tiles of that zoom are used. Measured on a mid-range phone
        // (contours + hillshade + 3D terrain, z10.5, tilt 45): this table against a uniform 100/50/10
        // one costs 10.7 CPU-seconds of tile generation in the first 30 seconds instead of 16.2.
        _intervalMultipliers = { { 9, 50.0f }, { 11, 10.0f }, { 13, 5.0f }, { -1, 1.0f } };
        // NO per-zoom grid by default, deliberately. A tile is drawn at roughly the same SCREEN size
        // whatever its zoom, so the tracing grid is what fixes the shape on screen and must not
        // shrink with zoom: at z9 a 48-sample grid puts contour vertices 1.6 km apart, and the far
        // half of any tilted view - which is made of exactly those tiles - turns into long straight
        // chords. Cost at low zoom belongs to the INTERVAL (fewer levels), which does not distort
        // the lines it keeps. The table is here for apps that measure otherwise.
        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource->registerOnChangeListener(_dataSourceListener);
    }

    ContourTileDataSource::ContourTileDataSource(const std::shared_ptr<TileDataSource>& dataSource) :
        ContourTileDataSource(dataSource, std::shared_ptr<ElevationDecoder>())
    {
    }

    ContourTileDataSource::~ContourTileDataSource() {
        _dataSource->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();
    }

    std::string ContourTileDataSource::getLayerName() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _layerName;
    }

    void ContourTileDataSource::setLayerName(const std::string& name) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _layerName = name;
        }
        notifyTilesChanged(false);
    }

    float ContourTileDataSource::getBaseInterval() const {
        return _baseInterval;
    }

    void ContourTileDataSource::setBaseInterval(float interval) {
        if (interval <= 0.0f) {
            throw InvalidArgumentException("Base interval must be positive");
        }
        _baseInterval = interval;
        notifyTilesChanged(false);
    }

    int ContourTileDataSource::getResolution() const {
        return _resolution;
    }

    void ContourTileDataSource::setResolution(int resolution) {
        _resolution = (resolution > 0 ? std::max(8, resolution) : 0);
        notifyTilesChanged(false);
    }

    int ContourTileDataSource::getMinVisibleZoom() const {
        return _minVisibleZoom;
    }

    void ContourTileDataSource::setMinVisibleZoom(int zoom) {
        _minVisibleZoom = zoom;
        notifyTilesChanged(false);
    }

    bool ContourTileDataSource::isSeamlessEdgesEnabled() const {
        return _seamlessEdges;
    }

    void ContourTileDataSource::setSeamlessEdgesEnabled(bool enabled) {
        _seamlessEdges = enabled;
        notifyTilesChanged(false);
    }

    std::shared_ptr<TerrainOptions> ContourTileDataSource::getTerrainOptions() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _terrainOptions;
    }

    void ContourTileDataSource::setTerrainOptions(const std::shared_ptr<TerrainOptions>& terrainOptions) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_terrainOptions == terrainOptions) {
                return;
            }
            _terrainOptions = terrainOptions;
        }
        notifyTilesChanged(false);
    }

    bool ContourTileDataSource::isLabelStubsEnabled() const {
        return _labelStubs;
    }

    void ContourTileDataSource::setLabelStubsEnabled(bool enabled) {
        _labelStubs = enabled;
        notifyTilesChanged(false);
    }

    float ContourTileDataSource::getLabelInterval() const {
        return _labelInterval;
    }

    void ContourTileDataSource::setLabelInterval(float interval) {
        if (interval < 0.0f) {
            throw InvalidArgumentException("Label interval must not be negative");
        }
        _labelInterval = interval;
        notifyTilesChanged(false);
    }

    float ContourTileDataSource::getSimplifyTolerance() const {
        return _simplifyTolerance;
    }

    void ContourTileDataSource::setSimplifyTolerance(float tolerance) {
        _simplifyTolerance = tolerance;
        notifyTilesChanged(false);
    }

    int ContourTileDataSource::getMinZoom() const {
        // Report the DEM's real min zoom rather than clamping up to MinVisibleZoom: if we clamped up, then
        // when the camera is below that zoom the layer would fill the whole viewport with min-zoom tiles (an
        // exponential tile-count blowup as you zoom out). Instead the layer requests few tiles at the camera
        // zoom, and loadTile returns an empty tile cheaply below MinVisibleZoom (no DEM fetch, no tracing).
        return _dataSource->getMinZoom();
    }

    int ContourTileDataSource::getMaxZoom() const {
        return _dataSource->getMaxZoom();
    }

    MapBounds ContourTileDataSource::getDataExtent() const {
        return _dataSource->getDataExtent();
    }

    std::string ContourTileDataSource::getContainerMetaData(const std::string& key) const {
        return _dataSource->getContainerMetaData(key);
    }

    std::shared_ptr<ElevationDecoder> ContourTileDataSource::resolveDecoder(const std::shared_ptr<TileData>& tileData) const {
        return ElevationDecoder::Resolve(tileData, _dataSource.get(), _elevationDecoder);
    }

    const std::size_t ContourTileDataSource::MAX_CACHED_BITMAPS = 16;

    std::shared_ptr<Bitmap> ContourTileDataSource::loadCachedBitmap(const MapTile& tile) {
        long long key = (static_cast<long long>(tile.getZoom()) << 58) | (static_cast<long long>(tile.getX()) << 29) | static_cast<long long>(tile.getY());
        {
            std::lock_guard<std::mutex> lock(_bitmapCacheMutex);
            for (std::size_t i = 0; i < _bitmapCache.size(); i++) {
                if (_bitmapCache[i].first == key) {
                    std::shared_ptr<Bitmap> bitmap = _bitmapCache[i].second;
                    std::rotate(_bitmapCache.begin(), _bitmapCache.begin() + i, _bitmapCache.begin() + i + 1);
                    return bitmap;
                }
            }
        }

        std::shared_ptr<TileData> tileData = _dataSource->loadTile(tile);
        if (!tileData || !tileData->getData() || tileData->isReplaceWithParent()) {
            return std::shared_ptr<Bitmap>();
        }
        std::shared_ptr<Bitmap> bitmap = DecodeTileBitmap(tileData);
        if (bitmap) {
            cacheBitmap(tile, bitmap);
        }
        return bitmap;
    }

    void ContourTileDataSource::cacheBitmap(const MapTile& tile, const std::shared_ptr<Bitmap>& bitmap) {
        long long key = (static_cast<long long>(tile.getZoom()) << 58) | (static_cast<long long>(tile.getX()) << 29) | static_cast<long long>(tile.getY());
        std::lock_guard<std::mutex> lock(_bitmapCacheMutex);
        for (const std::pair<long long, std::shared_ptr<Bitmap> >& entry : _bitmapCache) {
            if (entry.first == key) {
                return;
            }
        }
        _bitmapCache.insert(_bitmapCache.begin(), std::make_pair(key, bitmap));
        if (_bitmapCache.size() > MAX_CACHED_BITMAPS) {
            _bitmapCache.resize(MAX_CACHED_BITMAPS);
        }
    }

    std::shared_ptr<TileData> ContourTileDataSource::buildLabelStubTile(const MapTile& mapTile, const HeightSampler& sampler, double interval) {
        int zoom = mapTile.getZoom();
        std::string layerName;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            layerName = _layerName;
        }
        double labelInterval = _labelInterval.load();
        if (!(labelInterval > 0.0)) {
            labelInterval = interval;
        }

        // The tile's footprint. Note the getFlipped(): grid v = 0 is the tile's SOUTH edge, as in
        // the DEM bitmap and in ElevationManager; the builder takes the raw tile x/y, so the same
        // footprint comes back without a double flip.
        std::shared_ptr<Projection> projection = getProjection();
        MapBounds bounds = TileUtils::CalculateMapTileBounds(mapTile.getFlipped(), projection);
        double minX = bounds.getMin().getX(), minY = bounds.getMin().getY();
        double sizeX = bounds.getMax().getX() - minX;
        double sizeY = bounds.getMax().getY() - minY;
        auto uvToWgs84 = [&](const GridPoint& p) -> mbvtbuilder::MBVTTileBuilder::Point {
            MapPos pos(minX + std::min(std::max(p.first, 0.0), 1.0) * sizeX,
                       minY + std::min(std::max(p.second, 0.0), 1.0) * sizeY);
            MapPos wgs84 = projection->toWgs84(pos);
            return mbvtbuilder::MBVTTileBuilder::Point(wgs84.getX(), wgs84.getY());
        };

        mbvtbuilder::MBVTTileBuilder tileBuilder(zoom, zoom);
        tileBuilder.setSimplifyTolerance(_simplifyTolerance);
        int layerIndex = tileBuilder.createLayer(layerName);

        // Their grid alignment: seeds sit at the same geographic positions across zoom levels, so a
        // label does not jump when the tile it comes from is replaced by a finer one.
        const int ngrid = LABEL_GRID_SIZE;
        double gridStart = 0.5 / (1 << std::max(0, 15 - zoom));
        Polyline stub;
        for (int col = 0; col < ngrid; col++) {
            double v = (col + gridStart) / ngrid;
            for (int row = 0; row < ngrid; row++) {
                double u = (row + gridStart) / ngrid;
                stub.clear();
                double level = traceLabelStub(sampler, labelInterval, u, v, stub);
                if (!(level > 0.0) || stub.size() < 2) {
                    continue;
                }
                long long ele = static_cast<long long>(std::llround(level));
                std::vector<mbvtbuilder::MBVTTileBuilder::Point> line;
                line.reserve(stub.size());
                for (const GridPoint& gp : stub) {
                    line.push_back(uvToWgs84(gp));
                }
                mbvtbuilder::MBVTTileBuilder::MultiLineString lines;
                lines.push_back(std::move(line));

                picojson::object props;
                props["ele"] = picojson::value(static_cast<std::int64_t>(ele));
                props["div"] = picojson::value(static_cast<std::int64_t>(computeDiv(ele)));
                // So a style that draws contour LINES from this layer can exclude the stubs, which
                // are only long enough to carry text: '#contour[stub=0] { line-width: .. }'.
                props["stub"] = picojson::value(static_cast<std::int64_t>(1));
                tileBuilder.addMultiLineString(layerIndex, std::move(lines), picojson::value(static_cast<std::int64_t>(ele)), picojson::value(props), false);
            }
        }

        try {
            protobuf::encoded_message encodedTile;
            tileBuilder.buildTile(zoom, mapTile.getX(), mapTile.getY(), encodedTile);
            auto data = std::make_shared<BinaryData>(reinterpret_cast<const unsigned char*>(encodedTile.data().data()), encodedTile.data().size());
            auto tileData = std::make_shared<TileData>(data);
            applyTileMetaData(tileData);
            return tileData;
        }
        catch (const std::exception& ex) {
            Log::Errorf("ContourTileDataSource::loadTile: Failed to build contour label tile %s: %s", mapTile.toString().c_str(), ex.what());
            return std::shared_ptr<TileData>();
        }
    }

    namespace {
        // (maxZoom, value) rungs in ascending order; maxZoom -1 means "everything above the rest".
        template <typename T>
        void setZoomTableEntry(std::vector<std::pair<int, T> >& table, int maxZoom, T value) {
            auto it = std::find_if(table.begin(), table.end(), [maxZoom](const std::pair<int, T>& entry) { return entry.first == maxZoom; });
            if (it != table.end()) {
                it->second = value;
                return;
            }
            it = std::find_if(table.begin(), table.end(), [maxZoom](const std::pair<int, T>& entry) { return entry.first < 0 || entry.first > maxZoom; });
            table.insert(maxZoom < 0 ? table.end() : it, std::make_pair(maxZoom, value));
        }

        template <typename T>
        T getZoomTableEntry(const std::vector<std::pair<int, T> >& table, int zoom, T defaultValue) {
            for (const std::pair<int, T>& entry : table) {
                if (entry.first < 0 || zoom <= entry.first) {
                    return entry.second;
                }
            }
            return defaultValue;
        }
    }

    void ContourTileDataSource::setIntervalMultiplier(int maxZoom, float multiplier) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            setZoomTableEntry(_intervalMultipliers, maxZoom, std::max(1.0f, multiplier));
        }
        notifyTilesChanged(false);
    }

    float ContourTileDataSource::getIntervalMultiplier(int zoom) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return getZoomTableEntry(_intervalMultipliers, zoom, 1.0f);
    }

    void ContourTileDataSource::clearIntervalMultipliers() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _intervalMultipliers.clear();
        }
        notifyTilesChanged(false);
    }

    void ContourTileDataSource::setResolutionForZoom(int maxZoom, int resolution) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            setZoomTableEntry(_zoomResolutions, maxZoom, resolution > 0 ? std::max(8, resolution) : 0);
        }
        notifyTilesChanged(false);
    }

    int ContourTileDataSource::getResolutionForZoom(int zoom) const {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const std::pair<int, int>& entry : _zoomResolutions) {
                if (entry.first < 0 || zoom <= entry.first) {
                    return entry.second;
                }
            }
        }
        return _resolution.load();
    }

    void ContourTileDataSource::clearResolutionsForZoom() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _zoomResolutions.clear();
        }
        notifyTilesChanged(false);
    }

    double ContourTileDataSource::getIntervalForZoom(int zoom) const {
        // What the tile CARRIES, not what is drawn - the style filters on 'div' per camera zoom.
        // A cost rule, and the rungs must NEST or a line stops dead at a tile border.
        // See docs/internals/rendering/07-hillshade-contours.md; the defaults are a starting point, the app
        // sets its own with setIntervalMultiplier.
        return _baseInterval * getIntervalMultiplier(zoom);
    }

    long long ContourTileDataSource::computeDiv(long long ele) {
        // Matches the gdal_contour based pipeline: largest "nice" divisor of the elevation.
        long long a = std::llabs(ele);
        if (a % 1000 == 0) return 1000;
        if (a % 500 == 0) return 500;
        if (a % 250 == 0) return 250;
        if (a % 200 == 0) return 200;
        if (a % 100 == 0) return 100;
        if (a % 50 == 0) return 50;
        if (a % 20 == 0) return 20;
        return 10;
    }

    std::shared_ptr<TileData> ContourTileDataSource::loadTile(const MapTile& mapTile) {
        int zoom = mapTile.getZoom();

        std::string layerName;
        float simplifyTolerance;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            layerName = _layerName;
            simplifyTolerance = _simplifyTolerance;
        }

        // Below the useful contour zoom, emit an empty (but valid) tile without fetching or decoding the
        // DEM. This keeps zoomed-out frames cheap even though the layer may request many such tiles.
        if (zoom < _minVisibleZoom.load()) {
            try {
                mbvtbuilder::MBVTTileBuilder emptyBuilder(zoom, zoom);
                emptyBuilder.createLayer(layerName);
                protobuf::encoded_message encodedTile;
                emptyBuilder.buildTile(zoom, mapTile.getX(), mapTile.getY(), encodedTile);
                auto data = std::make_shared<BinaryData>(reinterpret_cast<const unsigned char*>(encodedTile.data().data()), encodedTile.data().size());
                auto tileData = std::make_shared<TileData>(data);
                applyTileMetaData(tileData);
                return tileData;
            }
            catch (const std::exception& ex) {
                return std::shared_ptr<TileData>();
            }
        }

        // Label stubs off the terrain's own elevation, which is how tangram generates them: their
        // ContourTextStyleBuilder marches over the tile's elevation raster, the one the terrain has
        // already fetched and decoded, and carries no DEM tile of its own. A stub needs a few
        // hundred samples, not a decoded image, so with the terrain wired up this path costs
        // neither the tile load nor the image decode - measured at 44% of a tile decode thread,
        // 23% of it in the WebP decode alone.
        // Traced contour GEOMETRY does not take this path: it needs the DEM at its own resolution,
        // and the terrain's elevation level is capped to what its mesh can express.
        if (_labelStubs.load()) {
            std::shared_ptr<TerrainOptions> terrainOptions;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                terrainOptions = _terrainOptions;
            }
            if (terrainOptions) {
                if (std::shared_ptr<ElevationManager> elevationManager = terrainOptions->getElevationManager()) {
                    if (std::shared_ptr<ElevationTileGrid> grid = elevationManager->getTileGrid(mapTile, ElevationManager::LoadMode::ALLOW_LOAD)) {
                        std::shared_ptr<Projection> projection = getProjection();
                        MapBounds bounds = TileUtils::CalculateMapTileBounds(mapTile.getFlipped(), projection);
                        MapPos minInternal = projection->toInternal(bounds.getMin());
                        MapPos maxInternal = projection->toInternal(bounds.getMax());
                        double minIX = minInternal.getX(), minIY = minInternal.getY();
                        double sizeIX = maxInternal.getX() - minIX, sizeIY = maxInternal.getY() - minIY;
                        auto sampler = [grid, minIX, minIY, sizeIX, sizeIY](double u, double v, double& gu, double& gv) {
                            double x = minIX + std::min(std::max(u, 0.0), 1.0) * sizeIX;
                            double y = minIY + std::min(std::max(v, 0.0), 1.0) * sizeIY;
                            float dhdx = 0.0f, dhdy = 0.0f;
                            grid->sampleGradient(x, y, dhdx, dhdy);
                            // The walk works in uv, so the gradient is per unit of uv, not per
                            // internal unit.
                            gu = dhdx * sizeIX;
                            gv = dhdy * sizeIY;
                            return static_cast<double>(grid->sampleHeight(x, y));
                        };
                        return buildLabelStubTile(mapTile, sampler, getIntervalForZoom(zoom));
                    }
                }
            }
        }

        std::shared_ptr<TileData> elevTileData = _dataSource->loadTile(mapTile);
        if (!elevTileData || !elevTileData->getData()) {
            return std::shared_ptr<TileData>();
        }
        // Propagate parent-replacement/overzoom to keep behaviour consistent with the wrapped source.
        if (elevTileData->isReplaceWithParent()) {
            auto emptyData = std::make_shared<BinaryData>(std::vector<unsigned char>());
            auto tileData = std::make_shared<TileData>(emptyData);
            tileData->setReplaceWithParent(true);
            return tileData;
        }

        std::shared_ptr<ElevationDecoder> decoder = resolveDecoder(elevTileData);
        std::array<double, 4> coeffs = decoder->getColorComponentCoefficients();

        // Through the MRU cache: with seamless edges this tile was very likely already decoded as
        // a neighbour of a tile traced just before it.
        std::shared_ptr<Bitmap> bitmap = loadCachedBitmap(mapTile);
        if (!bitmap) {
            Log::Errorf("ContourTileDataSource::loadTile: Failed to decode elevation bitmap for %s", mapTile.toString().c_str());
            return std::shared_ptr<TileData>();
        }

        int fullW = bitmap->getWidth();
        int fullH = bitmap->getHeight();
        if (fullW < 2 || fullH < 2) {
            return std::shared_ptr<TileData>();
        }

        int bytesPerPixel = 0;
        switch (bitmap->getColorFormat()) {
            case ColorFormat::COLOR_FORMAT_GRAYSCALE: bytesPerPixel = 1; break;
            case ColorFormat::COLOR_FORMAT_RGB:       bytesPerPixel = 3; break;
            case ColorFormat::COLOR_FORMAT_RGBA:      bytesPerPixel = 4; break;
            default:
                Log::Error("ContourTileDataSource::loadTile: Unsupported bitmap color format");
                return std::shared_ptr<TileData>();
        }

        // Trace on an at-most 'resolution'-per-side grid; 0 = the DEM's own, which is what a
        // contour over 3D TERRAIN needs (a subsampled grid follows a height field the displaced
        // ground does not have and cuts through spurs). Nodes include BOTH endpoints, so adjacent
        // tiles share their boundary samples and meet without holes.
        int resolutionSetting = getResolutionForZoom(zoom); // per-zoom override, else Resolution
        int resolution = (resolutionSetting > 0 ? std::max(8, resolutionSetting) : std::max(fullW, fullH));
        int W = std::min(fullW, resolution);
        int H = std::min(fullH, resolution);
        if (W < 2 || H < 2) {
            return std::shared_ptr<TileData>();
        }

        // Optionally fetch neighbour DEM tiles so the tile's east/north edges use the neighbours' own
        // edge samples, making contour lines meet across tile boundaries. The DEM bitmap is stored
        // south-to-north / west-to-east and the tile bounds use mapTile.getFlipped(). In that flipped
        // (projection) tile scheme north = flipped.y + 1, which flips back to datasource y - 1. So the
        // geographic east/north/north-east neighbours are datasource tiles (x+1, y) / (x, y-1) / (x+1, y-1).
        bool seamless = _seamlessEdges.load();
        std::shared_ptr<Bitmap> eastBitmap, northBitmap, neBitmap;
        if (seamless) {
            auto fetchNeighbour = [&](int dx, int dy) -> std::shared_ptr<Bitmap> {
                MapTile nt(mapTile.getX() + dx, mapTile.getY() + dy, zoom, mapTile.getFrameNr());
                std::shared_ptr<Bitmap> bm = loadCachedBitmap(nt);
                if (!bm || bm->getWidth() != fullW || bm->getHeight() != fullH || bm->getColorFormat() != bitmap->getColorFormat()) {
                    return std::shared_ptr<Bitmap>(); // fall back to edge duplication
                }
                return bm;
            };
            eastBitmap = fetchNeighbour(1, 0);
            northBitmap = fetchNeighbour(0, -1);
            neBitmap = fetchNeighbour(1, -1);
        }

        const std::vector<std::uint8_t>& pixelData = bitmap->getPixelData();
        auto decodePixel = [&](const std::vector<std::uint8_t>& pd, int lx, int ly) -> float {
            const std::uint8_t* ptr = &pd[(static_cast<std::size_t>(ly) * fullW + lx) * bytesPerPixel];
            double r = 0, g = 0, b = 0, a = 255;
            switch (bytesPerPixel) {
                case 1: r = g = b = ptr[0]; break;
                case 3: r = ptr[0]; g = ptr[1]; b = ptr[2]; break;
                case 4: r = ptr[0]; g = ptr[1]; b = ptr[2]; a = ptr[3]; break;
            }
            return static_cast<float>(coeffs[0] * r + coeffs[1] * g + coeffs[2] * b + coeffs[3] * (a / 255.0));
        };
        // Sample the DEM at pixel (px, py). With seamless edges px may reach fullW and py may reach fullH,
        // which pull from the east/north/north-east neighbours' opposite edge (or duplicate our own edge
        // if a neighbour is missing).
        auto sampleHeight = [&](int px, int py) -> float {
            bool east = (px >= fullW);
            bool north = (py >= fullH);
            if (east && north) {
                if (neBitmap) return decodePixel(neBitmap->getPixelData(), 0, 0);
                return decodePixel(pixelData, fullW - 1, fullH - 1);
            }
            if (east) {
                if (eastBitmap) return decodePixel(eastBitmap->getPixelData(), 0, py);
                return decodePixel(pixelData, fullW - 1, py);
            }
            if (north) {
                if (northBitmap) return decodePixel(northBitmap->getPixelData(), px, 0);
                return decodePixel(pixelData, px, fullH - 1);
            }
            return decodePixel(pixelData, px, py);
        };

        // Decode DEM into a resampled height grid (row 0 = south). Nodes span [0, spanW]/[0, spanH]:
        // without seamless edges spanW = fullW-1 (last sample = last pixel), with seamless edges spanW = fullW
        // (last sample = east neighbour's first column), so node W-1 lands exactly on the tile's east edge.
        int spanW = seamless ? fullW : (fullW - 1);
        int spanH = seamless ? fullH : (fullH - 1);
        std::vector<float> heights(static_cast<std::size_t>(W) * H);
        float minH = 1.0e9f, maxH = -1.0e9f;
        for (int gy = 0; gy < H; gy++) {
            int py = static_cast<int>(std::llround(static_cast<double>(gy) * spanH / (H - 1)));
            for (int gx = 0; gx < W; gx++) {
                int px = static_cast<int>(std::llround(static_cast<double>(gx) * spanW / (W - 1)));
                float h = sampleHeight(px, py);
                heights[static_cast<std::size_t>(gy) * W + gx] = h;
                if (h < minH) minH = h;
                if (h > maxH) maxH = h;
            }
        }

        // Geographic bounds of this tile (EPSG3857). Note the getFlipped(): the DEM bitmap's
        // south edge (grid row 0) corresponds to the flipped tile's minimum-y bound, matching
        // ElevationManager. The MBVT builder is called with the raw tile x/y (as GeoJSONVectorTileDataSource
        // does), so the same footprint is reproduced without a double flip.
        std::shared_ptr<Projection> projection = getProjection();
        MapBounds bounds = TileUtils::CalculateMapTileBounds(mapTile.getFlipped(), projection);
        double minX = bounds.getMin().getX(), minY = bounds.getMin().getY();
        double sizeX = bounds.getMax().getX() - minX;
        double sizeY = bounds.getMax().getY() - minY;

        auto gridToWgs84 = [&](const GridPoint& p) -> mbvtbuilder::MBVTTileBuilder::Point {
            // Node 0 -> tile min edge, node W-1/H-1 -> tile max edge (even resample spans full extent).
            double fx = p.first / static_cast<double>(W - 1);
            double fy = p.second / static_cast<double>(H - 1);
            if (fx > 1.0) fx = 1.0;
            if (fy > 1.0) fy = 1.0;
            MapPos pos3857(minX + fx * sizeX, minY + fy * sizeY);
            MapPos wgs84 = projection->toWgs84(pos3857);
            return mbvtbuilder::MBVTTileBuilder::Point(wgs84.getX(), wgs84.getY());
        };

        double interval = getIntervalForZoom(zoom);

        mbvtbuilder::MBVTTileBuilder tileBuilder(zoom, zoom);
        tileBuilder.setSimplifyTolerance(simplifyTolerance);
        int layerIndex = tileBuilder.createLayer(layerName);

        // Label stubs instead of traced contours: a short polyline ON a contour per seed, which is
        // all a label needs. Tangram's ContourTextStyleBuilder (core/src/style/contourTextStyle.cpp)
        // generates its contour labels this way and carries no contour geometry at all - the lines
        // are a fragment block on the terrain draw, as they are here when the hillshade layer draws
        // them. Their algorithm, their constants.
        if (_labelStubs.load()) {
            HeightSampler sampler = [&heights, W, H](double u, double v, double& gu, double& gv) {
                return sampleHeightGrad(heights, W, H, u, v, gu, gv);
            };
            return buildLabelStubTile(mapTile, sampler, interval);
        }

        // Generate one feature (a MultiLineString) per contour level. The bounds are STRICT: a level
        // sitting exactly on the tile's minimum or maximum crosses nothing, and marching squares run
        // on it walks cell edges instead of crossings - long straight lines with no relation to the
        // terrain. A tile of constant height hits this every time, and there is one in most frames:
        // before the camera settles the culler asks for tiles far outside the view, whose DEM is
        // ocean or no-data and decodes to a flat 0 m. Those were the straight lines flashing across
        // the map at startup, different ones each run depending on which arrived first.
        long long firstLevel = static_cast<long long>(std::floor(minH / interval)) + 1;
        long long lastLevel = static_cast<long long>(std::ceil(maxH / interval)) - 1;
        // Safety cap: a very low-zoom tile can span kilometres of relief. Beyond this many levels the
        // tile is unreadable anyway, so bound the tracing cost. (Raise base interval to see more range.)
        const long long MAX_LEVELS = 200;
        if (lastLevel - firstLevel + 1 > MAX_LEVELS) {
            Log::Warnf("ContourTileDataSource::loadTile: %s spans %lld contour levels at interval %g m; capping to %lld. Increase base interval or restrict min zoom.",
                       mapTile.toString().c_str(), lastLevel - firstLevel + 1, interval, MAX_LEVELS);
            lastLevel = firstLevel + MAX_LEVELS - 1;
        }
        if (lastLevel < firstLevel) {
            lastLevel = firstLevel - 1; // flat tile: no level crosses it
        }
        std::vector<std::vector<Segment>> segmentsPerLevel(static_cast<std::size_t>(std::max(0LL, lastLevel - firstLevel + 1)));
        if (!segmentsPerLevel.empty()) {
            marchingSquaresAllLevels(heights, W, H, interval, firstLevel, lastLevel, segmentsPerLevel);
        }
        LinkBuffers linkBuffers;
        linkBuffers.resize(static_cast<std::size_t>(W) * H * 2);

        for (long long l = firstLevel; l <= lastLevel; l++) {
            double level = l * interval;
            long long ele = static_cast<long long>(std::llround(level));

            std::vector<Polyline> polylines = linkSegments(segmentsPerLevel[static_cast<std::size_t>(l - firstLevel)], linkBuffers);
            if (polylines.empty()) {
                continue;
            }

            mbvtbuilder::MBVTTileBuilder::MultiLineString lines;
            lines.reserve(polylines.size());
            for (const Polyline& pl : polylines) {
                std::vector<mbvtbuilder::MBVTTileBuilder::Point> line;
                line.reserve(pl.size());
                for (const GridPoint& gp : pl) {
                    line.push_back(gridToWgs84(gp));
                }
                lines.push_back(std::move(line));
            }

            picojson::object props;
            props["ele"] = picojson::value(static_cast<std::int64_t>(ele));
            props["div"] = picojson::value(static_cast<std::int64_t>(computeDiv(ele)));
            // Always present, so a style can filter on it in both modes: an undefined attribute
            // does not compare equal to 0, so a '[stub=0]' line rule would drop the traced
            // geometry too if only the stubs carried it.
            props["stub"] = picojson::value(static_cast<std::int64_t>(0));
            tileBuilder.addMultiLineString(layerIndex, std::move(lines), picojson::value(static_cast<std::int64_t>(ele)), picojson::value(props), false);
        }

        try {
            protobuf::encoded_message encodedTile;
            tileBuilder.buildTile(zoom, mapTile.getX(), mapTile.getY(), encodedTile);
            auto data = std::make_shared<BinaryData>(reinterpret_cast<const unsigned char*>(encodedTile.data().data()), encodedTile.data().size());
            auto tileData = std::make_shared<TileData>(data);
            applyTileMetaData(tileData);
            return tileData;
        }
        catch (const std::exception& ex) {
            Log::Errorf("ContourTileDataSource::loadTile: Failed to build contour tile %s: %s", mapTile.toString().c_str(), ex.what());
            return std::shared_ptr<TileData>();
        }
    }

    ContourTileDataSource::DataSourceListener::DataSourceListener(ContourTileDataSource& dataSource) :
        _dataSource(dataSource)
    {
    }

    void ContourTileDataSource::DataSourceListener::onTilesChanged(bool removeTiles) {
        _dataSource.notifyTilesChanged(removeTiles);
    }

}
