/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONTILEGRID_H_
#define _MASSIF_ELEVATIONTILEGRID_H_

#include "core/MapTile.h"
#include "terrain/ElevationNodeField.h"
#include "core/MapBounds.h"
#include "graphics/Bitmap.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace massif {

    /**
     * A single decoded DEM tile, kept as the encoded source raster and decoded on the fly (tangram's model),
     * so nothing is re-quantised and the GPU texture is a copy of the same texels. Rows are south-to-north
     * (Bitmap row order). Internal class, not exposed in the public API.
     */
    class ElevationTileGrid {
    public:
        ElevationTileGrid(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs, int nodesPerEdge, int boxCells);

        const MapTile& getTile() const { return _tile; }
        const MapBounds& getInternalBounds() const { return _internalBounds; }
        int getWidth() const { return _width; }
        int getHeight() const { return _height; }
        float getMinHeight() const { return _minHeight; }
        float getMaxHeight() const { return _maxHeight; }
        std::size_t getDataSize() const;

        /** The texture built from this grid has the source raster's own format. */
        ColorFormat::ColorFormat getColorFormat() const;
        int getBytesPerTexel() const { return _bytesPerTexel; }

        /**
         * Bilinearly sampled elevation in meters at the given internal coordinates.
         * Coordinates are clamped to the grid bounds.
         */
        float sampleHeight(double internalX, double internalY) const;
        /**
         * The drawn surface's height: a bilinear sample of the node field, for queries that must agree with the
         * ground (sampleHeight without nodes). Edge nodes clamp here where the GPU texture reads the neighbour,
         * so the two differ slightly along a DEM tile edge only.
         */
        float sampleNodeHeight(double internalX, double internalY) const;
        /** Mesh nodes per grid edge the node field was built for, 0 for none. */
        int getNodesPerEdge() const { return _nodesPerEdge; }
        /**
         * This decode's identity, unique for the process. A re-decode keeps the tile id, so caches
         * compare this to notice a replaced grid (e.g. after setSurfaceResolution).
         */
        unsigned long long getSerial() const { return _serial; }
        /**
         * Elevation gradient (dh/dx, dh/dy) in meters per internal unit at the given internal coordinates.
         */
        void sampleGradient(double internalX, double internalY, float& dhdx, float& dhdy) const;

        /**
         * The shader decode: meters = dot(sample, decode) + getDecodeOffset(), coefficients scaled by 255 for a
         * normalized sample; the offset is separate so the raster's alpha is ignored. Linear, so a LINEAR
         * texture sample matches sampleHeight exactly.
         */
        std::array<float, 4> getDecode() const;
        float getDecodeOffset() const { return static_cast<float>(_coeffs[3]); }

        /**
         * Copies the raster into a texture padded by 'border' texels (>= 1; a tap N out needs N + 1) from the
         * neighbours (W, E, S, N, SW, SE, NW, NE): same-level texel-exactly, coarser resampled, missing clamped.
         * Unlike tangram's unpadded raster, this keeps adjacent tiles from showing a ridge at every border.
         */
        void encodeTextureWithBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, int border, std::vector<std::uint8_t>& textureData) const;

        /**
         * The border keeping taps reaching 'reachMetres' (equator metres) inside real data: 1 for a
         * reach of 0, capped at MAX_TEXTURE_BORDER_TEXELS and the grid size.
         */
        int getTextureBorderTexels(double reachMetres) const;
        static constexpr int MAX_TEXTURE_BORDER_TEXELS = 32;

        /**
         * The (border + 1)-texel strips a neighbour landing can change (the ring plus our outermost row/column),
         * patched in instead of rebuilding the texture. South/north are (width + 2 * border) x (border + 1),
         * west/east (border + 1) x (height + 2 * border); they overlap and agree at the corners.
         */
        struct BorderStrips {
            std::vector<std::uint8_t> south, north, west, east;
        };
        void encodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, int border, BorderStrips& strips) const;

        /**
         * The node field as a texture in this grid's encoding, (nodes + 1)^2 texels, rows south-to-north. Edge
         * nodes are recomputed from the neighbours (as the border texels are) so both tiles get the same value;
         * against a coarser neighbour the box widens to its cell to meet its lattice.
         */
        void encodeNodeTexture(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, std::vector<std::uint8_t>& textureData) const;
        /**
         * The four edge rows/columns of the node texture, the only texels a neighbour landing can change.
         * South/north are (nodes + 1) x 1, west/east 1 x (nodes + 1).
         */
        void encodeNodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, BorderStrips& strips) const;

        /**
         * Wraps a DEM bitmap in a grid with the given color coefficients; null for an unsupported format.
         * nodesPerEdge is the node field's lattice (0 = none), boxCells the cells a node averages.
         */
        static std::shared_ptr<ElevationTileGrid> DecodeBitmap(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs, int nodesPerEdge, int boxCells);

    private:
        // Built once per encode and shared, so the full copy and the border patch cannot disagree.
        std::function<void(int, int, std::uint8_t*)> makeTexelSampler(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const;

        const std::uint8_t* texel(int gx, int gy) const {
            return &_pixelData[(static_cast<std::size_t>(gy) * _width + gx) * _bytesPerTexel];
        }

        float decodeTexel(const std::uint8_t* p) const {
            double h = _coeffs[3];
            for (int i = 0; i < _bytesPerTexel && i < 3; i++) {
                h += _coeffs[i] * p[i];
            }
            return static_cast<float>(h);
        }

        // The inverse of decodeTexel for resampled border texels: both encodings are base-256 positional,
        // so a greedy division by the coefficients, largest first, gives the digits.
        void encodeHeight(float height, std::uint8_t* dst) const;

        float getHeight(int gx, int gy) const { return decodeTexel(texel(gx, gy)); }

        // A concrete functor: std::function indirection dominated the edge box read cost.
        struct NodeTexelSampler {
            const ElevationTileGrid* grid;
            std::array<std::shared_ptr<ElevationTileGrid>, 8> keep; // holds the neighbours alive
            std::array<const ElevationTileGrid*, 8> neighbours;
            std::array<bool, 8> sameLevel;
            double texelX, texelY;

            float operator()(int gx, int gy) const;

            // True only for an existing coarser neighbour, the one case summed in closed form.
            bool coarseMapping(int dx, int dy, ElevationNodeField::LatticeMapping& mapping) const;
            // That neighbour's own texel, for the closed form's corners.
            float neighbourHeight(int dx, int dy, int x, int y) const;
        };

        // The field's own value inside, a box over 'texel' on an edge, widened by the edge's scale.
        float nodeTexelHeight(int i, int j, const std::array<int, 4>& edgeScales, const NodeTexelSampler& texel,
                              const ElevationNodeField::SummedAreaTable& sat) const;

        NodeTexelSampler makeNodeTexelSampler(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const;

        // Prefix sums over this grid's texels for the node boxes; built per encode, not cached (MBs each).
        void buildHeightSat(ElevationNodeField::SummedAreaTable& sat) const;

        // How much coarser each neighbour (W, E, S, N) is, as a power of two (1 = not coarser).
        std::array<int, 4> edgeBoxScales(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const;

        const unsigned long long _serial;
        const MapTile _tile;
        const MapBounds _internalBounds;
        const std::shared_ptr<Bitmap> _bitmap;
        const std::uint8_t* _pixelData;
        const std::array<double, 4> _coeffs;
        int _width;
        int _height;
        int _bytesPerTexel;
        float _minHeight;
        float _maxHeight;
        int _nodesPerEdge;
        int _boxCells;
        std::vector<float> _nodeHeights; // (_nodesPerEdge + 1)^2, row-major, rows south-to-north
    };
}

#endif
