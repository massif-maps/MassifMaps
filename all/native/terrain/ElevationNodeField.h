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
     * The height field the terrain surface stands on: one height per mesh node, the DEM box-filtered over
     * the node's cell so the lattice does not alias relief finer than it (shading keeps the full DEM).
     * See docs/internals/rendering/04-terrain.md, "The node texture".
     */
    struct ElevationNodeField {
        /**
         * How many mesh cells the box spans. Two, not one: a one-cell box leaves a road's cut as a full
         * step within a cell, drawn as a staircase at a grazing tilt; wider trades real relief. See 04-terrain.md.
         */
        static constexpr int DEFAULT_BOX_CELLS = 2;

        /**
         * The slot of the neighbour in direction (dx, dy), in grid packing order W E S N SW SE NW NE;
         * -1 is this grid itself. Standalone so a test can pin it: a wrong slot is a seam.
         */
        static int neighbourSlot(int dx, int dy) {
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
                return -1;
            }
            static const int SLOT[9] = { 4, 2, 5, 0, -1, 1, 6, 3, 7 };
            return SLOT[(dy + 1) * 3 + (dx + 1)];
        }

        /**
         * Box width in texels spanning `cells` of the `nodes` cells across a `width`-texel raster.
         * 1 when the raster is coarser than the box, where the box is a plain bilinear sample.
         */
        static int boxTexels(int width, int nodes, int cells) {
            return std::max(1, std::max(1, cells) * width / std::max(1, nodes));
        }

        /**
         * An edge node's box widening for a coarser neighbour grid, capped at one level: a coarser one is a stand-in
         * ancestor, and matching it lifts the edge row into a wall across a valley (04-terrain.md, "The edge box widens one level").
         */
        static int edgeBoxScale(double ourTexel, double neighbourTexel) {
            int scale = 1;
            while (neighbourTexel > ourTexel * scale * 1.5 && scale < 2) {
                scale *= 2;
            }
            return scale;
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
         * Area weights of [a, a + box) over unit texel cells: covered cells weigh 1, the two end cells
         * their overlap; sums to box. Partial end cells keep the mean centred on the node, not half a texel off.
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
         * One axis of a bilinear lattice sum: consecutive samples sharing a coarse cell, summed as
         * s0*H(c0) + s1*H(c1), with c0/c1 the clamped corners sampleHeight would use.
         */
        struct LatticeRun {
            int c0 = 0, c1 = 0;
            double s0 = 0, s1 = 0;
        };

        /**
         * Splits `count` samples at f0 + i*df into runs of constant (c0, c1), accumulating the
         * weights. Corners clamp to `dim` exactly as sampleHeight does.
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
         * The weighted sum of a bilinear raster (texel `corner(x, y)`) over the lattice the run lists
         * describe; exact, since a bilinear is separable.
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
         * Prefix sums over a raster: a block sum in four lookups. Accumulated in double so a
         * 512x512 grid does not lose the low bits the seam depends on.
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

        /** nodeHeight, with the full-weight texels inside the raster taken from the summed area table. */
        template <typename TexelFn>
        static float nodeHeightSat(double cx, double cy, int boxX, int boxY, const SummedAreaTable& sat, const TexelFn& texel) {
            if (!sat.valid()) {
                return nodeHeight(cx, cy, boxX, boxY, texel);
            }
            std::vector<float> wx, wy;
            int firstX = boxWeights(cx - 0.5 * boxX, boxX, wx);
            int firstY = boxWeights(cy - 0.5 * boxY, boxY, wy);
            // Full-weight texels inside the raster; the fractional rim stays per texel.
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
         * Mean height over the boxX x boxY texel block centred on (cx, cy). `texel` must answer outside the
         * raster too (neighbour or clamped): an edge node reads half a box into the next tile, and two tiles
         * reading the same texels there is what keeps the surface seam-free.
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
         * Affine map from our texel index g to a coarse neighbour's continuous texel coordinate,
         * f = origin + step * g, as ElevationTileGrid::sampleHeight computes it.
         */
        struct LatticeMapping {
            double originX = 0, stepX = 0;
            double originY = 0, stepY = 0;
            int dimX = 0, dimY = 0;
        };

        /**
         * nodeHeight summed per region (up to 3x3 bands): a coarse neighbour's band in closed form
         * when `mapping` answers true, our own band via the table, the rest per texel through `texel`.
         * Matches nodeHeight to a relative 1e-6, not bit-exactly; changes want a device seam check.
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
            // The band edges as indices into wx/wy.
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
                // Only our own band's full-weight texels come from the table.
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
         * Every node of an N-cell lattice over a width x height raster, row-major, (N + 1)^2 values. Node (i, j)
         * sits on the cell corner (i * width / N, j * height / N): nodes 0 and N are the tile edges, shared with
         * the adjacent tiles.
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
