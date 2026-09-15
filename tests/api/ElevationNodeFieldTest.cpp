/*
 * Tests for the node field (all/native/terrain/ElevationNodeField.h): the box a mesh node averages
 * the DEM over, its weights at the ends and off a texel boundary, the seam rule (two tiles sharing
 * an edge node compute the same value from the same texels), and the bilinear sample between nodes.
 *
 * NOT covered here: ElevationTileGrid's use of it (needs Bitmap), the node TEXTURE the elevation
 * texture cache uploads from it, and the vertex shader that displaces the surface from that
 * texture - all device checks. That the sawtooth on a road edge at a grazing tilt is gone is a
 * screenshot at the camera in docs/internals/rendering/04-terrain.md, "The node texture".
 */

#include "terrain/ElevationNodeField.h"

#include <array>
#include <cmath>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    bool nearly(double value, double expected, double tolerance = 1.0e-4) {
        return std::fabs(value - expected) < tolerance;
    }

    void testBoxSize() {
        TEST_CHECK(ElevationNodeField::boxTexels(512, 128, 1) == 4, "a 512-texel grid under 128 cells averages 4 texels per node per cell");
        TEST_CHECK(ElevationNodeField::boxTexels(256, 64, 1) == 4, "the same with a 256-texel grid under 64 cells");
        TEST_CHECK(ElevationNodeField::boxTexels(512, 128, ElevationNodeField::DEFAULT_BOX_CELLS) == 8, "the default box is two cells, 8 texels there");
        TEST_CHECK(ElevationNodeField::boxTexels(512, 508, 1) == 1, "a lattice as fine as the raster does not filter");
        TEST_CHECK(ElevationNodeField::boxTexels(64, 128, 2) == 1, "a raster coarser than the box does not filter either");
        TEST_CHECK(!ElevationNodeField::boxReachesOutside(1, 512, 128, 8) && ElevationNodeField::boxReachesOutside(0, 512, 128, 8) && ElevationNodeField::boxReachesOutside(128, 512, 128, 8),
                   "with a two-cell box only the edge nodes reach into the neighbour tile");
        TEST_CHECK(ElevationNodeField::boxReachesOutside(1, 512, 128, 16), "a four-cell box also pulls the next node in");
    }

    void testBoxWeights() {
        std::vector<float> w;
        // An even box on a texel boundary: the plain block, no end texels.
        int first = ElevationNodeField::boxWeights(8.0 - 2.0, 4, w);
        TEST_CHECK(first == 6 && w.size() == 4 && w[0] == 1 && w[1] == 1 && w[2] == 1 && w[3] == 1,
                   "a 4-texel box centred on texel boundary 8 is texels 6..9 at full weight");
        // A 1-texel box on a boundary: half of each neighbour, i.e. the bilinear value there.
        first = ElevationNodeField::boxWeights(8.0 - 0.5, 1, w);
        TEST_CHECK(first == 7 && w.size() == 2 && nearly(w[0], 0.5) && nearly(w[1], 0.5),
                   "a 1-texel box on a boundary takes half of the texel on each side");
        // Off a boundary the ends split so the mean stays centred on the node.
        first = ElevationNodeField::boxWeights(8.25 - 2.0, 4, w);
        double sum = 0;
        for (float x : w) sum += x;
        TEST_CHECK(first == 6 && w.size() == 5 && nearly(w[0], 0.75) && nearly(w[4], 0.25) && nearly(sum, 4.0),
                   "a box centred a quarter texel past a boundary shifts a quarter of its weight to the next texel, still summing to the box");
    }

    void testNodeIsCellMean() {
        // A linear ramp: the mean over a box centred on the node IS the ramp at the node, so a
        // node field over a ramp is the ramp sampled at the nodes - any offset would show a
        // phase error. Texel tx holds the height at its CENTRE tx + 0.5, so the ramp at
        // texel-space x is 3 (x - 0.5). A point sample gives the same here; the spike below is
        // where the two differ.
        auto ramp = [](int tx, int ty) { return 3.0f * tx + 0.5f * ty; };
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(8.0, 4.0, 4, 4, ramp), 3.0 * (8.0 - 0.5) + 0.5 * (4.0 - 0.5)),
                   "over a linear ramp the node height is the ramp at the node - the box is centred");
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(8.25, 4.0, 4, 4, ramp), 3.0 * (8.25 - 0.5) + 0.5 * (4.0 - 0.5)),
                   "also for a node between texel boundaries");
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(8.0, 4.0, 8, 8, ramp), 3.0 * (8.0 - 0.5) + 0.5 * (4.0 - 0.5)),
                   "and under the two-cell box: a wider symmetric box keeps a ramp exact");
        // A one-cell step, the shape of a road cut: a one-cell box leaves it a full step between
        // two nodes, the two-cell box spreads it over two - which is what halves the staircase.
        auto step = [](int tx, int) { return tx >= 8 ? 4.0f : 0.0f; };
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(4.0, 0.0, 4, 1, step), 0.0) && nearly(ElevationNodeField::nodeHeight(8.0, 0.0, 4, 1, step), 2.0) && nearly(ElevationNodeField::nodeHeight(12.0, 0.0, 4, 1, step), 4.0),
                   "a one-cell box takes a step from 0 to 4 within one node spacing");
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(4.0, 0.0, 8, 1, step), 0.0) && nearly(ElevationNodeField::nodeHeight(8.0, 0.0, 8, 1, step), 2.0) && nearly(ElevationNodeField::nodeHeight(12.0, 0.0, 8, 1, step), 4.0),
                   "the two-cell box reaches the same values at those nodes...");
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(6.0, 0.0, 4, 1, step), 0.0) && nearly(ElevationNodeField::nodeHeight(6.0, 0.0, 8, 1, step), 1.0),
                   "...but halfway between them the one-cell box still reads 0 and the two-cell box has begun to rise");
        // A 1-texel spike two texels from the node: a point sample misses it entirely, the box
        // spreads it as one sixteenth. This is the whole difference the field makes: relief
        // narrower than a cell reaches the node as its area share instead of by luck.
        auto spike = [](int tx, int ty) { return (tx == 6 && ty == 2) ? 16.0f : 0.0f; };
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(8.0, 4.0, 4, 4, spike), 1.0),
                   "a one-texel spike inside the cell reaches the node as its area share, not as zero or as 16");
        TEST_CHECK(nearly(ElevationNodeField::nodeHeight(8.0, 4.0, 4, 4, [](int tx, int ty) { return (tx == 5 && ty == 2) ? 16.0f : 0.0f; }), 0.0),
                   "and a spike outside the cell does not");
    }

    void testEdgeNodesAreShared() {
        // Two 8-texel tiles side by side over one ramp with a step at their border. Each reads its
        // own texels by local index and the other's through the neighbour rule, exactly as the
        // grid's sampler does. Their shared edge node - tile A's node 2, tile B's node 0 - must be
        // the same number, or the surface cracks along the tile border.
        const int width = 8, nodes = 2;
        auto world = [](int x) { return x < 8 ? 1.0f * x : 100.0f + 1.0f * x; };
        auto tileA = [&](int tx, int) { return world(tx); };       // tile A owns world texels 0..7
        auto tileB = [&](int tx, int) { return world(tx + 8); };   // tile B owns world texels 8..15
        std::vector<float> fieldA, fieldB;
        ElevationNodeField::build(width, 1, nodes, 1, tileA, fieldA);
        ElevationNodeField::build(width, 1, nodes, 1, tileB, fieldB);
        float sharedFromA = fieldA[2];
        float sharedFromB = fieldB[0];
        TEST_CHECK(nearly(sharedFromA, sharedFromB), "the node on a shared tile edge is the same height from either tile");
        TEST_CHECK(nearly(sharedFromA, (6.0 + 7.0 + 108.0 + 109.0) / 4.0), "and it is the mean of the two texels on each side of the border");
        TEST_CHECK(fieldA.size() == 9 && fieldB.size() == 9, "an N-cell field holds (N + 1)^2 nodes");
        // Clamped instead (no neighbour): the edge node differs, which is why the texture path
        // recomputes edge nodes with the neighbours and the CPU field documents the difference.
        std::vector<float> clamped;
        ElevationNodeField::build(width, 1, nodes, 1, [&](int tx, int ty) { return tileA(std::min(std::max(tx, 0), 7), ty); }, clamped);
        TEST_CHECK(!nearly(clamped[2], sharedFromA), "without the neighbour the edge node is a different (clamped) value");
    }

    void testSample() {
        std::vector<float> field = { 0, 10, 20,
                                     0, 10, 20,
                                     0, 10, 20 }; // 2 cells, height 10 per node along x
        TEST_CHECK(nearly(ElevationNodeField::sample(field, 2, 1.0, 1.0), 10.0), "a sample on a node is that node");
        TEST_CHECK(nearly(ElevationNodeField::sample(field, 2, 0.25, 0.5), 2.5), "between nodes the sample is bilinear");
        TEST_CHECK(nearly(ElevationNodeField::sample(field, 2, 2.0, 2.0), 20.0), "the last node is reachable");
        TEST_CHECK(nearly(ElevationNodeField::sample(field, 2, 5.0, -1.0), 20.0), "outside the lattice the sample clamps to the edge");
        TEST_CHECK(ElevationNodeField::sample(field, 4, 1.0, 1.0) == 0.0f, "a field too small for its node count answers 0 instead of reading past its end");
    }

    /*
     * Which neighbour a direction means (ElevationNodeField::neighbourSlot). The node texel sampler
     * used to find this by scanning an eight-entry table per texel - up to 23k texels for ONE edge
     * node - and now indexes it. A wrong slot reads the wrong neighbour, which is a seam along a
     * tile edge, so the mapping is pinned here against the packing order it must match: W E S N
     * then SW SE NW NE, exactly the order ElevationTileGrid fills its neighbour array in.
     */
    void testNeighbourSlot() {
        const std::array<std::pair<int, int>, 8> DIRS = { {
            { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
        } };
        for (int i = 0; i < 8; i++) {
            if (ElevationNodeField::neighbourSlot(DIRS[i].first, DIRS[i].second) != i) {
                TEST_CHECK(false, "every direction maps to the slot the neighbour array packs it in");
                return;
            }
        }
        TEST_CHECK(true, "every direction maps to the slot the neighbour array packs it in");
        TEST_CHECK(ElevationNodeField::neighbourSlot(0, 0) == -1, "and the centre is this grid, not a neighbour");
        TEST_CHECK(ElevationNodeField::neighbourSlot(2, 0) == -1 && ElevationNodeField::neighbourSlot(0, -3) == -1,
                   "a direction that is not a neighbour reads this grid rather than past the array");
    }

    /*
     * nodeHeightSat must be nodeHeight, to the bit that a seam would show in. It answers the
     * full-weight texels inside the raster from prefix sums and leaves the fractional rim and
     * everything past the raster to the same callback, so the only difference allowed is the
     * reassociation of a double sum.
     */
    void testSatMatchesBruteForce() {
        const int W = 24, H = 20;
        std::vector<float> grid(static_cast<std::size_t>(W) * H);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                grid[static_cast<std::size_t>(y) * W + x] = static_cast<float>(100 + x * 7 - y * 3 + (x * y) % 11);
            }
        }
        // Outside the raster is a neighbour's business; any answer will do as long as BOTH paths
        // get the same one, which is what the rim and the past-the-edge texels exercise.
        auto texel = [&grid](int x, int y) -> float {
            if (x < 0 || y < 0 || x >= W || y >= H) {
                return static_cast<float>(1000 + x * 2 - y);
            }
            return grid[static_cast<std::size_t>(y) * W + x];
        };

        ElevationNodeField::SummedAreaTable sat;
        sat.build(W, H, texel);
        TEST_CHECK(sat.valid(), "the table builds over the raster");
        TEST_CHECK(std::abs(sat.rectSum(0, 0, W - 1, H - 1) - [&]{ double t = 0; for (float v : grid) { t += v; } return t; }()) < 1e-6,
                   "and its whole-raster sum is the raster's sum");

        // Boxes wholly inside, straddling each edge and each corner, and wider than the raster -
        // the last is the case an edge node at a large zoom gap actually hits.
        const double centres[][2] = { { 12.0, 10.0 }, { 12.5, 10.5 }, { 0.0, 10.0 }, { 24.0, 10.0 },
                                      { 12.0, 0.0 }, { 12.0, 20.0 }, { 0.0, 0.0 }, { 24.0, 20.0 } };
        const int sizes[] = { 1, 2, 3, 8, 30, 64 };
        double worst = 0;
        for (const auto& c : centres) {
            for (int box : sizes) {
                float ref = ElevationNodeField::nodeHeight(c[0], c[1], box, box, texel);
                float fast = ElevationNodeField::nodeHeightSat(c[0], c[1], box, box, sat, texel);
                worst = std::max(worst, std::abs(static_cast<double>(ref) - fast));
            }
        }
        TEST_CHECK(worst < 1.0e-3, "and every box mean matches the per-texel sum");

        // A table that was never built must not silently answer zero.
        ElevationNodeField::SummedAreaTable empty;
        TEST_CHECK(ElevationNodeField::nodeHeightSat(12.0, 10.0, 8, 8, empty, texel)
                   == ElevationNodeField::nodeHeight(12.0, 10.0, 8, 8, texel),
                   "an unbuilt table falls back to the per-texel sum");
    }

    /*
     * The lattice closed form against the per-sample bilinear it replaces. This is the half of an
     * edge node's box that lies in a COARSER neighbour, where the old path called sampleHeight once
     * per texel of OUR grid - up to a quarter of a million times for one node, most of them landing
     * in the same neighbour cell. Exactness is not optional here: this value is what makes two tiles
     * agree on their shared edge.
     */
    void testLatticeSumMatchesPerSampleBilinear() {
        const int NW = 9, NH = 7;                       // the coarse neighbour
        std::vector<float> nb(static_cast<std::size_t>(NW) * NH);
        for (int y = 0; y < NH; y++) {
            for (int x = 0; x < NW; x++) {
                nb[static_cast<std::size_t>(y) * NW + x] = static_cast<float>(50 + x * 13 - y * 5 + (x * y) % 7);
            }
        }
        auto corner = [&nb](int x, int y) { return nb[static_cast<std::size_t>(y) * NW + x]; };

        // scale = how much coarser the neighbour is; df = 1/scale, the step our texels take in it.
        for (int scale : { 2, 4, 8, 16 }) {
            for (double f0 : { -2.5, -0.5, 0.0, 0.25, 3.75 }) {
                const int count = 6 * scale;
                std::vector<float> wx(count, 1.0f), wy(count, 1.0f);
                wx.front() = 0.4f; wx.back() = 0.6f;     // the box's fractional rim
                wy.front() = 0.7f; wy.back() = 0.3f;
                double df = 1.0 / scale;

                double brute = 0;
                for (int j = 0; j < count; j++) {
                    double fy = f0 + df * j;
                    int gy0 = static_cast<int>(std::floor(fy));
                    double dy = fy - gy0;
                    int cy1 = std::min(std::max(gy0 + 1, 0), NH - 1);
                    int cy0 = std::min(std::max(gy0, 0), NH - 1);
                    for (int i = 0; i < count; i++) {
                        double fx = f0 + df * i;
                        int gx0 = static_cast<int>(std::floor(fx));
                        double dx = fx - gx0;
                        int cx1 = std::min(std::max(gx0 + 1, 0), NW - 1);
                        int cx0 = std::min(std::max(gx0, 0), NW - 1);
                        double h = (1 - dx) * (1 - dy) * corner(cx0, cy0) + dx * (1 - dy) * corner(cx1, cy0)
                                 + (1 - dx) * dy * corner(cx0, cy1) + dx * dy * corner(cx1, cy1);
                        brute += static_cast<double>(wx[i]) * wy[j] * h;
                    }
                }

                std::vector<ElevationNodeField::LatticeRun> xRuns, yRuns;
                ElevationNodeField::latticeRuns(f0, df, wx.data(), count, NW, xRuns);
                ElevationNodeField::latticeRuns(f0, df, wy.data(), count, NH, yRuns);
                double fast = ElevationNodeField::latticeSum(xRuns, yRuns, corner);

                if (std::abs(brute - fast) > 1.0e-6 * std::max(1.0, std::abs(brute))) {
                    TEST_CHECK(false, "the lattice closed form is the per-sample bilinear sum");
                    return;
                }
                // ... and it is the point of the exercise that it took far fewer terms.
                if (static_cast<int>(xRuns.size()) > count / scale + 3) {
                    TEST_CHECK(false, "and groups the lattice into one run per neighbour cell");
                    return;
                }
            }
        }
        TEST_CHECK(true, "the lattice closed form is the per-sample bilinear sum");
        TEST_CHECK(true, "and groups the lattice into one run per neighbour cell");
    }

}

void testElevationNodeField() {
    testLatticeSumMatchesPerSampleBilinear();
    testSatMatchesBruteForce();
    testNeighbourSlot();
    testBoxSize();
    testBoxWeights();
    testNodeIsCellMean();
    testEdgeNodesAreShared();
    testSample();
}
