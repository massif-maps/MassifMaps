#include "GraphBuilder.h"

#include <unordered_set>

#include <tesselator.h>

namespace {
    bool pointInsideTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos, double epsilon = 0.0) {
        cglib::vec3<double> v0 = triangle[2] - triangle[0];
        cglib::vec3<double> v1 = triangle[1] - triangle[0];
        cglib::vec3<double> v2 = pos - triangle[0];

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
    void GraphBuilder::addLineString(const std::vector<Point>& coordsList, const Graph::FeatureProperties& properties) {
        Graph::FeatureId featureId = addFeature(properties);
        addLineString(featureId, coordsList, properties);
    }

    void GraphBuilder::addPolygon(const std::vector<std::vector<Point>>& rings, const Graph::FeatureProperties& properties) {
        Graph::FeatureId featureId = addFeature(properties);
        addPolygon(featureId, rings, properties);
    }

    void GraphBuilder::importGeoJSON(const picojson::value& geoJSON) {
        std::string type = geoJSON.get("type").get<std::string>();
        if (type == "FeatureCollection") {
            importGeoJSONFeatureCollection(geoJSON);
        } else if (type == "Feature") {
            importGeoJSONFeature(geoJSON);
        } else {
            throw std::runtime_error("Unexpected element type");
        }
    }

    void GraphBuilder::importGeoJSONFeatureCollection(const picojson::value& featureCollectionDef) {
        const picojson::array& featuresDef = featureCollectionDef.get("features").get<picojson::array>();

        for (const picojson::value& featureDef : featuresDef) {
            std::string type = featureDef.get("type").get<std::string>();
            if (type != "Feature") {
                throw std::runtime_error("Unexpected element type");
            }
            
            importGeoJSONFeature(featureDef);
        }
    }

    void GraphBuilder::importGeoJSONFeature(const picojson::value& featureDef) {
        const picojson::value& geometryDef = featureDef.get("geometry");
        const picojson::value& properties = featureDef.get("properties");

        importGeoJSONGeometry(geometryDef, properties);
    }

    void GraphBuilder::importGeoJSONGeometry(const picojson::value& geometryDef, const Graph::FeatureProperties& properties) {
        std::string type = geometryDef.get("type").get<std::string>();
        const picojson::value& coordsDef = geometryDef.get("coordinates");
        
        Graph::FeatureId featureId = addFeature(properties);
        if (type == "Point") {
            // Can ignore
        } else if (type == "LineString") {
            addLineString(featureId, parseCoordinatesList(coordsDef), properties);
        } else if (type == "Polygon") {
            addPolygon(featureId, parseCoordinatesRings(coordsDef), properties);
        } else if (type == "MultiPoint") {
            // Can ignore
        } else if (type == "MultiLineString") {
            for (const picojson::value& subCoordsDef : coordsDef.get<picojson::array>()) {
                addLineString(featureId, parseCoordinatesList(subCoordsDef), properties);
            }
        } else if (type == "MultiPolygon") {
            for (const picojson::value& subCoordsDef : coordsDef.get<picojson::array>()) {
                addPolygon(featureId, parseCoordinatesRings(subCoordsDef), properties);
            }
        } else {
            throw std::runtime_error("Invalid geometry type");
        }
    }

    std::shared_ptr<StaticGraph> GraphBuilder::build() const {
        static constexpr double EPSILON = 1.0e-9;
        
        std::vector<Graph::Node> nodes = _nodes;
        std::vector<Graph::Edge> edges = _edges;

        // Connect line vertices with triangles
        for (const LineVertex& lineVertex : _lineVertices) {
            if (lineVertex.linkMode == Graph::LinkMode::NONE) {
                continue;
            } else if (lineVertex.linkMode == Graph::LinkMode::ENDPOINTS) {
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
                    edge.edgeFlags = Graph::EdgeFlags(0);
                    edge.featureId = triangle.featureId;
                    edge.triangleId = static_cast<Graph::TriangleId>(i);
                    edge.attributesId = triangle.attributesId;
                    edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeId, lineVertex.nodeId }};
                    edge.searchCriteria = triangle.searchCriteria;
                    edges.push_back(edge);

                    std::swap(edge.nodeIds[0], edge.nodeIds[1]);
                    edges.push_back(edge);
                }
            }
        }

        return std::make_shared<StaticGraph>(std::move(nodes), std::move(edges), _featureProperties, _attributes);
    }

    void GraphBuilder::addLineString(Graph::FeatureId featureId, const std::vector<Point>& coordsList, const Graph::FeatureProperties& properties) {
        Graph::LinkMode linkMode = Graph::LinkMode::ALL;
        Graph::SearchCriteria searchCriteria = Graph::SearchCriteria::EDGE;
        Graph::Attributes attribs;
        matchRules(properties, linkMode, searchCriteria, attribs, true);
        Graph::AttributesId attribsId = addAttributes(attribs);

        Graph::LinkMode linkModeBackwards = linkMode;
        Graph::SearchCriteria searchCriteriaBackwards = searchCriteria;
        Graph::Attributes attribsBackwards = attribs;
        matchRules(properties, linkModeBackwards, searchCriteriaBackwards, attribsBackwards, false);
        Graph::AttributesId attribsIdBackwards = addAttributes(attribsBackwards);

        // Create node for each vertex
        std::vector<Graph::NodeId> nodeIds;
        for (std::size_t i = 0; i < coordsList.size(); i++) {
            const Point& point = coordsList[i];

            Graph::Node node;
            node.nodeFlags = Graph::NodeFlags(i == 0 || i + 1 == coordsList.size() ? Graph::NodeFlags::ENDPOINT_VERTEX | Graph::NodeFlags::GEOMETRY_VERTEX : Graph::NodeFlags::GEOMETRY_VERTEX);
            node.points = std::array<Point, 2> {{ point, point }};
            Graph::NodeId nodeId = addNode(node);
            nodeIds.push_back(nodeId);

            // Store vertex information needed for linking it with underlying triangle
            LineVertex lineVertex;
            lineVertex.nodeId = nodeId;
            lineVertex.linkMode = linkMode;
            lineVertex.point = point;
            _lineVertices.push_back(lineVertex);
        }

        // Create edges (both forward and backward)
        for (std::size_t i = 1; i < nodeIds.size(); i++) {
            const Graph::Node& node0 = getNode(nodeIds[i - 1]);
            const Graph::Node& node1 = getNode(nodeIds[i]);

            std::pair<double, double> dist2D = calculateDistance2D(node0.points[0], node1.points[0]);

            if (!(dist2D.first != 0 && attribs.speed == FloatParameter(0.0f)) && !(dist2D.second != 0 && attribs.zSpeed == FloatParameter(0.0f))) {
                Graph::Edge edge;
                edge.edgeFlags = Graph::EdgeFlags(Graph::EdgeFlags::GEOMETRY_EDGE);
                edge.featureId = featureId;
                edge.attributesId = attribsId;
                edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i - 1], nodeIds[i] }};
                edge.searchCriteria = searchCriteria;
                addEdge(edge);
            }

            if (!(dist2D.first != 0 && attribsBackwards.speed == FloatParameter(0.0f)) && !(dist2D.second != 0 && attribsBackwards.zSpeed == FloatParameter(0.0f))) {
                Graph::Edge edge;
                edge.edgeFlags = Graph::EdgeFlags(Graph::EdgeFlags::GEOMETRY_EDGE);
                edge.featureId = featureId;
                edge.attributesId = attribsIdBackwards;
                edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i], nodeIds[i - 1] }};
                edge.searchCriteria = searchCriteriaBackwards; // Note: always equal to searchCriteria
                addEdge(edge);
            }
        }
    }

    void GraphBuilder::addPolygon(Graph::FeatureId featureId, const std::vector<std::vector<Point>>& rings, const Graph::FeatureProperties& properties) {
        Graph::LinkMode linkMode = Graph::LinkMode::ALL;
        Graph::SearchCriteria searchCriteria = Graph::SearchCriteria::SURFACE;
        Graph::Attributes attribs;
        matchRules(properties, linkMode, searchCriteria, attribs);
        attribs.delay = FloatParameter(0.0f); // reset the delay manually
        Graph::AttributesId attribsId = addAttributes(attribs);

        TESSalloc ma;
        memset(&ma, 0, sizeof(ma));
        ma.memalloc = [](void* userData, unsigned int size) { return malloc(size); };
        ma.memrealloc = [](void* userData, void* ptr, unsigned int size) { return realloc(ptr, size); };
        ma.memfree = [](void* userData, void* ptr) { free(ptr); };
        ma.extraVertices = 256;

        TESStesselator* tessPtr = tessNewTess(&ma);
        if (!tessPtr) {
            throw std::runtime_error("Failed to create polygon tesselator");
        }
        std::shared_ptr<TESStesselator> tess(tessPtr, tessDeleteTess);

        // Triangulate polygon and store original edges in a set
        std::unordered_set<std::array<double, 6>, boost::hash<std::array<double, 6>>> geometryEdges;
        for (std::size_t i = 0; i < rings.size(); i++) {
            const std::vector<Point>& coordsList = rings[i];

            std::size_t coordsCount = coordsList.size();
            if (coordsCount > 1 && coordsList.back() == coordsList.front()) {
                coordsCount--;
            }

            std::vector<TESSreal> tessCoords(coordsCount * 3);
            for (std::size_t j = 0; j < coordsCount; j++) {
                const Point& point = coordsList[j];
                tessCoords[j * 3 + 0] = static_cast<TESSreal>(point(0));
                tessCoords[j * 3 + 1] = static_cast<TESSreal>(point(1));
                tessCoords[j * 3 + 2] = static_cast<TESSreal>(point(2));
            }

            for (std::size_t j = 0; j < coordsCount; j++) {
                const Point& point0 = coordsList[j];
                const Point& point1 = coordsList[(j + 1) % coordsCount];
                geometryEdges.insert(std::array<double, 6> {{ point0(0), point0(1), point0(2), point1(0), point1(1), point1(2) }});
            }

            tessAddContour(tess.get(), 3, tessCoords.data(), 3 * sizeof(TESSreal), static_cast<int>(coordsCount));
        }
        tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 3, 0);
        const TESSreal* coords = tessGetVertices(tess.get());
        const int* elements = tessGetElements(tess.get());
        int vertexCount = tessGetVertexCount(tess.get());
        int elementCount = tessGetElementCount(tess.get());

        // Build nodes and edges for triangles
        for (int i = 0; i < elementCount; i++) {
            // Create the nodes. Each node corresponds to a triangle edge.
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

            // Store the triangle needed for linking linestrings with triangles at later stage.
            Graph::TriangleId triangleId = Graph::TriangleId(-1);
            if (points.size() == 3) { // Note: less points than 3 possible only for invalid (intersecting) geometry
                triangleId = static_cast<Graph::TriangleId>(_triangles.size());
                Triangle triangle;
                triangle.featureId = featureId;
                triangle.attributesId = attribsId;
                triangle.searchCriteria = searchCriteria;
                triangle.points = std::array<Point, 3> {{ points[0], points[1], points[2] }};
                triangle.nodeIds = nodeIds;
                _triangles.push_back(triangle);
            }

            // Build the edges
            for (std::size_t i0 = 0; i0 + 1 < nodeIds.size(); i0++) {
                for (std::size_t i1 = i0 + 1; i1 < nodeIds.size(); i1++) {
                    const Graph::Node& node0 = getNode(nodeIds[i0]);
                    const Graph::Node& node1 = getNode(nodeIds[i1]);

                    std::pair<double, double> dist2D = calculateDistance2D((node0.points[0] + node0.points[1]) * 0.5, (node1.points[0] + node1.points[1]) * 0.5);

                    if (!(dist2D.first != 0 && attribs.speed == FloatParameter(0.0f)) && !(dist2D.second != 0 && attribs.zSpeed == FloatParameter(0.0f))) {
                        Graph::Edge edge;
                        edge.edgeFlags = Graph::EdgeFlags(getNode(nodeIds[nodeIds.size() - i0 - i1]).nodeFlags & Graph::NodeFlags::GEOMETRY_VERTEX ? Graph::EdgeFlags::GEOMETRY_EDGE : 0); // use the 'geometry vertex' flag from the third (opposite edge)
                        edge.featureId = featureId;
                        edge.triangleId = triangleId;
                        edge.attributesId = attribsId;
                        edge.nodeIds = std::array<Graph::NodeId, 2> {{ nodeIds[i0], nodeIds[i1] }};
                        edge.searchCriteria = searchCriteria;
                        addEdge(edge);

                        std::swap(edge.nodeIds[0], edge.nodeIds[1]);
                        addEdge(edge);
                    }
                }
            }
        }
    }

    const Graph::Node& GraphBuilder::getNode(Graph::NodeId nodeId) const {
        return _nodes.at(nodeId);
    }

    const Graph::Edge& GraphBuilder::getEdge(Graph::EdgeId edgeId) const {
        return _edges.at(edgeId);
    }

    Graph::NodeId GraphBuilder::addNode(const Graph::Node& node) {
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

    Graph::EdgeId GraphBuilder::addEdge(const Graph::Edge& edge) {
        Graph::EdgeId edgeId = static_cast<Graph::EdgeId>(_edges.size());
        _edges.push_back(edge);
        return edgeId;
    }

    Graph::FeatureId GraphBuilder::addFeature(const Graph::FeatureProperties& properties) {
        const picojson::value& key = properties;
        auto it = _featurePropertiesIdMap.find(key);
        if (it != _featurePropertiesIdMap.end()) {
            return it->second;
        }
        Graph::FeatureId featureId = static_cast<Graph::EdgeId>(_featureProperties.size());
        _featureProperties.push_back(properties);
        _featurePropertiesIdMap[key] = featureId;
        return featureId;
    }

    Graph::AttributesId GraphBuilder::addAttributes(const Graph::Attributes& attribs) {
        for (std::size_t i = _attributes.size(); i-- > 0; ) {
            if (_attributes[i] == attribs) {
                return static_cast<Graph::AttributesId>(i);
            }
        }
        Graph::AttributesId attribsId = static_cast<Graph::AttributesId>(_attributes.size());
        _attributes.push_back(attribs);
        return attribsId;
    }

    void GraphBuilder::matchRules(const Graph::FeatureProperties& properties, Graph::LinkMode& linkMode, Graph::SearchCriteria& searchCriteria, Graph::Attributes& attribs, bool forward) const {
        for (const Rule& rule : _ruleList.getRules()) {
            if (auto filters = rule.getFilters()) {
                bool match = false;
                for (const FeatureFilter& filter : *filters) {
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

            rule.apply(attribs, forward);

            if (auto ruleLinkMode = rule.getLinkMode()) {
                linkMode = *ruleLinkMode;
            }

            if (auto ruleSearchCriteria = rule.getSearchCriteria()) {
                searchCriteria = *ruleSearchCriteria;
            }
        }
    }

    std::vector<std::vector<Point>> GraphBuilder::parseCoordinatesRings(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();

        std::vector<std::vector<Point>> rings;
        rings.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++) {
            std::vector<Point> coordsList = parseCoordinatesList(coordsArray[i]);
            rings.push_back(std::move(coordsList));
        }
        return rings;
    }

    std::vector<Point> GraphBuilder::parseCoordinatesList(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();

        std::vector<Point> coordsList;
        coordsList.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++) {
            Point point = parseCoordinates(coordsArray[i]);
            coordsList.push_back(point);
        }
        return coordsList;
    }

    Point GraphBuilder::parseCoordinates(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();
        return Point(coordsArray.at(0).get<double>(), coordsArray.at(1).get<double>(), coordsArray.size() > 2 ? coordsArray.at(2).get<double>() : 0.0);
    }

    std::pair<double, double> GraphBuilder::calculateDistance2D(const Point& pos0, const Point& pos1) {
        // Note: we simply use this to detect if points are different; thus we can ignore longitude scaling
        cglib::vec3<double> posDelta = pos1 - pos0;
        double distXY = cglib::length(cglib::vec2<double>(posDelta(0), posDelta(1)));
        double distZ = std::abs(posDelta(2));
        return std::make_pair(distXY, distZ);
    }
} }
