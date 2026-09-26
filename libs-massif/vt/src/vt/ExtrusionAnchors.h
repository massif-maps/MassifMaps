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
     * One footprint's ground anchor and the bounds of its ring. A merged source packs many buildings under
     * one id, so each footprint has an entry and a drawn polygon picks its own by the bounds its centroid is in.
     */
    struct ExtrusionAnchor {
        cglib::bbox2<float> bounds; // of the unclipped outer ring, so a clipped piece still lands in it
        cglib::vec2<float> anchor;
    };

    /**
     * The point each footprint's base elevation is read at, keyed by local id: parts sharing a vertex or a
     * `building_id` (not a feature id) share one anchor, and tile-cut groups anchor on their edge crossing.
     * `sourceBox` is the box the tile's data was cut at (the ancestor's under overzoom). See docs/internals/rendering/04-terrain.md.
     */
    std::unordered_map<long long, std::vector<ExtrusionAnchor>> buildExtrusionAnchors(const std::vector<ExtrusionFootprint>& footprints, const cglib::bbox2<float>& sourceBox);

    /** The entry a ring belongs to: the smallest bounds holding its centroid, or null. */
    const ExtrusionAnchor* findExtrusionAnchor(const std::vector<ExtrusionAnchor>& anchors, const std::vector<cglib::vec2<float>>& ring);
}

#endif
