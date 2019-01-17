/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_GRAPHBUILDER_H_
#define _CARTO_SGRE_GRAPHBUILDER_H_

#include "Base.h"
#include "Rule.h"
#include "Graph.h"

#include <memory>
#include <vector>
#include <unordered_map>

#include <boost/functional/hash.hpp>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class GraphBuilder final {
    public:
        GraphBuilder() = delete;
        explicit GraphBuilder(RuleList ruleList) : _ruleList(std::move(ruleList)) { }

        void addLineString(const std::vector<Point>& coordsList, const picojson::value& properties);
        void addPolygon(const std::vector<std::vector<Point>>& rings, const picojson::value& properties);

        void importGeoJSON(const picojson::value& geoJSON);
        void importGeoJSONFeatureCollection(const picojson::value& featureCollectionDef);
        void importGeoJSONFeature(const picojson::value& featureDef);

        std::shared_ptr<StaticGraph> build() const;

    private:
        struct LineVertex {
            Graph::NodeId nodeId;
            Graph::LinkMode linkMode;
            Point point;
        };
        
        struct Triangle {
            Graph::FeatureId featureId;
            Graph::SearchCriteria searchCriteria;
            RoutingAttributes attributes;
            std::array<Point, 3> points;
            std::vector<Graph::NodeId> nodeIds;
        };
        
        void importGeoJSONGeometry(const picojson::value& geometryDef, const picojson::value& properties);

        void addLineString(Graph::FeatureId featureId, const std::vector<Point>& coordsList, const picojson::value& properties);
        void addPolygon(Graph::FeatureId featureId, const std::vector<std::vector<Point>>& rings, const picojson::value& properties);
        
        const Graph::Node& getNode(Graph::NodeId nodeId) const;
        const Graph::Edge& getEdge(Graph::EdgeId edgeId) const;
        const Graph::Feature& getFeature(Graph::FeatureId featureId) const;
        
        Graph::NodeId addNode(const Graph::Node& node);
        Graph::EdgeId addEdge(const Graph::Edge& edge);
        Graph::FeatureId addFeature(const Graph::Feature& feature);

        void matchRules(const picojson::value& properties, Graph::LinkMode& linkMode, Graph::SearchCriteria& searchCriteria, RoutingAttributes& attribs, bool forward = true) const;

        static std::vector<std::vector<Point>> parseCoordinatesRings(const picojson::value& coordsDef);
        static std::vector<Point> parseCoordinatesList(const picojson::value& coordsDef);
        static Point parseCoordinates(const picojson::value& coordsDef);

        static std::pair<double, double> calculateDistance2D(const Point& pos0, const Point& pos1);

        const RuleList _ruleList;

        std::vector<Graph::Node> _nodes;
        std::vector<Graph::Edge> _edges;
        std::vector<Graph::Feature> _features;

        std::vector<LineVertex> _lineVertices;
        std::vector<Triangle> _triangles;
        std::unordered_map<std::array<double, 6>, Graph::NodeId, boost::hash<std::array<double, 6>>> _coordsNodeIdMap;
        std::unordered_map<std::string, Graph::FeatureId> _propertiesFeatureIdMap;
    };
} }

#endif
