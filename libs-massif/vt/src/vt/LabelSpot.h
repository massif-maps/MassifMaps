/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELSPOT_H_
#define _MASSIF_VT_LABELSPOT_H_

#include <cglib/vec.h>

#include <cmath>
#include <map>
#include <tuple>

namespace massif::vt {

    /**
     * Where a point label stands, to a cell: two labels of one layer in one cell are one POI to the user whatever
     * their ids - the text is hashed into a label id, so a POI that gains its name a zoom level up gets a new one.
     * The cell is coarser than the 4096 grid of a tile, which puts one point on another spot in each zoom level's tile.
     */
    struct LabelSpot {
        int layerIndex;
        long long x;
        long long y;

        bool operator < (const LabelSpot& other) const {
            return std::tie(layerIndex, x, y) < std::tie(other.layerIndex, other.x, other.y);
        }
    };

    inline LabelSpot calculateLabelSpot(int layerIndex, const cglib::vec3<double>& position, double cellSize) {
        return LabelSpot { layerIndex, std::llround(position(0) / cellSize), std::llround(position(1) / cellSize) };
    }

    // The spot of the same layer within a cell of position, the one in its own cell first; end() if none.
    template <class Value>
    typename std::multimap<LabelSpot, Value>::iterator findNearSpot(std::multimap<LabelSpot, Value>& spots, int layerIndex, const cglib::vec3<double>& position, double cellSize) {
        LabelSpot centre = calculateLabelSpot(layerIndex, position, cellSize);
        auto it = spots.find(centre);
        for (int dy = -1; it == spots.end() && dy <= 1; dy++) {
            for (int dx = -1; it == spots.end() && dx <= 1; dx++) {
                it = spots.find(LabelSpot { layerIndex, centre.x + dx, centre.y + dy });
            }
        }
        return it;
    }

}

#endif
