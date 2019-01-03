#include "RouteFinder.h"

#include <cassert>
#include <algorithm>
#include <queue>
#include <set>
#include <map>

#include <boost/math/constants/constants.hpp>

namespace {
    double calculateClosestT(const cglib::vec3<double>& pos0, const cglib::vec3<double>& pos1, const std::array<cglib::vec3<double>, 2>& points) {
        cglib::vec3<double> posDelta = pos1 - pos0;
        cglib::vec3<double> pointsDelta = points[1] - points[0];
        if (cglib::norm(pointsDelta) == 0) {
            return 0;
        }

        double t = 0;
        if (cglib::norm(posDelta) != 0) {
            cglib::vec3<double> normal = cglib::vector_product(cglib::vector_product(pointsDelta, posDelta), pointsDelta);
            double dot = cglib::dot_product(posDelta, normal);
            if (dot == 0) {
                return 0.5;
            }
            t = cglib::dot_product(points[0] - pos0, normal) / dot;
        }
        return cglib::dot_product(pos0 + posDelta * t - points[0], pointsDelta) / cglib::norm(pointsDelta);
    }
}

namespace carto { namespace sgre {
    Result RouteFinder::find(const Query& query) const {
        static constexpr double DIST_EPSILON = 1.0e-6;
        
        struct EndPoint {
            Point point;
            Graph::TriangleId triangleId;
            std::set<Graph::EdgeId> edgeIds;
        };
        
        std::vector<EndPoint> endPoints[2];
        for (int i = 0; i < 2; i++) {
            std::vector<std::pair<Graph::EdgeId, Point>> edgePoints = _graph->findNearestEdgePoint(query.getPos(i));
            if (edgePoints.empty()) {
                return Result();
            }

            for (const std::pair<Graph::EdgeId, Point>& edgePoint : edgePoints) {
                const Graph::Edge& edge = _graph->getEdge(edgePoint.first);

                EndPoint endPoint;
                endPoint.point = edgePoint.second;
                endPoint.triangleId = edge.triangleId;
                endPoint.edgeIds = std::set<Graph::EdgeId> {{ edgePoint.first }};

                bool found = false;
                for (std::size_t j = 0; j < endPoints[i].size(); j++) {
                    std::pair<double, double> distance = calculateDistance(endPoints[i][j].point, endPoint.point, 1.0);
                    double dist = std::sqrt(distance.first * distance.first + distance.second * distance.second);
                    if (dist < DIST_EPSILON && endPoints[i][j].triangleId == endPoint.triangleId) {
                        endPoints[i][j].edgeIds.insert(edgePoint.first);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    endPoints[i].push_back(endPoint);
                }
            }
        }
        
        Path bestPath;
        double bestTime = std::numeric_limits<double>::infinity();
        double bestLngScale = 1.0;
        std::shared_ptr<DynamicGraph> bestGraph;
        for (std::size_t i0 = 0; i0 < endPoints[0].size(); i0++) {
            for (std::size_t i1 = 0; i1 < endPoints[1].size(); i1++) {
                auto graph = std::make_shared<DynamicGraph>(_graph);

                Graph::NodeId initialNodeId = createNode(*graph, endPoints[0][i0].point);
                Graph::NodeId finalNodeId = createNode(*graph, endPoints[1][i1].point);

                linkNodeToEdges(*graph, endPoints[0][i0].edgeIds, initialNodeId, 0);
                linkNodeToEdges(*graph, endPoints[1][i1].edgeIds, finalNodeId, 1);

                double lngScale = std::max(std::cos((endPoints[0][i0].point(1) + endPoints[1][i1].point(1)) * 0.5 * boost::math::constants::pi<double>() / 180.0), 0.01);

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

    void RouteFinder::linkNodeToEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds, Graph::NodeId nodeId, int nodeIdx) {
        std::set<Graph::EdgeId> linkedEdgeIds;
        for (Graph::EdgeId edgeId : edgeIds) {
            const Graph::Edge& edge = graph.getEdge(edgeId);
            linkedEdgeIds.insert(edgeId);
            if (edge.triangleId != Graph::TriangleId(-1)) {
                const Graph::Node& node = graph.getNode(edge.nodeIds[1 - nodeIdx]);
                for (Graph::EdgeId linkedEdgeId : node.edgeIds) {
                    const Graph::Edge& linkedEdge = graph.getEdge(linkedEdgeId);
                    if (linkedEdge.triangleId == edge.triangleId) {
                        linkedEdgeIds.insert(linkedEdgeId);
                        
                        if (nodeIdx == 1) {
                            const Graph::Node& nextNode = graph.getNode(linkedEdge.nodeIds[1]);
                            for (Graph::EdgeId nextLinkedEdgeId : nextNode.edgeIds) {
                                const Graph::Edge& nextLinkedEdge = graph.getEdge(nextLinkedEdgeId);
                                if (nextLinkedEdge.triangleId == edge.triangleId) {
                                    linkedEdgeIds.insert(nextLinkedEdgeId);
                                }
                            }
                        }
                    }
                }
            }
        }

        for (Graph::EdgeId linkedEdgeId : linkedEdgeIds) {
            Graph::Edge linkedEdge = graph.getEdge(linkedEdgeId);
            linkedEdge.nodeIds[nodeIdx] = nodeId;
            graph.addEdge(linkedEdge);
        }
    }

    RoutingAttributes RouteFinder::findFastestEdgeAttributes(const Graph& graph) {
        RoutingAttributes fastestAttributes;
        fastestAttributes.speed = 0;
        fastestAttributes.zSpeed = 0;
        fastestAttributes.turnSpeed = 0;
        fastestAttributes.delay = 0;
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
        static constexpr double DIST_EPSILON = 1.0e-8;
        static constexpr double MIN_TURN_ANGLE = 5.0 / 180 * boost::math::constants::pi<double>();

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
                Instruction waitInstruction(Instruction::Type::WAIT, feature, 0, edge.attributes.delay, points.size() - 2);
                instructions.push_back(std::move(waitInstruction));
            }

            Instruction::Type type = Instruction::Type::HEAD_ON;
            double turnAngle = 0;
            if (i > 0) {
                if (edge.featureId == lastFeatureId && edge.attributes.delay == 0) {
                    double dist0 = cglib::length(points[points.size() - 2] - points[points.size() - 3]);
                    double dist1 = cglib::length(points[points.size() - 1] - points[points.size() - 2]);
                    double dist2 = cglib::length(points[points.size() - 1] - points[points.size() - 3]);
                    if (dist0 + dist1 <= dist2 * (1 + DIST_EPSILON)) {
                        points[points.size() - 2] = points[points.size() - 1];
                        points.pop_back();
                        instructions.pop_back();
                    }
                }

                if (points.size() > 2) {
                    cglib::vec3<double> v0(0, 0, 0);
                    for (std::size_t j = points.size() - 2; j > 0; j--) {
                        cglib::vec3<double> delta = points[j] - points[j - 1];
                        v0 = cglib::vec3<double>(delta(0) * lngScale, delta(1), 0);
                        if (cglib::norm(v0) > 0) {
                            v0 = cglib::unit(v0);
                            break;
                        }
                    }
                    cglib::vec3<double> v1(0, 0, 0);
                    {
                        cglib::vec3<double> delta = points[points.size() - 1] - points[points.size() - 2];
                        v1 = cglib::vec3<double>(delta(0) * lngScale, delta(1), 0);
                        if (cglib::norm(v1) > 0) {
                            v1 = cglib::unit(v1);
                        }
                    }

                    cglib::vec3<double> cross = cglib::vector_product(v0, v1);
                    if (cross(2) < -std::sin(MIN_TURN_ANGLE)) {
                        type = Instruction::Type::TURN_RIGHT;
                    } else if (cross(2) > std::sin(MIN_TURN_ANGLE)) {
                        type = Instruction::Type::TURN_LEFT;
                    } else {
                        type = Instruction::Type::GO_STRAIGHT;
                    }

                    if (cglib::norm(v0) > 0 && cglib::norm(v1) > 0) {
                        double dot = cglib::dot_product(v0, v1);
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
                turnAngle = 0;
            }

            double dist = std::sqrt(distance.first * distance.first + distance.second * distance.second);
            double time = (turnAngle > 0 ? turnAngle / edge.attributes.turnSpeed : 0) + calculateTime(edge.attributes, pos0, pos1, lngScale);
            Instruction instruction(type, feature, dist, time, points.size() - 2);
            instructions.push_back(std::move(instruction));

            if (i + 1 == path.size()) {
                Instruction finalInstruction(Instruction::Type::REACHED_YOUR_DESTINATION, feature, 0, 0, points.size() - 1);
                instructions.push_back(std::move(finalInstruction));
            }

            lastFeatureId = edge.featureId;
        }
        return Result(std::move(instructions), std::move(points));
    }

    void RouteFinder::straightenPath(const Graph& graph, Path& path) {
        static constexpr int MAX_ITERATIONS = 1000;
        
        for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
            bool progress = false;
            for (std::size_t i = 0; i + 1 < path.size(); i++) {
                Point pos0 = (i == 0 ? getNodePoint(graph, path[0].edge.nodeIds[0], 0) : getNodePoint(graph, path[i - 1].edge.nodeIds[1], path[i - 1].targetNodeT));
                Point pos1 = getNodePoint(graph, path[i + 1].edge.nodeIds[1], path[i + 1].targetNodeT);

                const Graph::Edge& edge = path[i].edge;
                const Graph::Node& node = graph.getNode(edge.nodeIds[1]);

                double closestT = calculateClosestT(pos0, pos1, node.points);
                double clampedT = std::max(0.0, std::min(1.0, closestT));
                
                Point oldPos = getNodePoint(graph, edge.nodeIds[1], path[i].targetNodeT);
                double oldLength = cglib::length(oldPos - pos0) + cglib::length(pos1 - oldPos);
                Point newPos = node.points[0] + (node.points[1] - node.points[0]) * clampedT;
                double newLength = cglib::length(newPos - pos0) + cglib::length(pos1 - newPos);

                if (newLength < oldLength) {
                    path[i].targetNodeT = clampedT;
                    progress = true;
                }

                if (clampedT != closestT) {
                    const Graph::Node& node0 = graph.getNode(path[i].edge.nodeIds[0]);
                    for (Graph::EdgeId altEdgeId0 : node0.edgeIds) {
                        const Graph::Edge& altEdge0 = graph.getEdge(altEdgeId0);
                        assert(altEdge0.nodeIds[0] == path[i].edge.nodeIds[0]);
                        if (altEdge0.nodeIds[1] == path[i].edge.nodeIds[1]) {
                            continue;
                        }

                        const Graph::Node& altNode = graph.getNode(altEdge0.nodeIds[1]);
                        for (Graph::EdgeId altEdgeId1 : altNode.edgeIds) {
                            const Graph::Edge& altEdge1 = graph.getEdge(altEdgeId1);
                            assert(altEdge1.nodeIds[0] == altEdge0.nodeIds[1]);
                            if (altEdge1.nodeIds[1] == path[i + 1].edge.nodeIds[1]) {
                                double altClosestT = calculateClosestT(pos0, pos1, altNode.points);
                                double altClampedT = std::max(0.0, std::min(1.0, altClosestT));
                                
                                Point altPos = altNode.points[0] + (altNode.points[1] - altNode.points[0]) * altClampedT;
                                double altLength = cglib::length(altPos - pos0) + cglib::length(pos1 - altPos);

                                if (altLength < newLength && altLength < oldLength) {
                                    path[i].edge = altEdge0;
                                    path[i].targetNodeT = altClampedT;
                                    path[i + 1].edge = altEdge1;
                                    progress = true;
                                    newLength = altLength;
                                }
                            }
                        }
                    }
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
