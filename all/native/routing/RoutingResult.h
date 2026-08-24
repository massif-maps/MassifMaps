/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ROUTINGRESULT_H_
#define _MASSIF_ROUTINGRESULT_H_

#ifdef _MASSIF_ROUTING_SUPPORT

#include "core/MapPos.h"
#include "routing/RoutingInstruction.h"

#include <memory>
#include <vector>

namespace massif {
    class Projection;

    /**
     * A class that defines list of routing actions and path geometry.
     */
    class RoutingResult {
    public:
        /**
         * Constructs a new RoutingResult instance from projection, points and instructions.
         * @param projection The projection of the routing result (same as the request).
         * @param points The point list defining the routing path. Instructions refer to this list.
         * @param instructions The turn-by-turn instruction list.
         */
        RoutingResult(const std::shared_ptr<Projection>& projection, std::vector<MapPos> points, std::vector<RoutingInstruction> instructions, const std::string rawResult);
        virtual ~RoutingResult();

        /**
         * Returns the projection of the points in the result.
         * @return The projection of the result.
         */
        const std::shared_ptr<Projection>& getProjection() const;
        /**
         * Returns the point list of the result. The list contains all the points the route must pass in correct order.
         * @return The point list of the path.
         */
        const std::vector<MapPos>& getPoints() const;
        /**
         * Returns the turn-by-turn instruction list.
         * @return The turn-by-turn instruction list.
         */
        const std::vector<RoutingInstruction>& getInstructions() const;

        /**
         * Returns the number of turn-by-turn instructions.
         * @return The number of instructions in the list.
         */
        int getInstructionCount() const;

        /**
         * Returns every turn-by-turn instruction as one JSON array.
         *
         * A maneuver is nine scalars, and reading them one instruction at a time costs a call per
         * field: a mountain route has hundreds. The keys are the property names
         * (`action`, `pointIndex`, `streetName`, `instruction`, `turnAngle`, `azimuth`,
         * `distance`, `time`), and `action` is the enum's constant name.
         * @return The instruction list as JSON.
         */
        std::string getInstructionsJSON() const;

        /**
         * Returns the number of points in the path.
         * @return The number of points in the path.
         */
        int getPointCount() const;

        /**
         * Returns the total distance of the path.
         * @return The total distance in meters.
         */
        double getTotalDistance() const;
        /**
         * Returns the approximate total duration of the path.
         * @return The total duration in seconds.
         */
        double getTotalTime() const;
        /**
         * Returns raw result 
         */
        const std::string& getRawResult() const;

        /**
         * Creates a string representation of this result object, useful for logging.
         * @return The string representation of this result object.
         */
        std::string toString() const;
        
    private:
        std::shared_ptr<Projection> _projection;
        std::vector<MapPos> _points;
        std::vector<RoutingInstruction> _instructions;
        std::string _rawResult;
    };
    
}

#endif

#endif
