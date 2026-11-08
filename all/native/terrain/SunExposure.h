/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SUNEXPOSURE_H_
#define _MASSIF_SUNEXPOSURE_H_

#include <algorithm>
#include <cmath>
#include <limits>

namespace massif {

    /**
     * Whether the terrain hides the sun from a point: one ray toward the sun, over a ground accessor so a
     * synthetic height field can stand in for the DEM. Model and cost: docs/features/sun-exposure.md.
     */
    struct SunExposure {
        static constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
        static constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
        /** A sun lower than this, lit at the end of the march, may have been hidden beyond it (TRUNCATED). */
        static constexpr double TRUNCATED_BELOW = 4.0;
        /** A missing grid closer than this leaves the answer unknown (NO_DEM); further out it only degrades it. */
        static constexpr double NEAR_FIELD = 500.0;

        enum Status {
            COARSE_DEM = 1, // some sample came from a grid more than two levels coarser than wanted, or from none beyond NEAR_FIELD
            NO_DEM = 2,     // no ground at the point or within NEAR_FIELD: the fraction is unknown
            DARK = 4,       // the sun is below darkBelow: not marched
            LOW_SUN = 8,    // below lowSunBelow: refraction and the far field dominate
            TRUNCATED = 16  // lit, but the march stopped at maxDistance with the sun under TRUNCATED_BELOW
        };

        struct Params {
            double eyeHeight = 1.5;         // m above the ground at the point
            double maxDistance = 30000;     // m
            double discRadius = 0.2666;     // degrees: the penumbra's half-width
            double darkBelow = -0.2666;     // apparent altitude in degrees
            double lowSunBelow = 2.0;
            double firstStep = 10.0;        // m
            double stepGrowth = 0.003;      // step = max(firstStep, d * stepGrowth)
            double lodRatio = 1.0 / 300.0;  // wanted texel at distance d = max(minTexel, d * lodRatio)
            double minTexel = 5.0;          // m
            double refraction = 0.13;       // earth radius 1 / (1 - k) times larger, as ElevationManager::calculateHorizon
            double earthRadius = 6378137.0; // m
        };

        /** What the accessor answers for one sample. */
        struct GroundSample {
            float height = 0;         // m
            float tileMax = 0;        // highest ground of the grid that answered
            double exitDistance = 0;  // m along the ray until it leaves that grid
            bool coarse = false;      // answered from a grid more than two levels coarser than wanted
        };

        struct Point {
            double sunFraction = -1;     // visible share of the sun's disc, 0..1; -1 unknown
            double horizon = -90;        // degrees; a lower bound where shaded, -90 for none
            double shadowDistance = -1;  // m to the first sample that hid the sun's centre, -1 none
            int status = 0;
        };

        static double nextDistance(double distance, const Params& params) {
            return distance + std::max(params.firstStep, distance * params.stepGrowth);
        }

        static double wantedTexel(double distance, const Params& params) {
            return std::max(params.minTexel, distance * params.lodRatio);
        }

        /** The share of a disc of radius `disc` whose centre is `altitude` over a horizon edge, all in degrees. */
        static double visibleFraction(double altitude, double horizon, double disc) {
            double x = disc > 0 ? std::max(-1.0, std::min(1.0, (altitude - horizon) / disc)) : (altitude > horizon ? 1.0 : (altitude < horizon ? -1.0 : 0.0));
            return (std::acos(-x) + x * std::sqrt(1.0 - x * x)) / 3.14159265358979323846;
        }

        /** Tangent of the angle ground of height groundZ makes from the eye; it only falls with distance, which is what the exits rely on. */
        static double angleAt(double groundZ, double eyeZ, double distance, double effectiveRadius) {
            return (groundZ - distance * distance / (2.0 * effectiveRadius) - eyeZ) / distance;
        }

        /** groundAt(east, north, wantedTexel, dirEast, dirNorth, GroundSample&) is false where no grid answers; zRegionMax bounds the ground around the route. */
        template <typename GroundFunc>
        static Point exposure(GroundFunc groundAt, double azimuth, double altitude, const Params& params, double zRegionMax) {
            Point point;
            if (altitude < params.darkBelow) {
                point.status = DARK;
                point.sunFraction = 0;
                return point;
            }
            if (altitude < params.lowSunBelow) {
                point.status |= LOW_SUN;
            }
            GroundSample ground;
            if (!groundAt(0.0, 0.0, params.minTexel, 0.0, 0.0, ground)) {
                point.status |= NO_DEM;
                return point;
            }
            double eyeZ = ground.height + params.eyeHeight;
            double zMax = std::max(zRegionMax, static_cast<double>(ground.tileMax));
            double effectiveRadius = params.earthRadius / (1.0 - params.refraction);
            double dirEast = std::sin(azimuth * DEG_TO_RAD), dirNorth = std::cos(azimuth * DEG_TO_RAD);
            double tanTop = std::tan(std::min(altitude + params.discRadius, 89.9) * DEG_TO_RAD);
            double tanCentre = std::tan(std::min(altitude, 89.9) * DEG_TO_RAD);
            double tanBottom = std::tan(std::min(altitude - params.discRadius, 89.9) * DEG_TO_RAD);

            double highest = -std::numeric_limits<double>::infinity();
            bool shaded = false, ended = false, nearMissing = false, farMissing = false, coarse = false;
            double distance = params.firstStep;
            while (distance <= params.maxDistance) {
                if (!groundAt(dirEast * distance, dirNorth * distance, wantedTexel(distance, params), dirEast, dirNorth, ground)) {
                    (distance < NEAR_FIELD ? nearMissing : farMissing) = true;
                    distance = nextDistance(distance, params);
                    continue;
                }
                coarse = coarse || ground.coarse;
                zMax = std::max(zMax, static_cast<double>(ground.tileMax));
                double angle = angleAt(ground.height, eyeZ, distance, effectiveRadius);
                highest = std::max(highest, angle);
                if (point.shadowDistance < 0 && angle >= tanCentre) {
                    point.shadowDistance = distance;
                }
                if (highest >= tanTop) {
                    shaded = ended = true;
                    break;
                }
                double bound = std::max(highest, tanBottom);
                if (angleAt(zMax, eyeZ, distance, effectiveRadius) <= bound) {
                    ended = true;
                    break;
                }
                double next = nextDistance(distance, params);
                if (ground.exitDistance > 0 && angleAt(ground.tileMax, eyeZ, distance, effectiveRadius) <= bound) {
                    next = std::max(next, distance + ground.exitDistance + 0.5);
                }
                distance = next;
            }

            if (std::isfinite(highest)) {
                point.horizon = std::atan(highest) * RAD_TO_DEG;
            }
            if (coarse || farMissing) {
                point.status |= COARSE_DEM;
            }
            if (shaded) {
                point.sunFraction = 0;
                return point;
            }
            if (nearMissing) {
                point.status |= NO_DEM;
                return point;
            }
            point.sunFraction = visibleFraction(altitude, point.horizon, params.discRadius);
            if (!ended && altitude < TRUNCATED_BELOW) {
                point.status |= TRUNCATED;
            }
            return point;
        }

        /** The samples one march can touch, without sampling: visit(east, north, wantedTexel), up to where zRegionMax cannot reach the sun. groundZ may be a lower bound. */
        template <typename VisitFunc>
        static void plan(double azimuth, double altitude, const Params& params, double groundZ, double zRegionMax, VisitFunc visit) {
            if (altitude < params.darkBelow) {
                return;
            }
            double effectiveRadius = params.earthRadius / (1.0 - params.refraction);
            double eyeZ = groundZ + params.eyeHeight;
            double tanBottom = std::tan(std::min(altitude - params.discRadius, 89.9) * DEG_TO_RAD);
            // The first d where the march's lit exit fires, with nothing higher than zRegionMax in sight.
            double limit = params.maxDistance;
            if (tanBottom > 0) {
                double rise = zRegionMax - eyeZ;
                limit = rise > 0 ? std::min(limit, effectiveRadius * (std::sqrt(tanBottom * tanBottom + 2.0 * rise / effectiveRadius) - tanBottom)) : 0.0;
            }
            double dirEast = std::sin(azimuth * DEG_TO_RAD), dirNorth = std::cos(azimuth * DEG_TO_RAD);
            visit(0.0, 0.0, params.minTexel);
            for (double distance = params.firstStep; distance <= params.maxDistance; distance = nextDistance(distance, params)) {
                visit(dirEast * distance, dirNorth * distance, wantedTexel(distance, params));
                if (distance >= limit) {
                    break;
                }
            }
        }
    };

}

#endif
