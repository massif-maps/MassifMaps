/*
 * The anchor a building reads its ground at when the source hands it over in PIECES
 * (vt::buildExtrusionAnchors). Two things break one building apart, and a piece anchored on its
 * own centroid then stands at its own elevation - equal heights on stepped bases, which reads as
 * a sawtooth of separate slabs where a palace should be one roof:
 *
 *  - the source splits it into parts (measured over the Louvre on mapbox-streets z16: 160
 *    footprints, 33 buildings once parts that share a vertex are joined, base spread within one
 *    building up to 5.6 m).
 *  - the tile grid cuts it, and each side keeps a different piece (measured: 24 buildings cross
 *    the z16 border by the Louvre, their two halves' centroids 6-75 m apart, up to 5.5 m of base).
 *
 * NOT covered here: the ground sample itself, which is the SDK's elevation provider - a device
 * check. This is the geometry of where to sample.
 */

#include "ExtrusionAnchors.h"

#include "TestCheck.h"

#include <cmath>
#include <unordered_map>
#include <vector>

using namespace massif::vt;

namespace {
    using Ring = std::vector<cglib::vec2<float>>;

    const cglib::bbox2<float> UNIT_BOX(cglib::vec2<float>(0, 0), cglib::vec2<float>(1, 1));

    Ring rect(float x0, float y0, float x1, float y1) {
        return { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    }

    ExtrusionFootprint part(long long localId, const Ring& ring, long long buildingId = 0) {
        ExtrusionFootprint footprint;
        footprint.localId = localId;
        footprint.buildingId = buildingId;
        footprint.rings.push_back(ring);
        return footprint;
    }

    bool same(const cglib::vec2<float>& a, const cglib::vec2<float>& b, float tolerance = 1.0e-5f) {
        return std::abs(a(0) - b(0)) < tolerance && std::abs(a(1) - b(1)) < tolerance;
    }

    // What the builder does at draw time: the entry whose bounds hold the drawn ring's centroid.
    cglib::vec2<float> anchorOf(const std::unordered_map<long long, std::vector<ExtrusionAnchor>>& anchors, long long id, const Ring& ring) {
        auto it = anchors.find(id);
        if (it == anchors.end()) {
            return cglib::vec2<float>(1.0e6f, 1.0e6f);
        }
        const ExtrusionAnchor* anchor = findExtrusionAnchor(it->second, ring);
        return anchor ? anchor->anchor : cglib::vec2<float>(1.0e6f, 1.0e6f);
    }
}

void testExtrusionGroupAnchor() {
    // Two parts sharing an edge are one building: both read the ground at the mean of the two
    // rings, not at a centroid each.
    {
        Ring left = rect(0.20f, 0.20f, 0.30f, 0.30f), right = rect(0.30f, 0.20f, 0.40f, 0.30f);
        std::vector<ExtrusionFootprint> footprints { part(1, left), part(2, right) }; // share x = 0.30 exactly
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(anchors.size() == 2, "both parts are anchored");
        TEST_CHECK(same(anchorOf(anchors, 1, left), anchorOf(anchors, 2, right)), "parts that share a vertex stand at one elevation");
        TEST_CHECK(same(anchorOf(anchors, 1, left), cglib::vec2<float>(0.30f, 0.25f)), "on the mean of every outer ring point they own");
    }

    // Parts that touch nothing keep their own ground: they are different buildings, and forcing
    // them together would put one of them at the other's elevation.
    {
        Ring one = rect(0.20f, 0.20f, 0.30f, 0.30f), two = rect(0.60f, 0.60f, 0.70f, 0.70f);
        std::vector<ExtrusionFootprint> footprints { part(1, one), part(2, two) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, one), cglib::vec2<float>(0.25f, 0.25f)) && same(anchorOf(anchors, 2, two), cglib::vec2<float>(0.65f, 0.65f)), "unrelated footprints keep their own centroid");
    }

    // building_id joins parts that do NOT touch - mapbox's grouping, and the only thing that
    // catches a building whose parts the source separated by a courtyard.
    {
        Ring one = rect(0.20f, 0.20f, 0.30f, 0.30f), two = rect(0.60f, 0.20f, 0.70f, 0.30f);
        std::vector<ExtrusionFootprint> footprints { part(1, one, 77), part(2, two, 77) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, one), anchorOf(anchors, 2, two)), "same building_id, same anchor");
        TEST_CHECK(same(anchorOf(anchors, 1, one), cglib::vec2<float>(0.45f, 0.25f)), "at the mean of both parts");
    }

    // A MERGED feature: an OpenMapTiles mbtiles packs a whole tile of unrelated buildings into one
    // multipolygon (Grenoble z15: 3513 footprints, 18 ids). Sharing an id is not sharing a
    // building - one anchor for all of them puts a hillside's buildings under the valley floor.
    {
        Ring one = rect(0.20f, 0.20f, 0.30f, 0.30f), two = rect(0.60f, 0.20f, 0.70f, 0.30f);
        std::vector<ExtrusionFootprint> footprints { part(1, one), part(1, two) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(anchors[1].size() == 2, "one entry per footprint, not per feature");
        TEST_CHECK(same(anchorOf(anchors, 1, one), cglib::vec2<float>(0.25f, 0.25f)), "the drawn ring picks its own");
        TEST_CHECK(same(anchorOf(anchors, 1, two), cglib::vec2<float>(0.65f, 0.25f)), "and the next polygon of the same feature picks its own");
    }

    // The pieces of ONE building, merged into that same feature: they touch, so they still share -
    // per-footprint entries did not cost the grouping the tile-border fix depends on.
    {
        Ring left = rect(0.20f, 0.20f, 0.30f, 0.30f), right = rect(0.30f, 0.20f, 0.40f, 0.30f);
        std::vector<ExtrusionFootprint> footprints { part(1, left), part(1, right) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, left), anchorOf(anchors, 1, right)), "touching parts of one feature keep one anchor");
        TEST_CHECK(same(anchorOf(anchors, 1, left), cglib::vec2<float>(0.30f, 0.25f)), "at the mean of both");
    }

    // A CLIPPED piece still finds its own entry: the bounds come from the unclipped ring, and the
    // drawn ring's centroid moves but stays inside them.
    {
        Ring whole = rect(0.20f, 0.20f, 0.40f, 0.30f), clipped = rect(0.20f, 0.20f, 0.28f, 0.30f);
        std::vector<ExtrusionFootprint> footprints { part(1, whole), part(1, rect(0.60f, 0.20f, 0.70f, 0.30f)) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, clipped), cglib::vec2<float>(0.30f, 0.25f)), "a clipped ring reads the whole footprint's anchor");
    }

    // THE TILE CUT. The same building seen from the two tiles that share the border it crosses:
    // each holds a different piece, so neither centroid agrees - but the middle of where the ring
    // crosses the border is the same world point from both sides.
    {
        std::vector<ExtrusionFootprint> left { part(1, rect(0.90f, 0.40f, 1.20f, 0.60f)) };
        std::vector<ExtrusionFootprint> right { part(1, rect(-0.10f, 0.40f, 0.20f, 0.60f)) };
        auto leftAnchors = buildExtrusionAnchors(left, UNIT_BOX);
        auto rightAnchors = buildExtrusionAnchors(right, UNIT_BOX);
        // The left tile's x = 1 IS the right tile's x = 0.
        TEST_CHECK(same(anchorOf(leftAnchors, 1, left.front().rings.front()), cglib::vec2<float>(1.0f, 0.50f)), "the piece left of the border anchors on the crossing");
        TEST_CHECK(same(anchorOf(rightAnchors, 1, right.front().rings.front()), cglib::vec2<float>(0.0f, 0.50f)), "and the piece right of it on the same world point");
    }

    // A building cutting a CORNER crosses two edges; all four tiles around that corner see it and
    // the corner is the one point every one of them can name.
    {
        Ring ring = rect(0.80f, 0.80f, 1.20f, 1.20f);
        std::vector<ExtrusionFootprint> footprints { part(1, ring) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, ring), cglib::vec2<float>(1.0f, 1.0f)), "a corner cut anchors on the corner");
    }

    // Right across the tile, both opposite edges crossed: there is no crossing the neighbours
    // agree on, so the centroid stands. Better a step than a wrong elevation.
    {
        Ring ring = rect(-0.20f, 0.40f, 1.20f, 0.60f);
        std::vector<ExtrusionFootprint> footprints { part(1, ring) };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(same(anchorOf(anchors, 1, ring), cglib::vec2<float>(0.50f, 0.50f)), "a building spanning the tile keeps its centroid");
    }

    // Overzoom: the box is the ANCESTOR's, so the crossing rule tests the edges the data was
    // really cut at - the target tile's own edges cut nothing.
    {
        cglib::bbox2<float> sourceBox(cglib::vec2<float>(-4.0f, -4.0f), cglib::vec2<float>(12.0f, 12.0f));
        Ring ring = rect(11.0f, 3.0f, 13.0f, 5.0f);
        std::vector<ExtrusionFootprint> footprints { part(1, ring) };
        auto anchors = buildExtrusionAnchors(footprints, sourceBox);
        TEST_CHECK(same(anchorOf(anchors, 1, ring), cglib::vec2<float>(12.0f, 4.0f)), "the crossing is tested against the source box, not the unit square");
    }

    // A group whose only ring is empty must not divide by zero or invent an anchor.
    {
        std::vector<ExtrusionFootprint> footprints { ExtrusionFootprint { 1, 0, {} } };
        auto anchors = buildExtrusionAnchors(footprints, UNIT_BOX);
        TEST_CHECK(anchors.empty(), "a footprint with no ring is not anchored");
    }
}
