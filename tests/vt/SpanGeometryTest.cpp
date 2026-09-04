/*
 * The geometry behind LineElevationMode::SPAN - a bridge deck laid straight between its portals.
 *
 * Every rule here was got wrong once against real data, and none of them fails loudly: the deck
 * sags, or a shield sits on the ground under it, and the renderer reports nothing. The numbers
 * come from the Millau viaduct at 44.07-44.09 N - 2460 m long at a 3.025% grade, arriving from
 * mapbox as three tile-cut pieces.
 */

#include "SpanGeometry.h"

#include <cmath>

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    // One metre of the normalized world, the unit SpanGeometry's own constants are in. It is an
    // EQUATOR metre: the same normalized distance is cos(lat) fewer metres on the ground, so the
    // allowances below are ~0.72x these figures at the viaduct's latitude.
    constexpr double METRE = 1.0 / 40075017.0;

    cglib::vec2<double> at(double eastMetres, double northMetres) {
        return cglib::vec2<double>(eastMetres * METRE, northMetres * METRE);
    }

    bool near(double a, double b) {
        return std::abs(a - b) < 1.0e-9;
    }
}

void testSpanGeometry() {
    // An end the TILE cut is not a portal; one inside it is. Tested against the tile and not the
    // clip box, which is the fix for the deck arriving as three pieces whose cuts all read as
    // real ends - each fragment then drew its own chord, giving two 30% ramps and a middle.
    TEST_CHECK(SpanGeometry::isPortal(cglib::vec2<float>(0.5f, 0.5f)),
               "an end inside the tile is the feature's own");
    TEST_CHECK(!SpanGeometry::isPortal(cglib::vec2<float>(1.02f, 0.5f)),
               "an end past the tile edge is where the tile cut it");
    TEST_CHECK(!SpanGeometry::isPortal(cglib::vec2<float>(0.5f, -0.02f)),
               "...and so is one in the buffer on the low side");
    TEST_CHECK(!SpanGeometry::isPortal(cglib::vec2<float>(1.0f, 0.5f)),
               "an end ON the tile edge is a cut, not a portal");

    // The deck is straight in ELEVATION whatever the ground does between the portals: that is the
    // whole feature. A DSM spike off the deck used to lift the middle by 60 m.
    TEST_CHECK(near(SpanGeometry::chordHeight(100, 200, 0.0), 100.0), "the deck starts at its first portal");
    TEST_CHECK(near(SpanGeometry::chordHeight(100, 200, 1.0), 200.0), "...ends at its second");
    TEST_CHECK(near(SpanGeometry::chordHeight(100, 200, 0.25), 125.0), "...and interpolates in between");

    cglib::vec2<double> south = at(0, 0), north = at(0, 2460);

    // Anything past an abutment is back on the ground - a chord must not extend the deck.
    TEST_CHECK(near(SpanGeometry::chordParam(south, south, north), 0.0), "a point at the south portal is at t=0");
    TEST_CHECK(near(SpanGeometry::chordParam(north, south, north), 1.0), "one at the north portal is at t=1");
    TEST_CHECK(near(SpanGeometry::chordParam(at(0, 1230), south, north), 0.5), "one at mid-span is halfway");
    TEST_CHECK(!SpanGeometry::isOnChord(at(0, 2600), south, north),
               "a point beyond the abutment is not on the chord");

    // A long deck CURVES in plan while its chord is straight. Millau's ~20 km radius puts its
    // middle some 36 m off its own chord, and a fixed 25 m test missed exactly the labels that
    // stand on the bridge - they stayed on the ground 150 m below it.
    TEST_CHECK(SpanGeometry::isOnChord(at(0, 1230), south, north),
               "a label on the deck's centre line is on the chord");
    TEST_CHECK(SpanGeometry::isOnChord(at(36, 1230), south, north),
               "...and one 36 m off it, where the deck curves away, still is");
    TEST_CHECK(!SpanGeometry::isOnChord(at(200, 1230), south, north),
               "but a road 200 m away is not the bridge");

    // The allowance has to stay tight on a short bridge, where a road passing UNDERNEATH is the
    // thing that must not pick the deck up.
    TEST_CHECK(!SpanGeometry::isOnChord(at(30, 20), south, at(0, 40)),
               "a road 30 m from a 40 m footbridge is not on it");

    // Two pieces are one structure when the ends the TILE CUT meet. The source's buffer makes
    // neighbouring copies overlap rather than touch, so this is proximity - measured at 13-18 m
    // between the Millau pieces at z15, against gaps of 442-485 m where a piece was missing.
    double tolerance2 = (100 * METRE) * (100 * METRE);
    cglib::vec2<double> a0 = at(0, 0), a1 = at(0, 1000);
    cglib::vec2<double> b0 = at(0, 985), b1 = at(0, 2000); // overlapping by the buffer
    TEST_CHECK(SpanGeometry::piecesMeet(a0, a1, true, false, b0, b1, false, true, tolerance2),
               "two pieces whose cuts meet are one structure");
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, false, at(0, 1450), at(0, 2000), false, true, tolerance2),
               "a gap too wide is a piece missing, not a join");
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, true, b0, b1, true, true, tolerance2),
               "a real portal ends the run, so it never continues into another piece");

    // A chord must reach across the pieces it was collected from. At z15 the viaduct's two
    // northern tiles each hold a copy of the SAME abutment, so the group collected two portals
    // 45 m apart while its pieces ran 1305 m - and that passed every other test here, so the
    // deck resolved to a 45 m chord instead of borrowing its 3440 m one from the cache.
    auto span2 = [](double metres) { return (metres * METRE) * (metres * METRE); };
    TEST_CHECK(!SpanGeometry::chordSpansGroup(span2(45), span2(1305)),
               "two portals on the same abutment do not span a kilometre of deck");
    TEST_CHECK(SpanGeometry::chordSpansGroup(span2(2460), span2(2460)),
               "a chord between the structure's own two ends spans it exactly");
    TEST_CHECK(SpanGeometry::chordSpansGroup(span2(2460), span2(2500)),
               "...and the buffer overlap reaching past it by a fraction still does");
    TEST_CHECK(!SpanGeometry::chordSpansGroup(span2(1230), span2(2460)),
               "half a deck is a missing portal, not a chord");

    // A bed polygon and an extruded deck both need the ring's two ENDS, and a ring has none: the
    // span is its longest axis. Getting this wrong puts the chord across the deck's WIDTH, which
    // resolves as a metre-long span and leaves the deck on the ground.
    {
        // A deck-shaped ring: 2460 m long, 32 m wide, wound as a rectangle.
        std::vector<cglib::vec2<float>> ring;
        for (int i = 0; i <= 10; i++) {
            ring.push_back(cglib::vec2<float>(at(-16, i * 246.0)(0), at(-16, i * 246.0)(1)));
        }
        for (int i = 10; i >= 0; i--) {
            ring.push_back(cglib::vec2<float>(at(16, i * 246.0)(0), at(16, i * 246.0)(1)));
        }
        auto ends = SpanGeometry::farthestPair(ring);
        double length = std::sqrt(cglib::norm(ends.second - ends.first)) / METRE;
        TEST_CHECK(length > 2400 && length < 2500,
                   "a deck-shaped ring spans its LENGTH, not its width");
        // ...and the pair must be one from each end, not two corners of the same abutment.
        double across = std::abs(static_cast<double>(ends.first(1) - ends.second(1))) / METRE;
        TEST_CHECK(across > 2400, "...with one end at each abutment");
    }
    TEST_CHECK(SpanGeometry::farthestPair(std::vector<cglib::vec2<float>>()).first == cglib::vec2<float>(0, 0),
               "an empty ring has no span, and must not read off the end of it");

    // A crossing structure must not be absorbed: the D41 passes under the viaduct near its foot.
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, false, at(-500, 990), at(500, 990), false, false, tolerance2),
               "a crossing bridge is a different structure");
    // The two carriageways are digitised in opposite senses, so either direction counts.
    TEST_CHECK(SpanGeometry::piecesMeet(a0, a1, true, false, at(0, 2000), b0, true, false, tolerance2),
               "a piece digitised the other way round still joins");

    // A stranded piece names the tile its far end is in: the point is stepped PAST the cut,
    // because the source's buffer leaves the cut itself inside the neighbouring copy's overlap.
    {
        cglib::vec2<double> e0(0.0, 0.0), e1(-0.001, 0.0);
        cglib::vec2<double> beyond = SpanGeometry::beyondCutEnd(e0, e1, 14);
        TEST_CHECK(near(beyond(1), 0.0) && near(beyond(0), SpanGeometry::CUT_STEP_FRACTION / 16384.0),
                   "the point past a cut end lies along the piece, a twentieth of a tile beyond it");
        TEST_CHECK(SpanGeometry::beyondCutEnd(e0, e0, 14) == e0, "a zero-length piece steps nowhere");
    }
    // A span drape covers the deck's bounds alone; the bounds grow by a margin and stay in the tile,
    // the sampling transform follows them, and the bake's clip zoom puts them on the whole square.
    {
        cglib::vec4<float> bounds = SpanGeometry::expandBounds(cglib::vec4<float>(0.40f, 0.02f, 0.60f, 0.90f), 0.05f);
        auto nearf = [](float a, double b) { return std::abs(a - b) < 1.0e-6; };
        TEST_CHECK(nearf(bounds(0), 0.35) && nearf(bounds(1), 0.0) && nearf(bounds(2), 0.65) && nearf(bounds(3), 0.95),
                   "bounds grow by the margin on every side and clamp at the tile edge");
        cglib::vec4<float> transform = SpanGeometry::drapeTransformInBounds(cglib::vec4<float>(0.0f, 0.0f, 1.0f, 1.0f), cglib::vec4<float>(0.25f, 0.5f, 0.75f, 1.0f));
        TEST_CHECK(near(transform(0), -0.5) && near(transform(1), -1.0) && near(transform(2), 2.0) && near(transform(3), 2.0),
                   "a tile position maps into the bounds' share of the texture");
        cglib::vec4<float> sub = SpanGeometry::drapeTransformInBounds(cglib::vec4<float>(0.5f, 0.5f, 0.5f, 0.5f), cglib::vec4<float>(0.25f, 0.5f, 0.75f, 1.0f));
        TEST_CHECK(near(sub(0), 0.5) && near(sub(1), 0.0) && near(sub(2), 1.0) && near(sub(3), 1.0),
                   "...also through an ancestor's sub-rect: the north-east child of that tile fills the bounds' right half");
        cglib::mat4x4<float> zoom = SpanGeometry::clipZoomToBounds(cglib::vec4<float>(0.25f, 0.5f, 0.75f, 1.0f));
        cglib::vec3<float> lo = cglib::transform_point(cglib::vec3<float>(-0.5f, 0.0f, 0.0f), zoom);  // uv (0.25, 0.5) in clip
        cglib::vec3<float> hi = cglib::transform_point(cglib::vec3<float>(0.5f, 1.0f, 0.0f), zoom);   // uv (0.75, 1.0)
        TEST_CHECK(near(lo(0), -1.0) && near(lo(1), -1.0) && near(hi(0), 1.0) && near(hi(1), 1.0),
                   "the clip zoom sends the bounds' corners to the corners of the bake square");
    }
}
