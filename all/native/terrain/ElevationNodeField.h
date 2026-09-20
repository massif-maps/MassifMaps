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
        /**
         * The slot of the neighbour lying in direction (dx, dy), in the order a grid packs them:
         * W E S N then SW SE NW NE. -1 is this grid itself. Split out of the node texel sampler so
         * it can be pinned from a test: an edge node box reads up to 23k texels through that
         * sampler, and a wrong slot reads the wrong neighbour - a seam along the tile edge.
         */
        static int neighbourSlot(int dx, int dy) {
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
                return -1;
            }
            static const int SLOT[9] = { 4, 2, 5, 0, -1, 1, 6, 3, 7 };
            return SLOT[(dy + 1) * 3 + (dx + 1)];
        }

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
         * One axis of a bilinear lattice sum, grouped into runs that share a cell.
         *
         * A node box that reaches into a COARSER neighbour reads it with sampleHeight at this
         * grid's texel spacing, so `scale` consecutive samples land in the same neighbour cell,
         * where the height is bilinear in the two corners. Over such a run the weighted sum has a
         * closed form: the corners are constant, so only the weights times the interpolation
         * fraction need accumulating. The box covers (box/scale) cells an axis, so the pair loop
         * that follows is base^2 - constant however coarse the neighbour is - instead of box^2.
         *
         * c0/c1 are the clamped corner indices sampleHeight would use, s0/s1 the summed weights
         * against them. The sum over the run is s0*H(c0) + s1*H(c1).
         */
        struct LatticeRun {
            int c0 = 0, c1 = 0;
            double s0 = 0, s1 = 0;
        };

        /**
         * Splits `count` samples at f0 + i*df into runs of constant (c0, c1), accumulating the
         * weights. `dim` is the sampled raster's size: outside it the corners clamp, exactly as
         * sampleHeight clamps, so a run past the edge collapses onto one repeated texel.
         */
        static void latticeRuns(double f0, double df, const float* weights, int count, int dim, std::vector<LatticeRun>& runs) {
            runs.clear();
            for (int i = 0; i < count; i++) {
                double w = weights[i];
                if (w <= 0) {
                    continue;
                }
                double f = f0 + df * i;
                int g0 = static_cast<int>(std::floor(f));
                double d = f - g0;
                int c0 = std::min(std::max(g0, 0), dim - 1);
                int c1 = std::min(std::max(g0 + 1, 0), dim - 1);
                if (runs.empty() || runs.back().c0 != c0 || runs.back().c1 != c1) {
                    LatticeRun run;
                    run.c0 = c0;
                    run.c1 = c1;
                    runs.push_back(run);
                }
                runs.back().s0 += w * (1.0 - d);
                runs.back().s1 += w * d;
            }
        }

        /**
         * The weighted sum of a bilinear raster over the lattice the two run lists describe.
         * `corner(x, y)` is the raster's texel. Exactly the sum of the per-sample bilinears -
         * reassociated, not approximated, because a bilinear is separable in its two fractions.
         */
        template <typename CornerFn>
        static double latticeSum(const std::vector<LatticeRun>& xRuns, const std::vector<LatticeRun>& yRuns, const CornerFn& corner) {
            double sum = 0;
            for (const LatticeRun& y : yRuns) {
                double row = 0;
                for (const LatticeRun& x : xRuns) {
                    row += x.s0 * y.s0 * corner(x.c0, y.c0)
                         + x.s1 * y.s0 * corner(x.c1, y.c0)
                         + x.s0 * y.s1 * corner(x.c0, y.c1)
                         + x.s1 * y.s1 * corner(x.c1, y.c1);
                }
                sum += row;
            }
            return sum;
        }

        /**
         * Prefix sums over a raster, so the mean of an axis-aligned block of it is four lookups
         * instead of one read per texel. An edge node's box reaches 497 texels a side at a large
         * zoom gap - a quarter of a million reads for one node - and half of that box is the
         * grid's own texels, which this answers in O(1).
         *
         * EXACT, not an approximation: it is the same sum, reassociated. Double accumulation, so a
         * 512x512 grid of metre heights does not lose the low bits the seam depends on.
         */
        struct SummedAreaTable {
            int width = 0, height = 0;
            std::vector<double> sums; // (width + 1) * (height + 1), sums[0][*] and sums[*][0] are 0

            bool valid() const { return width > 0 && height > 0; }

            template <typename TexelFn>
            void build(int w, int h, const TexelFn& texel) {
                width = w;
                height = h;
                sums.assign(static_cast<std::size_t>(w + 1) * (h + 1), 0.0);
                for (int y = 0; y < h; y++) {
                    double row = 0;
                    for (int x = 0; x < w; x++) {
                        row += texel(x, y);
                        sums[static_cast<std::size_t>(y + 1) * (w + 1) + (x + 1)] =
                            sums[static_cast<std::size_t>(y) * (w + 1) + (x + 1)] + row;
                    }
                }
            }

            /** Sum over texels [x0, x1] x [y0, y1], inclusive. The caller clamps to the raster. */
            double rectSum(int x0, int y0, int x1, int y1) const {
                if (x1 < x0 || y1 < y0) {
                    return 0.0;
                }
                std::size_t stride = static_cast<std::size_t>(width) + 1;
                return sums[static_cast<std::size_t>(y1 + 1) * stride + (x1 + 1)]
                     - sums[static_cast<std::size_t>(y0) * stride + (x1 + 1)]
                     - sums[static_cast<std::size_t>(y1 + 1) * stride + x0]
                     + sums[static_cast<std::size_t>(y0) * stride + x0];
            }
        };

        /**
         * nodeHeight, with the whole-weight texels that lie INSIDE the raster taken from a summed
         * area table. Every other texel - the fractional rim of the box, and everything past the
         * raster, which is a neighbour's - still goes through `texel`, so the value is unchanged.
         */
        template <typename TexelFn>
        static float nodeHeightSat(double cx, double cy, int boxX, int boxY, const SummedAreaTable& sat, const TexelFn& texel) {
            if (!sat.valid()) {
                return nodeHeight(cx, cy, boxX, boxY, texel);
            }
            std::vector<float> wx, wy;
            int firstX = boxWeights(cx - 0.5 * boxX, boxX, wx);
            int firstY = boxWeights(cy - 0.5 * boxY, boxY, wy);
            // The span of FULL-weight texels that the table can answer: inside the raster, and not
            // the fractional rim. A weight is 1 only where the box covers the texel completely.
            int satX0 = firstX, satX1 = firstX + static_cast<int>(wx.size()) - 1;
            int satY0 = firstY, satY1 = firstY + static_cast<int>(wy.size()) - 1;
            while (satX0 <= satX1 && (satX0 < 0 || wx[satX0 - firstX] < 1.0f)) { satX0++; }
            while (satX1 >= satX0 && (satX1 >= sat.width || wx[satX1 - firstX] < 1.0f)) { satX1--; }
            while (satY0 <= satY1 && (satY0 < 0 || wy[satY0 - firstY] < 1.0f)) { satY0++; }
            while (satY1 >= satY0 && (satY1 >= sat.height || wy[satY1 - firstY] < 1.0f)) { satY1--; }

            double sum = 0;
            if (satX0 <= satX1 && satY0 <= satY1) {
                sum += sat.rectSum(satX0, satY0, satX1, satY1);
            }
            // Everything the block did not cover, one texel at a time, exactly as before.
            for (std::size_t j = 0; j < wy.size(); j++) {
                if (wy[j] <= 0) {
                    continue;
                }
                int ty = firstY + static_cast<int>(j);
                bool rowInBlock = (satY0 <= satY1 && ty >= satY0 && ty <= satY1);
                double row = 0;
                for (std::size_t i = 0; i < wx.size(); i++) {
                    if (wx[i] <= 0) {
                        continue;
                    }
                    int tx = firstX + static_cast<int>(i);
                    if (rowInBlock && satX0 <= satX1 && tx >= satX0 && tx <= satX1) {
                        continue; // the table has it
                    }
                    row += wx[i] * texel(tx, ty);
                }
                sum += wy[j] * row;
            }
            return static_cast<float>(sum / (static_cast<double>(boxX) * boxY));
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
         * Where a COARSE neighbour's raster sits under ours, as an affine map from OUR absolute
         * texel index to its continuous texel coordinate: f = origin + step * g, the same f
         * ElevationTileGrid::sampleHeight computes. step is our texel over theirs, so it is
         * 1/scale and `scale` consecutive samples of ours share one of their cells.
         */
        struct LatticeMapping {
            double originX = 0, stepX = 0;
            double originY = 0, stepY = 0;
            int dimX = 0, dimY = 0;
        };

        /**
         * nodeHeight over a box that straddles the tile border, with each REGION of the box
         * answered by whoever owns it instead of one dispatch per texel.
         *
         * An edge node's box reaches half its width into the neighbours, and on a DEM tile edge
         * shared with a coarser neighbour that half is the whole cost: measured on a Galaxy S22,
         * 98% of the texels an edge node reads come from a coarse neighbour, each one a bilinear
         * sampleHeight, 14.9 million of them in a second with the encode worker pinned at 100%.
         *
         * The box splits into at most nine regions - three column bands (west of the raster, our
         * own, east of it) by three row bands - and each band has ONE owner, so the dispatch moves
         * out of the texel loop. A band owned by a coarse neighbour is then summed by the closed
         * form (latticeRuns/latticeSum), which costs (box/scale)^2 instead of box^2; our own band
         * takes the summed-area table for its full-weight interior; anything else is per texel as
         * before.
         *
         * `mapping(dx, dy, out)` answers true and fills `out` only for a COARSE neighbour - for
         * our own raster, a same-level neighbour or a missing one it answers false and the band
         * falls back to `texel`. `corner(dx, dy, x, y)` is that neighbour's own raster.
         *
         * Not bit-identical to nodeHeight: the closed form is the same sum reassociated, exact to
         * a relative 1e-6 (tests/api/ElevationNodeFieldTest.cpp). Tile edges are where that shows,
         * so a change here wants a seam check on a device, not only the host suite.
         */
        template <typename TexelFn, typename MappingFn, typename CornerFn>
        static float nodeHeightRegions(double cx, double cy, int boxX, int boxY, int width, int height,
                                       const SummedAreaTable& sat, const TexelFn& texel,
                                       const MappingFn& mapping, const CornerFn& corner) {
            std::vector<float> wx, wy;
            int firstX = boxWeights(cx - 0.5 * boxX, boxX, wx);
            int firstY = boxWeights(cy - 0.5 * boxY, boxY, wy);
            int countX = static_cast<int>(wx.size());
            int countY = static_cast<int>(wy.size());
            // The band edges as indices into wx/wy. Most boxes use only two of the three.
            auto clampIndex = [](int value, int hi) { return std::min(std::max(value, 0), hi); };
            const int xCut[4] = { 0, clampIndex(-firstX, countX), clampIndex(width - firstX, countX), countX };
            const int yCut[4] = { 0, clampIndex(-firstY, countY), clampIndex(height - firstY, countY), countY };

            double sum = 0;
            for (int band = 0; band < 9; band++) {
                int bx = band % 3, by = band / 3;
                int x0 = xCut[bx], x1 = xCut[bx + 1];
                int y0 = yCut[by], y1 = yCut[by + 1];
                if (x0 >= x1 || y0 >= y1) {
                    continue;
                }
                int dx = bx - 1, dy = by - 1;
                LatticeMapping map;
                if ((dx != 0 || dy != 0) && mapping(dx, dy, map)) {
                    std::vector<LatticeRun> xRuns, yRuns;
                    latticeRuns(map.originX + map.stepX * (firstX + x0), map.stepX, wx.data() + x0, x1 - x0, map.dimX, xRuns);
                    latticeRuns(map.originY + map.stepY * (firstY + y0), map.stepY, wy.data() + y0, y1 - y0, map.dimY, yRuns);
                    sum += latticeSum(xRuns, yRuns, [&corner, dx, dy](int x, int y) { return corner(dx, dy, x, y); });
                    continue;
                }
                // Our own band can take the table for the texels the box covers WHOLE; everything
                // else in the band, and every other band, is one read per texel as before.
                bool own = (dx == 0 && dy == 0 && sat.valid());
                int satX0 = x0, satX1 = x1 - 1, satY0 = y0, satY1 = y1 - 1;
                if (own) {
                    while (satX0 <= satX1 && wx[satX0] < 1.0f) { satX0++; }
                    while (satX1 >= satX0 && wx[satX1] < 1.0f) { satX1--; }
                    while (satY0 <= satY1 && wy[satY0] < 1.0f) { satY0++; }
                    while (satY1 >= satY0 && wy[satY1] < 1.0f) { satY1--; }
                }
                bool haveBlock = own && satX0 <= satX1 && satY0 <= satY1;
                if (haveBlock) {
                    sum += sat.rectSum(firstX + satX0, firstY + satY0, firstX + satX1, firstY + satY1);
                }
                for (int j = y0; j < y1; j++) {
                    if (wy[j] <= 0) {
                        continue;
                    }
                    int ty = firstY + j;
                    bool rowInBlock = (haveBlock && j >= satY0 && j <= satY1);
                    double row = 0;
                    int i = x0;
                    while (i < x1) {
                        if (rowInBlock && i == satX0) {
                            i = satX1 + 1; // the table answered this whole span
                            continue;
                        }
                        if (wx[i] > 0) {
                            row += wx[i] * texel(firstX + i, ty);
                        }
                        i++;
                    }
                    sum += wy[j] * row;
                }
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
