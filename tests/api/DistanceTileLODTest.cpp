// The tile cut every planar layer makes (layers/DistanceTileLOD.h): the rule, the scale that fits the tile budget, and
// the cut it makes over a synthetic mountain range and a flat plane seen from many eyes. The point of it: a tile's level
// is a function of its distance from the eye, so looking around changes none of them. NOT covered here: the TileLayer
// walk itself (it needs Options and a renderer; this one mirrors it with a pinhole frustum and calls the same rule), the
// DEM boxes, a device check.

#include "graphics/ZoomConvention.h"
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

    enum class Rule { Distance, Area };

    // What a cull is held to: the ground, the rule, and the levels TileLayer bounds it by.
    struct Scene {
        bool flat = false; // the plane z = 0, not the mountain range
        Rule rule = Rule::Distance;
        DistanceTileLOD::Limits limits { 0, CAP, DistanceTileLOD::NO_VIEW_CAP }; // free roam imagery: only the data caps
        double farCoarsening = 0.0;
        int areaCap = CAP; // the camera's zoom, which the area rule is capped by
        double viewDistance = VIEW_DISTANCE;
        double lodFactor = 1.0; // on the 0.5 default's pixels
    };

    struct Cut {
        Scene scene;
        DistanceTileLOD::View view;
        std::vector<DistanceTileLOD::Node> nodes;
        std::vector<Tile> tiles;
        std::vector<double> distances; // from the eye to the tile's nearest point
    };

    struct Leaf {
        Tile tile;
        double distance;
    };

    void dot3(const double* a, const double* b, double& result) {
        result = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    // The old rule's split test on the plane: the tile's projected area against a block of nominal tiles. A corner behind
    // the eye splits, as it did.
    bool areaSplits(const Camera& camera, const double* basis, double minX, double minY, double edge, double maxEdge) {
        double tanV = std::tan(camera.fovY * 0.5 * PI / 180.0), tanH = tanV * camera.width / camera.height;
        double screen[4][2];
        for (int corner = 0; corner < 4; corner++) {
            double relative[3] = { minX + ((corner & 1) ? edge : 0.0) - camera.x, minY + ((corner & 2) ? edge : 0.0) - camera.y, -camera.z };
            double cx, cy, cz;
            dot3(relative, basis, cx);
            dot3(relative, basis + 3, cy);
            dot3(relative, basis + 6, cz);
            if (!(cz > 0)) {
                return true;
            }
            screen[corner][0] = cx / (cz * tanH) * camera.width * 0.5;
            screen[corner][1] = cy / (cz * tanV) * camera.height * 0.5;
        }
        const int order[4] = { 0, 1, 3, 2 };
        double area = 0;
        for (int i = 0; i < 4; i++) {
            const double* p = screen[order[i]];
            const double* q = screen[order[(i + 1) % 4]];
            area += p[0] * q[1] - q[0] * p[1];
        }
        return std::abs(area) * 0.5 >= maxEdge * maxEdge;
    }

    // What TileLayer::calculateVisibleTilesRecursive does on a plane, with a pinhole frustum.
    void visit(const Camera& camera, const double* basis, int zoom, long long x, long long y, double parentCritical, Cut& cut) {
        double edge = ROOT / static_cast<double>(1LL << zoom);
        double minX = (x - (1LL << zoom) / 2.0) * edge, minY = (y - (1LL << zoom) / 2.0) * edge;
        double minZ = 1e9, maxZ = -1e9;
        for (int i = 0; i <= 6; i++) {
            for (int j = 0; j <= 6; j++) {
                double height = cut.scene.flat ? 0.0 : terrainHeight(minX + edge * i / 6.0, minY + edge * j / 6.0);
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
            outside[5] = outside[5] && cz > cut.scene.viewDistance;
        }
        for (bool plane : outside) {
            if (plane) {
                return;
            }
        }
        double reach[3] = { nearest[0] - camera.x, nearest[1] - camera.y, nearest[2] - camera.z };
        double distance = std::sqrt(reach[0] * reach[0] + reach[1] * reach[1] + reach[2] * reach[2]);
        if (distance > cut.scene.viewDistance) {
            return;
        }
        double critical;
        if (cut.scene.rule == Rule::Distance) {
            critical = DistanceTileLOD::nodeCritical(cut.view, cut.scene.limits, zoom, edge, distance, std::abs(nearest[2] - camera.z));
        } else if (zoom < cut.scene.limits.minZoom) {
            critical = std::numeric_limits<double>::infinity();
        } else if (zoom >= cut.scene.areaCap) {
            critical = 0.0;
        } else {
            critical = areaSplits(camera, basis, minX, minY, edge, cut.view.maxEdgePixels) ? std::numeric_limits<double>::infinity() : 0.0;
        }
        cut.nodes.push_back({ critical, parentCritical, 0 });
        cut.tiles.push_back({ zoom, x, y });
        cut.distances.push_back(distance);
        if (critical > 1.0) {
            for (int n = 0; n < 4; n++) {
                visit(camera, basis, zoom + 1, x * 2 + (n & 1), y * 2 + (n >> 1), critical, cut);
            }
        }
    }

    DistanceTileLOD::View makeView(const Camera& camera) {
        double tanHalf = std::tan(camera.fovY * 0.5 * PI / 180.0);
        DistanceTileLOD::View view;
        view.focalPixels = camera.height * 0.5 / tanHalf;
        view.maxEdgePixels = 2.0 * 256.0 * camera.dpi * 0.5; // TileLODFactor 0.5, the default
        view.zoom0Distance = ZoomConvention::zoom0Distance(camera.height, ROOT, 256.0, 0.0, tanHalf, camera.dpi);
        view.zoomShift = 0.001;
        return view;
    }

    std::vector<Leaf> cull(const Camera& camera, const Scene& scene, std::size_t budget, double& scale, double& relaxedFactor) {
        double yaw = camera.yaw * PI / 180.0, pitch = camera.pitch * PI / 180.0;
        double right[3] = { std::cos(yaw), -std::sin(yaw), 0.0 };
        double forward[3] = { std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) };
        double up[3] = { right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0] };
        double basis[9] = { right[0], right[1], right[2], up[0], up[1], up[2], forward[0], forward[1], forward[2] };
        Cut cut;
        cut.scene = scene;
        cut.view = makeView(camera);
        cut.view.maxEdgePixels *= scene.lodFactor;
        cut.view.farCoarsening = scene.farCoarsening;
        visit(camera, basis, 0, 0, 0, std::numeric_limits<double>::infinity(), cut);
        scale = DistanceTileLOD::fitScale(cut.nodes, budget);
        // Neighbours stay within a level while a tile spans at least this many tiles' worth of distance (see the test).
        relaxedFactor = cut.view.focalPixels / (cut.view.maxEdgePixels * 2.2);
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

    Quality measure(const Camera& camera, const Scene& scene, std::size_t budget) {
        double relaxedFactor = 0;
        Quality quality;
        std::vector<Leaf> leaves = cull(camera, scene, budget, quality.scale, relaxedFactor);
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


    bool covers(const Tile& tile, double x, double y) {
        double edge = ROOT / static_cast<double>(1LL << tile.zoom);
        double minX = (tile.x - (1LL << tile.zoom) / 2.0) * edge, minY = (tile.y - (1LL << tile.zoom) / 2.0) * edge;
        return x >= minX && x < minX + edge && y >= minY && y < minY + edge;
    }

    // The level of the leaf over a ground point, -1 where the view holds none.
    int levelAt(const std::vector<Leaf>& leaves, double x, double y) {
        for (const Leaf& leaf : leaves) {
            if (covers(leaf.tile, x, y)) {
                return leaf.tile.zoom;
            }
        }
        return -1;
    }

    bool overlap(const Tile& a, const Tile& b) {
        long long unitA = 1LL << (CAP - a.zoom), unitB = 1LL << (CAP - b.zoom);
        long long ax0 = a.x * unitA, ay0 = a.y * unitA, bx0 = b.x * unitB, by0 = b.y * unitB;
        return std::min(ax0 + unitA, bx0 + unitB) > std::max(ax0, bx0) && std::min(ay0 + unitA, by0 + unitB) > std::max(ay0, by0);
    }

    // A camera orbiting its focus on the plane the way a classic map does: `tilt` from straight down, at `zoom`.
    Camera orbitCamera(double zoom, double heading, double tilt, double fovY, double width, double height, double dpi) {
        Camera camera { 0, 0, 0, heading, tilt - 90.0, fovY, width, height, dpi };
        double distance = makeView(camera).zoom0Distance / std::pow(2.0, zoom);
        double yaw = heading * PI / 180.0, elevation = camera.pitch * PI / 180.0;
        camera.x = -std::sin(yaw) * std::cos(elevation) * distance;
        camera.y = -std::cos(yaw) * std::cos(elevation) * distance;
        camera.z = -std::sin(elevation) * distance;
        return camera;
    }

    // What the classic map is held to: the camera's zoom caps the levels and the view distance bounds the walk
    // (ViewState::calculateViewDistance: twice the orbit over the cosine of the angle to the horizon edge).
    Scene classicScene(const Camera& camera, double zoom, Rule rule) {
        Scene scene;
        scene.flat = true;
        scene.rule = rule;
        scene.areaCap = static_cast<int>(zoom + 0.001);
        scene.limits.capOffset = 0;
        scene.limits.zoomCap = scene.areaCap;
        scene.farCoarsening = DistanceTileLOD::FAR_COARSENING;
        double distance = makeView(camera).zoom0Distance / std::pow(2.0, zoom);
        double toHorizon = (camera.pitch + 90.0 + camera.fovY * 0.5) * PI / 180.0;
        double reach = std::cos(toHorizon) > 0 ? 2.0 * distance / std::cos(toHorizon) : std::numeric_limits<double>::infinity();
        scene.viewDistance = std::min(reach, ROOT / std::pow(2.0, zoom) * 127.0);
        return scene;
    }

    // The ground points a view is judged at: rings round the eye from 40 m to 40 km, a level apart or so.
    std::vector<std::pair<double, double>> ringPoints(double eyeX, double eyeY) {
        std::vector<std::pair<double, double>> points;
        for (double radius = 40.0; radius < 40000.0; radius *= 1.7) {
            for (int step = 0; step < 24; step++) {
                double angle = step * PI / 12.0;
                points.push_back({ eyeX + radius * std::sin(angle), eyeY + radius * std::cos(angle) });
            }
        }
        return points;
    }

    struct Looks {
        int looksSkipped = 0; // looks the budget coarsened, which the rule alone does not describe
        int compared = 0; // points that two looks of one eye both hold a tile over
        int flipped = 0; // ...of which the two tiles are of different levels
        int treesFlipped = 0; // ...or one is a level with trees (15 and finer) and the other is not
        int withTrees = 0, withoutTrees = 0; // so that the last is not vacuous
    };

    // One eye, every way it can look: the level over each ground point must not depend on the look.
    Looks lookAround(const Camera& eye, const Scene& scene, std::size_t budget) {
        const int TREES_FROM = 15;
        Looks looks;
        std::vector<std::pair<double, double>> points = ringPoints(eye.x, eye.y);
        std::vector<int> first(points.size(), -1);
        for (double yaw = 0; yaw < 360.0; yaw += 30.0) {
            for (double pitch : { -90.0, -70.0, -45.0, -25.0, -10.0, 0.0, 12.0 }) {
                Camera look = eye;
                look.yaw = yaw;
                look.pitch = pitch;
                double scale = 1, relaxedFactor = 0;
                std::vector<Leaf> leaves = cull(look, scene, budget, scale, relaxedFactor);
                if (scale > 1.0) {
                    looks.looksSkipped++;
                    continue;
                }
                for (std::size_t i = 0; i < points.size(); i++) {
                    int level = levelAt(leaves, points[i].first, points[i].second);
                    if (level < 0) {
                        continue;
                    }
                    if (first[i] < 0) {
                        first[i] = level;
                        (level >= TREES_FROM ? looks.withTrees : looks.withoutTrees)++;
                        continue;
                    }
                    looks.compared++;
                    looks.flipped += level != first[i] ? 1 : 0;
                    looks.treesFlipped += (level >= TREES_FROM) != (first[i] >= TREES_FROM) ? 1 : 0;
                }
            }
        }
        return looks;
    }

    const double SIZES[][2] = { { 1280, 720 }, { 900, 1800 } };

    void testTheLevelIsTheDistances() {
        const std::size_t NO_BUDGET = std::numeric_limits<std::size_t>::max();
        // The eye stays; heading 0-330, pitch 90 degrees down to 12 up, two fields of view, two screens, over the
        // mountain range and over a plane, free roam (nothing bounds a level but the data's) and under a classic map's cap.
        Scene mountains, plane, capped;
        plane.flat = true;
        plane.limits.capOffset = 0;
        plane.farCoarsening = DistanceTileLOD::FAR_COARSENING;
        capped.limits.capOffset = 0;
        capped.farCoarsening = DistanceTileLOD::FAR_COARSENING;
        int compared = 0, flipped = 0, treesFlipped = 0, withTrees = 0, withoutTrees = 0, areaCompared = 0, areaFlipped = 0, areaTreesFlipped = 0;
        int budgetedCompared = 0, budgetedFlipped = 0, budgetedSkipped = 0;
        int cappedCompared = 0, cappedFlipped = 0;
        for (double above : { 3.0, 80.0, 900.0 }) {
            for (double fov : { 30.0, 50.0 }) {
                for (const double* size : SIZES) {
                    for (const Scene* scene : { &mountains, &plane, &capped }) {
                        Camera eye { 1234.0, 5678.0, scene->flat ? above : terrainHeight(1234.0, 5678.0) + above, 0, 0, fov, size[0], size[1], 1.0 };
                        Looks looks = lookAround(eye, *scene, NO_BUDGET);
                        compared += looks.compared;
                        flipped += looks.flipped;
                        treesFlipped += looks.treesFlipped;
                        withTrees += looks.withTrees;
                        withoutTrees += looks.withoutTrees;
                        // The zoom cap of a classic map: the zoom of the eye's height over the ground, which no look moves.
                        Scene referenced = *scene;
                        referenced.limits.zoomCap = DistanceTileLOD::viewLevel(makeView(eye), eye.z - (scene->flat ? 0.0 : terrainHeight(eye.x, eye.y)));
                        Looks referencedLooks = lookAround(eye, referenced, NO_BUDGET);
                        cappedCompared += referencedLooks.compared;
                        cappedFlipped += referencedLooks.flipped;
                        Looks budgeted = lookAround(eye, *scene, DistanceTileLOD::DEFAULT_BUDGET);
                        budgetedCompared += budgeted.compared;
                        budgetedFlipped += budgeted.flipped;
                        budgetedSkipped += budgeted.looksSkipped;
                        if (scene->flat) {
                            Scene area = *scene;
                            area.rule = Rule::Area;
                            Looks old = lookAround(eye, area, NO_BUDGET);
                            areaCompared += old.compared;
                            areaFlipped += old.flipped;
                            areaTreesFlipped += old.treesFlipped;
                        }
                    }
                }
            }
        }
        std::printf("      %d ground points held by two looks of one eye: %d change level (%d across the trees line); the area rule: %d of %d (%d)\n",
                    compared, flipped, treesFlipped, areaFlipped, areaCompared, areaTreesFlipped);
        TEST_CHECK(compared > 10000, "the sweep holds many points under more than one look");
        TEST_CHECK(flipped == 0, "no ground point changes level whichever way the eye looks");
        TEST_CHECK(treesFlipped == 0, "so no far tree appears or goes as the eye looks around");
        TEST_CHECK(withTrees > 500 && withoutTrees > 500, "and both sides of the trees line are in the sweep");
        TEST_CHECK(areaFlipped > 1000 && areaTreesFlipped > 100, "where the rule it replaces changed them by the look: the sweep can fail");
        std::printf("      within the budget of %d: %d points compared, %d change level, %d looks left out for being coarsened by it\n",
                    static_cast<int>(DistanceTileLOD::DEFAULT_BUDGET), budgetedCompared, budgetedFlipped, budgetedSkipped);
        TEST_CHECK(budgetedCompared > 10000 && budgetedFlipped == 0, "the same holds under the budget, for every look it leaves alone");
        TEST_CHECK(cappedCompared > 10000 && cappedFlipped == 0, "and under a zoom cap read off the eye's height over the ground");
    }

    void testTopDownIsTheOldCut() {
        // At tilt 90 the distance and the area rule agree: a tile sees the eye head on, so its nearest point and its
        // projected area are the same measure. Within a level, on a plane, whatever the zoom, field of view or screen.
        int worst = 0, compared = 0, nearestFiner = 0, ruleBinds = 0;
        for (double lodFactor : { 1.0, 2.0, 4.0 }) {
        for (double zoom : { 3.4, 6.2, 12.05, 13.5, 15.95, 17.3 }) {
            for (double fov : { 30.0, 36.87, 50.0 }) {
                for (const double* size : SIZES) {
                    Camera camera = orbitCamera(zoom, 25.0, 0.0, fov, size[0], size[1], 1.0);
                    double scale = 1, relaxedFactor = 0;
                    Scene distanceScene = classicScene(camera, zoom, Rule::Distance), areaScene = classicScene(camera, zoom, Rule::Area);
                    distanceScene.lodFactor = areaScene.lodFactor = lodFactor;
                    std::vector<Leaf> now = cull(camera, distanceScene, std::numeric_limits<std::size_t>::max(), scale, relaxedFactor);
                    std::vector<Leaf> old = cull(camera, areaScene, std::numeric_limits<std::size_t>::max(), scale, relaxedFactor);
                    for (const Leaf& a : now) {
                        for (const Leaf& b : old) {
                            if (overlap(a.tile, b.tile)) {
                                compared++;
                                worst = std::max(worst, std::abs(a.tile.zoom - b.tile.zoom));
                                nearestFiner += a.tile.zoom > b.tile.zoom ? 1 : 0;
                                ruleBinds += a.tile.zoom < static_cast<int>(zoom + 0.001) ? 1 : 0;
                            }
                        }
                    }
                }
            }
        }
        }
        std::printf("      top down: %d tiles against the area rule's, at most %d level apart, %d finer, %d under the camera's cap\n", compared, worst, nearestFiner, ruleBinds);
        TEST_CHECK(compared > 1500 && ruleBinds > 200, "the top-down cases include tiles the rule coarsened, not only the camera's cap");
        TEST_CHECK(worst <= 1, "looking straight down, the cut is the area rule's to within a level");
    }

    void testTheTileCountOfATiltedMap() {
        // Grenoble, z16 and a tilted map: how many tiles the area rule held and how many the distance rule asks for,
        // before and after the budget.
        std::printf("      tiles held, classic z16 on a 1000 px screen (area rule | distance rule | within the budget of %d)\n", static_cast<int>(DistanceTileLOD::DEFAULT_BUDGET));
        for (double tilt : { 0.0, 30.0, 45.0, 60.0, 70.0, 75.0 }) {
            Camera camera = orbitCamera(16.0, 20.0, tilt, 36.87, 1000, 1000, 1.0);
            double scale = 1, relaxedFactor = 0;
            std::size_t area = cull(camera, classicScene(camera, 16.0, Rule::Area), std::numeric_limits<std::size_t>::max(), scale, relaxedFactor).size();
            std::size_t distance = cull(camera, classicScene(camera, 16.0, Rule::Distance), std::numeric_limits<std::size_t>::max(), scale, relaxedFactor).size();
            std::size_t budgeted = cull(camera, classicScene(camera, 16.0, Rule::Distance), DistanceTileLOD::DEFAULT_BUDGET, scale, relaxedFactor).size();
            std::printf("      tilt %2.0f: %3d | %3d | %3d (scale %.2f)\n", tilt, static_cast<int>(area), static_cast<int>(distance), static_cast<int>(budgeted), scale);
            TEST_CHECK(budgeted <= DistanceTileLOD::DEFAULT_BUDGET || scale >= DistanceTileLOD::MAX_SCALE, "a tilted classic map fits the tile budget");
        }
    }

    // The mean ratio of the tiles a rule holds to the area rule's, over zooms, screens and densities at one tilt.
    double tileCountAgainstTheAreaRule(double tilt, bool fitted) {
        double sum = 0;
        int cases = 0;
        for (double zoom : { 14.6, 15.8, 16.3, 17.3 }) {
            for (const double* size : SIZES) {
                for (double dpi : { 1.0, 1.8 }) {
                    Camera camera = orbitCamera(zoom, 20.0, tilt, 36.87, size[0], size[1], dpi);
                    double scale = 1, relaxedFactor = 0;
                    Scene scene = classicScene(camera, zoom, Rule::Distance);
                    if (!fitted) {
                        scene.limits.zoomCap = std::numeric_limits<int>::max();
                        scene.farCoarsening = 0.5;
                    }
                    double now = cull(camera, scene, std::numeric_limits<std::size_t>::max(), scale, relaxedFactor).size();
                    double old = cull(camera, classicScene(camera, zoom, Rule::Area), std::numeric_limits<std::size_t>::max(), scale, relaxedFactor).size();
                    sum += std::log(now / old);
                    cases++;
                }
            }
        }
        return std::exp(sum / cases);
    }

    void testTheTileCountIsTheOldRules() {
        // The Crosscall drew 2.2x the area rule's tiles at z16.3 and tilt 10 (SDK convention) with no zoom cap and the
        // grazing exponent at 0.5. Tilt here is from straight down, 80 being that one.
        std::printf("      tiles held against the area rule, mean ratio (fitted | no zoom cap, exponent 0.5)\n");
        double worst = 0, unfitted = 0;
        for (double tilt : { 0.0, 28.0, 45.0, 60.0, 70.0, 80.0 }) {
            double ratio = tileCountAgainstTheAreaRule(tilt, true);
            double first = tileCountAgainstTheAreaRule(tilt, false);
            std::printf("      tilt %2.0f: %.2f | %.2f\n", tilt, ratio, first);
            worst = std::max(worst, std::abs(std::log(ratio)));
            unfitted = std::max(unfitted, first);
        }
        TEST_CHECK(worst < std::log(1.25), "a classic map holds the area rule's tile count to within 25 % at every tilt, 0 to 80");
        TEST_CHECK(unfitted > 1.5, "where the cut without the cap and the fit held more than half as many again: the check can fail");
    }

    void testNodeRule() {
        DistanceTileLOD::View view;
        view.focalPixels = 2700;
        view.maxEdgePixels = 512;
        view.zoom0Distance = 1.0e9;
        view.zoomShift = 0.001;
        const double inf = std::numeric_limits<double>::infinity();
        auto at = [&](int level) { return view.zoom0Distance / std::pow(2.0, level); }; // where the view zoom reads `level`

        DistanceTileLOD::Limits limits { 5, 20, 0, 3, std::numeric_limits<int>::max() };
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, limits, 4, 1000.0, at(16), at(16)) == inf, "under the layer's min zoom a tile always splits");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, limits, 16, 1000.0, at(16), at(16)) == 0.0, "at the view level a tile never splits");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, limits, 16, 1000.0, at(18), at(18)) > 0.0, "though it does where the view level is two finer");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, limits, 12, 1000.0, at(16), at(16)) == inf, "under the floor, coarsening under the view level, it splits though the rule would not");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 20, 0, 3, 9 }, 12, 1.0, at(16), at(16)) != inf, "unless the floor's own ceiling is under it");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 20, 2, -1, 99 }, 17, 1.0, at(16), at(16)) > 0.0 && DistanceTileLOD::nodeCritical(view, { 5, 20, 2, -1, 99 }, 18, 1.0, at(16), at(16)) == 0.0,
                   "a cap offset lets the tiles go that many levels over the view level");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 14, 0, -1, 99 }, 14, 1.0, at(20), at(20)) == 0.0, "the data's max zoom caps whatever the view level is");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 14, DistanceTileLOD::NO_VIEW_CAP, -1, 99 }, 13, 1000.0, at(10), at(10)) != 0.0, "and with no view cap only the data caps");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 20, 0, -1, 99, 16 }, 16, 1.0, at(18), at(18)) == 0.0 && DistanceTileLOD::nodeCritical(view, { 5, 20, 0, -1, 99, 16 }, 15, 1.0, at(18), at(18)) > 0.0,
                   "a zoom cap holds a tile at its level though the view level of its distance is two finer");
        TEST_CHECK(DistanceTileLOD::nodeCritical(view, { 5, 20, 0, -1, 99 }, 10, 200.0, at(14) * 0.5, at(14) * 0.5) == DistanceTileLOD::criticalScale(200.0, at(14) * 0.5, 2700.0, 512.0),
                   "in between it is the rule");

        // A zoom is a distance: the camera's own zoom at its focus distance, one less for every doubling.
        double distance = at(16);
        TEST_CHECK(std::abs(DistanceTileLOD::viewZoom(view, distance) - 16.001) < 1e-9, "the view zoom at the focus distance is the camera's zoom");
        TEST_CHECK(std::abs(DistanceTileLOD::viewZoom(view, distance * 2.0) - 15.001) < 1e-9, "and falls a level for every doubling of the distance");
        TEST_CHECK(std::isinf(DistanceTileLOD::viewZoom(view, 0.0)), "an eye inside the tile has no distance to read a zoom off");
        TEST_CHECK(DistanceTileLOD::viewLevel(view, 0.0) == DistanceTileLOD::NO_VIEW_CAP, "and its level is bounded");
        TEST_CHECK(DistanceTileLOD::styleZoom(view, { 5, 20, 0, -1, 99, 16 }, at(19)) < 17.0 && DistanceTileLOD::styleZoom(view, { 5, 20, 0, -1, 99, 16 }, at(19)) >= 16.0,
                   "a tile the zoom cap holds at z16 is styled under z17, though its nearest ground reads zoom 19");
        TEST_CHECK(DistanceTileLOD::styleZoom(view, { 5, 20, 0 }, at(12)) == DistanceTileLOD::viewZoom(view, at(12)), "and by its view zoom where the cap is above it");

        // The grazing allowance: a tile seen head on is judged by pixels alone, one seen at a grazing angle may span more.
        view.farCoarsening = 0.5;
        TEST_CHECK(DistanceTileLOD::farAllowance(view, 1000.0, 1000.0) == 1.0, "straight below the eye the allowance is 1");
        TEST_CHECK(std::abs(DistanceTileLOD::farAllowance(view, 1000.0, 250.0) - 2.0) < 1e-9, "four times as far as high, a tile may span twice the pixels");
        TEST_CHECK(std::abs(DistanceTileLOD::farAllowance(view, 1000.0, 0.0) - std::sqrt(1.0 / DistanceTileLOD::MIN_GRAZING)) < 1e-9, "level with the eye it is bounded, by the grazing floor");
        view.farCoarsening = 0.0;
        TEST_CHECK(DistanceTileLOD::farAllowance(view, 1000.0, 10.0) == 1.0, "and none with no exponent");
    }

    void testTheViewLevelBoundsTheClassicCut() {
        // Whatever the tilt, no tile is finer than the zoom a top-down camera at the nearest ground would be at (a
        // tile is only split while under the view level of its own nearest point, which no tile has nearer than the
        // eye's height). The camera's zoom is not an input of the cut at all.
        int tooFine = 0, cases = 0;
        for (double tilt : { 0.0, 30.0, 60.0, 75.0 }) {
            for (double zoom : { 14.3, 16.0, 17.8 }) {
                Camera camera = orbitCamera(zoom, 20.0, tilt, 36.87, 1000, 1000, 1.0);
                Scene scene = classicScene(camera, zoom, Rule::Distance);
                double scale = 1, relaxedFactor = 0;
                int finest = 0;
                for (const Leaf& leaf : cull(camera, scene, std::numeric_limits<std::size_t>::max(), scale, relaxedFactor)) {
                    finest = std::max(finest, leaf.tile.zoom);
                }
                cases++;
                tooFine += finest > DistanceTileLOD::viewLevel(makeView(camera), camera.z) ? 1 : 0;
            }
        }
        TEST_CHECK(cases == 12 && tooFine == 0, "no classic tile is finer than the view zoom at the ground under the eye");
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
                            Quality quality = measure(camera, Scene(), budget);
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
            std::vector<Leaf> leaves = cull(camera, Scene(), budget, scale, relaxedFactor);
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
            std::vector<Leaf> leaves = cull(camera, Scene(), budget, scale, relaxedFactor);
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

    testNodeRule();
    testTheViewLevelBoundsTheClassicCut();
    testTheLevelIsTheDistances();
    testTopDownIsTheOldCut();
    testTheTileCountOfATiltedMap();
    testTheTileCountIsTheOldRules();
}
