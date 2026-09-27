/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ELEVATIONPROVIDER_H_
#define _MASSIF_ELEVATIONPROVIDER_H_

#include "core/MapTile.h"

#include <cglib/ray.h>

namespace massif {
    /**
     * Internal interface for terrain elevation lookups.
     * All coordinates are in the internal planar coordinate system.
     * All methods must be non-blocking (may only use already cached elevation data) and thread-safe.
     */
    class ElevationProvider {
    public:
        virtual ~ElevationProvider() = default;

        /**
         * Returns the display height (internal z units, including exaggeration and Mercator scale)
         * of the terrain at the given internal coordinates. Returns 0 if no elevation data is cached.
         */
        virtual double getDisplayHeight(double internalX, double internalY) const = 0;
        /**
         * Returns the slope of the terrain at the given internal coordinates, as the partial
         * derivatives of the display height. Zero where no elevation data is cached.
         */
        virtual void getDisplayGradient(double internalX, double internalY, double& dhdx, double& dhdy) const = 0;
        /**
         * The maximum zoom level the elevation data itself carries, which is the finest relief
         * anything reading this provider can resolve.
         */
        virtual int getMaxDataZoom() const = 0;
        /**
         * Calculates the first intersection of the ray with the displaced terrain surface.
         * Returns true and sets t if a hit was found. The caller should fall back to
         * a ground plane intersection when false is returned.
         */
        virtual bool intersectRay(const cglib::ray3<double>& ray, double& t) const = 0;
        /**
         * Returns conservative display height bounds for the given map tile.
         */
        virtual void getMinMaxDisplayHeight(const MapTile& tile, double& minZ, double& maxZ) const = 0;
        /**
         * Returns the version counter. The version is incremented whenever previously returned
         * heights may have changed (new elevation tiles decoded, exaggeration changed, tiles invalidated).
         */
        virtual unsigned int getVersion() const = 0;
    };
}

#endif
