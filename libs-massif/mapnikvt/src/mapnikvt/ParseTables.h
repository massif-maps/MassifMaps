/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_PARSETABLES_H_
#define _MASSIF_MAPNIKVT_PARSETABLES_H_

#include "vt/Styles.h"

#include <string>
#include <unordered_map>

#include <cglib/vec.h>

namespace massif::mvt {
    template <typename T>
    using ParseTable = std::unordered_map<std::string, T>;

    const ParseTable<vt::LineCapMode>& getLineCapModeTable();
    const ParseTable<vt::LineJoinMode>& getLineJoinModeTable();
    const ParseTable<vt::CompOp>& getCompOpTable();
    const ParseTable<vt::LabelOrientation>& getLabelOrientationTable();
    // Points of a label's own box, normalized: (-1,-1) is its bottom left corner, (1,1) the top
    // right one. Used by the callout properties (leader line end, band alignment).
    const ParseTable<cglib::vec2<float>>& getLabelBoxAnchorTable();
}

#endif
