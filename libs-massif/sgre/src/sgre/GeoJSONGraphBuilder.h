/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_GEOJSONGRAPHBUILDER_H_
#define _CARTO_SGRE_GEOJSONGRAPHBUILDER_H_

#include "Base.h"
#include "Rule.h"
#include "Graph.h"

#include <memory>
#include <vector>
#include <unordered_map>

#include <boost/functional/hash.hpp>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class GeoJSONGraphBuilder final {
    public:
        GeoJSONGraphBuilder() = delete;
        explicit GeoJSONGraphBuilder(RuleList ruleList) : _ruleList(std::move(ruleList)) { }

        void importGeoJSON(const picojson::value& geoJSON);

        std::shared_ptr<StaticGraph> build() const;

    private:
        struct LineVertex {
			Graph::NodeId nodeId;
			Graph::SearchCriteria searchCriteria;
			Point point;
        };
        
        struct Triangle {
            Graph::FeatureId featureId;
            Graph::SearchCriteria searchCriteria;
            RoutingAttributes attributes;
            std::array<Point, 3> points;
            std::vector<Graph::NodeId> nodeIds;
        };
        
        void importFeatureCollection(const picojson::value& featureCollectionDef);
        void importFeature(const picojson::value& featureDef);
        
        void importGeometry(Graph::FeatureId featureId, const picojson::value& geometryDef, Graph::SearchCriteria searchCriteria, const std::array<RoutingAttributes, 2>& attribs);
        void importPoint(Graph::FeatureId featureId, const picojson::value& coordsDef, Graph::SearchCriteria searchCriteria, const RoutingAttributes& attribs);
        void importLineString(Graph::FeatureId featureId, const picojson::value& coordsDef, Graph::SearchCriteria searchCriteria, const std::array<RoutingAttributes, 2>& attribs);
        void importPolygon(Graph::FeatureId featureId, const picojson::value& ringsDef, Graph::SearchCriteria searchCriteria, const RoutingAttributes& attribs);
        
        const Graph::Node& getNode(Graph::NodeId nodeId) const;
        const Graph::Edge& getEdge(Graph::EdgeId edgeId) const;
        const Graph::Feature& getFeature(Graph::FeatureId featureId) const;
        
        Graph::NodeId addNode(const Graph::Node& node);
        Graph::EdgeId addEdge(const Graph::Edge& edge);
        Graph::FeatureId addFeature(const Graph::Feature& feature);

        void applyRules(const picojson::value& properties, Graph::SearchCriteria& searchCriteria, std::array<RoutingAttributes, 2>& attribs) const;

        static Point parseCoordinates(const picojson::value& coordsDef);

        static std::pair<double, double> calculateDistance(const Point& pos0, const Point& pos1);

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
