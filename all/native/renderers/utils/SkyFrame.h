/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SKYFRAME_H_
#define _MASSIF_SKYFRAME_H_

#include "projections/ProjectionSurface.h"
#include "utils/Const.h"

#include <cmath>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif {

    /**
     * What the sky shader needs to be drawn for an observer rather than for a flat earth. Its whole
     * model is local - the elevation angle, the star azimuth and the planet-relative atmosphere
     * origin - and LightOptions::getSunDirection already speaks that frame. Only the view ray
     * arrives in world space, so the ray is what has to be rotated.
     * Free of the renderer so it can be tested on the host; see
     * docs/internals/rendering/18-globe.md.
     */
    struct SkyFrame {
        /**
         * World -> local (east, north, up) rotation at pos. The identity on a planar surface, so
         * the plane keeps its shader output bit for bit.
         */
        static cglib::mat3x3<float> orientation(const ProjectionSurface& surface, const cglib::vec3<double>& pos) {
            cglib::mat4x4<double> frame = surface.calculateLocalFrameMatrix(pos);
            cglib::mat3x3<float> rotation = cglib::mat3x3<float>::identity();
            for (int axis = 0; axis < 3; axis++) {
                cglib::vec3<double> dir(frame(0, axis), frame(1, axis), frame(2, axis));
                double length = cglib::length(dir);
                if (!(length > 0) || !std::isfinite(length)) {
                    return cglib::mat3x3<float>::identity();
                }
                // Rows, not columns: the transpose of an orthonormal basis is its inverse.
                for (int i = 0; i < 3; i++) {
                    rotation(axis, i) = static_cast<float>(dir(i) / length);
                }
            }
            return rotation;
        }

        /**
         * The camera's height above the surface, in metres. Read through the surface's own internal
         * z so the plane keeps the exact value it had; that convention omits the Mercator latitude
         * scale, and both surfaces omit it alike rather than disagreeing.
         * Zero when the surface cannot express the height - a globe camera over a pole, where the
         * internal z of a point off the surface diverges.
         */
        static float cameraHeight(const ProjectionSurface& surface, const cglib::vec3<double>& cameraPos) {
            double internalZ = surface.calculateMapPos(cameraPos).getZ();
            if (!std::isfinite(internalZ)) {
                return 0.0f;
            }
            return static_cast<float>(internalZ * Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE);
        }
    };
}

#endif
