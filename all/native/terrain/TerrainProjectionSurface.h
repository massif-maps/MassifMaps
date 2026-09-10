/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINPROJECTIONSURFACE_H_
#define _MASSIF_TERRAINPROJECTIONSURFACE_H_

#include "projections/ProjectionSurface.h"

#include <memory>

namespace massif {
    class ElevationProvider;

    /**
     * A projection surface displaced by terrain elevation. Used when creating vector element draw
     * data so that elements are placed on top of the terrain (the z coordinate of element positions
     * is interpreted as height above terrain).
     * It DECORATES a base surface - the plane or the globe - rather than replacing it: the shape of
     * the world stays the base's and only the height is added, which works because an internal
     * MapPos means the same place and the same height on both (18-globe.md).
     * The elevation version is captured at construction time; VectorLayer creates a new instance
     * when the version changes, which triggers a draw data rebuild through the existing
     * projection-surface identity checks.
     * Internal class, not exposed in the public API.
     */
    class TerrainProjectionSurface : public ProjectionSurface {
    public:
        TerrainProjectionSurface(const std::shared_ptr<ProjectionSurface>& base, const std::shared_ptr<ElevationProvider>& elevationManager);

        const std::shared_ptr<ProjectionSurface>& getBase() const { return _base; }
        const std::shared_ptr<ElevationProvider>& getElevationManager() const { return _elevationManager; }
        unsigned int getElevationVersion() const { return _elevationVersion; }

        // The base's: terrain changes the height of a point, not the width of the world.
        virtual double getWorldWidth() const { return _base->getWorldWidth(); }
        virtual double calculateLocalScale(const cglib::vec3<double>& pos) const { return _base->calculateLocalScale(pos); }

        virtual MapPos calculateMapPos(const cglib::vec3<double>& pos) const;
        virtual MapVec calculateMapVec(const cglib::vec3<double>& pos, const cglib::vec3<double>& vec) const;
        virtual cglib::vec3<double> calculatePosition(const MapPos& mapPos) const;
        virtual cglib::vec3<double> calculateNormal(const MapPos& mapPos) const;
        virtual cglib::vec3<double> calculateVector(const MapPos& mapPos, const MapVec& mapVec) const;
        virtual double calculateDistance(const cglib::vec3<double> pos0, const cglib::vec3<double>& pos1) const;
        virtual cglib::vec3<double> calculateNearestPoint(const cglib::vec3<double>& pos, double height) const;
        virtual cglib::vec3<double> calculateNearestPoint(const cglib::ray3<double>& ray, double height, double& t) const;
        virtual bool calculateHitPoint(const cglib::ray3<double>& ray, double height, double& t) const;
        virtual cglib::mat4x4<double> calculateLocalFrameMatrix(const cglib::vec3<double>& pos) const;
        virtual cglib::mat4x4<double> calculateTranslateMatrix(const cglib::vec3<double>& pos0, const cglib::vec3<double>& pos1, double t) const;

        virtual void tesselateSegment(const MapPos& mapPos0, const MapPos& mapPos1, std::vector<MapPos>& mapPoses) const;
        virtual void tesselateTriangle(unsigned int i0, unsigned int i1, unsigned int i2, std::vector<unsigned int>& indices, std::vector<MapPos>& mapPoses) const;

    private:
        bool splitSegment(const MapPos& mapPos0, const MapPos& mapPos1, MapPos& mapPosM) const;

        static double CalculateSplitThreshold(const std::shared_ptr<ElevationProvider>& elevationManager);

        const std::shared_ptr<ProjectionSurface> _base;
        const std::shared_ptr<ElevationProvider> _elevationManager;
        const unsigned int _elevationVersion;
        const double _splitThreshold; // internal units; longer segments/triangle edges are subdivided to follow the terrain
        const double _heightLift; // internal units; draped elements are lifted slightly above the terrain surface
    };
}

#endif
