/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONNODEFIELD_H_
#define _MASSIF_ELEVATIONNODEFIELD_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace massif {

    /**
     * The height field the terrain SURFACE stands on: one height per mesh node, each the mean of
     * the DEM over the node's own cell. The surface mesh is a regular lattice of
     * TerrainOptions::MeshResolution cells per tile; a lidar-grade DEM carries relief far finer
     * than that (a road's cut and fill in 0.8 m texels under a 6.7 m cell), and a lattice that
     * samples such a DEM point by point aliases it - every road edge came out as a sawtooth at a
     * grazing tilt. Averaging over the cell is the prefilter that removes what the lattice cannot
     * carry; the per-fragment shading keeps the full DEM.
     * Free of the grid and of GL on purpose, so the host tests reach it. See
     * docs/internals/rendering/04-terrain.md, "The node texture".
     */
    struct ElevationNodeField {
        /**
         * How many mesh cells the box spans. Two, not one: a one-cell box removes what the
         * lattice cannot sample but leaves a road's cut as a full step within one cell, and a
         * step of H over one cell is drawn as a staircase of H/2 at a grazing tilt. Measured on
         * the Grenoble z15 DEM under a 64-cell mesh, the node field's roughness (p95 of the
         * cell Laplacian) is 5.0 m unfiltered, 3.75 m at one cell, 2.36 m at two, 1.20 m at
         * four; two is where the staircase stopped reading as one on screen. Wider trades real
         * relief for it. Measurement override: adb shell setprop debug.massif.nodebox <cells>.
         */
        static constexpr int DEFAULT_BOX_CELLS = 2;

        /**
         * Box width in texels for `nodes` cells across a `width`-texel raster: `cells` mesh
         * cells, so nothing narrower than that survives into a node. 1 when the raster is coarser
         * than the box, where the box is a plain bilinear sample.
         */
        static int boxTexels(int width, int nodes, int cells) {
            return std::max(1, std::max(1, cells) * width / std::max(1, nodes));
        }

        /**
         * Whether the box of node i (of `nodes`, over `width` texels) reaches past the raster:
         * such a node reads the neighbour tile, and the GPU texture recomputes it with one.
         */
        static bool boxReachesOutside(int i, int width, int nodes, int box) {
            double c = static_cast<double>(i) * width / std::max(1, nodes);
            return c - 0.5 * box < 0 || c + 0.5 * box > width;
        }

        /**
         * Area weights of the interval [a, a + box) over unit texel cells [t, t + 1): fully
         * covered cells weigh 1, the two end cells their overlap. Sums to box. A box centred on a
         * texel boundary with an even width is the plain block; an odd one (or a node between
         * boundaries) takes half of each end cell, which is what keeps the mean centred on the
         * node instead of half a texel off it.
         * @return The first texel index; weights[i] is the weight of texel first + i.
         */
        static int boxWeights(double a, int box, std::vector<float>& weights) {
            int first = static_cast<int>(std::floor(a));
            int last = static_cast<int>(std::ceil(a + box)) - 1;
            weights.clear();
            for (int t = first; t <= last; t++) {
                double w = std::min(static_cast<double>(t + 1), a + box) - std::max(static_cast<double>(t), a);
                weights.push_back(static_cast<float>(std::max(0.0, w)));
            }
            return first;
        }

        /**
         * Mean height over the boxX x boxY texel block centred on texel-space position (cx, cy).
         * `texel(tx, ty)` must answer OUTSIDE the raster too - a neighbour's texel, or a clamped
         * one - because a node on the tile edge reaches half a box into the next tile. Two tiles
         * computing their shared edge node from the same texels get the same height, which is
         * what keeps the surface seam-free.
         */
        template <typename TexelFn>
        static float nodeHeight(double cx, double cy, int boxX, int boxY, const TexelFn& texel) {
            std::vector<float> wx, wy;
            int firstX = boxWeights(cx - 0.5 * boxX, boxX, wx);
            int firstY = boxWeights(cy - 0.5 * boxY, boxY, wy);
            double sum = 0;
            for (std::size_t j = 0; j < wy.size(); j++) {
                if (wy[j] <= 0) {
                    continue;
                }
                double row = 0;
                for (std::size_t i = 0; i < wx.size(); i++) {
                    if (wx[i] > 0) {
                        row += wx[i] * texel(firstX + static_cast<int>(i), firstY + static_cast<int>(j));
                    }
                }
                sum += wy[j] * row;
            }
            return static_cast<float>(sum / (static_cast<double>(boxX) * boxY));
        }

        /**
         * Every node of an N-cell lattice over a width x height raster, row-major, row j at
         * texel-space y = j * height / N, (N + 1)^2 values. Node (i, j) sits on the cell corner
         * (i * width / N, j * height / N): node 0 is the tile's west/south EDGE, node N its
         * east/north edge, so adjacent tiles share their edge nodes.
         */
        template <typename TexelFn>
        static void build(int width, int height, int nodes, int cells, const TexelFn& texel, std::vector<float>& out) {
            int boxX = boxTexels(width, nodes, cells);
            int boxY = boxTexels(height, nodes, cells);
            out.resize(static_cast<std::size_t>(nodes + 1) * (nodes + 1));
            for (int j = 0; j <= nodes; j++) {
                double cy = static_cast<double>(j) * height / nodes;
                for (int i = 0; i <= nodes; i++) {
                    double cx = static_cast<double>(i) * width / nodes;
                    out[static_cast<std::size_t>(j) * (nodes + 1) + i] = nodeHeight(cx, cy, boxX, boxY, texel);
                }
            }
        }

        /**
         * Bilinear sample of a node field at lattice coordinates (nx, ny) in [0, nodes], clamped.
         * Between nodes this is exactly what the GPU draws: the surface vertices ARE the nodes at
         * the nominal zoom, and an overzoomed tile's vertices interpolate the same field.
         */
        static float sample(const std::vector<float>& field, int nodes, double nx, double ny) {
            if (nodes < 1 || field.size() < static_cast<std::size_t>(nodes + 1) * (nodes + 1)) {
                return 0.0f;
            }
            double fx = std::min(std::max(nx, 0.0), static_cast<double>(nodes));
            double fy = std::min(std::max(ny, 0.0), static_cast<double>(nodes));
            int i0 = std::min(static_cast<int>(std::floor(fx)), nodes - 1);
            int j0 = std::min(static_cast<int>(std::floor(fy)), nodes - 1);
            float dx = static_cast<float>(fx - i0);
            float dy = static_cast<float>(fy - j0);
            int stride = nodes + 1;
            float h00 = field[static_cast<std::size_t>(j0) * stride + i0];
            float h10 = field[static_cast<std::size_t>(j0) * stride + i0 + 1];
            float h01 = field[static_cast<std::size_t>(j0 + 1) * stride + i0];
            float h11 = field[static_cast<std::size_t>(j0 + 1) * stride + i0 + 1];
            return (h00 * (1 - dx) + h10 * dx) * (1 - dy) + (h01 * (1 - dx) + h11 * dx) * dy;
        }
    };

}

#endif
