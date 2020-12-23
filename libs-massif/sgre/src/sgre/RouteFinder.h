/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_ROUTEFINDER_H_
#define _CARTO_SGRE_ROUTEFINDER_H_

#include "Base.h"
#include "Graph.h"
#include "Query.h"
#include "Result.h"

#include <memory>
#include <string>
#include <vector>
#include <set>
#include <limits>

#include <boost/optional.hpp>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class RouteFinder final {
    public:
        struct RouteOptions {
            bool pathStraightening = true;
            double tesselationDistance = std::numeric_limits<double>::infinity();
            double zSensitivity = 1.0;
            double minTurnAngle = 5.0;
            double minUpDownAngle = 45.0;
        };

        RouteFinder() = delete;
        explicit RouteFinder(std::shared_ptr<const StaticGraph> graph) : _graph(std::move(graph)) { }

        const RouteOptions& getRouteOptions() const { return _routeOptions; }
        void setRouteOptions(const RouteOptions& routeOptions) { _routeOptions = routeOptions; }

        std::map<std::string, float> getParameters() const { return _paramValues; }
        void setParameters(const std::map<std::string, float>& paramValues) { _paramValues = paramValues; }

        float getParameter(const std::string& paramName) const { auto it = _paramValues.find(paramName); return (it != _paramValues.end() ? it->second : std::numeric_limits<float>::quiet_NaN()); }
        void setParameter(const std::string& paramName, float value) { _paramValues[paramName] = value; }

        Result find(const Query& query) const;

        static std::unique_ptr<RouteFinder> create(std::shared_ptr<const StaticGraph> graph, const picojson::value& configDef);

    private:
        static constexpr double DIST_EPSILON = 1.0e-6;
        
        static constexpr float DEFAULT_SPEED = 1.38f;      // default speed in m/s
        static constexpr float DEFAULT_ZSPEED = 0.5f;      // default speed in m/s
        static constexpr float DEFAULT_TURNSPEED = 180.0f; // default speed in deg/s
        static constexpr float DEFAULT_DELAY = 0.0f;       // default delay in seconds

        struct EvaluatedAttributes {
            float speed = 0;
            float zSpeed = 0;
            float turnSpeed = 0;
            float delay = 0;
        };

        struct RouteNode {
            Graph::FeatureId featureId = Graph::FeatureId(-1);
            Graph::AttributesId attributesId = Graph::AttributesId(-1);
            Graph::NodeId targetNodeId = Graph::NodeId(-1);
            double targetNodeT = 0;
        };

        using EvaluatedAttributesTable = std::vector<EvaluatedAttributes>;
        
        using Route = std::vector<RouteNode>;

        static Graph::NodeId createNode(DynamicGraph& graph, const Point& point);
        
        static void linkNodeToEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds, Graph::NodeId nodeId, int nodeIdx);

        static void linkNodesToCommonEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds0, const std::set<Graph::EdgeId>& edgeIds1, Graph::NodeId nodeId0, Graph::NodeId nodeId1);

        static bool isNodeVisible(const Graph& graph, Graph::NodeId nodeId0, double t0, Graph::NodeId nodeId1, double t1, double lngScale, std::set<Graph::NodeId> visitedNodeIds);

        static Result buildResult(const Graph& graph, const EvaluatedAttributesTable& attributesTable, const Route& route, double lngScale, double minTurnAngle, double minUpDownAngle);
        
        static Route straightenRoute(const Graph& graph, const Route& route, double lngScale);
        
        static boost::optional<Route> findFastestRoute(const Graph& graph, const EvaluatedAttributesTable& attributesTable, const std::vector<Graph::NodeId>& initialNodeIds, const std::vector<Graph::NodeId>& finalNodeIds, double lngScale, double tesselationDistance);
        
        static double calculateTime(const EvaluatedAttributes& attribs, bool applyDelay, double turnAngle, const Point& pos0, const Point& pos1, double lngScale);

        static double calculateDistance(const Point& pos0, const Point& pos1, double lngScale);
        
        static std::pair<double, double> calculateDistance2D(const Point& pos0, const Point& pos1, double lngScale);

        RouteOptions _routeOptions;

        std::map<std::string, float> _paramValues;

        const std::shared_ptr<const StaticGraph> _graph;
    };
} }

#endif
