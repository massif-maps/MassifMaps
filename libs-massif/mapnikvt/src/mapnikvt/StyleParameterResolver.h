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
     * The style parameters of a map whose every use is a property the renderer evaluates per frame -
     * a colour, an opacity, a width that is not also read while the tile is built. Changing one of
     * these means swapping the value in the StyleParameterStore and redrawing; changing any other
     * parameter means decoding the tiles again.
     *
     * Deliberately conservative: a parameter is live only when EVERY place it appears says so, and
     * a parameter that appears in no rule (in the map settings, say) is not reported as live.
     */
    std::set<std::string> resolveLiveStyleParameters(const Map& map);

    /**
     * Verifies the parameter a style declared as SELECTING a feature - one compared with a feature
     * field to pick a route or a POI out ("selects": true in styleparameters). Reported so the
     * decoder can fold the comparison both ways and answer a change with a repaint instead of a
     * decode (see SelectionParameter), and marks the properties it may fold with
     * Property::setSelectionFoldable.
     *
     * OPT-IN, and cheap when unused: a style that declares no such parameter returns immediately,
     * without the walk over its rules.
     *
     * Conservative, because a fold that gets the tesselation wrong cannot be undone by a repaint.
     * The parameter has to be read ONLY by the stroke, stroke-opacity and stroke-width of line
     * symbolizers - the three that end up as style slots and touch no vertex - always inside an
     * '=' against the SAME field expression, never in a rule filter, and never alongside another
     * parameter in the same property. A dashed line whose width is selected is refused too: the
     * dash raster is sized by the width, so the two branches would not share their vertices. A
     * declared parameter that fails any of this is reported to the logger and falls back to the
     * re-decode path, rather than the style silently losing what it asked for.
     */
    std::optional<SelectionParameter> resolveSelectionParameter(Map& map, const std::shared_ptr<Logger>& logger);
}

#endif
