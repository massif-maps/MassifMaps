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

#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#include <boost/optional.hpp>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class RouteFinder final {
    public:
        RouteFinder() = delete;
        explicit RouteFinder(std::shared_ptr<const StaticGraph> graph) : _fastestAttributes(findFastestEdgeAttributes(*graph)), _graph(std::move(graph)) { }

        bool getPathStraightening() const { return _pathStraightening; }
        void setPathStraightening(bool pathStraightening) { _pathStraightening = pathStraightening; }

        double getTesselationDistance() const { return _tesselationDistance; }
        void setTesselationDistance(double tesselationDistance) { _tesselationDistance = tesselationDistance; }

        Result find(const Query& query) const;

    private:
        struct PathNode {
            Graph::Edge edge;
            double targetNodeT;
        };
        
        using Path = std::vector<PathNode>;

        static Graph::NodeId createNode(DynamicGraph& graph, const Point& point);
        
        static void linkNodeToEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds, Graph::NodeId nodeId, int nodeIdx);

        static void linkNodesToCommonEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds0, const std::set<Graph::EdgeId>& edgeIds1, Graph::NodeId nodeId0, Graph::NodeId nodeId1);

        static RoutingAttributes findFastestEdgeAttributes(const Graph& graph);
        
        static Point getNodePoint(const Graph& graph, Graph::NodeId nodeId, double t);

        static Result buildResult(const Graph& graph, const Path& path, double lngScale);
        
        static void straightenPath(const Graph& graph, Path& path, double lngScale);
        
        static boost::optional<Path> findOptimalPath(const Graph& graph, Graph::NodeId initialNodeId, Graph::NodeId finalNodeId, const RoutingAttributes& fastestAttributes, double lngScale, double tesselationDistance, double& bestTime);
        
        static double calculateTime(const RoutingAttributes& attrs, const Point& pos0, const Point& pos1, double lngScale);

        static double calculateDistance(const Point& pos0, const Point& pos1, double lngScale);
        
        static std::pair<double, double> calculateDistance2D(const Point& pos0, const Point& pos1, double lngScale);
        
        bool _pathStraightening = true;
        double _tesselationDistance = std::numeric_limits<double>::infinity();

        const RoutingAttributes _fastestAttributes;
        const std::shared_ptr<const StaticGraph> _graph;
    };
} }

#endif
