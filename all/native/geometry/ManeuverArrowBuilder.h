/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MANEUVERARROWBUILDER_H_
#define _MASSIF_MANEUVERARROWBUILDER_H_

#include "core/MapPos.h"

#include <memory>
#include <mutex>
#include <vector>

namespace massif {
    class FeatureCollection;
    class Projection;

    /**
     * A builder that cuts a navigation maneuver arrow out of a route: LengthBefore metres before the maneuver point, LengthAfter after.
     * The result is one WGS84 line in the travel direction; the head is drawn by the style's 'line-end-arrow', so it keeps
     * its screen size and a casing rule outlines the whole arrow evenly.
     */
    class ManeuverArrowBuilder {
    public:
        ManeuverArrowBuilder();
        virtual ~ManeuverArrowBuilder();

        /**
         * Returns the length of the arrow before the maneuver point.
         * @return The length before the maneuver point in meters.
         */
        float getLengthBefore() const;
        /**
         * Sets the length of the arrow before the maneuver point. The arrow is shortened if the
         * route is shorter than this. The default is 30.
         * @param length The length before the maneuver point in meters.
         */
        void setLengthBefore(float length);

        /**
         * Returns the length of the arrow after the maneuver point.
         * @return The length after the maneuver point in meters.
         */
        float getLengthAfter() const;
        /**
         * Sets the length of the arrow after the maneuver point. The arrow is shortened if the
         * route is shorter than this. The default is 30.
         * @param length The length after the maneuver point in meters.
         */
        void setLengthAfter(float length);

        /**
         * Builds the arrow around the route point nearest to the given position.
         * @param projection The projection of the route points. Null means WGS84.
         * @param points The route geometry.
         * @param maneuverPos The maneuver position, in the same projection as the points.
         * @return The arrow features in WGS84. Empty if the route is too short to cut an arrow from.
         */
        std::shared_ptr<FeatureCollection> buildArrow(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points, const MapPos& maneuverPos) const;
        /**
         * Builds the arrow around the route point at the given index. This is the index a routing
         * instruction refers to.
         * @param projection The projection of the route points. Null means WGS84.
         * @param points The route geometry.
         * @param maneuverIndex The index of the maneuver point in the route geometry.
         * @return The arrow features in WGS84. Empty if the route is too short to cut an arrow from.
         */
        std::shared_ptr<FeatureCollection> buildArrowAtIndex(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points, int maneuverIndex) const;

    private:
        // Metres in an equirectangular plane anchored at the maneuver latitude: exact enough for a tens-of-metres arrow.
        struct LocalPos {
            double x;
            double y;
        };

        static std::vector<MapPos> ConvertToWgs84(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points);

        std::shared_ptr<FeatureCollection> buildArrowAtSegment(const std::vector<MapPos>& wgs84Points, std::size_t segmentIndex, double segmentT) const;

        static LocalPos ToLocal(const MapPos& wgs84Pos, double originLat);
        static MapPos FromLocal(const LocalPos& pos, double originLat);
        static LocalPos Interpolate(const LocalPos& p0, const LocalPos& p1, double t);
        static double Distance(const LocalPos& p0, const LocalPos& p1);

        static const float DEFAULT_LENGTH;

        float _lengthBefore;
        float _lengthAfter;

        mutable std::mutex _mutex;
    };

}

#endif
