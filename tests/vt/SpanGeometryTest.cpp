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
    // Near is not enough: a NEIGHBOURING bridge crossing the same tile edge is parallel and, at
    // z14, well inside a 245 m radius - so the Seine's bridges chained into one group and no chord
    // could span it. A continuation lies on the same line; a neighbour is off it by its spacing.
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, false, at(60, 985), at(60, 2000), false, true, tolerance2),
               "a parallel bridge 60 m away is a neighbour, not a continuation");
    TEST_CHECK(SpanGeometry::piecesMeet(a0, a1, true, false, at(4, 985), at(60, 2000), false, true, tolerance2),
               "...while a piece that bends away past the cut still continues it");
    TEST_CHECK(!SpanGeometry::piecesMeet(a0, a1, true, false, at(30, 985), at(30, 2000), false, true, tolerance2),
               "...and one 30 m to the side is already the next structure");

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

    // A deck polygon takes the chord of the road it carries: its own corners overhang the bank,
    // where the drawn surface is pulled down by the water beside it (Petit-Pont: 1.3 m under the
    // road's own ends). The tiler splits the road at every junction, so the deck overlaps each
    // piece partly and its midpoint is on none of them in particular.
    TEST_CHECK(SpanGeometry::chordOverlap(at(2, -8), at(-2, 68), at(0, 0), at(0, 40)) > 0,
               "a deck overlapping a road piece by more than half the piece is on it");
    TEST_CHECK(SpanGeometry::chordOverlap(at(2, -8), at(-2, 68), at(0, 30), at(0, 70)) > 0,
               "...and the second piece of the same road too");
    TEST_CHECK(SpanGeometry::chordOverlap(at(2, -8), at(-2, 68), at(0, 60), at(0, 100)) == 0,
               "but a piece it barely reaches is the approach, not the deck's road");
    // A ring's chord is CORNER to corner: on a short wide deck it runs diagonally, 31 degrees off
    // the road at Petit-Pont, and the deck stayed on its own corners under the road.
    TEST_CHECK(SpanGeometry::chordOverlap(at(-30, -8), at(30, 68), at(0, 0), at(0, 40)) > 0,
               "a wide deck's diagonal chord still adopts the road along it");
    TEST_CHECK(SpanGeometry::chordOverlap(at(-40, 30), at(40, 30), at(0, 0), at(0, 60)) == 0,
               "while a road crossing under the bridge is not its road");
    TEST_CHECK(SpanGeometry::chordOverlap(at(2, -8), at(-2, 68), at(40, 0), at(40, 40)) == 0,
               "and a parallel road 40 m away is another street");

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

    // The deck's portals are the CENTRES of its ends, on the road it carries - not its farthest
    // corners, which on a short wide deck run diagonally and end over the bank beside the road,
    // where the drawn surface is pulled down by the water (Petit-Pont: 1.3 m under the road).
    {
        std::vector<cglib::vec2<float>> deck = { cglib::vec2<float>(0, 0), cglib::vec2<float>(20, 0), cglib::vec2<float>(20, 30), cglib::vec2<float>(0, 30) };
        auto ends = SpanGeometry::endCentres(deck);
        TEST_CHECK(std::abs(ends.first(0) - 10) < 0.01f && std::abs(ends.first(1) - 0) < 0.01f,
                   "a 30 x 20 deck's first portal is the middle of its south edge");
        TEST_CHECK(std::abs(ends.second(0) - 10) < 0.01f && std::abs(ends.second(1) - 30) < 0.01f,
                   "...and its second the middle of its north edge, whatever the corners");
        std::vector<cglib::vec2<float>> narrow = { cglib::vec2<float>(0, 0), cglib::vec2<float>(4, 0), cglib::vec2<float>(4, 50), cglib::vec2<float>(4, 100), cglib::vec2<float>(0, 100), cglib::vec2<float>(0, 50) };
        ends = SpanGeometry::endCentres(narrow);
        TEST_CHECK(std::abs(ends.first(1) - 0) < 0.01f && std::abs(ends.second(1) - 100) < 0.01f && std::abs(ends.first(0) - 2) < 0.01f,
                   "a side vertex halfway along a long deck belongs to neither end");
    }

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
    // A stranded piece borrows its OWN feature's chord when it kept a portal - the chord that ends
    // there - and only a piece cut at both ends takes the longest chord over its midpoint.
    {
        struct Chord { cglib::vec2<double> portal0, portal1; };
        std::vector<Chord> chords = {
            { at(0, 0), at(0, 1000) },     // the whole deck's chord, first in the cache
            { at(0, 0), at(0, 200) },      // an abutment feature's own chord
            { at(500, 100), at(600, 100) } // a crossing street, over neither piece
        };
        auto own = SpanGeometry::borrowChord(at(0, 0), true, at(0, 120), false, chords.begin(), chords.end());
        TEST_CHECK(own == chords.begin() + 1, "a piece with its own portal takes the chord ending there, not the first hit");
        auto middle = SpanGeometry::borrowChord(at(0, 60), false, at(0, 140), false, chords.begin(), chords.end());
        TEST_CHECK(middle == chords.begin(), "a piece cut at both ends takes the longest chord over its midpoint");
        TEST_CHECK(SpanGeometry::borrowChord(at(300, 100), false, at(300, 120), false, chords.begin(), chords.end()) == chords.end(),
                   "and a piece on no chord borrows nothing");
    }
    // The same deck from two source tiles resolves two chords whose ends sit metres apart; the
    // shorter lies on the longer and is the same structure. A bridge further along is not.
    {
        cglib::vec2<double> south = at(0, 0), north = at(0, 240);
        TEST_CHECK(SpanGeometry::chordLiesOn(at(4, 20), at(-3, 232), south, north),
                   "a copy clipped 20 m short at one end still lies on the full chord");
        TEST_CHECK(!SpanGeometry::chordLiesOn(at(0, 200), at(0, 400), south, north),
                   "a chord running past the far portal is another structure");
        TEST_CHECK(!SpanGeometry::chordLiesOn(at(60, 20), at(60, 220), south, north),
                   "and so is a parallel one 60 m aside");
    }
}
