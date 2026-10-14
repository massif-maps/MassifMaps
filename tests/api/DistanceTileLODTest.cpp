// The free roam raster cut (layers/DistanceTileLOD.h): the scale that fits the tile budget, and the cut it makes over
// a synthetic mountain range seen from many eyes. NOT covered here: the TileLayer walk itself (it needs Options and a
// renderer; this one mirrors it on a plane), a device check.

#include "layers/DistanceTileLOD.h"

#include "TestCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace massif;

namespace {

    const double PI = 3.14159265358979323846;
    const double ROOT = 28.3e6; // metres across the z0 tile at 45 degrees north
    const int CAP = 17;
    const double VIEW_DISTANCE = 106000.0;

    double terrainHeight(double x, double y) {
        return 800.0 + 700.0 * std::sin(x / 9000.0 + 1.0) * std::cos(y / 7000.0) + 300.0 * std::sin(x / 2300.0) * std::sin(y / 2900.0) + 500.0 * std::cos((x + y) / 21000.0);
    }

    struct Camera {
        double x, y, z;
        double yaw, pitch; // degrees, 0 = north, positive pitch looks up
        double fovY, width, height, dpi;
    };

    struct Tile {
        int zoom;
        long long x, y;
    };

    struct Cut {
        std::vector<DistanceTileLOD::Node> nodes;
        std::vector<Tile> tiles;
        std::vector<double> distances; // from the eye to the tile's nearest point
        double focal = 0, maxEdge = 0;
    };

    struct Leaf {
        Tile tile;
        double distance;
    };

    void dot3(const double* a, const double* b, double& result) {
        result = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    // What TileLayer::calculateVisibleTilesRecursive does in the free roam branch, on a plane, with a pinhole frustum.
    void visit(const Camera& camera, const double* basis, int zoom, long long x, long long y, double parentCritical, Cut& cut) {
        double edge = ROOT / static_cast<double>(1LL << zoom);
        double minX = (x - (1LL << zoom) / 2.0) * edge, minY = (y - (1LL << zoom) / 2.0) * edge;
        double minZ = 1e9, maxZ = -1e9;
        for (int i = 0; i <= 6; i++) {
            for (int j = 0; j <= 6; j++) {
                double height = terrainHeight(minX + edge * i / 6.0, minY + edge * j / 6.0);
                minZ = std::min(minZ, height);
                maxZ = std::max(maxZ, height);
            }
        }
        double tanV = std::tan(camera.fovY * 0.5 * PI / 180.0), tanH = tanV * camera.width / camera.height;
        const double* right = basis;
        const double* up = basis + 3;
        const double* forward = basis + 6;
        bool outside[6] = { true, true, true, true, true, true };
        double nearest[3] = { std::max(minX, std::min(minX + edge, camera.x)), std::max(minY, std::min(minY + edge, camera.y)), std::max(minZ, std::min(maxZ, camera.z)) };
        for (int corner = 0; corner < 8; corner++) {
            double relative[3] = { (corner & 1 ? minX + edge : minX) - camera.x, (corner & 2 ? minY + edge : minY) - camera.y, (corner & 4 ? maxZ : minZ) - camera.z };
            double cx, cy, cz;
            dot3(relative, right, cx);
            dot3(relative, up, cy);
            dot3(relative, forward, cz);
            outside[0] = outside[0] && cz - cx / tanH < 0;
            outside[1] = outside[1] && cz + cx / tanH < 0;
            outside[2] = outside[2] && cz - cy / tanV < 0;
            outside[3] = outside[3] && cz + cy / tanV < 0;
            outside[4] = outside[4] && cz < 0;
            outside[5] = outside[5] && cz > VIEW_DISTANCE;
        }
        for (bool plane : outside) {
            if (plane) {
                return;
            }
        }
        double reach[3] = { nearest[0] - camera.x, nearest[1] - camera.y, nearest[2] - camera.z };
        double distance = std::sqrt(reach[0] * reach[0] + reach[1] * reach[1] + reach[2] * reach[2]);
        if (distance > VIEW_DISTANCE) {
            return;
        }
        double critical = zoom < CAP ? DistanceTileLOD::criticalScale(edge, distance, cut.focal, cut.maxEdge) : 0.0;
        cut.nodes.push_back({ critical, parentCritical, 0 });
        cut.tiles.push_back({ zoom, x, y });
        cut.distances.push_back(distance);
        if (critical > 1.0) {
            for (int n = 0; n < 4; n++) {
                visit(camera, basis, zoom + 1, x * 2 + (n & 1), y * 2 + (n >> 1), critical, cut);
            }
        }
    }

    std::vector<Leaf> cull(const Camera& camera, std::size_t budget, double& scale, double& relaxedFactor) {
        double yaw = camera.yaw * PI / 180.0, pitch = camera.pitch * PI / 180.0;
        double right[3] = { std::cos(yaw), -std::sin(yaw), 0.0 };
        double forward[3] = { std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) };
        double up[3] = { right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0] };
        double basis[9] = { right[0], right[1], right[2], up[0], up[1], up[2], forward[0], forward[1], forward[2] };
        Cut cut;
        cut.focal = camera.height * 0.5 / std::tan(camera.fovY * 0.5 * PI / 180.0);
        cut.maxEdge = 2.0 * 256.0 * camera.dpi * 0.5; // TileLODFactor 0.5, the default
        visit(camera, basis, 0, 0, 0, std::numeric_limits<double>::infinity(), cut);
        scale = DistanceTileLOD::fitScale(cut.nodes, budget);
        // Neighbours stay within a level while a tile spans at least this many tiles' worth of distance (see the test).
        relaxedFactor = cut.focal / (cut.maxEdge * 2.2);
        std::vector<Leaf> leaves;
        for (std::size_t i = 0; i < cut.nodes.size(); i++) {
            if (DistanceTileLOD::isLeaf(cut.nodes[i], scale)) {
                leaves.push_back({ cut.tiles[i], cut.distances[i] });
            }
        }
        return leaves;
    }

    bool adjacent(const Tile& a, const Tile& b) {
        long long unitA = 1LL << (CAP - a.zoom), unitB = 1LL << (CAP - b.zoom);
        long long ax0 = a.x * unitA, ax1 = ax0 + unitA, ay0 = a.y * unitA, ay1 = ay0 + unitA;
        long long bx0 = b.x * unitB, bx1 = bx0 + unitB, by0 = b.y * unitB, by1 = by0 + unitB;
        bool overlapX = std::min(ax1, bx1) > std::max(ax0, bx0), overlapY = std::min(ay1, by1) > std::max(ay0, by0);
        return (overlapY && (ax1 == bx0 || bx1 == ax0)) || (overlapX && (ay1 == by0 || by1 == ay0));
    }

    struct Quality {
        std::size_t count = 0;
        double scale = 1.0;
        int neighbourStep = 0; // the largest level difference between adjacent tiles
        int overlaps = 0;
        int nearerCoarser = 0; // the largest number of levels a nearer tile is coarser than a farther one
        bool balanced = true; // whether the scale is one where neighbours must be within a level
    };

    Quality measure(const Camera& camera, std::size_t budget) {
        double relaxedFactor = 0;
        Quality quality;
        std::vector<Leaf> leaves = cull(camera, budget, quality.scale, relaxedFactor);
        quality.count = leaves.size();
        quality.balanced = quality.scale <= relaxedFactor;
        for (std::size_t i = 0; i < leaves.size(); i++) {
            for (std::size_t j = i + 1; j < leaves.size(); j++) {
                const Tile& a = leaves[i].tile;
                const Tile& b = leaves[j].tile;
                if (adjacent(a, b)) {
                    quality.neighbourStep = std::max(quality.neighbourStep, std::abs(a.zoom - b.zoom));
                }
                long long unitA = 1LL << (CAP - a.zoom), unitB = 1LL << (CAP - b.zoom);
                long long ax0 = a.x * unitA, ay0 = a.y * unitA, bx0 = b.x * unitB, by0 = b.y * unitB;
                if (std::min(ax0 + unitA, bx0 + unitB) > std::max(ax0, bx0) && std::min(ay0 + unitA, by0 + unitB) > std::max(ay0, by0)) {
                    quality.overlaps++;
                }
                const Leaf& nearer = leaves[i].distance <= leaves[j].distance ? leaves[i] : leaves[j];
                const Leaf& farther = leaves[i].distance <= leaves[j].distance ? leaves[j] : leaves[i];
                quality.nearerCoarser = std::max(quality.nearerCoarser, farther.tile.zoom - nearer.tile.zoom);
            }
        }
        return quality;
    }

}

void testDistanceTileLOD() {
    // The rule: a tile's edge on screen over the pixels a tile may span.
    double near = DistanceTileLOD::criticalScale(200.0, 1000.0, 2700.0, 512.0);
    double far = DistanceTileLOD::criticalScale(200.0, 2000.0, 2700.0, 512.0);
    TEST_CHECK(std::abs(near / far - 2.0) < 1e-12, "doubling the distance halves the scale a tile splits to: one level per doubling");
    TEST_CHECK(std::abs(DistanceTileLOD::criticalScale(400.0, 1000.0, 2700.0, 512.0) / near - 2.0) < 1e-12, "a tile twice the size asks for twice the scale");
    TEST_CHECK(std::isinf(DistanceTileLOD::criticalScale(200.0, 0.0, 2700.0, 512.0)), "an eye inside the tile splits it down to the cap");
    TEST_CHECK(std::isinf(DistanceTileLOD::criticalScale(200.0, 1000.0, 2700.0, 0.0)), "no pixel budget splits to the cap");

    // The partition: at any scale, the leaves are the nodes the walk would stop at.
    std::vector<DistanceTileLOD::Node> nodes = {
        { 4.0, std::numeric_limits<double>::infinity(), 0 }, // root, splits under 4
        { 0.0, 4.0, 0 }, { 2.5, 4.0, 0 }, { 0.0, 4.0, 1 }, { 0.5, 4.0, 0 }, // its children; one splits under 2.5
        { 0.0, 2.5, 0 }, { 0.0, 2.5, 0 }, { 0.0, 2.5, 0 }, { 0.0, 2.5, 0 } // that one's
    };
    TEST_CHECK(DistanceTileLOD::countVisible(nodes, 1.0) == 6, "at the rule: the root and the split child are gone, 2 + 4 visible leaves");
    TEST_CHECK(DistanceTileLOD::countVisible(nodes, 3.0) == 3, "past 2.5 the split child is a leaf, 3 visible");
    TEST_CHECK(DistanceTileLOD::countVisible(nodes, 5.0) == 1, "past 4 the root is a leaf alone");
    TEST_CHECK(DistanceTileLOD::fitScale(nodes, 6) == 1.0, "a cut in budget is the rule itself");
    TEST_CHECK(DistanceTileLOD::fitScale(nodes, 5) == 2.5, "one tile over budget moves to the nearest breakpoint, not a half level");
    TEST_CHECK(DistanceTileLOD::fitScale(nodes, 1) == 4.0, "and on to the next");
    TEST_CHECK(DistanceTileLOD::fitScale(nodes, 0) == DistanceTileLOD::MAX_SCALE, "a budget nothing meets stops at five levels, it cannot loop");

    TEST_CHECK(DistanceTileLOD::budget(0, 160, 256) == DistanceTileLOD::DEFAULT_BUDGET, "an automatic drape resolution takes the default budget");
    TEST_CHECK(DistanceTileLOD::budget(512, 160, 160) == 112, "160 MB of 512 px drapes is 160 entries: 70% of them");
    TEST_CHECK(DistanceTileLOD::budget(512, 160, 256) == 112, "...whatever the entry cap");
    TEST_CHECK(DistanceTileLOD::budget(512, 320, 256) == 179, "320 MB reaches the 256 entry cap");
    TEST_CHECK(DistanceTileLOD::budget(1024, 160, 256) == 28, "1024 px drapes are 4 MB: 40 entries");
    TEST_CHECK(DistanceTileLOD::budget(2048, 16, 256) == DistanceTileLOD::MIN_BUDGET, "never under the floor");

    // The cut over a mountain range from a grid of eyes: the budget holds, tiles never overlap, a neighbour is within a
    // level (two once the budget has coarsened the rule past what a tile's reach guarantees), and a nearer tile is
    // never coarser than a farther one by more than a level.
    const double yaws[] = { 0, 60, 135, 200, 290 };
    const double pitches[] = { -90, -45, -12, 0, 12 };
    const double fovs[] = { 30, 36.87, 50 };
    const double sizes[][2] = { { 1280, 720 }, { 2800, 1800 }, { 900, 1800 } };
    const double places[][2] = { { 1234.0, 5678.0 }, { 9100.0, -3300.0 }, { -14000.0, 8800.0 } };
    const std::size_t budget = 112;
    int cases = 0, overBudget = 0, overlapping = 0, balancedBroken = 0, nearerCoarser = 0, capped = 0;
    int worstStep = 0;
    for (const double* place : places) {
        for (double above : { 60.0, 400.0, 1500.0 }) {
            for (double yaw : yaws) {
                for (double pitch : pitches) {
                    for (double fov : fovs) {
                        for (const double* size : sizes) {
                            Camera camera { place[0], place[1], terrainHeight(place[0], place[1]) + above, yaw, pitch, fov, size[0], size[1], 2.0 };
                            Quality quality = measure(camera, budget);
                            cases++;
                            overBudget += quality.count > budget && quality.scale < DistanceTileLOD::MAX_SCALE ? 1 : 0;
                            capped += quality.scale >= DistanceTileLOD::MAX_SCALE ? 1 : 0;
                            overlapping += quality.overlaps;
                            if (quality.balanced) {
                                balancedBroken += quality.neighbourStep > 1 ? 1 : 0;
                            } else {
                                balancedBroken += quality.neighbourStep > 2 ? 1 : 0;
                            }
                            worstStep = std::max(worstStep, quality.neighbourStep);
                            nearerCoarser += quality.nearerCoarser > 1 ? 1 : 0;
                        }
                    }
                }
            }
        }
    }
    std::printf("      %d eyes: worst neighbour step %d levels, %d at the scale ceiling\n", cases, worstStep, capped);
    TEST_CHECK(overBudget == 0, "a cut fits the tile budget wherever the eye is");
    TEST_CHECK(overlapping == 0, "the leaves partition the ground, none sits on another");
    TEST_CHECK(balancedBroken == 0, "neighbouring tiles differ by one level, two when the budget coarsened the rule");
    TEST_CHECK(nearerCoarser == 0, "a nearer tile is never more than a level coarser than a farther one");

    // The fall of the level with the distance: the coarsest zoom among the leaves in each octave of distance from the
    // eye (the mean is skewed by the many small tiles nearer the octave's start).
    double steepest = 0, gentlest = 99;
    for (double above : { 100.0, 400.0, 1200.0 }) {
        for (double pitch : { -30.0, 0.0 }) {
            Camera camera { 1234.0, 5678.0, terrainHeight(1234.0, 5678.0) + above, 20.0, pitch, 36.87, 2800, 1800, 2.0 };
            double scale = 1, relaxedFactor = 0;
            std::vector<Leaf> leaves = cull(camera, budget, scale, relaxedFactor);
            int coarsest[16];
            int count[16] = {};
            std::fill(coarsest, coarsest + 16, 99);
            for (const Leaf& leaf : leaves) {
                int octave = std::max(0, std::min(15, static_cast<int>(std::floor(std::log2(std::max(leaf.distance, 1.0))))));
                coarsest[octave] = std::min(coarsest[octave], leaf.tile.zoom);
                count[octave]++;
            }
            // Over the octaves under the data's zoom cap and short of the view distance, which cuts the last tiles whole.
            int first = -1, last = -1;
            for (int octave = 0; octave < 16; octave++) {
                if (count[octave] >= 3 && coarsest[octave] < CAP && (1 << (octave + 1)) < VIEW_DISTANCE / 2) {
                    first = first < 0 ? octave : first;
                    last = octave;
                }
            }
            if (last - first >= 4) {
                double slope = static_cast<double>(coarsest[first] - coarsest[last]) / (last - first);
                steepest = std::max(steepest, slope);
                gentlest = std::min(gentlest, slope);
            }
        }
    }
    std::printf("      the coarsest level falls %.2f to %.2f per doubling of the distance\n", gentlest, steepest);
    TEST_CHECK(gentlest > 0.6 && steepest < 1.4, "the level falls one per doubling of the distance, not two");

    // The eye's height, 400 to 1200 m in 50 m steps over the same ground: the scale that fits the budget moves
    // smoothly, where a half level step moved every tile at once.
    for (double yaw : { 0.0, 135.0 }) {
        double previous = -1;
        double worstJump = 0;
        std::size_t previousCount = 0;
        std::size_t worstCountJump = 0;
        for (double above = 400.0; above <= 1200.0; above += 50.0) {
            Camera camera { 1234.0, 5678.0, terrainHeight(1234.0, 5678.0) + above, yaw, -8.0, 36.87, 2800, 1800, 2.0 };
            double scale = 1, relaxedFactor = 0;
            std::vector<Leaf> leaves = cull(camera, budget, scale, relaxedFactor);
            if (previous > 0) {
                worstJump = std::max(worstJump, std::abs(std::log2(scale / previous)));
                worstCountJump = std::max(worstCountJump, leaves.size() > previousCount ? leaves.size() - previousCount : previousCount - leaves.size());
            }
            previous = scale;
            previousCount = leaves.size();
        }
        std::printf("      eye height sweep at yaw %.0f: scale moves at most %.2f levels per 50 m, the count %d tiles\n", yaw, worstJump, static_cast<int>(worstCountJump));
        TEST_CHECK(worstJump < 0.5, "50 m of eye height moves the budget scale less than half a level");
    }
}
