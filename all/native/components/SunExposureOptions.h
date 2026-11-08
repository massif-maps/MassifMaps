/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SUNEXPOSUREOPTIONS_H_
#define _MASSIF_SUNEXPOSUREOPTIONS_H_

namespace massif {

    /**
     * How HillshadeRasterTileLayer::computeSunExposure and TerrainOptions::computeSunExposure march
     * toward the sun and load elevation tiles. Every default is the measured one; most callers only set
     * the eye height.
     */
    class SunExposureOptions {
    public:
        /**
         * Constructs the default options.
         */
        SunExposureOptions();

        /**
         * Returns the height of the eye above the ground at each point.
         * @return The eye height in meters. The default is 1.5.
         */
        double getEyeHeight() const;
        /**
         * Sets the height of the eye above the ground at each point (1.5 walking, 1.2 cycling).
         * @param eyeHeight The eye height in meters, 0 or more.
         */
        void setEyeHeight(double eyeHeight);

        /**
         * Returns how far toward the sun the march goes.
         * @return The distance cap in meters. The default is 30000.
         */
        double getMaxDistance() const;
        /**
         * Sets how far toward the sun the march goes. The march stops sooner where nothing in the region
         * can reach the sun's altitude.
         * @param maxDistance The distance cap in meters, 100 to 200000.
         */
        void setMaxDistance(double maxDistance);

        /**
         * Returns the sun disc's angular radius, the half-width of the penumbra.
         * @return The radius in degrees. The default is 0.2666.
         */
        double getDiscRadius() const;
        /**
         * Sets the sun disc's angular radius.
         * @param discRadius The radius in degrees, 0 or more. 0 makes the answer 0 or 1, never between.
         */
        void setDiscRadius(double discRadius);

        /**
         * Returns the apparent altitude under which a point is DARK and not marched.
         * @return The altitude in degrees. The default is -0.2666, the apparent sunset.
         */
        double getDarkBelow() const;
        /**
         * Sets the altitude under which a point is DARK. Use -0.833 when the caller's sun is the true
         * altitude (astronomical almanac convention) rather than the apparent one.
         * @param darkBelow The altitude in degrees.
         */
        void setDarkBelow(double darkBelow);

        /**
         * Returns the apparent altitude under which a point is flagged LOW_SUN.
         * @return The altitude in degrees. The default is 2.
         */
        double getLowSunBelow() const;
        /**
         * Sets the altitude under which a point is flagged LOW_SUN: refraction and the far field then
         * decide the answer more than the march.
         * @param lowSunBelow The altitude in degrees.
         */
        void setLowSunBelow(double lowSunBelow);

        /**
         * Returns the first march step, also the smallest.
         * @return The step in meters. The default is 10.
         */
        double getFirstStep() const;
        /**
         * Sets the first march step, also the smallest: step = max(firstStep, distance * stepGrowth).
         * @param firstStep The step in meters, more than 0.
         */
        void setFirstStep(double firstStep);

        /**
         * Returns how fast the march step grows with distance.
         * @return The growth per meter of distance. The default is 0.003.
         */
        double getStepGrowth() const;
        /**
         * Sets how fast the march step grows with distance.
         * @param stepGrowth The growth per meter of distance, more than 0.
         */
        void setStepGrowth(double stepGrowth);

        /**
         * Returns the wanted elevation texel size per meter of distance.
         * @return The ratio. The default is 1/300.
         */
        double getLodRatio() const;
        /**
         * Sets the wanted elevation texel size per meter of distance: at distance d the march reads the
         * DEM level whose texel is closest to max(minTexel, d * lodRatio).
         * @param lodRatio The ratio, more than 0.
         */
        void setLodRatio(double lodRatio);

        /**
         * Returns the finest texel size the march asks for.
         * @return The texel size in meters. The default is 5.
         */
        double getMinTexel() const;
        /**
         * Sets the finest texel size the march asks for. A finer DEM than this is read at the level
         * closest to it, no finer.
         * @param minTexel The texel size in meters, more than 0.
         */
        void setMinTexel(double minTexel);

        /**
         * Returns the atmospheric refraction coefficient.
         * @return The coefficient. The default is 0.13, as for the horizon query.
         */
        double getRefraction() const;
        /**
         * Sets the atmospheric refraction coefficient: the earth is modelled 1 / (1 - k) times larger.
         * @param refraction The coefficient, 0 to 0.5.
         */
        void setRefraction(double refraction);

        /**
         * Returns whether the call loads the elevation tiles it needs.
         * @return True to load, false to only use what is decoded already. The default is true.
         */
        bool isLoadMissingTiles() const;
        /**
         * Sets whether the call loads the elevation tiles it needs, waiting up to the load timeout. When
         * false it never waits: it asks for the missing tiles in the background and flags what it lacked,
         * so calling it again a moment later gives a fuller answer.
         * @param loadMissingTiles True to load, false to use only what is decoded already.
         */
        void setLoadMissingTiles(bool loadMissingTiles);

        /**
         * Returns the deadline for loading tiles in one call.
         * @return The deadline in milliseconds, 0 for none. The default is 20000.
         */
        int getLoadTimeoutMs() const;
        /**
         * Sets the deadline for loading tiles in one call. When it passes, the call computes with what it
         * has and flags the rest (header flag bit 0).
         * @param loadTimeoutMs The deadline in milliseconds, 0 for none.
         */
        void setLoadTimeoutMs(int loadTimeoutMs);

        /**
         * Returns the memory one call may hold in decoded elevation grids.
         * @return The budget in megabytes. The default is 96.
         */
        int getCacheMB() const;
        /**
         * Sets the memory one call may hold in decoded elevation grids (a 512-texel grid is ~1 MB). Tiles
         * past it are not loaded and their points are flagged, so give a long route in chunks.
         * @param cacheMB The budget in megabytes, 1 or more.
         */
        void setCacheMB(int cacheMB);

        /**
         * Returns whether the call empties the elevation cache once it is done.
         * @return True to empty it. The default is false.
         */
        bool isReleaseCache() const;
        /**
         * Sets whether the call empties the elevation cache of the layer or terrain it ran on once it is
         * done, to give the memory back after the last chunk of a route.
         * @param releaseCache True to empty it.
         */
        void setReleaseCache(bool releaseCache);

    private:
        double _eyeHeight;
        double _maxDistance;
        double _discRadius;
        double _darkBelow;
        double _lowSunBelow;
        double _firstStep;
        double _stepGrowth;
        double _lodRatio;
        double _minTexel;
        double _refraction;
        bool _loadMissingTiles;
        int _loadTimeoutMs;
        int _cacheMB;
        bool _releaseCache;
    };

}

#endif
