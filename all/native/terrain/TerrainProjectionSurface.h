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
     * Decorates a base surface (plane or globe) with terrain height, so vector element z is height above terrain.
     * The elevation version is captured at construction; a new instance per version triggers the draw data
     * rebuild through the surface identity checks. Internal class, not exposed in the public API.
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
