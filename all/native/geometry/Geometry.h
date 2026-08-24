/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_GEOMETRY_H_
#define _MASSIF_GEOMETRY_H_

#include "core/MapPos.h"
#include "core/MapBounds.h"

#include <string>

namespace massif {

    namespace GeometryType {
        /**
         * Geometry type, following the GeoJSON names.
         */
        enum GeometryType {
            /**
             * A single position.
             */
            GEOMETRY_TYPE_POINT,
            /**
             * An ordered list of positions.
             */
            GEOMETRY_TYPE_LINE,
            /**
             * An outer ring and any number of holes.
             */
            GEOMETRY_TYPE_POLYGON,
            /**
             * Several points.
             */
            GEOMETRY_TYPE_MULTIPOINT,
            /**
             * Several lines.
             */
            GEOMETRY_TYPE_MULTILINE,
            /**
             * Several polygons.
             */
            GEOMETRY_TYPE_MULTIPOLYGON,
            /**
             * A mixed collection.
             */
            GEOMETRY_TYPE_COLLECTION
        };
    }

    /**
     * A base class for all geometry types.
     */
    class Geometry {
    public:
        virtual ~Geometry() { }
        
        /**
         * Returns the center point of the geometry.
         * @return The center point of the geometry.
         */
        virtual MapPos getCenterPos() const = 0;

        /**
         * Returns what kind of geometry this is.
         *
         * Without it the only way to tell a MultiPoint from a Point is a downcast, or - from a
         * scripting binding - matching on the wrapper's class name.
         * @return The geometry type.
         */
        virtual GeometryType::GeometryType getType() const = 0;
    
        /**
         * Returns the minimal bounds for the geometry.
         * @return The bounds for the geometry.
         */
        const MapBounds& getBounds() const {
            return _bounds;
        }

        /**
         * Returns the geometry as a GeoJSON string, in its own coordinates.
         *
         * Here rather than only on Feature because serialising a shape otherwise means constructing
         * a GeoJSONGeometryWriter, which no string-based binding can do.
         * @return The geometry as GeoJSON.
         */
        std::string getGeoJSON() const;

    protected:
        Geometry() : _bounds() { }
    
        MapBounds _bounds;
    };
    
}

#endif
