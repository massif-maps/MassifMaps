#include "GeoJSONGraphBuilder.h"

#include <unordered_set>

#include <tesselator.h>

namespace {
    bool pointInsideTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos, double epsilon = 0.0) {
        cglib::vec3<double> v0 = triangle[2] - triangle[0];
        cglib::vec3<double> v1 = triangle[1] - triangle[0];
        cglib::vec3<double>	v2 = pos - triangle[0];

        double dot00 = cglib::dot_product(v0, v0);
        double dot01 = cglib::dot_product(v0, v1);
        double dot02 = cglib::dot_product(v0, v2);
        double dot11 = cglib::dot_product(v1, v1);
        double dot12 = cglib::dot_product(v1, v2);

        double invDenom = 1 / (dot00 * dot11 - dot01 * dot01);
        double u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        double v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        cglib::vec3<double> posProj = triangle[0] + v0 * u + v1 * v;
        return u >= -epsilon && v >= -epsilon && u + v <= 1 + epsilon && cglib::length(pos - posProj) <= epsilon;
    }
}

namespace carto { namespace sgre {
    void GeoJSONGraphBuilder::importGeoJSON(const picojson::value& geoJSON) {
        std::string type = geoJSON.get("type").get<std::string>();
        if (type == "FeatureCollection") {
            importFeatureCollection(geoJSON);
        } else if (type == "Feature") {
            importFeature(geoJSON);
        } else {
            throw std::runtime_error("Unexpected element type");
        }
    }

    std::shared_ptr<StaticGraph> GeoJSONGraphBuilder::build() const {
        static constexpr double EPSILON = 1.0e-9;
        
        std::vector<Graph::Node> nodes = _nodes;
        std::vector<Graph::Edge> edges = _edges;
        for (const LineVertex& lineVertex : _lineVertices) {
            if (lineVertex.searchCriteria == Graph::SearchCriteria::END_VERTEX) {
                const Graph::Node& node = getNode(lineVertex.nodeId);
                if (!(node.nodeFlags & Graph::NodeFlags::ENDPOINT_VERTEX)) {
                    continue;
                }
            }
            
            for (std::size_t i = 0; i < _triangles.size(); i++) {
                const Triangle& triangle = _triangles[i];
                if (!pointInsideTriangle(triangle.points, lineVertex.point, EPSILON)) {
                    continue;
                }

                for (Graph::NodeId nodeId : triangle.nodeIds) {
                    Graph::Edge edge;
                    edge.featureId = triangle.featureId;
                    edge.triangleId = static_cast<Graph::TriangleId>(i);
                    edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeId, lineVertex.nodeId }};
                    edge.searchCriteria = triangle.searchCriteria;
                    edge.attributes = triangle.attributes;
                    edges.push_back(edge);

                    std::swap(edge.nodeIds[0], edge.nodeIds[1]);
                    edges.push_back(edge);
                }
            }
        }

        return std::make_shared<StaticGraph>(std::move(nodes), std::move(edges), _features);
    }

    void GeoJSONGraphBuilder::importFeatureCollection(const picojson::value& featureCollectionDef) {
        const picojson::array& featuresDef = featureCollectionDef.get("features").get<picojson::array>();

        for (const picojson::value& featureDef : featuresDef) {
            std::string type = featureDef.get("type").get<std::string>();
            if (type != "Feature") {
                throw std::runtime_error("Unexpected element type");
            }
            
            importFeature(featureDef);
        }
    }

    void GeoJSONGraphBuilder::importFeature(const picojson::value& featureDef) {
        const picojson::value& geometryDef = featureDef.get("geometry");
        const picojson::value& properties = featureDef.get("properties");

        Graph::SearchCriteria searchCriteria = Graph::SearchCriteria::FULL;
        std::array<RoutingAttributes, 2> attribs;
        applyRules(properties, searchCriteria, attribs);

        Graph::FeatureId featureId = addFeature(properties);
        importGeometry(featureId, geometryDef, searchCriteria, attribs);
    }

    void GeoJSONGraphBuilder::importGeometry(Graph::FeatureId featureId, const picojson::value& geometryDef, Graph::SearchCriteria searchCriteria, const std::array<RoutingAttributes, 2>& attribs) {
        std::string type = geometryDef.get("type").get<std::string>();
        
        std::vector<picojson::value> coordsDefList;
        if (type.substr(0, 5) == "Multi") {
            type = type.substr(5);
            for (const picojson::value& coordsDef : geometryDef.get("coordinates").get<picojson::array>()) {
                coordsDefList.push_back(coordsDef);
            }
        } else {
            coordsDefList.push_back(geometryDef.get("coordinates"));
        }
        
        if (type == "Point") {
            for (const picojson::value& coordsDef : coordsDefList) {
                importPoint(featureId, coordsDef, searchCriteria, attribs[0]);
            }
        } else if (type == "LineString") {
            for (const picojson::value& coordsDef : coordsDefList) {
                importLineString(featureId, coordsDef, searchCriteria, attribs);
            }
        } else if (type == "Polygon") {
            for (const picojson::value& coordsDef : coordsDefList) {
                importPolygon(featureId, coordsDef, searchCriteria, attribs[0]);
            }
        } else {
            throw std::runtime_error("Invalid geometry type");
        }
    }

    void GeoJSONGraphBuilder::importPoint(Graph::FeatureId featureId, const picojson::value& coordsDef, Graph::SearchCriteria searchCriteria, const RoutingAttributes& attribs) {
        // Note: not needed
    }

    void GeoJSONGraphBuilder::importLineString(Graph::FeatureId featureId, const picojson::value& coordsDef, Graph::SearchCriteria searchCriteria, const std::array<RoutingAttributes, 2>& attribs) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();

        std::vector<Graph::NodeId> nodeIds;
        nodeIds.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++) {
            Point point = parseCoordinates(coordsArray[i]);
            Graph::Node node;
            node.nodeFlags = Graph::NodeFlags(i == 0 || i + 1 == coordsArray.size() ? Graph::NodeFlags::ENDPOINT_VERTEX | Graph::NodeFlags::GEOMETRY_VERTEX : Graph::NodeFlags::GEOMETRY_VERTEX);
            node.points = std::array<Point, 2> {{ point, point }};
            Graph::NodeId nodeId = addNode(node);
            nodeIds.push_back(nodeId);

            bool addLineVertex = false;
            switch (searchCriteria) {
            case Graph::SearchCriteria::EDGE:
            case Graph::SearchCriteria::FULL:
            case Graph::SearchCriteria::ANY_VERTEX:
                addLineVertex = true;
                break;
            case Graph::SearchCriteria::END_VERTEX:
                addLineVertex = (i == 0 || i + 1 == coordsArray.size());
                break;
            }
            if (addLineVertex) {
                LineVertex lineVertex;
                lineVertex.nodeId = nodeId;
                lineVertex.searchCriteria = searchCriteria;
                lineVertex.point = point;
                _lineVertices.push_back(lineVertex);
            }
        }

        for (std::size_t i = 1; i < nodeIds.size(); i++) {
            const Graph::Node& node0 = getNode(nodeIds[i - 1]);
            const Graph::Node& node1 = getNode(nodeIds[i]);

            std::pair<double, double> distance = calculateDistance(node0.points[0], node1.points[0]);

            if (!(distance.first != 0 && attribs[0].speed == 0) && !(distance.second != 0 && attribs[0].zSpeed == 0)) {
                Graph::Edge edge;
                edge.featureId = featureId;
                edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i - 1], nodeIds[i] }};
                edge.searchCriteria = searchCriteria;
                edge.attributes = attribs[0];
                addEdge(edge);
            }

            if (!(distance.first != 0 && attribs[1].speed == 0) && !(distance.second != 0 && attribs[1].zSpeed == 0)) {
                Graph::Edge edge;
                edge.featureId = featureId;
                edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i], nodeIds[i - 1] }};
                edge.searchCriteria = searchCriteria;
                edge.attributes = attribs[1];
                addEdge(edge);
            }
        }
    }

    void GeoJSONGraphBuilder::importPolygon(Graph::FeatureId featureId, const picojson::value& ringsDef, Graph::SearchCriteria searchCriteria, const RoutingAttributes& attribs) {
        const picojson::array& ringsArray = ringsDef.get<picojson::array>();

        TESSalloc ma;
        memset(&ma, 0, sizeof(ma));
        ma.memalloc = [](void* userData, unsigned int size) { return malloc(size); };
        ma.memrealloc = [](void* userData, void* ptr, unsigned int size) { return realloc(ptr, size); };
        ma.memfree = [](void* userData, void* ptr) { free(ptr); };
        ma.extraVertices = 256; // allow 256 extra vertices.

        TESStesselator* tessPtr = tessNewTess(&ma);
        if (!tessPtr) {
            throw std::runtime_error("Failed to create polygon tesselator");
        }
        std::shared_ptr<TESStesselator> tess(tessPtr, tessDeleteTess);

        std::unordered_set<std::array<double, 6>, boost::hash<std::array<double, 6>>> geometryEdges;
        for (std::size_t i = 0; i < ringsArray.size(); i++) {
            const picojson::array& coordsArray = ringsArray[i].get<picojson::array>();
            std::size_t coordsCount = coordsArray.size();
            if (coordsCount > 1 && coordsArray.back() == coordsArray.front()) {
                coordsCount--;
            }

            std::vector<TESSreal> coords(coordsCount * 3);
            for (std::size_t j = 0; j < coordsCount; j++) {
                Point point = parseCoordinates(coordsArray[j]);
                coords[j * 3 + 0] = static_cast<TESSreal>(point(0));
                coords[j * 3 + 1] = static_cast<TESSreal>(point(1));
                coords[j * 3 + 2] = static_cast<TESSreal>(point(2));
            }

            for (std::size_t i0 = 0; i0 < coordsCount; i0++) {
                std::size_t i1 = (i0 + 1) % coordsCount;
                Point point0(coords[i0 * 3 + 0], coords[i0 * 3 + 1], coords[i0 * 3 + 2]);
                Point point1(coords[i1 * 3 + 0], coords[i1 * 3 + 1], coords[i1 * 3 + 2]);
                geometryEdges.insert(std::array<double, 6> {{ point0(0), point0(1), point0(2), point1(0), point1(1), point1(2) }});
            }

            tessAddContour(tess.get(), 3, coords.data(), 3 * sizeof(TESSreal), static_cast<int>(coordsCount));
        }
        tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 3, 0);
        const TESSreal* coords = tessGetVertices(tess.get());
        const int* elements = tessGetElements(tess.get());
        int vertexCount = tessGetVertexCount(tess.get());
        int elementCount = tessGetElementCount(tess.get());

        for (int i = 0; i < elementCount; i++) {
            std::vector<Point> points;
            std::vector<Graph::NodeId> nodeIds;
            for (int j = 0; j < 3; j++) {
                int i0 = elements[i * 3 + j];
                int i1 = elements[i * 3 + (j + 1) % 3];
                if (i0 == TESS_UNDEF || i1 == TESS_UNDEF) {
                    continue;
                }
                Point point0(coords[i0 * 3 + 0], coords[i0 * 3 + 1], coords[i0 * 3 + 2]);
                Point point1(coords[i1 * 3 + 0], coords[i1 * 3 + 1], coords[i1 * 3 + 2]);
                points.push_back(point0);

                Graph::Node node;
                node.nodeFlags = geometryEdges.count({{ point0(0), point0(1), point0(2), point1(0), point1(1), point1(2) }}) + geometryEdges.count({{ point1(0), point1(1), point1(2), point0(0), point0(1), point0(2) }}) > 0 ? Graph::NodeFlags::GEOMETRY_VERTEX : Graph::NodeFlags();
                node.points = std::array<Point, 2> {{ point0, point1 }};
                Graph::NodeId nodeId = addNode(node);
                nodeIds.push_back(nodeId);
            }

            Graph::TriangleId triangleId = Graph::TriangleId(-1);
            if (points.size() == 3) {
                triangleId = static_cast<Graph::TriangleId>(_triangles.size());
                Triangle triangle;
                triangle.featureId = featureId;
                triangle.searchCriteria = searchCriteria;
                triangle.attributes = attribs;
                triangle.points = std::array<Point, 3> {{ points[0], points[1], points[2] }};
                triangle.nodeIds = nodeIds;
                _triangles.push_back(triangle);
            }

            for (std::size_t i0 = 0; i0 + 1 < nodeIds.size(); i0++) {
                for (std::size_t i1 = i0 + 1; i1 < nodeIds.size(); i1++) {
                    const Graph::Node& node0 = getNode(nodeIds[i0]);
                    const Graph::Node& node1 = getNode(nodeIds[i1]);

                    std::pair<double, double> distance = calculateDistance((node0.points[0] + node0.points[1]) * 0.5, (node1.points[0] + node1.points[1]) * 0.5);

                    if (!(distance.first != 0 && attribs.speed == 0) && !(distance.second != 0 && attribs.zSpeed == 0)) {
                        Graph::Edge edge;
                        edge.featureId = featureId;
                        edge.triangleId = triangleId;
                        edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i0], nodeIds[i1] }};
                        edge.searchCriteria = searchCriteria;
                        edge.attributes = attribs;
                        addEdge(edge);

                        std::swap(edge.nodeIds[0], edge.nodeIds[1]);
                        addEdge(edge);
                    }
                }
            }
        }
    }

    const Graph::Node& GeoJSONGraphBuilder::getNode(Graph::NodeId nodeId) const {
        return _nodes.at(nodeId);
    }

    const Graph::Edge& GeoJSONGraphBuilder::getEdge(Graph::EdgeId edgeId) const {
        return _edges.at(edgeId);
    }

    const Graph::Feature& GeoJSONGraphBuilder::getFeature(Graph::FeatureId featureId) const {
        return _features.at(featureId);
    }

    Graph::NodeId GeoJSONGraphBuilder::addNode(const Graph::Node& node) {
        std::array<double, 6> key {{ node.points[0](0), node.points[0](1), node.points[0](2), node.points[1](0), node.points[1](1), node.points[1](2) }};
        auto it = _coordsNodeIdMap.find(key);
        if (it == _coordsNodeIdMap.end() && node.points[0] != node.points[1]) {
            std::array<double, 6> revKey {{ node.points[1](0), node.points[1](1), node.points[1](2), node.points[0](0), node.points[0](1), node.points[0](2) }};
            it = _coordsNodeIdMap.find(revKey);
        }
        if (it != _coordsNodeIdMap.end()) {
            return it->second;
        }
        Graph::NodeId nodeId = static_cast<Graph::NodeId>(_nodes.size());
        _nodes.push_back(node);
        _coordsNodeIdMap[key] = nodeId;
        return nodeId;
    }

    Graph::EdgeId GeoJSONGraphBuilder::addEdge(const Graph::Edge& edge) {
        Graph::EdgeId edgeId = static_cast<Graph::EdgeId>(_edges.size());
        _edges.push_back(edge);
        return edgeId;
    }

    Graph::FeatureId GeoJSONGraphBuilder::addFeature(const Graph::Feature& feature) {
        std::string key = feature.serialize();
        auto it = _propertiesFeatureIdMap.find(key);
        if (it != _propertiesFeatureIdMap.end()) {
            return it->second;
        }
        Graph::FeatureId featureId = static_cast<Graph::EdgeId>(_features.size());
        _features.push_back(feature);
        _propertiesFeatureIdMap[key] = featureId;
        return featureId;
    }

    void GeoJSONGraphBuilder::applyRules(const picojson::value& properties, Graph::SearchCriteria& searchCriteria, std::array<RoutingAttributes, 2>& attribs) const {
        for (const Rule& rule : _ruleList.getRules()) {
            if (auto filters = rule.getFilters()) {
                bool match = false;
                for (const Rule::Filter& filter : *filters) {
                    auto it = filter.begin();
                    for (; it != filter.end(); it++) {
                        if (!properties.contains(it->first) || properties.get(it->first) != it->second) {
                            break;
                        }
                    }
                    if (it == filter.end()) {
                        match = true;
                        break;
                    }
                }
                if (!match) {
                    continue;
                }
            }

            rule.apply(attribs[0], true);
            rule.apply(attribs[1], false);
            if (auto ruleSearchCriteria = rule.getSearchCriteria()) {
                searchCriteria = *ruleSearchCriteria;
            }
        }
    }

    Point GeoJSONGraphBuilder::parseCoordinates(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();
        return Point(coordsArray.at(0).get<double>(), coordsArray.at(1).get<double>(), coordsArray.size() > 2 ? coordsArray.at(2).get<double>() : 0.0);
    }

    std::pair<double, double> GeoJSONGraphBuilder::calculateDistance(const Point& pos0, const Point& pos1) {
        // Note: we simply use this to detect if points are different; thus we can ignore longitude scaling
        cglib::vec3<double> posDelta = pos1 - pos0;
        double distXY = cglib::length(cglib::vec2<double>(posDelta(0), posDelta(1)));
        double distZ = std::abs(posDelta(2));
        return std::make_pair(distXY, distZ);
    }
} }
