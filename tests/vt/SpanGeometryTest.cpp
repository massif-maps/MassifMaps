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

    // A crossing structure must not be absorbed: the D41 passes under the viaduct near its foot.
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, false, at(-500, 990), at(500, 990), false, false, tolerance2),
               "a crossing bridge is a different structure");
    // The two carriageways are digitised in opposite senses, so either direction counts.
    TEST_CHECK(SpanGeometry::piecesMeet(a0, a1, true, false, at(0, 2000), b0, true, false, tolerance2),
               "a piece digitised the other way round still joins");
}
