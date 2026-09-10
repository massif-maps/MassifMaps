/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PROJECTIONSURFACE_H_
#define _MASSIF_PROJECTIONSURFACE_H_

#include "core/MapPos.h"
#include "core/MapVec.h"

#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/ray.h>

namespace massif {

    class ProjectionSurface {
    public:
        virtual ~ProjectionSurface() { }

        /**
         * How wide the world is in world units, measured at the equator. The camera's zoom is
         * calibrated on it, and it is NOT the same on the two surfaces: the sphere's equator is
         * twice the planar map's width (docs/internals/rendering/18-globe.md).
         */
        virtual double getWorldWidth() const = 0;

        /**
         * World units per INTERNAL unit AT this position: 1 everywhere on a plane, and
         * `getWorldWidth() / WORLD_SIZE * cos(latitude)` on a sphere, which has none of Mercator's
         * own 1/cos stretch. The camera calibrates on it, so the same zoom frames the same ground
         * on either surface (docs/internals/rendering/18-globe.md).
         */
        virtual double calculateLocalScale(const cglib::vec3<double>& pos) const = 0;

        virtual MapPos calculateMapPos(const cglib::vec3<double>& pos) const = 0;
        virtual MapVec calculateMapVec(const cglib::vec3<double>& pos, const cglib::vec3<double>& vec) const = 0;

        virtual cglib::vec3<double> calculatePosition(const MapPos& mapPos) const = 0;
        virtual cglib::vec3<double> calculateNormal(const MapPos& mapPos) const = 0;
        virtual cglib::vec3<double> calculateVector(const MapPos& mapPos, const MapVec& mapVec) const = 0;

        virtual void tesselateSegment(const MapPos& mapPos0, const MapPos& mapPos1, std::vector<MapPos>& mapPoses) const = 0;
        virtual void tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, std::vector<unsigned int>& indices, std::vector<MapPos>& mapPoses) const = 0;

        virtual double calculateDistance(const cglib::vec3<double> pos0, const cglib::vec3<double>& pos1) const = 0;
        virtual cglib::vec3<double> calculateNearestPoint(const cglib::vec3<double>& pos, double height) const = 0;
        virtual cglib::vec3<double> calculateNearestPoint(const cglib::ray3<double>& ray, double height, double& t) const = 0;
        virtual bool calculateHitPoint(const cglib::ray3<double>& ray, double height, double& t) const = 0;

        virtual cglib::mat4x4<double> calculateLocalFrameMatrix(const cglib::vec3<double>& pos) const = 0;
        virtual cglib::mat4x4<double> calculateTranslateMatrix(const cglib::vec3<double>& pos0, const cglib::vec3<double>& pos1, double t) const = 0;
    };
    
}

#endif
