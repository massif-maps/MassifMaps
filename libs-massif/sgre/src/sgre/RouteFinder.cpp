#include "RouteFinder.h"

#include <cassert>
#include <algorithm>
#include <queue>
#include <set>
#include <map>

#include <boost/math/constants/constants.hpp>

namespace carto { namespace sgre {
    Result RouteFinder::find(const Query& query) const {
        std::vector<std::pair<Graph::EdgeId, Point>> edgePoints[2];
        for (int i = 0; i < 2; i++) {
            edgePoints[i] = _graph->findNearestEdgePoint(query.getPos(i));
            if (edgePoints[i].empty()) {
                return Result();
            }

            std::set<Graph::TriangleId> triangleIds;
            for (auto it = edgePoints[i].begin(); it != edgePoints[i].end(); ) {
                const Graph::Edge& edge = _graph->getEdge(it->first);
                if (edge.triangleId != Graph::TriangleId(-1)) {
                    if (triangleIds.count(edge.triangleId) > 0) {
                        it = edgePoints[i].erase(it);
                        continue;
                    }
                    triangleIds.insert(edge.triangleId);
                }
                it++;
            }
        }
        
        Path bestPath;
        double bestTime = std::numeric_limits<double>::infinity();
        double bestLngScale = 1.0;
        std::shared_ptr<DynamicGraph> bestGraph;
        for (std::size_t i0 = 0; i0 < edgePoints[0].size(); i0++) {
            for (std::size_t i1 = 0; i1 < edgePoints[1].size(); i1++) {
                auto graph = std::make_shared<DynamicGraph>(_graph);

                Graph::NodeId initialNodeId = createNode(*graph, edgePoints[0][i0].second);
                Graph::NodeId finalNodeId = createNode(*graph, edgePoints[1][i1].second);

                linkNodeToEdges(*graph, edgePoints[0][i0].first, initialNodeId);
                linkNodeToEdges(*graph, edgePoints[1][i1].first, finalNodeId);

                double lngScale = std::max(std::cos((edgePoints[0][i0].second(1) + edgePoints[1][i1].second(1)) * 0.5 * boost::math::constants::pi<double>() / 180.0), 0.01);

                if (auto path = findOptimalPath(*graph, initialNodeId, finalNodeId, _fastestAttributes, lngScale, _tesselationDistance, bestTime)) {
                    bestPath = *path;
                    bestLngScale = lngScale;
                    bestGraph = graph;
                }
            }
        }

        if (!bestGraph) {
            return Result();
        }

        if (_pathStraightening) {
            straightenPath(*bestGraph, bestPath);
        }

        return buildResult(*bestGraph, bestPath, bestLngScale);
    }

    Graph::NodeId RouteFinder::createNode(DynamicGraph& graph, const Point& point) {
        Graph::Node node;
        node.points = std::array<Point, 2> {{ point, point }};
        return graph.addNode(node);
    }

    std::vector<Graph::EdgeId> RouteFinder::linkNodeToEdges(DynamicGraph& graph, Graph::EdgeId edgeId, Graph::NodeId nodeId) {
        const Graph::Edge& edge = graph.getEdge(edgeId);

        std::set<Graph::EdgeId> linkedEdgeIds;
        linkedEdgeIds.insert(edgeId);
        if (edge.triangleId != Graph::TriangleId(-1)) {
            for (int i = 0; i < 2; i++) {
                const Graph::Node& node = graph.getNode(edge.nodeIds[i]);
                for (Graph::EdgeId linkedEdgeId : node.edgeIds) {
                    const Graph::Edge& linkedEdge = graph.getEdge(linkedEdgeId);
                    if (linkedEdge.triangleId == edge.triangleId) {
                        linkedEdgeIds.insert(linkedEdgeId);
                    }
                }
            }
        }

        std::vector<Graph::EdgeId> edgeIds;
        for (Graph::EdgeId linkedEdgeId : linkedEdgeIds) {
            for (int i = 0; i < 2; i++) {
                Graph::Edge linkedEdge = graph.getEdge(linkedEdgeId);
                linkedEdge.nodeIds[i] = nodeId;
                edgeIds.push_back(graph.addEdge(linkedEdge));
            }
        }
        return edgeIds;
    }

    RoutingAttributes RouteFinder::findFastestEdgeAttributes(const Graph& graph) {
        RoutingAttributes fastestAttributes{ 0, 0, 0, 0 };
        for (Graph::EdgeId edgeId = 0; edgeId < graph.getEdgeIdRangeEnd(); edgeId++) {
            const Graph::Edge& edge = graph.getEdge(edgeId);
            fastestAttributes.speed = std::max(fastestAttributes.speed, edge.attributes.speed);
            fastestAttributes.zSpeed = std::max(fastestAttributes.zSpeed, edge.attributes.zSpeed);
            fastestAttributes.turnSpeed = std::max(fastestAttributes.turnSpeed, edge.attributes.turnSpeed);
        }
        return fastestAttributes;
    }

    Point RouteFinder::getNodePoint(const Graph& graph, Graph::NodeId nodeId, double t) {
        const Graph::Node& node = graph.getNode(nodeId);
        return node.points[0] + (node.points[1] - node.points[0]) * t;
    }

    Result RouteFinder::buildResult(const Graph& graph, const Path& path, double lngScale) {
        static constexpr double EPSILON = 1.0e-8;

        std::vector<Point> points;
        std::vector<Instruction> instructions;
        Graph::FeatureId lastFeatureId = Graph::FeatureId(-1);
        for (std::size_t i = 0; i < path.size(); i++) {
            const Graph::Edge& edge = path[i].edge;
            const Graph::Feature& feature = graph.getFeature(edge.featureId);
            
            if (i == 0) {
                points.push_back(getNodePoint(graph, edge.nodeIds[0], 0));
            }
            points.push_back(getNodePoint(graph, edge.nodeIds[1], path[i].targetNodeT));

            if (edge.attributes.delay > 0) {
                Instruction instruction(Instruction::Type::WAIT, feature, 0, edge.attributes.delay, points.size() - 1);
                instructions.push_back(std::move(instruction));
            }

            Instruction::Type type = Instruction::Type::HEAD_ON;
            double turnAngle = 0;
            if (i > 0) {
                if (edge.featureId == lastFeatureId && edge.attributes.delay == 0) {
                    double dist0 = cglib::length(points[points.size() - 2] - points[points.size() - 3]);
                    double dist1 = cglib::length(points[points.size() - 1] - points[points.size() - 2]);
                    double dist2 = cglib::length(points[points.size() - 1] - points[points.size() - 3]);
                    if (dist0 + dist1 <= dist2 * (1 + EPSILON)) {
                        points[points.size() - 2] = points[points.size() - 1];
                        points.pop_back();
                        instructions.pop_back();
                    }
                }

                if (points.size() > 2) {
                    cglib::vec3<double> v0(0, 0, 0);
                    for (std::size_t j = points.size() - 2; j > 0; j--) {
                        v0 = points[j] - points[j - 1];
                        if (cglib::norm(v0) > 0) {
                            break;
                        }
                    }
                    cglib::vec3<double> v1 = points[points.size() - 1] - points[points.size() - 2];
                    cglib::vec3<double> normal = cglib::vector_product(cglib::vector_product(v0, v1), v0);
                    double dot = cglib::dot_product(normal, v1);
                    if (dot < -EPSILON) {
                        type = Instruction::Type::TURN_LEFT;
                    } else if (dot > EPSILON) {
                        type = Instruction::Type::TURN_RIGHT;
                    } else {
                        type = Instruction::Type::GO_STRAIGHT;
                    }

                    if (cglib::norm(v0) > 0 && cglib::norm(v1) > 0) {
                        double dot = cglib::dot_product(cglib::unit(v0), cglib::unit(v1));
                        turnAngle = std::acos(std::max(-1.0, std::min(1.0, dot))) * 180.0 / boost::math::constants::pi<double>();
                    }
                }
            }

            const Point& pos0 = points[points.size() - 2];
            const Point& pos1 = points[points.size() - 1];
            std::pair<double, double> distance = calculateDistance(pos0, pos1, lngScale);
            if (distance.second > distance.first) {
                if (pos1(2) > pos0(2)) {
                    type = Instruction::Type::GO_UP;
                } else {
                    type = Instruction::Type::GO_DOWN;
                }
            }

            double dist = std::sqrt(distance.first * distance.first + distance.second * distance.second);
            double time = (turnAngle > 0 ? turnAngle / edge.attributes.turnSpeed : 0) + calculateTime(edge.attributes, pos0, pos1, lngScale);
            Instruction instruction(type, feature, dist, time, points.size() - 1);
            instructions.push_back(std::move(instruction));

            if (i + 1 == path.size()) {
                Instruction finalInstruction(Instruction::Type::REACHED_YOUR_DESTINATION, feature, 0, 0, points.size());
                instructions.push_back(std::move(finalInstruction));
            }

            lastFeatureId = edge.featureId;
        }
        return Result(std::move(instructions), std::move(points));
    }

    void RouteFinder::straightenPath(const Graph& graph, Path& path) {
        for (std::size_t iter = 0; iter < path.size(); iter++) {
            bool progress = false;
            for (std::size_t i = 0; i + 1 < path.size(); i++) {
                Point pos0 = (i == 0 ? getNodePoint(graph, path[0].edge.nodeIds[0], 0) : getNodePoint(graph, path[i - 1].edge.nodeIds[1], path[i - 1].targetNodeT));
                Point pos1 = getNodePoint(graph, path[i + 1].edge.nodeIds[1], path[i + 1].targetNodeT);

                const Graph::Edge& edge = path[i].edge;
                const Graph::Node& node = graph.getNode(edge.nodeIds[1]);
                if (node.points[0] == node.points[1]) {
                    continue;
                }

                cglib::vec3<double> posDelta = pos1 - pos0;
                cglib::vec3<double> nodeDelta = node.points[1] - node.points[0];
                
                double t = 0;
                if (cglib::norm(posDelta) != 0) {
                    cglib::vec3<double> nodeNormal = cglib::vector_product(cglib::vector_product(nodeDelta, posDelta), nodeDelta);
                    double dot = cglib::dot_product(posDelta, nodeNormal);
                    if (dot == 0) {
                        continue;
                    }
                    t = cglib::dot_product(node.points[0] - pos0, nodeNormal) / dot;
                }
                double closestT = cglib::dot_product(pos0 + posDelta * t - node.points[0], nodeDelta) / cglib::norm(nodeDelta);
                double clampedT = std::max(0.0, std::min(1.0, closestT));
                
                Point newPos = node.points[0] + nodeDelta * clampedT;
                Point oldPos = getNodePoint(graph, edge.nodeIds[1], path[i].targetNodeT);
                double newLength = cglib::length(newPos - pos0) + cglib::length(pos1 - newPos);
                double oldLength = cglib::length(oldPos - pos0) + cglib::length(pos1 - oldPos);
                if (newLength < oldLength) {
                    path[i].targetNodeT = clampedT;
                    progress = true;
                }
            }

            if (!progress) {
                break;
            }
        }
    }

    boost::optional<RouteFinder::Path> RouteFinder::findOptimalPath(const Graph& graph, Graph::NodeId initialNodeId, Graph::NodeId finalNodeId, const RoutingAttributes& fastestAttributes, double lngScale, double tesselationDistance, double& bestTime) {
        struct NodeKey {
            Graph::NodeId nodeId;
            double nodeT;

            bool operator < (const NodeKey& key) const { if (nodeId != key.nodeId) return nodeId < key.nodeId; return nodeT < key.nodeT; }
        };
        
        struct NodeRecord {
            double time;
            Graph::NodeId nodeId;
            double nodeT;

            bool operator < (const NodeRecord& rec) const { return rec.time < time; }
        };

        const Graph::Node& initialNode = graph.getNode(initialNodeId);
        const Graph::Node& finalNode = graph.getNode(finalNodeId);

        std::map<NodeKey, NodeRecord> bestPathMap;
        bestPathMap[{ initialNodeId, 0.0 }] = { 0.0, Graph::NodeId(-1), -1.0 };

        std::priority_queue<NodeRecord> nodeQueue;
        double bestTotalEstTime = calculateTime(fastestAttributes, initialNode.points[0], finalNode.points[0], lngScale);
        nodeQueue.push({ bestTotalEstTime, initialNodeId, 0.0 });

        while (!nodeQueue.empty()) {
            NodeRecord rec = nodeQueue.top();
            nodeQueue.pop();

            if (rec.time >= bestTime) {
                break;
            }

            if (rec.nodeId == finalNodeId) {
                break;
            }

            Graph::NodeId nodeId = rec.nodeId;
            const Graph::Node& node = graph.getNode(nodeId);
            double nodeT = rec.nodeT;
            Point nodePos = node.points[0] + (node.points[1] - node.points[0]) * nodeT;

            for (Graph::EdgeId edgeId : node.edgeIds) {
                const Graph::Edge& edge = graph.getEdge(edgeId);
                assert(edge.nodeIds[0] == nodeId);
                Graph::NodeId targetNodeId = edge.nodeIds[1];
                const Graph::Node& targetNode = graph.getNode(targetNodeId);

                std::pair<double, double> pointsDistance = calculateDistance(targetNode.points[0], targetNode.points[1], lngScale);
                int tesselationLevel = static_cast<int>(std::floor(std::sqrt(pointsDistance.first * pointsDistance.first + pointsDistance.second * pointsDistance.second) / tesselationDistance)) + 1;
                for (int i = 0; i < tesselationLevel; i++) {
                    double targetNodeT = (targetNodeId == finalNodeId ? 0.0 : (i + 1.0) / (tesselationLevel + 1.0));
                    Point targetNodePos = targetNode.points[0] + (targetNode.points[1] - targetNode.points[0]) * targetNodeT;

                    double targetTime = bestPathMap[{ nodeId, nodeT }].time + calculateTime(edge.attributes, nodePos, targetNodePos, lngScale);
                    auto it = bestPathMap.find({ targetNodeId, targetNodeT });
                    if (it != bestPathMap.end() && it->second.time <= targetTime) {
                        continue;
                    }
                    bestPathMap[{ targetNodeId, targetNodeT }] = { targetTime, nodeId, nodeT };

                    double bestTotalEstTime = targetTime + calculateTime(fastestAttributes, targetNodePos, finalNode.points[0], lngScale);
                    nodeQueue.push({ bestTotalEstTime, targetNodeId, targetNodeT });
                }
            }
        }

        auto it = bestPathMap.find({ finalNodeId, 0.0 });
        if (it == bestPathMap.end() || it->second.time >= bestTime) {
            return boost::optional<Path>();
        }
        bestTime = it->second.time;

        Path bestPath;
        do {
            Graph::NodeId nodeId = it->second.nodeId;
            Graph::NodeId targetNodeId = it->first.nodeId;
            const Graph::Node& node = graph.getNode(nodeId);
            for (Graph::EdgeId edgeId : node.edgeIds) {
                const Graph::Edge& edge = graph.getEdge(edgeId);
                if (edge.nodeIds[0] == nodeId && edge.nodeIds[1] == targetNodeId) {
                    bestPath.push_back({ edge, it->first.nodeT });
                    break;
                }
            }
            if (nodeId == initialNodeId) {
                it = bestPathMap.end();
            } else {
                it = bestPathMap.find({ nodeId, it->second.nodeT });
            }
        } while (it != bestPathMap.end());
        std::reverse(bestPath.begin(), bestPath.end());
        return bestPath;
    }

    std::pair<double, double> RouteFinder::calculateDistance(const Point& pos0, const Point& pos1, double lngScale) {
        cglib::vec3<double> posDelta = pos1 - pos0;
        double distXY = cglib::length(cglib::vec2<double>(posDelta(0) * lngScale, posDelta(1)));
        double distZ = std::abs(posDelta(2));
        return std::make_pair(distXY * EARTH_RADIUS * boost::math::constants::pi<double>() / 180.0, distZ);
    }

    double RouteFinder::calculateTime(const RoutingAttributes& attrs, const Point& pos0, const Point& pos1, double lngScale) {
        std::pair<double, double> distance = calculateDistance(pos0, pos1, lngScale);
        return attrs.delay + (distance.first > 0 ? distance.first / attrs.speed : 0) + (distance.second > 0 ? distance.second / attrs.zSpeed : 0);
    }
} }
