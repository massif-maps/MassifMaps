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

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Graph {
    public:
        using NodeId = std::size_t;
        using EdgeId = std::size_t;
        using FeatureId = std::size_t;
        using TriangleId = std::size_t;

        enum NodeFlags : unsigned int {
            GEOMETRY_VERTEX = 1,
            ENDPOINT_VERTEX = 2
        };

        enum class SearchCriteria {
            NONE,
            ANY_VERTEX,
            END_VERTEX,
            EDGE,
            FULL
        };

        struct Node {
            NodeFlags nodeFlags = NodeFlags(0);
            std::array<Point, 2> points; // edge points or identical points, in case of point node
            std::vector<EdgeId> edgeIds; // outward edge ids
        };

        struct Edge {
            FeatureId featureId = FeatureId(-1); // link to original feature
            TriangleId triangleId = TriangleId(-1); // id of the triangle this edge belongs to. Or -1 for line geometry edges
            std::array<NodeId, 2> nodeIds = {{ NodeId(-1), NodeId(-1) }};
            SearchCriteria searchCriteria = SearchCriteria::FULL;
            RoutingAttributes attributes;
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
        StaticGraph() = default;
        explicit StaticGraph(std::vector<Node> nodes, std::vector<Edge> edges, std::vector<Feature> features) : _nodes(std::move(nodes)), _edges(std::move(edges)), _features(std::move(features)) { linkNodeEdgeIds(const_cast<std::vector<Node>&>(_nodes), _edges); }

        virtual NodeId getNodeIdRangeEnd() const override { return static_cast<NodeId>(_nodes.size()); }
        virtual EdgeId getEdgeIdRangeEnd() const override { return static_cast<EdgeId>(_edges.size()); }
        virtual FeatureId getFeatureIdRangeEnd() const override { return static_cast<FeatureId>(_features.size()); }

        virtual const Node& getNode(NodeId nodeId) const override { return _nodes.at(nodeId); }
        virtual const Edge& getEdge(EdgeId edgeId) const override { return _edges.at(edgeId); }
        virtual const Feature& getFeature(FeatureId featureId) const override { return _features.at(featureId); }

        std::vector<std::pair<EdgeId, Point>> findNearestEdgePoint(const Point& pos) const;

    private:
        static double calculateDistance(const Point& pos0, const Point& pos1);

        static void linkNodeEdgeIds(std::vector<Node>& nodes, const std::vector<Edge>& edges);

        static boost::optional<Point> findClosestPoint(const Node& node0, const Node& node1, const Edge& edge, const Point& pos);
        
        const std::vector<Node> _nodes;
        const std::vector<Edge> _edges;
        const std::vector<Feature> _features;
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
