/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_EXTRUSIONANCHORS_H_
#define _MASSIF_VT_EXTRUSIONANCHORS_H_

#include <unordered_map>
#include <vector>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace massif::vt {
    /**
     * One extruded footprint as the anchor pass sees it: its rings in tile coordinates, the id the
     * symbolizer will draw it under, and the building it declares itself part of.
     */
    struct ExtrusionFootprint {
        long long localId = 0;
        long long buildingId = 0; // 0 = the source names none
        std::vector<std::vector<cglib::vec2<float>>> rings; // outer ring first
    };

    /**
     * One footprint's answer: where it reads the ground, and the bounds of the ring that asked.
     * A feature holding many footprints has one entry each, and the drawn polygon picks its own by
     * the bounds its centroid falls in - a merged source packs hundreds of buildings under one id.
     */
    struct ExtrusionAnchor {
        cglib::bbox2<float> bounds; // of the UNCLIPPED outer ring, so a clipped piece still lands in it
        cglib::vec2<float> anchor;
    };

    /**
     * The point each footprint's base elevation is read at, keyed by local id.
     *
     * A building is a rigid prism standing at ONE elevation, so every piece of it has to ask the
     * ground at the same place - a per-footprint centroid gives the wings of a palace a base each,
     * and equal heights on stepped bases read as a sawtooth of separate slabs. Two things break a
     * building into pieces and both are handled here:
     *
     *  - the source splits it into parts. Parts that share a vertex are one building (measured on
     *    mapbox-streets z16 over the Louvre: 738 of 1815 tile vertices are shared, and the union
     *    turns 160 footprints into 33 groups), and so are parts carrying the same `building_id`.
     *    Sharing an ID is NOT one of them: an OpenMapTiles mbtiles packs a whole tile of unrelated
     *    buildings into one multipolygon feature (measured at Grenoble z15: 3513 footprints, 18
     *    ids), and one anchor for all of them buries a hillside's buildings under the valley floor.
     *  - the tile grid cuts it. Each side then holds a different piece with a different centroid,
     *    so a group crossing exactly one edge of `sourceBox` anchors on the MIDDLE of its crossing
     *    of that edge, which both sides compute identically; a group cutting a corner anchors on
     *    the corner. Anything more tangled keeps its centroid - there is no local rule two tiles
     *    would agree on.
     *
     * `sourceBox` is the box the tile's own data covers, in the same coordinates as the rings -
     * under overzoom that is the ancestor tile's box, not the unit square, since the ancestor's
     * edges are the only ones the data was ever cut at.
     */
    std::unordered_map<long long, std::vector<ExtrusionAnchor>> buildExtrusionAnchors(const std::vector<ExtrusionFootprint>& footprints, const cglib::bbox2<float>& sourceBox);

    /** The entry a ring belongs to: the smallest bounds holding its centroid, or null. */
    const ExtrusionAnchor* findExtrusionAnchor(const std::vector<ExtrusionAnchor>& anchors, const std::vector<cglib::vec2<float>>& ring);
}

#endif
