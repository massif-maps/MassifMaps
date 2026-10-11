/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELSPOT_H_
#define _MASSIF_VT_LABELSPOT_H_

#include <cglib/vec.h>

#include <cmath>
#include <tuple>

namespace massif::vt {

    /**
     * Where a point label stands, to a cell: two labels of one layer in one cell are one POI to the user whatever
     * their ids - the text is hashed into a label id, so a POI that gains its name a zoom level up gets a new one.
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

}

#endif
