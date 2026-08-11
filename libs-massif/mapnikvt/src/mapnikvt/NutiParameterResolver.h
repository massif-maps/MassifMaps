/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_NUTIPARAMETERRESOLVER_H_
#define _CARTO_MAPNIKVT_NUTIPARAMETERRESOLVER_H_

#include <set>
#include <string>

namespace carto::mvt {
    class Map;

    /**
     * The nuti parameters of a map whose every use is a property the renderer evaluates per frame -
     * a colour, an opacity, a width that is not also read while the tile is built. Changing one of
     * these means swapping the value in the NutiParameterStore and redrawing; changing any other
     * parameter means decoding the tiles again.
     *
     * Deliberately conservative: a parameter is live only when EVERY place it appears says so, and
     * a parameter that appears in no rule (in the map settings, say) is not reported as live.
     */
    std::set<std::string> resolveLiveNutiParameters(const Map& map);
}

#endif
