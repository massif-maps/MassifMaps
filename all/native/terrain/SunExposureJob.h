/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SUNEXPOSUREJOB_H_
#define _MASSIF_SUNEXPOSUREJOB_H_

#include "core/MapPos.h"

#include <memory>
#include <vector>

namespace massif {
    class ElevationManager;
    class SunExposureOptions;

    /**
     * Terrain shade along a route over the DEM of an ElevationManager: loads the tiles the marches need,
     * then runs SunExposure on each point. Internal class, reached through computeSunExposure.
     */
    class SunExposureJob {
    public:
        static constexpr int RESULT_VERSION = 1;
        static constexpr int HEADER_SIZE = 8;
        static constexpr int POINT_STRIDE = 4;
        static constexpr int FLAG_DEADLINE_HIT = 1;
        static constexpr int FLAG_TRUNCATED = 2;

        /**
         * @param poses WGS84 positions in route order.
         * @param sunDirections Two numbers per position: azimuth (degrees clockwise from north) and apparent altitude.
         * @param options Null for the defaults.
         * @return The header (HEADER_SIZE numbers: version, count, stride, tilesLoaded, tilesMissing, minZoomUsed,
         *         maxZoomUsed, flags) and POINT_STRIDE numbers per position: sunFraction, horizon, shadowDistance, status.
         * @throws std::invalid_argument If sunDirections is not two numbers per position.
         */
        static std::vector<double> compute(ElevationManager& manager, const std::vector<MapPos>& poses, const std::vector<double>& sunDirections, const std::shared_ptr<SunExposureOptions>& options);
    };

}

#endif
