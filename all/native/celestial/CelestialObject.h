/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALOBJECT_H_
#define _MASSIF_CELESTIALOBJECT_H_

#include "core/MapPos.h"
#include "core/Variant.h"
#include "graphics/Color.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <cglib/vec.h>

namespace massif {
    class CelestialLayer;

    /**
     * A base class for objects that are placed in the sky rather than on the map.
     *
     * An object is anchored in one of two ways:
     *  - by DIRECTION (azimuth and altitude), with an optional distance. A distance of 0 means
     *    infinitely far: the object keeps its direction whatever the camera does, which is what
     *    a body at an effectively infinite distance needs. A finite distance gives real parallax
     *    and is what an aircraft or a satellite overhead needs.
     *  - by geographic POSITION plus an altitude in meters, for an object that belongs to a place
     *    on the map but is above it.
     *
     * Objects are hit-tested against the touch ray like any other layer's elements, so a click on
     * one is reported through the layer's listener, and terrain in front of it wins the hit.
     */
    class CelestialObject {
    public:
        virtual ~CelestialObject();

        /**
         * Returns true if the object is anchored by direction, false if by geographic position.
         * @return True if the object is anchored by direction.
         */
        bool isDirectionAnchored() const;

        /**
         * Returns the azimuth of a direction-anchored object.
         * @return The azimuth in degrees, clockwise from north.
         */
        float getAzimuth() const;
        /**
         * Returns the altitude of a direction-anchored object.
         * @return The altitude in degrees above the horizon.
         */
        float getAltitude() const;
        /**
         * Returns the distance of a direction-anchored object.
         * @return The distance in meters, or 0 for infinitely far.
         */
        double getDistance() const;
        /**
         * Anchors the object by direction. This is the anchor for anything that behaves like a
         * celestial body.
         * @param azimuth The azimuth in degrees, clockwise from north.
         * @param altitude The altitude in degrees above the horizon.
         * @param distance The distance in meters. 0 means infinitely far, so the object never
         *                 parallaxes when the camera moves.
         */
        void setDirection(float azimuth, float altitude, double distance);

        /**
         * Returns the geographic position of a position-anchored object.
         * @return The position, in the coordinate system of the layer's data source projection.
         */
        MapPos getPosition() const;
        /**
         * Returns the altitude of a position-anchored object.
         * @return The altitude in meters above the ground.
         */
        double getPositionAltitude() const;
        /**
         * Anchors the object above a place on the map. Use this for aircraft, satellites, or
         * anything else that has a real location.
         * @param pos The position, in the coordinate system of the layer's data source projection.
         * @param altitude The altitude in meters above the ground.
         */
        void setPosition(const MapPos& pos, double altitude);

        /**
         * Returns the color of the object.
         * @return The color of the object.
         */
        Color getColor() const;
        /**
         * Sets the color of the object. The color multiplies the bitmap, if there is one.
         * @param color The new color.
         */
        void setColor(const Color& color);

        /**
         * Returns the visibility of the object.
         * @return True if the object is drawn.
         */
        bool isVisible() const;
        /**
         * Sets the visibility of the object. An invisible object is neither drawn nor clickable.
         * @param visible The new visibility.
         */
        void setVisible(bool visible);

        /**
         * Returns whether the map in front hides the object.
         * @return True if the terrain and the map cover the object where they are in front of it.
         */
        bool isOccludedByMap() const;
        /**
         * Sets whether the map in front hides the object. On by default: a body behind a ridge is
         * behind the ridge. Off draws it over the map, which is what a label naming a point of the
         * skyline wants - it sits on the ridge and would otherwise be half covered by it.
         * @param occluded True to let the map in front cover the object.
         */
        void setOccludedByMap(bool occluded);

        /**
         * Returns a meta data value.
         * @param key The key of the value.
         * @return The value, or an empty variant if the key does not exist.
         */
        Variant getMetaDataElement(const std::string& key) const;
        /**
         * Sets a meta data value. Meta data is carried through to the click listener, which is how
         * an application tells its objects apart.
         * @param key The key of the value.
         * @param element The value.
         */
        void setMetaDataElement(const std::string& key, const Variant& element);

        /**
         * Returns the direction of the object as a unit vector, in the map's local frame
         * (x east, y north, z up). Meaningless for position-anchored objects.
         * @return The unit direction vector.
         */
        cglib::vec3<double> calculateDirectionVector() const;

        void setComponents(const std::shared_ptr<CelestialLayer>& layer);
        std::shared_ptr<CelestialLayer> getLayer() const;

    protected:
        CelestialObject();

        void notifyChanged();

        mutable std::mutex _mutex;

    private:
        bool _directionAnchored;
        float _azimuth;
        float _altitude;
        double _distance;
        MapPos _position;
        double _positionAltitude;
        Color _color;
        bool _visible;
        bool _occludedByMap;
        std::map<std::string, Variant> _metaData;
        std::weak_ptr<CelestialLayer> _layer;
    };

}

#endif
