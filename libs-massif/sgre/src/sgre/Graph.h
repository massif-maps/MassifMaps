/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_GRAPH_H_
#define _CARTO_SGRE_GRAPH_H_

#include "Base.h"

#include <memory>
#include <array>
#include <vector>
#include <unordered_map>

#include <boost/optional.hpp>

#include <cglib/bbox.h>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Graph {
    public:
        using NodeId = std::size_t;
        using EdgeId = std::size_t;
        using FeatureId = std::size_t;
        using TriangleId = std::size_t;

        enum NodeFlags : unsigned int {
            GEOMETRY_VERTEX = 1, // node is part of the original geometry (always the case with linestrings; in case of triangles, the edge formed by node endpoints must be an edge of original polygon)
            ENDPOINT_VERTEX = 2  // node is an endpoint of the linestring (first or last vertex)
        };

        enum EdgeFlags : unsigned int {
            GEOMETRY_EDGE = 1    // the edge is part of the original geometry (always the case with linestrings; in case of triangles, the edge formed by non-identical points of the nodes must be edge of original polygon)
        };

        enum class LinkMode {
            NONE,                // linestring vertices are not linked with polygons
            ENDPOINTS,           // linestring endpoints (first and last vertex) are linked with polygons
            ALL                  // all linestring vertices are linked with polygons
        };

        enum class SearchCriteria {
            NONE,                // the edge is ignored when matching routing endpoints
            VERTEX,              // only vertices of the geometry are used when matching routing endpoints
            FIRST_LAST_VERTEX,   // only first and last vertex of the linestring are used when matching routing endpoints
            EDGE,                // only edges of the geometry are used when matching routing endpoints
            SURFACE              // whole surface of the geometry is used when matching routing endpoints
        };

        struct Node {
            NodeFlags nodeFlags = NodeFlags(0);     // bitflags for this node
            std::vector<EdgeId> edgeIds;            // outward edge ids. Filled automatically when the graph is constructed.
            std::array<Point, 2> points = {{ Point(0, 0, 0), Point(0, 0, 0) }}; // edge points or identical points, in case of point node
        };

        struct Edge {
            EdgeFlags edgeFlags = EdgeFlags(0);     // bitflags for this edge
            FeatureId featureId = FeatureId(-1);    // link to original feature
            TriangleId triangleId = TriangleId(-1); // id of the triangle this edge belongs to. Or -1 for line geometry edges.
            std::array<NodeId, 2> nodeIds = {{ NodeId(-1), NodeId(-1) }}; // source and target nodes
            SearchCriteria searchCriteria = SearchCriteria::NONE; // endpoint matching criteria
            RoutingAttributes attributes = RoutingAttributes(); // speed parameters relevant for routing
        };

        using Feature = picojson::value;

        virtual ~Graph() = default;

        virtual NodeId getNodeIdRangeEnd() const = 0;
        virtual EdgeId getEdgeIdRangeEnd() const = 0;
        virtual FeatureId getFeatureIdRangeEnd() const = 0;

        virtual const Node& getNode(NodeId nodeId) const = 0;
        virtual const Edge& getEdge(EdgeId edgeId) const = 0;
        virtual const Feature& getFeature(FeatureId featureId) const = 0;
    };

    class StaticGraph final : public Graph {
    public:
        struct SearchOptions {
            double zSensitivity = 1.0;
        };
        
        StaticGraph() = default;
        explicit StaticGraph(std::vector<Node> nodes, std::vector<Edge> edges, std::vector<Feature> features);

        virtual NodeId getNodeIdRangeEnd() const override { return static_cast<NodeId>(_nodes.size()); }
        virtual EdgeId getEdgeIdRangeEnd() const override { return static_cast<EdgeId>(_edges.size()); }
        virtual FeatureId getFeatureIdRangeEnd() const override { return static_cast<FeatureId>(_features.size()); }

        virtual const Node& getNode(NodeId nodeId) const override { return _nodes.at(nodeId); }
        virtual const Edge& getEdge(EdgeId edgeId) const override { return _edges.at(edgeId); }
        virtual const Feature& getFeature(FeatureId featureId) const override { return _features.at(featureId); }

        std::vector<std::pair<EdgeId, Point>> findNearestEdgePoint(const Point& pos, const picojson::object& filter, const SearchOptions& options) const;

    private:
        struct RTreeNode {
            cglib::bbox3<double> bounds;
            std::vector<EdgeId> edgeIds;
            std::array<std::shared_ptr<RTreeNode>, 2> subNodes;
        };
        
        boost::optional<Point> findNearestEdgePoint(const Edge& edge, const Point& pos, const cglib::vec3<double>& scale) const;

        std::shared_ptr<RTreeNode> buildRTree(const cglib::bbox3<double>& bounds, std::vector<EdgeId> edgeIds) const;

        static void linkNodeEdgeIds(std::vector<Node>& nodes, const std::vector<Edge>& edges);

        static double calculateDistance(const Point& pos0, const Point& pos1, const cglib::vec3<double>& scale);

        std::vector<Node> _nodes;
        std::vector<Edge> _edges;
        std::vector<Feature> _features;
        std::shared_ptr<const RTreeNode> _rootNode;
    };

    class DynamicGraph final : public Graph {
    public:
        DynamicGraph() = delete;
        explicit DynamicGraph(std::shared_ptr<const StaticGraph> staticGraph) : _staticGraph(std::move(staticGraph)) { }

        virtual NodeId getNodeIdRangeEnd() const override;
        virtual EdgeId getEdgeIdRangeEnd() const override { return _staticGraph->getEdgeIdRangeEnd() + static_cast<EdgeId>(_edges.size()); }
        virtual FeatureId getFeatureIdRangeEnd() const override { return _staticGraph->getFeatureIdRangeEnd() + static_cast<FeatureId>(_features.size()); }
        
        virtual const Node& getNode(NodeId nodeId) const override;
        virtual const Edge& getEdge(EdgeId edgeId) const override;
        virtual const Feature& getFeature(FeatureId featureId) const override;

        void reset();

        NodeId addNode(Node node);
        EdgeId addEdge(Edge edge);
        FeatureId addFeature(Feature feature);

    private:
        std::unordered_map<NodeId, Node> _nodes;
        std::vector<Edge> _edges;
        std::vector<Feature> _features;

        const std::shared_ptr<const StaticGraph> _staticGraph;
    };
} }

#endif
