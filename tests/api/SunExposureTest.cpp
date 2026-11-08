// Whether terrain hides the sun from a point (terrain/SunExposure.h), over a synthetic height field. The
// accessor stands in for the DEM grids: it answers a height, the highest ground of the block it sits in and
// the distance to that block's edge, which is all the march reads.

#include "terrain/SunExposure.h"

#include "TestCheck.h"

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <utility>

using namespace massif;

namespace {
    using Height = std::function<double(double, double)>;

    // The ground around a point at (originX, originY), blocks of `block` metres, posts every `texel(d)`
    // metres when `snap` (what a coarse grid does to a distant ridge), exact otherwise.
    struct Field {
        Height height;
        double originX = 0, originY = 0;
        double block = 0;       // 0: no block, so no skip and a tileMax nothing can exceed
        bool snap = false;
        int* calls = nullptr;
        std::function<bool(double, double)> hole;
        std::shared_ptr<std::map<std::pair<int, int>, double> > maxima = std::make_shared<std::map<std::pair<int, int>, double> >();

        double blockMax(int bx, int by) const {
            auto it = maxima->find({ bx, by });
            if (it != maxima->end()) {
                return it->second;
            }
            double best = -1e9;
            for (double y = by * block; y <= (by + 1) * block; y += 25) {
                for (double x = bx * block; x <= (bx + 1) * block; x += 25) {
                    best = std::max(best, height(x, y));
                }
            }
            return (*maxima)[{ bx, by }] = best + 1.0;
        }

        double sampleAt(double x, double y, double texel) const {
            if (!snap) {
                return height(x, y);
            }
            double fx = x / texel, fy = y / texel;
            double x0 = std::floor(fx), y0 = std::floor(fy);
            double tx = fx - x0, ty = fy - y0;
            auto h = [&](double i, double j) { return height(i * texel, j * texel); };
            return (h(x0, y0) * (1 - tx) + h(x0 + 1, y0) * tx) * (1 - ty) + (h(x0, y0 + 1) * (1 - tx) + h(x0 + 1, y0 + 1) * tx) * ty;
        }

        bool operator()(double east, double north, double texel, double dirEast, double dirNorth, SunExposure::GroundSample& out) const {
            if (calls) {
                (*calls)++;
            }
            double x = originX + east, y = originY + north;
            if (hole && hole(east, north)) {
                return false;
            }
            out.height = static_cast<float>(sampleAt(x, y, texel));
            out.coarse = false;
            if (block <= 0) {
                out.tileMax = 1e6f;
                out.exitDistance = 0;
                return true;
            }
            int bx = static_cast<int>(std::floor(x / block)), by = static_cast<int>(std::floor(y / block));
            out.tileMax = static_cast<float>(blockMax(bx, by));
            double exitX = dirEast > 0 ? ((bx + 1) * block - x) / dirEast : (dirEast < 0 ? (bx * block - x) / dirEast : 1e18);
            double exitY = dirNorth > 0 ? ((by + 1) * block - y) / dirNorth : (dirNorth < 0 ? (by * block - y) / dirNorth : 1e18);
            out.exitDistance = std::max(0.0, std::min(exitX, exitY));
            return true;
        }
    };

    Field flatField(double z) {
        Field field;
        field.height = [z](double, double) { return z; };
        return field;
    }

    // A wall of height H across x in [distance, distance + 50]: the only ground that can hide an eastern sun.
    Field wallEast(double distance, double wallHeight) {
        Field field;
        field.height = [=](double x, double) { return (x >= distance && x < distance + 50) ? wallHeight : 0.0; };
        return field;
    }

    SunExposure::Params exactParams() {
        SunExposure::Params params;
        params.eyeHeight = 0;
        return params;
    }

    double wallAngle(double distance, double wallHeight) {
        return std::atan(wallHeight / distance) * SunExposure::RAD_TO_DEG;
    }

    void testFlatIsLit() {
        SunExposure::Params params;
        SunExposure::Point point = SunExposure::exposure(flatField(100), 90, 30, params, 100);
        TEST_CHECK(point.sunFraction == 1.0, "flat ground is lit");
        TEST_CHECK(point.status == 0, "and nothing is flagged");
        TEST_CHECK(point.shadowDistance < 0, "no shadow distance");
        TEST_CHECK(point.horizon < 0 && point.horizon > -1, "the horizon on flat ground is the curvature dip, not the ground");

        SunExposure::Point low = SunExposure::exposure(flatField(100), 90, 10, params, 100);
        TEST_CHECK(low.sunFraction == 1.0 && low.status == 0, "a 10 degree sun over flat ground is lit with no flag");
    }

    void testWall() {
        SunExposure::Params params = exactParams();
        double distance = 2000, wallHeight = 500;
        double edge = wallAngle(distance, wallHeight);
        Field wall = wallEast(distance, wallHeight);

        SunExposure::Point under = SunExposure::exposure(wall, 90, edge - 1.0, params, wallHeight);
        TEST_CHECK(under.sunFraction == 0.0, "a sun a degree under the wall's top is hidden");
        TEST_CHECK(under.shadowDistance >= distance && under.shadowDistance < distance + 50, "and the shadow starts at the wall");
        SunExposure::Point over = SunExposure::exposure(wall, 90, edge + 1.0, params, wallHeight);
        TEST_CHECK(over.sunFraction == 1.0, "a degree over it, lit");
        TEST_CHECK(over.horizon > edge - 0.1 && over.horizon < edge + 0.01, "with the wall as the horizon");
        TEST_CHECK(SunExposure::exposure(wall, 270, edge - 1.0, params, wallHeight).sunFraction == 1.0, "a sun on the other side of the wall is lit");
        TEST_CHECK(SunExposure::exposure(wall, 0, edge - 1.0, params, wallHeight).sunFraction == 1.0, "and one that is off to the side");

        SunExposure::Point nearTop = SunExposure::exposure(wall, 90, edge, params, wallHeight);
        SunExposure::Point atHorizon = SunExposure::exposure(wall, 90, nearTop.horizon, params, wallHeight);
        TEST_CHECK(std::fabs(atHorizon.sunFraction - 0.5) < 1e-9, "the sun's centre on the edge shows half its disc");
        SunExposure::Point below = SunExposure::exposure(wall, 90, nearTop.horizon - 0.1, params, wallHeight);
        SunExposure::Point above = SunExposure::exposure(wall, 90, nearTop.horizon + 0.1, params, wallHeight);
        TEST_CHECK(below.sunFraction < 0.5 && below.sunFraction > 0 && above.sunFraction > 0.5 && above.sunFraction < 1, "less than half under it, more over it, inside the disc");

        TEST_CHECK(std::fabs(SunExposure::visibleFraction(10, 10 - 0.2666, 0.2666) - 1.0) < 1e-12, "a disc's radius over the edge is whole");
        TEST_CHECK(std::fabs(SunExposure::visibleFraction(10, 10 + 0.2666, 0.2666)) < 1e-12, "a radius under it is gone");

        SunExposure::Params tall = exactParams();
        tall.eyeHeight = 400;
        TEST_CHECK(SunExposure::exposure(wall, 90, edge - 1.0, tall, wallHeight).sunFraction == 1.0, "the eye above most of the wall sees past it");
    }

    void testCurvature() {
        // 1000 m of ground 20 km out is 2.862 degrees flat, 2.784 with the earth's drop.
        SunExposure::Params params = exactParams();
        params.discRadius = 0.01;
        Field far;
        far.height = [](double x, double) { return (x >= 20000 && x < 20100) ? 1000.0 : 0.0; };
        TEST_CHECK(SunExposure::exposure(far, 90, 2.82, params, 1000).sunFraction == 1.0, "the earth's curvature drops a distant ridge under the sun");
        params.earthRadius = 1e15;
        TEST_CHECK(SunExposure::exposure(far, 90, 2.82, params, 1000).sunFraction == 0.0, "which a flat earth would have kept above it");
    }

    void testDark() {
        int calls = 0;
        Field field = flatField(0);
        field.calls = &calls;
        SunExposure::Point point = SunExposure::exposure(field, 90, -5, SunExposure::Params(), 0);
        TEST_CHECK(point.status == SunExposure::DARK && point.sunFraction == 0.0, "a sun well under the horizon is DARK and unlit");
        TEST_CHECK(calls == 0, "and nothing is marched");
        point = SunExposure::exposure(flatField(0), 90, -0.1, SunExposure::Params(), 0);
        TEST_CHECK((point.status & SunExposure::DARK) == 0 && (point.status & SunExposure::LOW_SUN) != 0, "a sun inside the refraction band is low, not dark");
    }

    void testNoDem() {
        SunExposure::Params params = exactParams();
        Field none = flatField(0);
        none.hole = [](double, double) { return true; };
        SunExposure::Point point = SunExposure::exposure(none, 90, 30, params, 0);
        TEST_CHECK((point.status & SunExposure::NO_DEM) != 0 && point.sunFraction == -1.0, "no ground at the point is NO_DEM and unknown");

        Field near = flatField(0);
        near.hole = [](double east, double north) { return east != 0 || north != 0 ? (east > 20 && east < 200) : false; };
        point = SunExposure::exposure(near, 90, 30, params, 0);
        TEST_CHECK((point.status & SunExposure::NO_DEM) != 0 && point.sunFraction == -1.0, "a hole in the near field leaves it unknown");

        Field farHole = flatField(0);
        farHole.hole = [](double east, double) { return east > 1000 && east < 2000; };
        point = SunExposure::exposure(farHole, 90, 5, params, 0);
        TEST_CHECK((point.status & SunExposure::NO_DEM) == 0 && (point.status & SunExposure::COARSE_DEM) != 0 && point.sunFraction == 1.0, "a hole further out only degrades the answer");

        Field wallBehindHole = wallEast(300, 500);
        wallBehindHole.hole = [](double east, double) { return east > 100 && east < 200; };
        point = SunExposure::exposure(wallBehindHole, 90, 5, params, 500);
        TEST_CHECK(point.sunFraction == 0.0 && (point.status & SunExposure::NO_DEM) == 0, "shade found past a near hole is still shade");
    }

    void testTruncated() {
        SunExposure::Params params = exactParams();
        SunExposure::Point low = SunExposure::exposure(flatField(0), 90, 1.0, params, 1000);
        TEST_CHECK((low.status & SunExposure::TRUNCATED) != 0 && (low.status & SunExposure::LOW_SUN) != 0 && low.sunFraction == 1.0, "a low sun lit at the end of the march is flagged TRUNCATED");
        SunExposure::Point high = SunExposure::exposure(flatField(0), 90, 20.0, params, 1000);
        TEST_CHECK((high.status & SunExposure::TRUNCATED) == 0, "a high one ends the march inside the range");
    }

    // A few smooth peaks: slopes up to ~25 degrees, so a ray sees ridges at every distance.
    double peaks(double x, double y) {
        static const double P[][4] = { { 3000, 1500, 1400, 2200 }, { 9000, -2500, 1800, 3000 }, { -4000, 5000, 1200, 2000 }, { 14000, 6000, 2500, 3800 }, { -9000, -6000, 1600, 2600 }, { 2000, -9000, 1000, 1800 } };
        double z = 600;
        for (const auto& peak : P) {
            double dx = x - peak[0], dy = y - peak[1];
            z += peak[3] * std::exp(-(dx * dx + dy * dy) / (2 * peak[2] * peak[2]));
        }
        return z;
    }

    int classOf(double fraction) {
        return fraction >= 0.7 ? 2 : (fraction < 0.3 ? 0 : 1);
    }

    void testExitsAreExact() {
        SunExposure::Params params;
        int compared = 0, equal = 0, shaded = 0;
        double suns[][2] = { { 90, 6 }, { 135, 12 }, { 200, 3 }, { 270, 25 }, { 45, 9 } };
        for (double py = -6000; py <= 6000; py += 1500) {
            for (double px = -6000; px <= 6000; px += 1500) {
                for (const auto& sun : suns) {
                    Field blocks;
                    blocks.height = peaks;
                    blocks.originX = px;
                    blocks.originY = py;
                    blocks.block = 3000;
                    Field full = blocks;
                    full.block = 0;
                    double regionMax = 3500;
                    SunExposure::Point a = SunExposure::exposure(blocks, sun[0], sun[1], params, regionMax);
                    SunExposure::Point b = SunExposure::exposure(full, sun[0], sun[1], params, 1e6);
                    compared++;
                    equal += std::fabs(a.sunFraction - b.sunFraction) < 1e-9 ? 1 : 0;
                    shaded += b.sunFraction < 0.3 ? 1 : 0;
                }
            }
        }
        TEST_CHECK(equal == compared, "the exits and the tile skip never change the answer of a full march");
        TEST_CHECK(shaded > compared / 10 && shaded < compared * 9 / 10, "over a field that really mixes light and shade");
    }

    void testStepAndLodAgreeWithAFineReference() {
        SunExposure::Params fineParams;
        fineParams.firstStep = 2;
        fineParams.stepGrowth = 0.0005;
        SunExposure::Params params; // the defaults under test
        int compared = 0, agree = 0;
        double suns[][2] = { { 90, 6 }, { 135, 12 }, { 200, 3 }, { 270, 25 }, { 45, 9 }, { 160, 5 } };
        for (double py = -7000; py <= 7000; py += 700) {
            for (double px = -7000; px <= 7000; px += 700) {
                for (const auto& sun : suns) {
                    Field fine;
                    fine.height = peaks;
                    fine.originX = px;
                    fine.originY = py;
                    Field coarse = fine;
                    coarse.snap = true;
                    coarse.block = 3000;
                    SunExposure::Point reference = SunExposure::exposure(fine, sun[0], sun[1], fineParams, 1e6);
                    SunExposure::Point point = SunExposure::exposure(coarse, sun[0], sun[1], params, 3500);
                    compared++;
                    agree += classOf(reference.sunFraction) == classOf(point.sunFraction) ? 1 : 0;
                }
            }
        }
        TEST_CHECK(agree >= compared * 98 / 100, "the default steps and the distance-driven texel agree with a fine march on 98% of points");
    }
}

void testSunExposure() {
    testFlatIsLit();
    testWall();
    testCurvature();
    testDark();
    testNoDem();
    testTruncated();
    testExitsAreExact();
    testStepAndLodAgreeWithAFineReference();
}
