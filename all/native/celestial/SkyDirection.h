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
     * The local frame at a map position - x east, y north, z up - as orthonormal world vectors, header-only
     * so the placement CelestialRenderer does and its inverse (sky.clicked) are testable without a renderer.
     */
    class SkyFrame {
    public:
        // On the globe the surface's up image is 1/cos(lat) longer than east and north: normalising after
        // the map would tilt every direction upwards, so the axes are normalised first.
        SkyFrame(const ProjectionSurface& surface, const MapPos& mapPos) :
            _east(cglib::unit(surface.calculateVector(mapPos, MapVec(1, 0, 0)))),
            _north(cglib::unit(surface.calculateVector(mapPos, MapVec(0, 1, 0)))),
            _up(cglib::unit(surface.calculateVector(mapPos, MapVec(0, 0, 1))))
        {
        }

        cglib::vec3<double> toWorld(const cglib::vec3<double>& localDirection) const {
            return _east * localDirection(0) + _north * localDirection(1) + _up * localDirection(2);
        }

        cglib::vec3<double> toLocal(const cglib::vec3<double>& worldDirection) const {
            return cglib::vec3<double>(cglib::dot_product(worldDirection, _east), cglib::dot_product(worldDirection, _north), cglib::dot_product(worldDirection, _up));
        }

    private:
        cglib::vec3<double> _east;
        cglib::vec3<double> _north;
        cglib::vec3<double> _up;
    };

    /**
     * A world direction as azimuth and altitude at a map position: the inverse of SkyFrame::toWorld.
     * @param surface The projection surface.
     * @param mapPos The position whose local frame the direction is read in.
     * @param worldDirection The direction, in world coordinates.
     * @param azimuth The azimuth in degrees, clockwise from north, 0..360.
     * @param altitude The altitude in degrees above the horizon.
     */
    inline void CalculateSkyDirection(const ProjectionSurface& surface, const MapPos& mapPos, const cglib::vec3<double>& worldDirection, float& azimuth, float& altitude) {
        cglib::vec3<double> local = SkyFrame(surface, mapPos).toLocal(worldDirection);
        double degrees = std::atan2(local(0), local(1)) * Const::RAD_TO_DEG;
        azimuth = static_cast<float>(degrees < 0 ? degrees + 360 : degrees);
        altitude = static_cast<float>(std::atan2(local(2), std::sqrt(local(0) * local(0) + local(1) * local(1))) * Const::RAD_TO_DEG);
    }

}

#endif
