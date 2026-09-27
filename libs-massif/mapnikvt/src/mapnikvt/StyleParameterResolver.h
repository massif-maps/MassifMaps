/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_STYLEPARAMETERRESOLVER_H_
#define _MASSIF_MAPNIKVT_STYLEPARAMETERRESOLVER_H_

#include "SelectionParameter.h"
#include "Logger.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

namespace massif::mvt {
    class Map;

    /**
     * The style parameters whose every use is evaluated per frame, so changing one is a redraw, not a decode.
     * Conservative: live only when every use is, and a parameter used in no rule is not live.
     */
    std::set<std::string> resolveLiveStyleParameters(const Map& map);

    /**
     * Verifies the parameter a style declared as selecting a feature ("selects": true) and marks the properties
     * it may fold with Property::setSelectionFoldable. Returns immediately for a style declaring none; a parameter
     * breaking the rules in docs/features/style-parameters.md is logged and falls back to re-decoding.
     */
    std::optional<SelectionParameter> resolveSelectionParameter(Map& map, const std::shared_ptr<Logger>& logger);
}

#endif
