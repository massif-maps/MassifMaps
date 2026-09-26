#include "ElevationTileGrid.h"
#include "ElevationNodeField.h"
#include "graphics/Bitmap.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <vt/RenderStats.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace massif {

    namespace {
        // Never reused, so an old grid is distinguishable from its replacement. See getSerial.
        std::atomic<unsigned long long> gridSerialCounter(0);
    }

    ElevationTileGrid::ElevationTileGrid(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs, int nodesPerEdge, int boxCells) :
        _serial(++gridSerialCounter),
        _tile(tile),
        _internalBounds(internalBounds),
        _bitmap(bitmap),
        _pixelData(bitmap ? bitmap->getPixelData().data() : nullptr),
        _coeffs(coeffs),
        _width(bitmap ? bitmap->getWidth() : 0),
        _height(bitmap ? bitmap->getHeight() : 0),
        _bytesPerTexel(bitmap ? bitmap->getBytesPerPixel() : 0),
        _minHeight(0),
        _maxHeight(0),
        _nodesPerEdge(0),
        _boxCells(std::max(1, boxCells)),
        _nodeHeights()
    {
        if (_pixelData && _width > 0 && _height > 0) {
            // One pass for the height range, which culling and the shadow box need. Everything
            // else is decoded on demand.
            float minHeight = getHeight(0, 0);
            float maxHeight = minHeight;
            std::size_t count = static_cast<std::size_t>(_width) * _height;
            for (std::size_t i = 0; i < count; i++) {
                float h = decodeTexel(&_pixelData[i * _bytesPerTexel]);
                minHeight = std::min(minHeight, h);
                maxHeight = std::max(maxHeight, h);
            }
            _minHeight = minHeight;
            _maxHeight = maxHeight;

            // The node field, on the decode thread where the raster is walked anyway. Edge nodes
            // clamp here; encodeNodeTexture redoes them with the neighbours for the GPU.
            if (nodesPerEdge > 0) {
                _nodesPerEdge = nodesPerEdge;
                ElevationNodeField::build(_width, _height, _nodesPerEdge, _boxCells, [this](int tx, int ty) {
                    return getHeight(std::min(std::max(tx, 0), _width - 1), std::min(std::max(ty, 0), _height - 1));
                }, _nodeHeights);
            }
        }
    }

    std::size_t ElevationTileGrid::getDataSize() const {
        return (_bitmap ? _bitmap->getPixelData().size() : 0) + _nodeHeights.size() * sizeof(float) + sizeof(ElevationTileGrid);
    }

    ColorFormat::ColorFormat ElevationTileGrid::getColorFormat() const {
        return _bitmap ? _bitmap->getColorFormat() : ColorFormat::COLOR_FORMAT_UNSUPPORTED;
    }

    std::array<float, 4> ElevationTileGrid::getDecode() const {
        // The coefficients apply to raw 0..255 bytes; a texture sample arrives normalized. The
        // constant term is NOT put on the alpha channel: a source raster's alpha is not part of any
        // DEM encoding, so it is ignored and the constant travels in its own uniform.
        return { { static_cast<float>(_coeffs[0] * 255.0), static_cast<float>(_coeffs[1] * 255.0), static_cast<float>(_coeffs[2] * 255.0), 0.0f } };
    }

    void ElevationTileGrid::encodeHeight(float height, std::uint8_t* dst) const {
        // Both encodings are POSITIONAL in base 256, so the digits are the base-256 split of the
        // height in the smallest unit. That keeps the carry exact, where a greedy division by each
        // coefficient can round the last digit to 256 and lose a whole quantum when clamped.
        double quantum = (_bytesPerTexel >= 3 ? _coeffs[2] : (_bytesPerTexel >= 2 ? _coeffs[1] : _coeffs[0]));
        long long units = (quantum != 0 ? static_cast<long long>(std::floor((height - _coeffs[3]) / quantum + 0.5)) : 0);
        int digits = std::min(_bytesPerTexel, 3);
        long long maxUnits = 1;
        for (int i = 0; i < digits; i++) {
            maxUnits *= 256;
        }
        units = std::min(maxUnits - 1, std::max(0LL, units));
        for (int i = digits - 1; i >= 0; i--) {
            dst[i] = static_cast<std::uint8_t>(units & 255);
            units >>= 8;
        }
        for (int i = digits; i < _bytesPerTexel; i++) {
            dst[i] = 255; // alpha, which the decode ignores, stays opaque
        }
    }

    float ElevationTileGrid::sampleHeight(double internalX, double internalY) const {
        double boundsWidth = _internalBounds.getMax().getX() - _internalBounds.getMin().getX();
        double boundsHeight = _internalBounds.getMax().getY() - _internalBounds.getMin().getY();
        if (boundsWidth <= 0 || boundsHeight <= 0 || _width < 1 || _height < 1) {
            return 0.0f;
        }

        // Sample positions at pixel centers, bilinear interpolation between them, clamped at edges
        double fx = (internalX - _internalBounds.getMin().getX()) / boundsWidth * _width - 0.5;
        double fy = (internalY - _internalBounds.getMin().getY()) / boundsHeight * _height - 0.5;
        int gx0 = static_cast<int>(std::floor(fx));
        int gy0 = static_cast<int>(std::floor(fy));
        float dx = static_cast<float>(fx - gx0);
        float dy = static_cast<float>(fy - gy0);

        int gx1 = std::min(std::max(gx0 + 1, 0), _width - 1);
        int gy1 = std::min(std::max(gy0 + 1, 0), _height - 1);
        gx0 = std::min(std::max(gx0, 0), _width - 1);
        gy0 = std::min(std::max(gy0, 0), _height - 1);

        float h00 = getHeight(gx0, gy0);
        float h10 = getHeight(gx1, gy0);
        float h01 = getHeight(gx0, gy1);
        float h11 = getHeight(gx1, gy1);
        return (h00 * (1 - dx) + h10 * dx) * (1 - dy) + (h01 * (1 - dx) + h11 * dx) * dy;
    }

    float ElevationTileGrid::sampleNodeHeight(double internalX, double internalY) const {
        if (_nodesPerEdge < 1) {
            return sampleHeight(internalX, internalY);
        }
        double boundsWidth = _internalBounds.getMax().getX() - _internalBounds.getMin().getX();
        double boundsHeight = _internalBounds.getMax().getY() - _internalBounds.getMin().getY();
        if (boundsWidth <= 0 || boundsHeight <= 0) {
            return 0.0f;
        }
        double nx = (internalX - _internalBounds.getMin().getX()) / boundsWidth * _nodesPerEdge;
        double ny = (internalY - _internalBounds.getMin().getY()) / boundsHeight * _nodesPerEdge;
        return ElevationNodeField::sample(_nodeHeights, _nodesPerEdge, nx, ny);
    }

    void ElevationTileGrid::sampleGradient(double internalX, double internalY, float& dhdx, float& dhdy) const {
        double boundsWidth = _internalBounds.getMax().getX() - _internalBounds.getMin().getX();
        double boundsHeight = _internalBounds.getMax().getY() - _internalBounds.getMin().getY();
        dhdx = 0;
        dhdy = 0;
        if (boundsWidth <= 0 || boundsHeight <= 0 || _width < 2 || _height < 2) {
            return;
        }

        double texelX = boundsWidth / _width;
        double texelY = boundsHeight / _height;
        dhdx = static_cast<float>((sampleHeight(internalX + texelX, internalY) - sampleHeight(internalX - texelX, internalY)) / (2 * texelX));
        dhdy = static_cast<float>((sampleHeight(internalX, internalY + texelY) - sampleHeight(internalX, internalY - texelY)) / (2 * texelY));
    }

    std::function<void(int, int, std::uint8_t*)> ElevationTileGrid::makeTexelSampler(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const {
        // Same DEM level, grid size and encoding: the border texel is one of the neighbour's own
        // texels, so it can be copied bit-exactly by index.
        auto sameLevel = [this](const std::shared_ptr<ElevationTileGrid>& grid) {
            // The same grid standing in for a neighbour (both tiles resolved to one ancestor)
            // must NOT be index-copied - that would wrap around to its opposite edge. Nor may a
            // differently encoded one: its bytes mean different heights.
            return grid && grid->_width == _width && grid->_height == _height && grid->_bytesPerTexel == _bytesPerTexel && grid->_coeffs == _coeffs && grid->_tile.getZoom() == _tile.getZoom() && !(grid->_tile == _tile);
        };
        // Coarser ancestor standing in for the neighbour: sample its height field at the border
        // texel centre. Real DEM beats duplicating our own edge texel, which leaves a full-texel
        // height step - tens of metres on a slope - at the tile border.
        double texelX = (_internalBounds.getMax().getX() - _internalBounds.getMin().getX()) / _width;
        double texelY = (_internalBounds.getMax().getY() - _internalBounds.getMin().getY()) / _height;
        // EDGE BOX FILTER: a coarser neighbour interpolates 2^k averages along a shared edge, so
        // averaging this tile's outermost row/column over its footprint makes both sides meet.
        // alongY: the edge runs north-south, so texel ROWS are grouped and fixedIndex is the column.
        auto edgeFilter = [&, this](const std::shared_ptr<ElevationTileGrid>& neighbour, bool alongY, int fixedIndex) -> std::vector<float> {
            std::vector<float> result;
            if (!neighbour || sameLevel(neighbour) || neighbour->_width < 1 || neighbour->_height < 1) {
                return result;
            }
            double ourTexel = alongY ? texelY : texelX;
            double neighbourTexel = alongY
                ? (neighbour->_internalBounds.getMax().getY() - neighbour->_internalBounds.getMin().getY()) / neighbour->_height
                : (neighbour->_internalBounds.getMax().getX() - neighbour->_internalBounds.getMin().getX()) / neighbour->_width;
            if (!(neighbourTexel > ourTexel * 1.5)) {
                return result; // same resolution or finer: this tile is already the smooth side
            }
            double neighbourOrigin = alongY ? neighbour->_internalBounds.getMin().getY() : neighbour->_internalBounds.getMin().getX();
            double ourOrigin = alongY ? _internalBounds.getMin().getY() : _internalBounds.getMin().getX();
            auto groupOf = [&](int i) {
                return static_cast<long long>(std::floor((ourOrigin + (i + 0.5) * ourTexel - neighbourOrigin) / neighbourTexel));
            };
            int count = alongY ? _height : _width;
            result.resize(count);
            for (int i = 0; i < count; ) {
                long long group = groupOf(i);
                int last = i;
                double sum = 0;
                while (last < count && groupOf(last) == group) {
                    sum += alongY ? getHeight(fixedIndex, last) : getHeight(last, fixedIndex);
                    last++;
                }
                float average = static_cast<float>(sum / (last - i));
                for (int j = i; j < last; j++) {
                    result[j] = average;
                }
                i = last;
            }
            return result;
        };
        std::vector<float> westEdge = edgeFilter(neighbours[0], true, 0);
        std::vector<float> eastEdge = edgeFilter(neighbours[1], true, _width - 1);
        std::vector<float> southEdge = edgeFilter(neighbours[2], false, 0);
        std::vector<float> northEdge = edgeFilter(neighbours[3], false, _height - 1);

        // Texel at padded (gx, gy), up to a border's width outside; border texels come from the neighbour
        // that covers them, falling back to edge clamping. Captured BY VALUE - the sampler outlives
        // this call, and the edge filters are the expensive part of it.
        return [this, neighbours, texelX, texelY, westEdge, eastEdge, southEdge, northEdge](int gx, int gy, std::uint8_t* dst) {
            auto sameLevel = [this](const std::shared_ptr<ElevationTileGrid>& grid) {
                return grid && grid->_width == _width && grid->_height == _height && grid->_bytesPerTexel == _bytesPerTexel && grid->_coeffs == _coeffs && grid->_tile.getZoom() == _tile.getZoom() && !(grid->_tile == _tile);
            };
            static const std::array<std::pair<int, int>, 8> DIRS = { {
                { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
            } };
            int dx = (gx < 0 ? -1 : (gx >= _width ? 1 : 0));
            int dy = (gy < 0 ? -1 : (gy >= _height ? 1 : 0));
            if (dx != 0 || dy != 0) {
                for (std::size_t i = 0; i < DIRS.size(); i++) {
                    if (DIRS[i].first != dx || DIRS[i].second != dy) {
                        continue;
                    }
                    const std::shared_ptr<ElevationTileGrid>& neighbour = neighbours[i];
                    if (sameLevel(neighbour)) {
                        int nx = gx - dx * _width;
                        int ny = gy - dy * _height;
                        std::copy_n(neighbour->texel(nx, ny), _bytesPerTexel, dst);
                        return;
                    }
                    if (neighbour) {
                        double px = _internalBounds.getMin().getX() + (gx + 0.5) * texelX;
                        double py = _internalBounds.getMin().getY() + (gy + 0.5) * texelY;
                        encodeHeight(neighbour->sampleHeight(px, py), dst);
                        return;
                    }
                    break;
                }
            }
            // no neighbour data: duplicate our own edge texel
            int cx = std::min(std::max(gx, 0), _width - 1);
            int cy = std::min(std::max(gy, 0), _height - 1);
            // Own texel, but on an edge shared with a coarser neighbour: the box-filtered value.
            // A corner texel lies on two such edges and takes the mean of both, which is what the
            // two neighbours (and the diagonal one between them) average to as well.
            double filtered = 0;
            int filterCount = 0;
            if (!westEdge.empty() && cx == 0) {
                filtered += westEdge[cy];
                filterCount++;
            }
            if (!eastEdge.empty() && cx == _width - 1) {
                filtered += eastEdge[cy];
                filterCount++;
            }
            if (!southEdge.empty() && cy == 0) {
                filtered += southEdge[cx];
                filterCount++;
            }
            if (!northEdge.empty() && cy == _height - 1) {
                filtered += northEdge[cx];
                filterCount++;
            }
            if (filterCount > 0) {
                encodeHeight(static_cast<float>(filtered / filterCount), dst);
                return;
            }
            std::copy_n(texel(cx, cy), _bytesPerTexel, dst);
        };
    }

    ElevationTileGrid::NodeTexelSampler ElevationTileGrid::makeNodeTexelSampler(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const {
        // The same three cases as makeTexelSampler, in metres and for any distance past the
        // edge: a node box reaches half a cell out, not one texel. Per-grid lookups are resolved once here.
        NodeTexelSampler sampler;
        sampler.grid = this;
        sampler.keep = neighbours;
        sampler.texelX = (_internalBounds.getMax().getX() - _internalBounds.getMin().getX()) / _width;
        sampler.texelY = (_internalBounds.getMax().getY() - _internalBounds.getMin().getY()) / _height;
        for (std::size_t i = 0; i < neighbours.size(); i++) {
            const ElevationTileGrid* neighbour = neighbours[i].get();
            sampler.neighbours[i] = neighbour;
            sampler.sameLevel[i] = neighbour && neighbour->_width == _width && neighbour->_height == _height
                                && neighbour->_tile.getZoom() == _tile.getZoom() && !(neighbour->_tile == _tile);
        }
        return sampler;
    }

    float ElevationTileGrid::NodeTexelSampler::operator()(int gx, int gy) const {
        int width = grid->_width, height = grid->_height;
        int dx = (gx < 0 ? -1 : (gx >= width ? 1 : 0));
        int dy = (gy < 0 ? -1 : (gy >= height ? 1 : 0));
        if (dx != 0 || dy != 0) {
            int slot = ElevationNodeField::neighbourSlot(dx, dy);
            const ElevationTileGrid* neighbour = (slot >= 0 ? neighbours[slot] : nullptr);
            if (neighbour) {
                if (sameLevel[slot]) {
                    VT_STAT_INC(demNodeTexelsSameLevel);
                    int nx = std::min(std::max(gx - dx * width, 0), width - 1);
                    int ny = std::min(std::max(gy - dy * height, 0), height - 1);
                    return neighbour->getHeight(nx, ny);
                }
                VT_STAT_INC(demNodeTexelsCoarse);
                double px = grid->_internalBounds.getMin().getX() + (gx + 0.5) * texelX;
                double py = grid->_internalBounds.getMin().getY() + (gy + 0.5) * texelY;
                return neighbour->sampleHeight(px, py);
            }
        }
        VT_STAT_INC(demNodeTexelsOwn);
        return grid->getHeight(std::min(std::max(gx, 0), width - 1), std::min(std::max(gy, 0), height - 1));
    }

    bool ElevationTileGrid::NodeTexelSampler::coarseMapping(int dx, int dy, ElevationNodeField::LatticeMapping& mapping) const {
        int slot = ElevationNodeField::neighbourSlot(dx, dy);
        if (slot < 0) {
            return false;
        }
        const ElevationTileGrid* neighbour = neighbours[slot];
        if (!neighbour || sameLevel[slot] || neighbour->_width < 1 || neighbour->_height < 1) {
            return false; // our own read, a texel-exact copy, or nothing to map onto
        }
        double neighbourWidth = neighbour->_internalBounds.getMax().getX() - neighbour->_internalBounds.getMin().getX();
        double neighbourHeight = neighbour->_internalBounds.getMax().getY() - neighbour->_internalBounds.getMin().getY();
        if (!(neighbourWidth > 0) || !(neighbourHeight > 0)) {
            return false;
        }
        // The composition of operator()'s texel centre and sampleHeight's texel mapping, both affine in gx.
        double neighbourTexelX = neighbourWidth / neighbour->_width;
        double neighbourTexelY = neighbourHeight / neighbour->_height;
        mapping.stepX = texelX / neighbourTexelX;
        mapping.stepY = texelY / neighbourTexelY;
        mapping.originX = (grid->_internalBounds.getMin().getX() + 0.5 * texelX - neighbour->_internalBounds.getMin().getX()) / neighbourTexelX - 0.5;
        mapping.originY = (grid->_internalBounds.getMin().getY() + 0.5 * texelY - neighbour->_internalBounds.getMin().getY()) / neighbourTexelY - 0.5;
        mapping.dimX = neighbour->_width;
        mapping.dimY = neighbour->_height;
        return true;
    }

    float ElevationTileGrid::NodeTexelSampler::neighbourHeight(int dx, int dy, int x, int y) const {
        int slot = ElevationNodeField::neighbourSlot(dx, dy);
        const ElevationTileGrid* neighbour = (slot >= 0 ? neighbours[slot] : nullptr);
        return neighbour ? neighbour->getHeight(x, y) : 0.0f;
    }

    void ElevationTileGrid::buildHeightSat(ElevationNodeField::SummedAreaTable& sat) const {
        if (_width < 1 || _height < 1 || !_pixelData) {
            return; // nodeHeightSat falls back to the per-texel sum on an unbuilt table
        }
        sat.build(_width, _height, [this](int x, int y) { return getHeight(x, y); });
    }

    std::array<int, 4> ElevationTileGrid::edgeBoxScales(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const {
        // How much coarser the W, E, S, N neighbour is, as the power of two its texel is of ours
        // (edgeFilter's rule: coarser means more than 1.5x). Its lattice cell is that much wider,
        // so an edge node's box widens to match what the neighbour interpolates at the same spot.
        double texelX = (_internalBounds.getMax().getX() - _internalBounds.getMin().getX()) / _width;
        double texelY = (_internalBounds.getMax().getY() - _internalBounds.getMin().getY()) / _height;
        std::array<int, 4> scales = { { 1, 1, 1, 1 } };
        for (int side = 0; side < 4; side++) {
            const std::shared_ptr<ElevationTileGrid>& neighbour = neighbours[side];
            if (!neighbour || neighbour->_width < 1 || neighbour->_height < 1) {
                continue;
            }
            bool alongY = side < 2;
            double ourTexel = alongY ? texelY : texelX;
            double neighbourTexel = alongY
                ? (neighbour->_internalBounds.getMax().getY() - neighbour->_internalBounds.getMin().getY()) / neighbour->_height
                : (neighbour->_internalBounds.getMax().getX() - neighbour->_internalBounds.getMin().getX()) / neighbour->_width;
            int scale = 1;
            while (neighbourTexel > ourTexel * scale * 1.5 && scale < 64) {
                scale *= 2;
            }
            scales[side] = scale;
        }
        return scales;
    }

    float ElevationTileGrid::nodeTexelHeight(int i, int j, const std::array<int, 4>& edgeScales, const NodeTexelSampler& texel,
                                             const ElevationNodeField::SummedAreaTable& sat) const {
        int n = _nodesPerEdge;
        int boxX = ElevationNodeField::boxTexels(_width, n, _boxCells);
        int boxY = ElevationNodeField::boxTexels(_height, n, _boxCells);
        // A node whose box stays inside the grid is the field's own value; one that reaches out
        // is redone with the neighbours.
        bool edge = ElevationNodeField::boxReachesOutside(i, _width, n, boxX) || ElevationNodeField::boxReachesOutside(j, _height, n, boxY);
        if (!edge) {
            return _nodeHeights[static_cast<std::size_t>(j) * (n + 1) + i];
        }
        int scale = 1;
        if (i == 0) scale = std::max(scale, edgeScales[0]);
        if (i == n) scale = std::max(scale, edgeScales[1]);
        if (j == 0) scale = std::max(scale, edgeScales[2]);
        if (j == n) scale = std::max(scale, edgeScales[3]);
        boxX *= scale;
        boxY *= scale;
        VT_STAT_INC(demNodeEdgeCalls);
        VT_STAT_ADD(demNodeBoxTexels, static_cast<long long>(boxX) * boxY);
        double cx = static_cast<double>(i) * _width / n;
        double cy = static_cast<double>(j) * _height / n;
        // Per region: own full-weight texels from the prefix sums, a coarser neighbour's band in
        // closed form, the rest per texel.
        return ElevationNodeField::nodeHeightRegions(cx, cy, boxX, boxY, _width, _height, sat, texel,
            [&texel](int dx, int dy, ElevationNodeField::LatticeMapping& mapping) { return texel.coarseMapping(dx, dy, mapping); },
            [&texel](int dx, int dy, int x, int y) { return texel.neighbourHeight(dx, dy, x, y); });
    }

    void ElevationTileGrid::encodeNodeTexture(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, std::vector<std::uint8_t>& textureData) const {
        int n = _nodesPerEdge;
        if (n < 1) {
            textureData.clear();
            return;
        }
        int stride = n + 1;
        textureData.resize(static_cast<std::size_t>(stride) * stride * _bytesPerTexel);
        NodeTexelSampler texel = makeNodeTexelSampler(neighbours);
        std::array<int, 4> scales = edgeBoxScales(neighbours);
        ElevationNodeField::SummedAreaTable sat;
        buildHeightSat(sat);
        std::size_t s = 0;
        for (int j = 0; j <= n; j++) {
            for (int i = 0; i <= n; i++, s += _bytesPerTexel) {
                encodeHeight(nodeTexelHeight(i, j, scales, texel, sat), &textureData[s]);
            }
        }
    }

    void ElevationTileGrid::encodeNodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, BorderStrips& strips) const {
        int n = _nodesPerEdge;
        if (n < 1) {
            return;
        }
        NodeTexelSampler texel = makeNodeTexelSampler(neighbours);
        std::array<int, 4> scales = edgeBoxScales(neighbours);
        ElevationNodeField::SummedAreaTable sat;
        buildHeightSat(sat);
        std::size_t bytes = static_cast<std::size_t>(n + 1) * _bytesPerTexel;
        strips.south.resize(bytes);
        strips.north.resize(bytes);
        strips.west.resize(bytes);
        strips.east.resize(bytes);
        for (int k = 0; k <= n; k++) {
            std::size_t s = static_cast<std::size_t>(k) * _bytesPerTexel;
            encodeHeight(nodeTexelHeight(k, 0, scales, texel, sat), &strips.south[s]);
            encodeHeight(nodeTexelHeight(k, n, scales, texel, sat), &strips.north[s]);
            encodeHeight(nodeTexelHeight(0, k, scales, texel, sat), &strips.west[s]);
            encodeHeight(nodeTexelHeight(n, k, scales, texel, sat), &strips.east[s]);
        }
    }

    int ElevationTileGrid::getTextureBorderTexels(double reachMetres) const {
        if (!(reachMetres > 0) || _width < 1) {
            return 1;
        }
        double texelMetres = (_internalBounds.getMax().getX() - _internalBounds.getMin().getX()) / _width * Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE;
        if (!(texelMetres > 0)) {
            return 1;
        }
        // The tap is a bilinear read, so the texel PAST its reach is sampled too.
        int border = static_cast<int>(std::ceil(reachMetres / texelMetres)) + 1;
        return std::max(1, std::min(border, std::min(MAX_TEXTURE_BORDER_TEXELS, std::min(_width, _height))));
    }

    void ElevationTileGrid::encodeTextureWithBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, int border, std::vector<std::uint8_t>& textureData) const {
        border = std::max(1, border);
        int paddedWidth = _width + 2 * border;
        int paddedHeight = _height + 2 * border;
        textureData.resize(static_cast<std::size_t>(paddedWidth) * paddedHeight * _bytesPerTexel);

        std::function<void(int, int, std::uint8_t*)> texelValue = makeTexelSampler(neighbours);

        // Only the border ring and the two outermost own rows/columns come from elsewhere (a
        // coarser neighbour box-filters them); the rest is this grid's own texel at its own index,
        // so a whole row is one memcpy - it replaced a per-texel re-encode worth 4.3 ms a tile.
        std::size_t i = 0;
        for (int gy = -border; gy < _height + border; gy++) {
            bool ownRow = (gy > 0 && gy < _height - 1);
            for (int gx = -border; gx < _width + border; gx++) {
                if (ownRow && gx == 1) {
                    // The row's own span, straight out of the source raster.
                    std::size_t span = static_cast<std::size_t>(_width - 2) * _bytesPerTexel;
                    std::copy_n(texel(1, gy), span, &textureData[i]);
                    i += span;
                    gx = _width - 1;
                }
                texelValue(gx, gy, &textureData[i]);
                i += _bytesPerTexel;
            }
        }
    }

    void ElevationTileGrid::encodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, int border, BorderStrips& strips) const {
        border = std::max(1, border);
        int paddedWidth = _width + 2 * border;
        int paddedHeight = _height + 2 * border;
        int thickness = border + 1; // the ring, and the own outermost row/column a coarser neighbour filters

        std::function<void(int, int, std::uint8_t*)> texelValue = makeTexelSampler(neighbours);

        // South and north: full-width rows (gy = -border .. 0 and height - 1 .. height + border - 1).
        strips.south.resize(static_cast<std::size_t>(paddedWidth) * thickness * _bytesPerTexel);
        strips.north.resize(static_cast<std::size_t>(paddedWidth) * thickness * _bytesPerTexel);
        for (int row = 0; row < thickness; row++) {
            std::size_t s = static_cast<std::size_t>(row) * paddedWidth * _bytesPerTexel;
            for (int gx = -border; gx < _width + border; gx++, s += _bytesPerTexel) {
                texelValue(gx, -border + row, &strips.south[s]);
                texelValue(gx, _height - 1 + row, &strips.north[s]);
            }
        }
        // West and east: full-height columns (gx = -border .. 0 and width - 1 .. width + border - 1).
        strips.west.resize(static_cast<std::size_t>(paddedHeight) * thickness * _bytesPerTexel);
        strips.east.resize(static_cast<std::size_t>(paddedHeight) * thickness * _bytesPerTexel);
        for (int gy = -border; gy < _height + border; gy++) {
            std::size_t s = static_cast<std::size_t>(gy + border) * thickness * _bytesPerTexel;
            for (int col = 0; col < thickness; col++) {
                texelValue(-border + col, gy, &strips.west[s + col * _bytesPerTexel]);
                texelValue(_width - 1 + col, gy, &strips.east[s + col * _bytesPerTexel]);
            }
        }
    }

    std::shared_ptr<ElevationTileGrid> ElevationTileGrid::DecodeBitmap(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs, int nodesPerEdge, int boxCells) {
        if (!bitmap) {
            return std::shared_ptr<ElevationTileGrid>();
        }

        int width = bitmap->getWidth();
        int height = bitmap->getHeight();
        if (width < 1 || height < 1) {
            return std::shared_ptr<ElevationTileGrid>();
        }

        switch (bitmap->getColorFormat()) {
        case ColorFormat::COLOR_FORMAT_GRAYSCALE:
        case ColorFormat::COLOR_FORMAT_RGB:
        case ColorFormat::COLOR_FORMAT_RGBA:
            break;
        default:
            Log::Error("ElevationTileGrid::DecodeBitmap: Unsupported bitmap color format");
            return std::shared_ptr<ElevationTileGrid>();
        }

        // Bitmap pixel data rows are stored bottom-up relative to the image, which means
        // row 0 of the pixel data corresponds to the southern (minimum y) edge of the tile.
        // This matches the grid row order, so the raster can be used as it is.
        auto grid = std::make_shared<ElevationTileGrid>(tile, internalBounds, bitmap, coeffs, nodesPerEdge, boxCells);
        if (grid->getMinHeight() < -12000.0f || grid->getMaxHeight() > 10000.0f) {
            Log::Warnf("ElevationTileGrid::DecodeBitmap: Implausible elevation range %g..%g m for tile %d/%d/%d - check that the elevation data source encoding ('terrarium'/'mapbox') matches the data",
                       grid->getMinHeight(), grid->getMaxHeight(), tile.getZoom(), tile.getX(), tile.getY());
        }
        return grid;
    }
}
