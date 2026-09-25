/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALARC_H_
#define _MASSIF_CELESTIALARC_H_

#include "celestial/CelestialObject.h"

#include <vector>

namespace massif {

    /**
     * A curve drawn on the sky.
     *
     * Two ways to define one, and the first is what a daily path is:
     *  - as a CIRCLE about an axis: every direction at a fixed angle from the axis. The daily
     *    path of a distant body is exactly this - the axis is the rotation axis and the angle is
     *    the complement of the declination - so an application draws it with an axis and one angle
     *    rather than by sampling positions through the day.
     *  - as an explicit list of directions, for a path that is not a circle: a satellite track, a
     *    flight plan, a sampled trajectory.
     *
     * The curve is generated once and drawn as a strip widened on screen; its width is in pixels, so it stays
     * legible at any field of view.
     */
    class CelestialArc : public CelestialObject {
    public:
        CelestialArc();
        virtual ~CelestialArc();

        /**
         * Defines the arc as a circle about an axis.
         * @param axisAzimuth The azimuth of the axis in degrees, clockwise from north.
         * @param axisAltitude The altitude of the axis in degrees above the horizon.
         * @param radius The angular radius in degrees: the angle between the axis and the curve.
         */
        void setCircle(float axisAzimuth, float axisAltitude, float radius);

        /**
         * Defines the arc as an explicit list of directions, each an (azimuth, altitude) pair in
         * degrees. The curve runs through them in order and is not closed.
         * @param directions The directions, as alternating azimuth and altitude values in degrees.
         */
        void setDirections(const std::vector<double>& directions);

        /**
         * Defines the arc as a list of SEPARATE segments: every pair of directions is one line and
         * consecutive pairs are not joined. A figure drawn between fixed directions is exactly
         * this - a set of lines that is not a single path - and it stays ONE object, so it is one
         * draw call and one clickable thing.
         * @param directions The directions, as alternating azimuth and altitude values in degrees.
         */
        void setSegments(const std::vector<double>& directions);

        /**
         * Returns true if the directions are read as separate segments rather than as a path.
         * @return True if the arc is a segment list.
         */
        bool isSegmented() const;

        /**
         * Returns the directions of an explicitly defined arc.
         * @return The directions, as alternating azimuth and altitude values in degrees.
         */
        std::vector<double> getDirections() const;

        /**
         * Returns the angular radius of a circular arc.
         * @return The angular radius in degrees.
         */
        float getRadius() const;

        /**
         * Returns the line width.
         * @return The width in pixels.
         */
        float getWidth() const;
        /**
         * Sets the line width.
         * @param pixels The width in pixels.
         */
        void setWidth(float pixels);

        /**
         * Returns whether the part of the curve below the horizon is drawn.
         * @return True if the curve is drawn below the horizon.
         */
        bool isBelowHorizonVisible() const;
        /**
         * Sets whether the part of the curve below the horizon is drawn. A daily path looks
         * right with this off - the arc then rises and sets like the body on it does.
         * @param visible True to draw the curve below the horizon.
         */
        void setBelowHorizonVisible(bool visible);

        /**
         * Returns the click radius of the curve.
         * @return The click radius in degrees.
         */
        float getClickRadius() const;
        /**
         * Sets the click radius of the curve: how far, in degrees, a touch ray may miss the curve
         * and still hit it. A curve is a line a pixel or two wide, so without this nothing could
         * ever be aimed at. 0 makes the curve unclickable. The default is 2 degrees.
         * @param degrees The click radius in degrees.
         */
        void setClickRadius(float degrees);

        /**
         * Builds the curve as unit direction vectors, in the map's local frame (x east, y north,
         * z up). Called by the renderer; an application does not need it.
         * @return The direction vectors along the curve.
         */
        std::vector<cglib::vec3<double> > buildDirections() const;

    private:
        static const int CIRCLE_SEGMENTS;

        bool _circular;
        bool _segmented;
        float _axisAzimuth;
        float _axisAltitude;
        float _radius;
        std::vector<double> _directions;
        float _width;
        bool _belowHorizonVisible;
        float _clickRadius;
    };

}

#endif
