#include "ElevationTileGrid.h"
#include "graphics/Bitmap.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>

namespace massif {

    ElevationTileGrid::ElevationTileGrid(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs) :
        _tile(tile),
        _internalBounds(internalBounds),
        _bitmap(bitmap),
        _pixelData(bitmap ? bitmap->getPixelData().data() : nullptr),
        _coeffs(coeffs),
        _width(bitmap ? bitmap->getWidth() : 0),
        _height(bitmap ? bitmap->getHeight() : 0),
        _bytesPerTexel(bitmap ? bitmap->getBytesPerPixel() : 0),
        _minHeight(0),
        _maxHeight(0)
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
        }
    }

    std::size_t ElevationTileGrid::getDataSize() const {
        return (_bitmap ? _bitmap->getPixelData().size() : 0) + sizeof(ElevationTileGrid);
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
        // Both supported encodings are POSITIONAL in base 256 - terrarium (256, 1, 1/256, -32768)
        // and mapbox (6553.6, 25.6, 0.1, -10000) - so the digits are the base-256 split of the
        // height in the smallest unit. Splitting that way rather than dividing greedily by each
        // coefficient in turn keeps the carry exact: a greedy split can leave a remainder that
        // rounds the last digit to 256, and clamping it there loses a whole quantum.
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
        // Different level (a coarser ancestor grid stands in for the neighbour): sample the
        // neighbour's height field at the geographic position of the border texel center.
        // Real DEM data at the tile edge beats duplicating our own edge texel, which leaves
        // a full-texel height step (tens of meters on a slope) at the tile border.
        double texelX = (_internalBounds.getMax().getX() - _internalBounds.getMin().getX()) / _width;
        double texelY = (_internalBounds.getMax().getY() - _internalBounds.getMin().getY()) / _height;
        // EDGE BOX FILTER. A coarser neighbour interpolates 2^k averages along a shared edge while
        // this tile interpolates its own texels; backfill alone leaves half the local detail as a
        // dotted speckle line. Averaging this tile's outermost row/column over the neighbour's
        // footprint makes both sides meet on the same value. Only towards a coarser neighbour, and
        // groups are found geographically so an unaligned one degrades to a no-op.
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

        // Texel at padded coordinates (gx, gy in [-1, width/height]); border texels come from the
        // neighbour that actually covers them, falling back to edge clamping.
        // Captured BY VALUE: the sampler outlives this call, and the edge filters are the
        // expensive part of it.
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

    void ElevationTileGrid::encodeTextureWithBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, std::vector<std::uint8_t>& textureData) const {
        int paddedWidth = _width + 2;
        int paddedHeight = _height + 2;
        textureData.resize(static_cast<std::size_t>(paddedWidth) * paddedHeight * _bytesPerTexel);

        std::function<void(int, int, std::uint8_t*)> texelValue = makeTexelSampler(neighbours);

        // Only the border ring and the two outermost own rows/columns can come from anywhere but
        // this grid: the border ring by definition, the outermost own texels because a coarser
        // neighbour box-filters them (edgeFilter above). Everything else is this grid's own texel
        // at its own index, and a whole row of those is one memcpy - the copy is what replaced the
        // per-texel re-encode this used to do (measured 4.3ms a tile on the encode worker).
        std::size_t i = 0;
        for (int gy = -1; gy <= _height; gy++) {
            bool ownRow = (gy > 0 && gy < _height - 1);
            for (int gx = -1; gx <= _width; gx++) {
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

    void ElevationTileGrid::encodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, BorderStrips& strips) const {
        int paddedWidth = _width + 2;
        int paddedHeight = _height + 2;

        std::function<void(int, int, std::uint8_t*)> texelValue = makeTexelSampler(neighbours);

        // South and north: two full-width rows each (gy = -1, 0 and height-1, height).
        strips.south.resize(static_cast<std::size_t>(paddedWidth) * 2 * _bytesPerTexel);
        strips.north.resize(static_cast<std::size_t>(paddedWidth) * 2 * _bytesPerTexel);
        for (int row = 0; row < 2; row++) {
            std::size_t s = static_cast<std::size_t>(row) * paddedWidth * _bytesPerTexel;
            for (int gx = -1; gx <= _width; gx++, s += _bytesPerTexel) {
                texelValue(gx, -1 + row, &strips.south[s]);
                texelValue(gx, _height - 1 + row, &strips.north[s]);
            }
        }
        // West and east: two full-height columns each (gx = -1, 0 and width-1, width).
        strips.west.resize(static_cast<std::size_t>(paddedHeight) * 2 * _bytesPerTexel);
        strips.east.resize(static_cast<std::size_t>(paddedHeight) * 2 * _bytesPerTexel);
        for (int gy = -1; gy <= _height; gy++) {
            std::size_t s = static_cast<std::size_t>(gy + 1) * 2 * _bytesPerTexel;
            for (int col = 0; col < 2; col++) {
                texelValue(-1 + col, gy, &strips.west[s + col * _bytesPerTexel]);
                texelValue(_width - 1 + col, gy, &strips.east[s + col * _bytesPerTexel]);
            }
        }
    }

    std::shared_ptr<ElevationTileGrid> ElevationTileGrid::DecodeBitmap(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs) {
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
        auto grid = std::make_shared<ElevationTileGrid>(tile, internalBounds, bitmap, coeffs);
        if (grid->getMinHeight() < -12000.0f || grid->getMaxHeight() > 10000.0f) {
            Log::Warnf("ElevationTileGrid::DecodeBitmap: Implausible elevation range %g..%g m for tile %d/%d/%d - check that the elevation data source encoding ('terrarium'/'mapbox') matches the data",
                       grid->getMinHeight(), grid->getMaxHeight(), tile.getZoom(), tile.getX(), tile.getY());
        }
        return grid;
    }
}
