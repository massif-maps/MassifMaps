/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SKYDIRECTION_H_
#define _MASSIF_SKYDIRECTION_H_

#include "core/MapPos.h"
#include "core/MapVec.h"
#include "projections/ProjectionSurface.h"
#include "utils/Const.h"

#include <cmath>

#include <cglib/vec.h>

namespace massif {

    /**
     * A world direction as azimuth and altitude at a map position: the inverse of the placement
     * CelestialRenderer does (x east, y north, z up), header-only so it is testable without a renderer.
     * @param surface The projection surface.
     * @param mapPos The position whose local frame the direction is read in.
     * @param worldDirection The direction, in world coordinates.
     * @param azimuth The azimuth in degrees, clockwise from north, 0..360.
     * @param altitude The altitude in degrees above the horizon.
     */
    inline void CalculateSkyDirection(const ProjectionSurface& surface, const MapPos& mapPos, const cglib::vec3<double>& worldDirection, float& azimuth, float& altitude) {
        // The axes are orthogonal but not equally long (on the globe height scales apart from x/y), so each
        // component is divided by its axis's squared length rather than read off unit vectors.
        cglib::vec3<double> east = surface.calculateVector(mapPos, MapVec(1, 0, 0));
        cglib::vec3<double> north = surface.calculateVector(mapPos, MapVec(0, 1, 0));
        cglib::vec3<double> up = surface.calculateVector(mapPos, MapVec(0, 0, 1));
        double x = cglib::dot_product(worldDirection, east) / cglib::dot_product(east, east);
        double y = cglib::dot_product(worldDirection, north) / cglib::dot_product(north, north);
        double z = cglib::dot_product(worldDirection, up) / cglib::dot_product(up, up);
        double degrees = std::atan2(x, y) * Const::RAD_TO_DEG;
        azimuth = static_cast<float>(degrees < 0 ? degrees + 360 : degrees);
        altitude = static_cast<float>(std::atan2(z, std::sqrt(x * x + y * y)) * Const::RAD_TO_DEG);
    }

}

#endif
