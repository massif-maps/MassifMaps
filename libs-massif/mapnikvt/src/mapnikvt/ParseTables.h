/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_PARSETABLES_H_
#define _CARTO_MAPNIKVT_PARSETABLES_H_

#include "vt/Styles.h"

#include <string>
#include <unordered_map>

namespace carto { namespace mvt {
    template <typename T>
    using ParseTable = std::unordered_map<std::string, T>;

    const ParseTable<vt::LineCapMode>& getLineCapModeTable();
    const ParseTable<vt::LineJoinMode>& getLineJoinModeTable();
    const ParseTable<vt::CompOp>& getCompOpTable();
    const ParseTable<vt::LabelOrientation>& getLabelOrientationTable();
} }

#endif
