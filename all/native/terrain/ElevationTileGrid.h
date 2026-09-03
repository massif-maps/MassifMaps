/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONTILEGRID_H_
#define _MASSIF_ELEVATIONTILEGRID_H_

#include "core/MapTile.h"
#include "core/MapBounds.h"
#include "graphics/Bitmap.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace massif {

    /**
     * A single decoded DEM tile.
     *
     * The grid IS the source raster: the encoded tile bitmap (mapbox or terrarium RGB) is kept as
     * it arrived and every height is decoded from it on the fly, which is tangram's model
     * (util/elevationManager.cpp reads its elevation straight out of the raster's texture buffer).
     * Nothing is re-quantised, so the height field has exactly the precision the data source
     * offers - 1/256m for terrarium, 0.1m for mapbox - and the GPU texture is a copy of the same
     * texels, decoded in the shader with the source's own coefficients.
     *
     * Grid rows are stored south-to-north (row 0 corresponds to the minimum internal y), which is
     * the Bitmap row order.
     * Internal class, not exposed in the public API.
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
         * The height the terrain SURFACE has at the given internal coordinates: a bilinear sample
         * of the node field (ElevationNodeField), which is the DEM box-filtered to the mesh cell.
         * This is what a query that must agree with the drawn ground asks - a label anchor, an
         * extrusion base, the raycast. sampleHeight is the DEM itself, which the surface cannot
         * carry. Falls back to sampleHeight on a grid built without nodes.
         * The node field is this grid's own: its edge nodes clamp at the tile border where the
         * GPU texture reads the neighbour, so the two can differ by a fraction of a texel step
         * along a DEM tile edge and nowhere else.
         */
        float sampleNodeHeight(double internalX, double internalY) const;
        /** Mesh nodes per grid edge the node field was built for, 0 for none. */
        int getNodesPerEdge() const { return _nodesPerEdge; }
        /**
         * Elevation gradient (dh/dx, dh/dy) in meters per internal unit at the given internal coordinates.
         */
        void sampleGradient(double internalX, double internalY, float& dhdx, float& dhdy) const;

        /**
         * The decode the shader applies to a texture sample: meters = dot(sample, decode) +
         * getDecodeOffset(). The source coefficients apply to the raw 0..255 byte values, so they
         * are scaled by 255 for the normalized texture sample; the constant term is handed over
         * separately rather than riding on the alpha channel, so a source raster's alpha is
         * ignored rather than trusted.
         * The mapping is linear in every channel, so bilinear texture filtering commutes with
         * decoding and a GPU CLAMP_TO_EDGE + LINEAR sample at
         * uv = (pos - internalBounds.min) / internalBounds.size matches sampleHeight exactly.
         */
        std::array<float, 4> getDecode() const;
        float getDecodeOffset() const { return static_cast<float>(_coeffs[3]); }

        /**
         * Copies the source raster into a texture padded with a 1-texel border taken from the
         * neighbouring grids (order: W, E, S, N, SW, SE, NW, NE). Same-level neighbours are copied
         * TEXEL-EXACTLY (the raw encoded bytes, so no round trip through metres); coarser
         * (ancestor) neighbour grids are sampled at the border texel centers and re-encoded, which
         * still gives real DEM data across the tile border. Only missing neighbours fall back to
         * duplicating this grid's edge texels.
         * Adjacent tiles then interpolate across the border from IDENTICAL texel pairs, making
         * same-level tile borders seam-free. The padded texture covers the grid bounds extended by
         * one texel on each side.
         * This padding is the one place this deliberately does more than tangram, which samples the
         * raster unpadded and extrapolates at the edges: without it, adjacent DEM tiles disagree
         * within the outermost half texel and the terrain shows a ridge along every tile border.
         */
        void encodeTextureWithBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, std::vector<std::uint8_t>& textureData) const;

        /**
         * The four 2-texel-thick strips of the padded texture that depend on the NEIGHBOURS:
         * the border ring itself, plus this grid's own outermost row/column, which a coarser
         * neighbour box-filters (see encodeTextureWithBorders). Everything else in the texture
         * comes from this grid alone and cannot change when a neighbour arrives.
         *
         * A neighbour landing is by far the most common reason to rebuild a border - during a pan
         * it is continuous - and rebuilding the whole texture for a 2-texel ring is most of what
         * the elevation texture pipeline costs. Patching these strips into the existing texture is
         * the same result for ~1.5% of the texels.
         *
         * Strip layout, rows south-to-north and columns west-to-east, as in the padded texture:
         * south/north are (width + 2) x 2, west/east are 2 x (height + 2). Corners are covered by
         * south and north, so the strips overlap there and agree.
         */
        struct BorderStrips {
            std::vector<std::uint8_t> south, north, west, east;
        };
        void encodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, BorderStrips& strips) const;

        /**
         * The node field as a texture in this grid's own encoding: (nodes + 1)^2 texels, texel
         * (i, j) = node (i, j), rows south-to-north like the raster. The interior is the field
         * built at decode time; the EDGE nodes are recomputed here because their box reaches half
         * a cell into the neighbour (same rule as the border texels above: a same-level neighbour
         * texel-exactly, a coarser one sampled at the texel centre, none at all clamped), so the
         * two tiles sharing an edge compute the same node from the same texels. On an edge shared
         * with a coarser neighbour the box is widened to that neighbour's cell, so our node meets
         * the value its lattice interpolates there - the node-field form of the edge box filter.
         */
        void encodeNodeTexture(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, std::vector<std::uint8_t>& textureData) const;
        /**
         * The four edge rows/columns of the node texture, (nodes + 1) texels each, which are the
         * only node texels a neighbour landing can change. south/north are (nodes + 1) x 1,
         * west/east 1 x (nodes + 1).
         */
        void encodeNodeTextureBorders(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours, BorderStrips& strips) const;

        /**
         * Wraps a DEM bitmap (mapbox/terrarium RGB encoded) in an elevation grid using the given
         * color component coefficients. Returns null if the bitmap has an unsupported format.
         * nodesPerEdge is the mesh lattice this grid's node field is built for (0 = none), boxCells
         * how many of its cells a node averages (ElevationNodeField::DEFAULT_BOX_CELLS).
         */
        static std::shared_ptr<ElevationTileGrid> DecodeBitmap(const MapTile& tile, const MapBounds& internalBounds, const std::shared_ptr<Bitmap>& bitmap, const std::array<double, 4>& coeffs, int nodesPerEdge, int boxCells);

    private:
        // The padded texture's texel at (gx, gy), gx in [-1, width] and gy in [-1, height], written
        // into 'dst': this grid's own texel, a neighbour's, or a box-filtered edge value. Built
        // once per encode because the edge filters it needs are O(width + height) to compute; both
        // the full copy and the border patch go through it, so they cannot disagree.
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

        // The inverse of decodeTexel, for the border texels that have to be RESAMPLED from a
        // coarser neighbour rather than copied. Both supported encodings are positional in base
        // 256 (terrarium 256, 1, 1/256; mapbox 25.6, 0.1 with a x256 head), so the digits come out
        // of a plain greedy division by the coefficients, largest first.
        void encodeHeight(float height, std::uint8_t* dst) const;

        float getHeight(int gx, int gy) const { return decodeTexel(texel(gx, gy)); }

        // Height of node (i, j) for the node TEXTURE: the field's own value inside, a box over
        // 'texel' (which answers outside the grid) on an edge, widened by the edge's scale.
        template <typename TexelFn>
        float nodeTexelHeight(int i, int j, const std::array<int, 4>& edgeScales, const TexelFn& texel) const;
        // Neighbour texel access in metres for the node boxes, and how much coarser each
        // neighbour (W, E, S, N) is than this grid, as a power of two (1 = not coarser).
        std::function<float(int, int)> makeNodeTexelSampler(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const;
        std::array<int, 4> edgeBoxScales(const std::array<std::shared_ptr<ElevationTileGrid>, 8>& neighbours) const;

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
