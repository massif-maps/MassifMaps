/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILELODRULE_H_
#define _MASSIF_TILELODRULE_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * maplibre's tile LOD rule (src/geo/projection/covering_tiles.ts,
     * createCalculateTileZoomFunction), expressed as the two corrections our screen-area rule
     * needs to become it - see docs/internals/rendering/02-tiles.md.
     */
    struct TileLODRule {
        double cosThetaExponent = 0.0; // p - 1: extra power on cos(incidence) in the area test
        double uniformLevelDrop = 0.0; // levels shed from EVERY tile to keep the pitched tile count in budget
    };

    /** The mercator horizon maplibre bounds every pitch by (mercator_utils.ts). */
    const double LOD_MAX_MERCATOR_HORIZON_ANGLE = 89.25;

    /** Definite integral of cos(x)^p, midpoint rule over 10 points - maplibre's integralOfCosXByP. */
    inline double lodIntegralOfCosXByP(double p, double x1, double x2) {
        const int numPoints = 10;
        double sum = 0;
        double dx = (x2 - x1) / numPoints;
        for (int i = 0; i < numPoints; i++) {
            double x = x1 + (i + 0.5) / numPoints * (x2 - x1);
            sum += dx * std::pow(std::cos(x), p);
        }
        return sum;
    }

    /**
     * maplibre's pitchTileLoadingBehavior: the power the level decays with the grazing angle, sized
     * so the worst case (horizon at the top of the screen) spans maxZoomLevelsOnScreen levels.
     * 1 at maplibre's defaults, which is our area rule exactly.
     */
    inline double lodCosThetaPower(double maxZoomLevelsOnScreen, double fovyDegrees) {
        const double toRad = 3.14159265358979323846 / 180.0;
        double span = std::log2(std::cos((LOD_MAX_MERCATOR_HORIZON_ANGLE - fovyDegrees) * toRad) /
                                std::cos(LOD_MAX_MERCATOR_HORIZON_ANGLE * toRad));
        if (!(span > 0)) {
            return 1.0;
        }
        return 2 * ((std::max(1.0, maxZoomLevelsOnScreen) - 1) / span - 1);
    }

    /**
     * The rule for one frame. centerPitch is the angle between the camera's view direction and the
     * ground normal, in radians - 0 looking straight down.
     */
    inline TileLODRule calculateTileLODRule(double maxZoomLevelsOnScreen, double tileCountMaxMinRatio, double fovyDegrees, double centerPitch) {
        const double toRad = 3.14159265358979323846 / 180.0;
        TileLODRule rule;
        double p = lodCosThetaPower(maxZoomLevelsOnScreen, fovyDegrees);
        rule.cosThetaExponent = p - 1;

        // How many tiles this pitch asks for against a top-down view of the same camera: maplibre
        // integrates cos^(p-1) over the pitch range the frustum covers.
        double halfFov = fovyDegrees * 0.5 * toRad;
        double tileCountPitch0 = 2 * lodIntegralOfCosXByP(p - 1, 0, halfFov);
        double highestPitch = std::min(LOD_MAX_MERCATOR_HORIZON_ANGLE * toRad, centerPitch + halfFov);
        double lowestPitch = std::min(highestPitch, centerPitch - halfFov);
        double tileCount = lodIntegralOfCosXByP(p - 1, lowestPitch, highestPitch);
        if (tileCountPitch0 > 0 && tileCountMaxMinRatio > 0) {
            rule.uniformLevelDrop = std::log2(std::max(1.0, tileCount / tileCountPitch0 / tileCountMaxMinRatio)) * 0.5;
        }
        return rule;
    }

}

#endif
