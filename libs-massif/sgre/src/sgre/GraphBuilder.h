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

        void addLineString(const std::vector<Point>& coordsList, const Graph::FeatureProperties& properties);
        void addPolygon(const std::vector<std::vector<Point>>& rings, const Graph::FeatureProperties& properties);

        void importGeoJSON(const picojson::value& geoJSON);
        void importGeoJSONFeatureCollection(const picojson::value& featureCollectionDef);
        void importGeoJSONFeature(const picojson::value& featureDef);

        std::shared_ptr<StaticGraph> build() const;

    private:
        struct LineVertex {
            Graph::NodeId nodeId = Graph::NodeId(-1);
            Graph::LinkMode linkMode = Graph::LinkMode::NONE;
            Point point;
        };
        
        struct Triangle {
            Graph::FeatureId featureId = Graph::FeatureId(-1);
            Graph::AttributesId attributesId = Graph::AttributesId(-1);
            Graph::SearchCriteria searchCriteria = Graph::SearchCriteria::NONE;
            std::array<Point, 3> points;
            std::vector<Graph::NodeId> nodeIds;
        };
        
        void importGeoJSONGeometry(const picojson::value& geometryDef, const Graph::FeatureProperties& properties);

        void addLineString(Graph::FeatureId featureId, const std::vector<Point>& coordsList, const Graph::FeatureProperties& properties);
        void addPolygon(Graph::FeatureId featureId, const std::vector<std::vector<Point>>& rings, const Graph::FeatureProperties& properties);
        
        const Graph::Node& getNode(Graph::NodeId nodeId) const;
        const Graph::Edge& getEdge(Graph::EdgeId edgeId) const;
        
        Graph::NodeId addNode(const Graph::Node& node);
        Graph::EdgeId addEdge(const Graph::Edge& edge);
        Graph::FeatureId addFeature(const Graph::FeatureProperties& properties);
        Graph::AttributesId addAttributes(const Graph::Attributes& attribs);

        void matchRules(const Graph::FeatureProperties& properties, Graph::LinkMode& linkMode, Graph::SearchCriteria& searchCriteria, Graph::Attributes& attribs, bool forward = true) const;

        static std::vector<std::vector<Point>> parseCoordinatesRings(const picojson::value& coordsDef);
        static std::vector<Point> parseCoordinatesList(const picojson::value& coordsDef);
        static Point parseCoordinates(const picojson::value& coordsDef);

        static std::pair<double, double> calculateDistance2D(const Point& pos0, const Point& pos1);

        const RuleList _ruleList;

        std::vector<Graph::Node> _nodes;
        std::vector<Graph::Edge> _edges;
        std::vector<Graph::FeatureProperties> _featureProperties;
        std::vector<Graph::Attributes> _attributes;

        std::vector<LineVertex> _lineVertices;
        std::vector<Triangle> _triangles;
        std::unordered_map<std::array<double, 6>, Graph::NodeId, boost::hash<std::array<double, 6>>> _coordsNodeIdMap;
        std::unordered_map<picojson::value, Graph::FeatureId> _featurePropertiesIdMap;
    };
} }

#endif
