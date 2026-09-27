/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DOUGLASPEUCKERGEOMETRYSIMPLIFIER_H_
#define _MASSIF_DOUGLASPEUCKERGEOMETRYSIMPLIFIER_H_

#include "geometry/GeometrySimplifier.h"

#include <vector>

namespace massif {
    class MapPos;

    /**
     * An implementation of Ramer-Douglas-Peucker algorithm for simplifying lines and polygons.
     * A fast Radial Distance vertex rejection pass runs first, then Ramer-Douglas-Peucker (worst case quadratic complexity).
     */
    class DouglasPeuckerGeometrySimplifier : public GeometrySimplifier {
    public:
        /**
         * Constructs a new simplifier, given tolerance.
         * @param tolerance The maximum error for simplification. The tolerance value gives maximum error in pixels.
         */
        explicit DouglasPeuckerGeometrySimplifier(float tolerance);

        virtual std::shared_ptr<Geometry> simplify(const std::shared_ptr<Geometry>& geometry, const std::shared_ptr<Projection>& projection, const std::shared_ptr<ProjectionSurface>& projectionSurface, float scale) const;

    private:
        class Helper;

        std::vector<MapPos> simplifyRing(const std::vector<MapPos>& ring, const std::shared_ptr<Projection>& projection, const std::shared_ptr<ProjectionSurface>& projectionSurface, float scale) const;

        const float _tolerance;
    };
}

#endif
