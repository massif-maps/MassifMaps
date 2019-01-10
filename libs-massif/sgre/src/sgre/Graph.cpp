#include "Graph.h"

#include <queue>
#include <algorithm>

#include <boost/math/constants/constants.hpp>

namespace {
    double clamp(double val, double min, double max) {
        return std::max(min, std::min(max, val));
    }

    bool pointOnLine(const std::array<cglib::vec3<double>, 2>& line, const cglib::vec3<double>& pos) {
        cglib::vec3<double> v0 = line[1] - line[0];
        cglib::vec3<double> v1 = pos - line[0];
        return (v1 == cglib::vec3<double>(0, 0, 0) || cglib::dot_product(cglib::unit(v0), cglib::unit(v1)) == 1);
    }

    bool pointInsideTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos) {
        cglib::vec3<double> v0 = triangle[2] - triangle[0];
        cglib::vec3<double> v1 = triangle[1] - triangle[0];
        cglib::vec3<double>	v2 = pos - triangle[0];
        if (cglib::dot_product(v2, cglib::vector_product(v0, v1)) != 0) {
            return false;
        }

        double dot00 = cglib::dot_product(v0, v0);
        double dot01 = cglib::dot_product(v0, v1);
        double dot02 = cglib::dot_product(v0, v2);
        double dot11 = cglib::dot_product(v1, v1);
        double dot12 = cglib::dot_product(v1, v2);

        double invDenom = 1 / (dot00 * dot11 - dot01 * dot01);
        double u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        double v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        return u >= 0 && v >= 0 && u + v <= 1;
    }

    cglib::vec3<double> closestPointOnLine(const std::array<cglib::vec3<double>, 2>& line, const cglib::vec3<double>& pos) {
        cglib::vec3<double> edge = line[1] - line[0];
        if (cglib::norm(edge) == 0) {
            return line[0];
        }
        cglib::vec3<double> v0 = pos - line[0];

        double c = cglib::dot_product(edge, v0);
        double d = cglib::dot_product(edge, edge);

        double s = (v0 != cglib::vec3<double>(0, 0, 0) ? clamp(c / d, 0, 1) : 0);
        return line[0] + edge * s;
    }

    cglib::vec3<double> closestPointOnTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos) {
        cglib::vec3<double> edge0 = triangle[1] - triangle[0];
        if (cglib::norm(edge0) == 0) {
            return closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ triangle[2], triangle[1] }}, pos);
        }
        cglib::vec3<double> edge1 = triangle[2] - triangle[0];
        if (cglib::norm(edge1) == 0) {
            return closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ triangle[1], triangle[0] }}, pos);
        }
        cglib::vec3<double> edge2 = triangle[1] - triangle[2];
        if (cglib::norm(edge2) == 0) {
            return closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ triangle[2], triangle[0] }}, pos);
        }
        cglib::vec3<double> v0 = triangle[0] - pos;

        double a = cglib::dot_product(edge0, edge0);
        double b = cglib::dot_product(edge0, edge1);
        double c = cglib::dot_product(edge1, edge1);
        double d = cglib::dot_product(edge0, v0);
        double e = cglib::dot_product(edge1, v0);

        double det = a * c - b * b;
        double s = b * e - c * d;
        double t = b * d - a * e;

        if (s + t < det) {
            if (s < 0) {
                if (t < 0) {
                    if (d < 0) {
                        s = clamp(-d / a, 0, 1);
                        t = 0;
                    } else {
                        s = 0;
                        t = clamp(-e / c, 0, 1);
                    }
                } else {
                    s = 0;
                    t = clamp(-e / c, 0, 1);
                }
            } else if (t < 0) {
                s = clamp(-d / a, 0, 1);
                t = 0;
            } else {
                double invDet = 1 / det;
                s *= invDet;
                t *= invDet;
            }
        } else {
            if (s < 0) {
                double tmp0 = b + d;
                double tmp1 = c + e;
                if (tmp1 > tmp0) {
                    double numer = tmp1 - tmp0;
                    double denom = a - 2 * b + c;
                    s = clamp(numer / denom, 0, 1);
                    t = 1 - s;
                } else {
                    t = clamp(-e / c, 0, 1);
                    s = 0;
                }
            } else if (t < 0) {
                if (a + d > b + e) {
                    double numer = c + e - b - d;
                    double denom = a - 2 * b + c;
                    s = clamp(numer / denom, 0, 1);
                    t = 1 - s;
                } else {
                    s = clamp(-e / c, 0, 1);
                    t = 0;
                }
            } else {
                double numer = c + e - b - d;
                double denom = a - 2 * b + c;
                s = clamp(numer / denom, 0, 1);
                t = 1 - s;
            }
        }

        return triangle[0] + edge0 * s + edge1 * t;
    }
}

namespace carto { namespace sgre {
    StaticGraph::StaticGraph(std::vector<Node> nodes, std::vector<Edge> edges, std::vector<Feature> features) :
        _nodes(std::move(nodes)), _edges(std::move(edges)), _features(std::move(features))
    {
        // Add edge ids to nodes
        linkNodeEdgeIds(_nodes, _edges);

        // Build RTree for faster spatial queries
        cglib::bbox3<double> bounds = cglib::bbox3<double>::smallest();
        std::vector<EdgeId> edgeIds;
        edgeIds.reserve(_edges.size());
        for (std::size_t i = 0; i < _edges.size(); i++) {
            EdgeId edgeId = static_cast<EdgeId>(i);
            const Edge& edge = getEdge(edgeId);
            const Node& node0 = getNode(edge.nodeIds[0]);
            const Node& node1 = getNode(edge.nodeIds[1]);
            bounds.add(node0.points.begin(), node0.points.end());
            bounds.add(node1.points.begin(), node1.points.end());
            edgeIds.push_back(edgeId);
        }
        _rootNode = buildRTree(bounds, edgeIds);
    }

    std::vector<std::pair<Graph::EdgeId, Point>> StaticGraph::findNearestEdgePoint(const Point& pos) const {
        struct RTreeNodeRecord {
            std::shared_ptr<const RTreeNode> node;
            double dist;

            bool operator < (const RTreeNodeRecord& rec) const { return rec.dist < dist; }
        };
        
        static constexpr double DIST_EPSILON = 1.0e-6;
        static constexpr double EARTH_RADIUS = 6378137.0;

        double latScale = EARTH_RADIUS * boost::math::constants::pi<double>() / 180.0;
        double lngScale = latScale * std::max(std::cos(pos(1) * boost::math::constants::pi<double>() / 180.0), 0.01);

        double bestDist = std::numeric_limits<double>::infinity();
        std::vector<std::pair<Graph::EdgeId, Point>> bestResults;

        // Do the matching starting from root RTree node. Use priority queue for sorting and rejecting subnodes.
        std::priority_queue<RTreeNodeRecord> nodeQueue;
        Point nearestRootPos = _rootNode->bounds.nearest_point(pos);
        double rootDist = calculateDistance(nearestRootPos, pos, lngScale, latScale);
        nodeQueue.push({ _rootNode, rootDist });
        while (!nodeQueue.empty()) {
            std::shared_ptr<const RTreeNode> node = nodeQueue.top().node;
            double dist = nodeQueue.top().dist;
            nodeQueue.pop();

            if (dist > bestDist + DIST_EPSILON) {
                break;
            }

            // Store subnodes in the priority queue
            for (const std::shared_ptr<const RTreeNode>& subNode : node->subNodes) {
                if (subNode) {
                    Point nearestPos = subNode->bounds.nearest_point(pos);
                    double dist = calculateDistance(nearestPos, pos, lngScale, latScale);
                    nodeQueue.push({ subNode, dist });
                }
            }

            // Do slow matching for edges store in this RTree node
            for (EdgeId edgeId : node->edgeIds) {
                const Edge& edge = getEdge(edgeId);

                if (auto closestPos = findNearestEdgePoint(edge, pos, lngScale, latScale)) {
                    double dist = calculateDistance(*closestPos, pos, lngScale, latScale);
                    if (dist <= bestDist + DIST_EPSILON) {
                        if (dist + DIST_EPSILON < bestDist) {
                            bestDist = dist;
                            bestResults.clear();
                        }
                        bestResults.emplace_back(edgeId, *closestPos);
                    }
                }
            }
        }

        return bestResults;
    }

    boost::optional<Point> StaticGraph::findNearestEdgePoint(const Edge& edge, const Point& pos, double lngScale, double latScale) const {
        const Node& node0 = getNode(edge.nodeIds[0]);
        const Node& node1 = getNode(edge.nodeIds[1]);

        // First find i0 and i1 for node points such that P[i0] == P[i1] (find the shared point of triangle edges)
        int i0 = 0, i1 = 0;
        for (int i = 0; i < 2; i++) {
            if (node0.points[i] == node1.points[0]) {
                i0 = i; i1 = 0;
                break;
            } else if (node0.points[i] == node1.points[1]) {
                i0 = i; i1 = 1;
                break;
            }
        }

        // Convert WGS84 coordinates to local orthonormal basis for simpler processing
        Point posScaled(pos(0) * lngScale, pos(1) * latScale, pos(2));
        Point points0Scaled[2] = { Point(node0.points[0](0) * lngScale, node0.points[0](1) * latScale, node0.points[0](2)), Point(node0.points[1](0) * lngScale, node0.points[1](1) * latScale, node0.points[1](2)) };
        Point points1Scaled[2] = { Point(node1.points[0](0) * lngScale, node1.points[0](1) * latScale, node1.points[0](2)), Point(node1.points[1](0) * lngScale, node1.points[1](1) * latScale, node1.points[1](2)) };

        // Handle the edge based on its search criteria. Some cases are very tricky and the code depends on the graph builder.
        switch (edge.searchCriteria) {
        case SearchCriteria::NONE:
            return boost::optional<Point>();
        case SearchCriteria::VERTEX:
            if (node0.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                if (node1.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                    return (cglib::norm(posScaled - points0Scaled[i0]) < cglib::norm(posScaled - points1Scaled[i1]) ? node0.points[i0] : node1.points[i1]);
                }
                return node0.points[i0];
            } else if (node1.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                return node1.points[i1];
            }
            return boost::optional<Point>();
        case SearchCriteria::FIRST_LAST_VERTEX:
            if (node0.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                if (node1.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                    return (cglib::norm(posScaled - points0Scaled[i0]) < cglib::norm(posScaled - points1Scaled[i1]) ? node0.points[i0] : node1.points[i1]);
                }
                return node0.points[i0];
            } else if (node1.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                return node1.points[i1];
            }
            return boost::optional<Point>();
        case SearchCriteria::EDGE:
            if (edge.edgeFlags & EdgeFlags::GEOMETRY_EDGE) {
                if (pointOnLine(std::array<cglib::vec3<double>, 2> {{ node0.points[1 - i0], node1.points[1 - i1] }}, pos)) {
                    return pos;
                }
                Point closestScaled = closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ points0Scaled[1 - i0], points1Scaled[1 - i1] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            }
            return boost::optional<Point>();
        case SearchCriteria::SURFACE:
            if (node0.points[i0] == node1.points[i1]) {
                if (pointInsideTriangle(std::array<cglib::vec3<double>, 3> {{ node0.points[i0], node0.points[1 - i0], node1.points[1 - i1] }}, pos)) {
                    return pos;
                }
                Point closestScaled = closestPointOnTriangle(std::array<cglib::vec3<double>, 3> {{ points0Scaled[i0], points0Scaled[1 - i0], points1Scaled[1 - i1] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            } else {
                if (pointOnLine(std::array<cglib::vec3<double>, 2> {{ node0.points[0], node1.points[0] }}, pos)) {
                    return pos;
                }
                Point closestScaled = closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ points0Scaled[0], points1Scaled[0] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            }
        }

        return boost::optional<Point>();
    }

    std::shared_ptr<StaticGraph::RTreeNode> StaticGraph::buildRTree(const cglib::bbox3<double>& bounds, std::vector<EdgeId> edgeIds) const {
        static const std::size_t SPLIT_THRESHOLD = 4;

        // Create non-leaf nodes?
        if (edgeIds.size() > SPLIT_THRESHOLD) {
            cglib::bbox3<double> splitBounds[3][2];
            std::vector<EdgeId> splitEdgeIds[3][2];
            for (int dim = 0; dim < 3; dim++) {
                splitBounds[dim][0] = splitBounds[dim][1] = cglib::bbox3<double>::smallest();
            }

            // Do simple splitting by comparing the center of each edge to the center of the parent
            cglib::vec3<double> center = bounds.center();
            for (EdgeId edgeId : edgeIds) {
                const Edge& edge = getEdge(edgeId);
                const Node& node0 = getNode(edge.nodeIds[0]);
                const Node& node1 = getNode(edge.nodeIds[1]);
                cglib::bbox3<double> edgeBounds = cglib::bbox3<double>::smallest();
                edgeBounds.add(node0.points.begin(), node0.points.end());
                edgeBounds.add(node1.points.begin(), node1.points.end());
                for (int dim = 0; dim < 3; dim++) {
                    int idx = edgeBounds.center()(dim) < center(dim) ? 0 : 1;
                    splitBounds[dim][idx].add(edgeBounds);
                    splitEdgeIds[dim][idx].push_back(edgeId);
                }
            }

            // Find best splitting dimension (best means 'most balanced' by the number of edges here)
            int bestDim = 0;
            for (int dim = 1; dim < 3; dim++) {
                if (std::min(splitEdgeIds[dim][0].size(), splitEdgeIds[dim][1].size()) > std::min(splitEdgeIds[bestDim][0].size(), splitEdgeIds[bestDim][1].size())) {
                    bestDim = dim;
                }
            }

            // If splitting succeeded (resulting subtrees contain enough elements), recurse
            if (splitEdgeIds[bestDim][0].size() > SPLIT_THRESHOLD / 4 && splitEdgeIds[bestDim][1].size() > SPLIT_THRESHOLD / 4) {
                auto node = std::make_shared<RTreeNode>();
                node->bounds = bounds;
                node->subNodes[0] = buildRTree(splitBounds[bestDim][0], std::move(splitEdgeIds[bestDim][0]));
                node->subNodes[1] = buildRTree(splitBounds[bestDim][1], std::move(splitEdgeIds[bestDim][1]));
                return node;
            }
        }

        // Create final leaf node
        auto node = std::make_shared<RTreeNode>();
        node->bounds = bounds;
        node->edgeIds = std::move(edgeIds);
        return node;
    }

    void StaticGraph::linkNodeEdgeIds(std::vector<Node>& nodes, const std::vector<Edge>& edges) {
        for (std::size_t i = 0; i < nodes.size(); i++) {
            nodes[i].edgeIds.reserve(3); // 3 should be optimal in most cases
        }
        for (std::size_t i = 0; i < edges.size(); i++) {
            const Edge& edge = edges[i];
            Node& node0 = nodes.at(edge.nodeIds[0]);
            node0.edgeIds.push_back(static_cast<EdgeId>(i));
        }
    }

    double StaticGraph::calculateDistance(const Point& pos0, const Point& pos1, double lngScale, double latScale) {
        Point pos0Scaled(pos0(0) * lngScale, pos0(1) * latScale, pos0(2));
        Point pos1Scaled(pos1(0) * lngScale, pos1(1) * latScale, pos1(2));
        return cglib::length(pos0Scaled - pos1Scaled);
    }

    Graph::NodeId DynamicGraph::getNodeIdRangeEnd() const {
        NodeId nodeIdRangeEnd = _staticGraph->getNodeIdRangeEnd();
        for (const std::pair<NodeId, Node>& nodePair : _nodes) {
            if (nodePair.first >= nodeIdRangeEnd) {
                nodeIdRangeEnd = nodePair.first + 1;
            }
        }
        return nodeIdRangeEnd;
    }

    const Graph::Node& DynamicGraph::getNode(NodeId nodeId) const {
        auto it = _nodes.find(nodeId);
        if (it != _nodes.end()) {
            return it->second;
        }
        return _staticGraph->getNode(nodeId);
    }
    
    const Graph::Edge& DynamicGraph::getEdge(EdgeId edgeId) const {
        if (edgeId < _staticGraph->getEdgeIdRangeEnd()) {
            return _staticGraph->getEdge(edgeId);
        }
        return _edges.at(edgeId - _staticGraph->getEdgeIdRangeEnd());
    }
    
    const Graph::Feature& DynamicGraph::getFeature(FeatureId featureId) const {
        if (featureId < _staticGraph->getFeatureIdRangeEnd()) {
            return _staticGraph->getFeature(featureId);
        }
        return _features.at(featureId - _staticGraph->getFeatureIdRangeEnd());
    }

    void DynamicGraph::reset() {
        _nodes.clear();
        _edges.clear();
        _features.clear();
    }

    Graph::NodeId DynamicGraph::addNode(Node node) {
        NodeId nodeId = getNodeIdRangeEnd();
        _nodes[nodeId] = std::move(node);
        return nodeId;
    }
    
    Graph::EdgeId DynamicGraph::addEdge(Edge edge) {
        EdgeId edgeId = getEdgeIdRangeEnd();
        Node& node0 = _nodes[edge.nodeIds[0]] = getNode(edge.nodeIds[0]);
        node0.edgeIds.push_back(edgeId);
        _edges.push_back(std::move(edge));
        return edgeId;
    }
    
    Graph::FeatureId DynamicGraph::addFeature(Feature feature) {
        FeatureId featureId = getFeatureIdRangeEnd();
        _features.push_back(std::move(feature));
        return featureId;
    }
} }
