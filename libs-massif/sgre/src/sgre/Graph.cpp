#include "Graph.h"

#include <queue>
#include <algorithm>

#include <boost/math/constants/constants.hpp>

namespace {
    double clamp(double val, double min, double max) {
        return std::max(min, std::min(max, val));
    }

    bool pointInsideTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos) {
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

        return u >= 0 && v >= 0 && u + v <= 1 && cglib::dot_product(v2, cglib::vector_product(v0, v1)) == 0;
    }

    cglib::vec3<double> closestPointOnLine(const std::array<cglib::vec3<double>, 2>& line, const cglib::vec3<double>& pos) {
        cglib::vec3<double> edge = line[1] - line[0];
        if (cglib::norm(edge) == 0) {
            return line[0];
        }
        cglib::vec3<double> v0 = pos - line[0];

        double d = cglib::dot_product(edge, edge);
        double c = cglib::dot_product(edge, v0);

        double s = clamp(c / d, 0, 1);
        return line[0] + edge * s;
    }

    cglib::vec3<double> closestPointOnTriangle(const std::array<cglib::vec3<double>, 3>& triangle, const cglib::vec3<double>& pos) {
        if (pointInsideTriangle(triangle, pos)) {
            return pos;
        }
        
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
    std::vector<std::pair<Graph::EdgeId, Point>> Graph::findNearestEdgePoint(const Point& pos) const {
        static constexpr double DIST_EPSILON = 1.0e-6;
        static constexpr double EARTH_RADIUS = 6378137.0;

        double latScale = EARTH_RADIUS * boost::math::constants::pi<double>() / 180.0;
        double lngScale = latScale * std::max(std::cos(pos(1) * boost::math::constants::pi<double>() / 180.0), 0.01);

        double bestDistance = std::numeric_limits<double>::infinity();
        std::vector<std::pair<Graph::EdgeId, Point>> results;
        for (EdgeId edgeId = 0; edgeId < getEdgeIdRangeEnd(); edgeId++) {
            const Edge& edge = getEdge(edgeId);
            
            if (auto closestPos = findNearestEdgePoint(edge, pos, lngScale, latScale)) {
                double distance = calculateDistance(*closestPos, pos, lngScale, latScale);
                if (distance <= bestDistance + DIST_EPSILON) {
                    if (distance + DIST_EPSILON < bestDistance) {
                        bestDistance = distance;
                        results.clear();
                    }
                    results.emplace_back(edgeId, *closestPos);
                }
            }
        }
        return results;
    }

    boost::optional<Point> Graph::findNearestEdgePoint(const Edge& edge, const Point& pos, double lngScale, double latScale) const {
        const Node& node0 = getNode(edge.nodeIds[0]);
        const Node& node1 = getNode(edge.nodeIds[1]);
        
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

        Point posScaled(pos(0) * lngScale, pos(1) * latScale, pos(2));
        Point points0Scaled[2] = { Point(node0.points[0](0) * lngScale, node0.points[0](1) * latScale, node0.points[0](2)), Point(node0.points[1](0) * lngScale, node0.points[1](1) * latScale, node0.points[1](2)) };
        Point points1Scaled[2] = { Point(node1.points[0](0) * lngScale, node1.points[0](1) * latScale, node1.points[0](2)), Point(node1.points[1](0) * lngScale, node1.points[1](1) * latScale, node1.points[1](2)) };

        switch (edge.searchCriteria) {
        case SearchCriteria::NONE:
            return boost::optional<Point>();
        case SearchCriteria::END_VERTEX:
            if (node0.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                if (node1.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                    return (cglib::norm(posScaled - points0Scaled[0]) < cglib::norm(posScaled - points1Scaled[0]) ? node0.points[0] : node1.points[0]);
                }
                return node0.points[0];
            } else if (node1.nodeFlags & NodeFlags::ENDPOINT_VERTEX) {
                return node1.points[0];
            }
            return boost::optional<Point>();
        case SearchCriteria::ANY_VERTEX:
            if (node0.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                if (node1.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                    return (cglib::norm(posScaled - points0Scaled[0]) < cglib::norm(posScaled - points1Scaled[0]) ? node0.points[0] : node1.points[0]);
                }
                return node0.points[0];
            } else if (node1.nodeFlags & NodeFlags::GEOMETRY_VERTEX) {
                return node1.points[0];
            }
            return boost::optional<Point>();
        case SearchCriteria::EDGE:
            if ((node0.nodeFlags & NodeFlags::GEOMETRY_VERTEX) && (node1.nodeFlags & NodeFlags::GEOMETRY_VERTEX)) {
                Point closestScaled = closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ points0Scaled[1 - i0], points1Scaled[1 - i1] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            }
            return boost::optional<Point>();
        case SearchCriteria::FULL:
            if (node0.points[i0] == node1.points[i1]) {
                Point closestScaled = closestPointOnTriangle(std::array<cglib::vec3<double>, 3> {{ points0Scaled[i0], points0Scaled[1 - i0], points1Scaled[1 - i1] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            } else {
                Point closestScaled = closestPointOnLine(std::array<cglib::vec3<double>, 2> {{ points0Scaled[0], points1Scaled[0] }}, posScaled);
                return Point(closestScaled(0) / lngScale, closestScaled(1) / latScale, closestScaled(2));
            }
        }

        return boost::optional<Point>();
    }

    double Graph::calculateDistance(const Point& pos0, const Point& pos1, double lngScale, double latScale) {
        Point pos0Scaled(pos0(0) * lngScale, pos0(1) * latScale, pos0(2));
        Point pos1Scaled(pos1(0) * lngScale, pos1(1) * latScale, pos1(2));
        return cglib::length(pos0Scaled - pos1Scaled);
    }

    void StaticGraph::linkNodeEdgeIds(std::vector<Node>& nodes, const std::vector<Edge>& edges) {
        for (std::size_t i = 0; i < edges.size(); i++) {
            EdgeId edgeId = static_cast<EdgeId>(i);
            const Edge& edge = edges[i];
            Node& node0 = nodes.at(edge.nodeIds[0]);
            node0.edgeIds.push_back(edgeId);
        }
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
