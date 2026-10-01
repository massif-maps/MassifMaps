// maplibre's getAnchors/checkMaxAngle (LineAnchors.h). Rue de la Viscose (Échirolles) jogs 90 degrees at
// its middle, where the old generator put its one anchor, so the max-angle test dropped the name.

#include "TestCheck.h"

#include <mapnikvt/LineAnchors.h>

#include <cmath>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    constexpr float MAX_ANGLE = 0.785398f; // maplibre's default text-max-angle, 45 degrees

    struct Line {
        std::vector<cglib::vec2<float>> points;
        std::vector<float> lengths;
    };

    Line polyline(std::vector<cglib::vec2<float>> points) {
        Line line { std::move(points), {} };
        for (std::size_t i = 0; i < line.points.size(); i++) {
            line.lengths.push_back(i == 0 ? 0.0f : line.lengths.back() + cglib::length(line.points[i] - line.points[i - 1]));
        }
        return line;
    }

    std::vector<float> anchors(const Line& line, bool continued, float step, float label, float glyph) {
        return mvt::lineLabelAnchors(line.points, line.lengths, continued, step, label, glyph, MAX_ANGLE, [](float) { return true; });
    }
}

void testLineAnchors() {
    // 200 px straight, an 80 px jog at 90 degrees, 220 px straight: a 140 px name fits the first stretch
    Line viscose = polyline({ { 0, 0 }, { 200, 0 }, { 200, 80 }, { 420, 80 } });
    {
        std::vector<float> at = anchors(viscose, false, 540, 140, 12);
        TEST_CHECK(at.size() == 1, "one anchor on a line shorter than the step");
        TEST_CHECK(!at.empty() && at[0] == 70 + 24, "half the label plus two ems from the start, on the first stretch");
        TEST_CHECK(mvt::checkLineLabelMaxAngle(viscose.points, viscose.lengths, 250, 140, 12 * mvt::LINE_LABEL_ANGLE_WINDOW, MAX_ANGLE) == false,
                   "the middle, where the old generator put it, turns 90 degrees within the run");
    }

    // stretches too short for the name on either side of the jog: maplibre drops it
    Line cramped = polyline({ { 0, 0 }, { 100, 0 }, { 100, 80 }, { 200, 80 } });
    TEST_CHECK(anchors(cramped, false, 540, 140, 12).empty(), "no straight stretch holds the name, so there is no anchor");
    // a billboard is not laid along the line, so the jog does not reject it: it takes the middle
    {
        std::vector<float> at = anchors(cramped, false, 540, 140, 0);
        TEST_CHECK(at.size() == 1, "a billboard still gets one anchor");
    }

    TEST_CHECK(anchors(polyline({ { 0, 0 }, { 100, 0 } }), false, 540, 140, 12).empty(), "a line shorter than the label carries none");

    // tested at half its size, the label keeps the offset of the whole label: halving it too put Rue
    // Émile Zola's name across its bend, short of the straight stretch maplibre uses
    {
        Line line = polyline({ { 0, 0 }, { 150, 0 } });
        std::vector<float> at = mvt::lineLabelAnchors(line.points, line.lengths, false, 540, 140, 12, MAX_ANGLE, [](float) { return true; }, 0.5f);
        TEST_CHECK(at.size() == 1 && at[0] == 70 + 24, "half the whole label plus two ems, fitted at half size");
    }

    // text-max-angle is the style's: a 35-degree bend under the run passes maplibre's 45, not a 30
    {
        Line bent = polyline({ { 0, 0 }, { 100, 0 }, { 100 + 100 * std::cos(0.61f), 100 * std::sin(0.61f) } });
        auto at = [&](float maxAngle) { return mvt::lineLabelAnchors(bent.points, bent.lengths, false, 540, 140, 12, maxAngle, [](float) { return true; }); };
        TEST_CHECK(at(MAX_ANGLE).size() == 1, "a 35-degree bend is followed under the default 45");
        TEST_CHECK(at(0.523599f).empty(), "a 35-degree bend drops the run under text-max-angle 30");
    }

    // continued from the next tile, the first anchor is half a step in, as before
    {
        std::vector<float> at = anchors(polyline({ { 0, 0 }, { 2000, 0 } }), true, 540, 140, 12);
        TEST_CHECK(at.size() == 4 && at[0] == 270, "half a step, then a step apart");
    }
    // Oneway arrows (marker-spacing): the first sits half a spacing into the WAY. Read off its first
    // segment, a 4 px stub at a junction put an arrow on the junction itself, on every way of a city.
    TEST_CHECK(std::fabs(mvt::lineMarkerStart(600, 200, 16) - 100) < 1e-4f, "a long way starts half a spacing in");
    TEST_CHECK(std::fabs(mvt::lineMarkerStart(80, 200, 16) - 40) < 1e-4f, "a way shorter than the spacing gets one at its middle");
    TEST_CHECK(mvt::lineMarkerStart(30, 200, 16) < 0, "a stub too short for two arrows gets none");
}
