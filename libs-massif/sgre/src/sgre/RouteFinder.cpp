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

        auto calculateAvgLngScale = [](const Point& point1, const Point& point2) -> double {
            return std::max(std::cos((point1(1) + point2(1)) * 0.5 * boost::math::constants::pi<double>() / 180.0), 0.01);
        };
        
        // Find nearest edges to the endpoints. Note that there could be multiple nearest edges for both endpoints (two-way edges)
        std::vector<EndPoint> endPoints[2];
        for (int i = 0; i < 2; i++) {
            StaticGraph::SearchOptions searchOptions;
            searchOptions.zSensitivity = _routeOptions.zSensitivity;
            std::vector<std::pair<Graph::EdgeId, Point>> edgePoints = _graph->findNearestEdgePoint(query.getPos(i), searchOptions);
            if (edgePoints.empty()) {
                return Result();
            }

            for (const std::pair<Graph::EdgeId, Point>& edgePoint : edgePoints) {
                const Graph::Edge& edge = _graph->getEdge(edgePoint.first);

                EndPoint endPoint;
                endPoint.point = edgePoint.second;
                endPoint.triangleId = edge.triangleId;
                endPoint.edgeIds = std::set<Graph::EdgeId> { edgePoint.first };

                bool found = false;
                for (std::size_t j = 0; j < endPoints[i].size(); j++) {
                    double lngScale = calculateAvgLngScale(endPoints[i][j].point, endPoint.point);
                    double dist = calculateDistance(endPoints[i][j].point, endPoint.point, lngScale);
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
        
        // Try all endpoint combinations and find the fastest one.
        Path bestPath;
        double bestTime = std::numeric_limits<double>::infinity();
        double bestLngScale = 0;
        std::shared_ptr<DynamicGraph> bestGraph;
        for (std::size_t i0 = 0; i0 < endPoints[0].size(); i0++) {
            for (std::size_t i1 = 0; i1 < endPoints[1].size(); i1++) {
                auto graph = std::make_shared<DynamicGraph>(_graph);

                Graph::NodeId initialNodeId = createNode(*graph, endPoints[0][i0].point);
                Graph::NodeId finalNodeId = createNode(*graph, endPoints[1][i1].point);

                linkNodeToEdges(*graph, endPoints[0][i0].edgeIds, initialNodeId, 0);
                linkNodeToEdges(*graph, endPoints[1][i1].edgeIds, finalNodeId, 1);
                linkNodesToCommonEdges(*graph, endPoints[0][i0].edgeIds, endPoints[1][i1].edgeIds, initialNodeId, finalNodeId);

                double lngScale = calculateAvgLngScale(endPoints[0][i0].point, endPoints[1][i1].point);
                if (auto path = findOptimalPath(*graph, initialNodeId, finalNodeId, _fastestAttributes, lngScale, _routeOptions.tesselationDistance, bestTime)) {
                    bestPath = *path;
                    bestGraph = graph;
                    bestLngScale = lngScale;
                }
            }
        }
        if (!bestGraph) {
            return Result();
        }

        // Do optional path straightening. This is very important for polygon/hybrid graphs.
        if (_routeOptions.pathStraightening) {
            straightenPath(*bestGraph, bestPath, bestLngScale);
        }

        // Build final result (remove duplicate nodes, create instructions)
        return buildResult(*bestGraph, bestPath, bestLngScale, _routeOptions.minTurnAngle, _routeOptions.minUpDownAngle);
    }

    std::unique_ptr<RouteFinder> RouteFinder::create(std::shared_ptr<const StaticGraph> graph, const picojson::value& configDef) {
        RouteOptions routeOptions;
        if (configDef.contains("pathstraightening")) {
            routeOptions.pathStraightening = configDef.get("pathstraightening").get<bool>();
        }
        if (configDef.contains("tesselationdistance")) {
            routeOptions.tesselationDistance = configDef.get("tesselationdistance").get<double>();
        }
        if (configDef.contains("zsensitivity")) {
            routeOptions.zSensitivity = configDef.get("zsensitivity").get<double>();
        }
        if (configDef.contains("min_turnangle")) {
            routeOptions.minTurnAngle = configDef.get("min_turnangle").get<double>();
        }
        if (configDef.contains("min_updownangle")) {
            routeOptions.minUpDownAngle = configDef.get("min_updownangle").get<double>();
        }

        auto routeFinder = std::unique_ptr<RouteFinder>(new RouteFinder(std::move(graph)));
        routeFinder->setRouteOptions(routeOptions);
        return routeFinder;
    }

    Graph::NodeId RouteFinder::createNode(DynamicGraph& graph, const Point& point) {
        Graph::Node node;
        node.nodeFlags = Graph::NodeFlags(0);
        node.points = std::array<Point, 2> {{ point, point }};
        return graph.addNode(node);
    }

    void RouteFinder::linkNodeToEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds, Graph::NodeId nodeId, int nodeIdx) {
        // Find all 'linked edge' ids. For triangles this means finding all edges of the triangle.
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

                        // For final node, we need to do another hop as we do not store incoming edge ids for nodes
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

        // Add new edges to the graph based on the found linked edges
        for (Graph::EdgeId linkedEdgeId : linkedEdgeIds) {
            Graph::Edge linkedEdge = graph.getEdge(linkedEdgeId);
            linkedEdge.edgeFlags = Graph::EdgeFlags(0);
            linkedEdge.nodeIds[nodeIdx] = nodeId;
            graph.addEdge(linkedEdge);
        }
    }

    void RouteFinder::linkNodesToCommonEdges(DynamicGraph& graph, const std::set<Graph::EdgeId>& edgeIds0, const std::set<Graph::EdgeId>& edgeIds1, Graph::NodeId nodeId0, Graph::NodeId nodeId1) {
        // Find intersection of edge ids
        std::set<Graph::EdgeId> commonEdgeIds;
        std::set_intersection(edgeIds0.begin(), edgeIds0.end(), edgeIds1.begin(), edgeIds1.end(), std::inserter(commonEdgeIds, commonEdgeIds.begin()));
        
        // Connect the shared edges
        for (Graph::EdgeId commonEdgeId : commonEdgeIds) {
            const Graph::Edge& commonEdge = graph.getEdge(commonEdgeId);
            if (commonEdge.triangleId == Graph::TriangleId(-1)) {
                // Check that the target point is further along the edge compared to the starting point
                const Point& pos0 = graph.getNode(commonEdge.nodeIds[0]).points[0];
                double relDist0 = cglib::length(graph.getNode(nodeId0).points[0] - pos0);
                double relDist1 = cglib::length(graph.getNode(nodeId1).points[0] - pos0);
                if (relDist0 > relDist1) {
                    continue;
                }
            }

            // Insert the edge linking starting and target nodes
            Graph::Edge linkedEdge = commonEdge;
            linkedEdge.edgeFlags = Graph::EdgeFlags(0);
            linkedEdge.nodeIds[0] = nodeId0;
            linkedEdge.nodeIds[1] = nodeId1;
            graph.addEdge(linkedEdge);
        }
    }

    RoutingAttributes RouteFinder::findFastestEdgeAttributes(const Graph& graph) {
        // Find the fastest routing attributes of all edges. This is needed for the A* path finding algorithm.
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

    Result RouteFinder::buildResult(const Graph& graph, const Path& path, double lngScale, double minTurnAngle, double minUpDownAngle) {
        static constexpr double DIST_EPSILON = 1.0e-6;
        
        // Build both path geometry and instructions in one pass
        std::vector<Point> points;
        std::vector<Instruction> instructions;
        Graph::FeatureId lastFeatureId = Graph::FeatureId(-1);
        for (std::size_t i = 0; i < path.size(); i++) {
            const Graph::Edge& edge = path[i].edge;
            const Graph::Feature& feature = graph.getFeature(edge.featureId);
            
            // Add starting point/next point
            if (points.empty()) {
                const Graph::Node& node = graph.getNode(edge.nodeIds[0]);
                points.push_back(node.points[0]);
            }
            const Graph::Node& targetNode = graph.getNode(edge.nodeIds[1]);
            points.push_back(targetNode.points[0] + (targetNode.points[1] - targetNode.points[0]) * path[i].targetNodeT);

            // Add optional waiting instruction.
            if (edge.attributes.delay > 0) {
                Instruction waitInstruction(Instruction::Type::WAIT, feature, 0, edge.attributes.delay, points.size() - 2);
                instructions.push_back(std::move(waitInstruction));
            }

            // Calculate the turning angle/mode starting from the second point
            Instruction::Type type = Instruction::Type::HEAD_ON;
            double turnAngle = 0;
            if (points.size() > 2) {
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

                    // Calculate instruction type based on the turning angle
                    cglib::vec3<double> cross = cglib::vector_product(v0, v1);
                    double signedTurnAngle = std::asin(std::max(-1.0, std::min(1.0, cross(2)))) * 180.0 / boost::math::constants::pi<double>();
                    if (signedTurnAngle < -minTurnAngle) {
                        type = Instruction::Type::TURN_RIGHT;
                    } else if (signedTurnAngle > minTurnAngle) {
                        type = Instruction::Type::TURN_LEFT;
                    } else {
                        // Note: in theory we should check the graph node for 'alternatives'.
                        // If there are 2 options: slight turn to left and slight turn to right, we should still use turn actions.
                        type = Instruction::Type::GO_STRAIGHT;
                    }

                    if (cglib::norm(v0) > 0 && cglib::norm(v1) > 0) {
                        double dot = cglib::dot_product(v0, v1);
                        turnAngle = std::acos(std::max(-1.0, std::min(1.0, dot))) * 180.0 / boost::math::constants::pi<double>();
                    }
                }
            }

            // Check if we need to move vertically
            const Point& pos0 = points[points.size() - 2];
            const Point& pos1 = points[points.size() - 1];
            std::pair<double, double> dist2D = calculateDistance2D(pos0, pos1, lngScale);
            double minUpDownAngleTan = std::tan(minUpDownAngle / 180.0 * boost::math::constants::pi<double>());
            if (dist2D.second > 0 && dist2D.second / minUpDownAngleTan > dist2D.first) {
                if (pos1(2) > pos0(2)) {
                    type = Instruction::Type::GO_UP;
                } else {
                    type = Instruction::Type::GO_DOWN;
                }
                turnAngle = 0;
            }

            // Calculate distance and time. We will add turning time here and do not apply delay (as its included in the previous 'wait' instruction)
            double dist = calculateDistance(pos0, pos1, lngScale);
            double time = calculateTime(edge.attributes, false, turnAngle, pos0, pos1, lngScale);
            if (!std::isfinite(time)) {
                return Result();
            }

            // Store the instruction. But first check if we can merge this instruction with the last one based on type
            std::size_t geometryIndex = points.size() - 2;
            if (instructions.size() > 0 && edge.featureId == lastFeatureId && type == Instruction::Type::GO_STRAIGHT) {
                type = instructions.back().getType();
                dist += instructions.back().getDistance();
                time += instructions.back().getTime();
                geometryIndex = instructions.back().getGeometryIndex();
                instructions.pop_back();

                // Remove redundant points from straight lines
                if (points.size() > 2) {
                    double dist0 = calculateDistance(points[points.size() - 2], points[points.size() - 3], lngScale);
                    double dist1 = calculateDistance(points[points.size() - 1], points[points.size() - 2], lngScale);
                    double dist2 = calculateDistance(points[points.size() - 1], points[points.size() - 3], lngScale);
                    if (dist0 + dist1 <= dist2 + DIST_EPSILON) {
                        points.erase(points.begin() + points.size() - 2);
                    }
                }
            }
            Instruction instruction(type, feature, dist, time, geometryIndex);
            instructions.push_back(std::move(instruction));

            // Add the final instruction if we are at the end
            if (i + 1 == path.size()) {
                Instruction finalInstruction(Instruction::Type::REACHED_YOUR_DESTINATION, feature, 0, 0, points.size() - 1);
                instructions.push_back(std::move(finalInstruction));
            }

            lastFeatureId = edge.featureId;
        }

        return Result(std::move(instructions), std::move(points));
    }

    void RouteFinder::straightenPath(const Graph& graph, Path& path, double lngScale) {
        static constexpr int MAX_ITERATIONS = 1000;
        static constexpr double DIST_EPSILON = 1.0e-9; // Note: this epsilon should be smaller than other epislon values in order to properly remove redundant points

        // Store node endpoint coordinates
        Point initialPoint = graph.getNode(path[0].edge.nodeIds[0]).points[0];
        std::vector<std::array<Point, 2>> nodePoints;
        for (std::size_t i = 0; i < path.size(); i++) {
            nodePoints.push_back(graph.getNode(path[i].edge.nodeIds[1]).points);
        }

        // Path straightening may need lots of iterations
        bool reverse = false;
        for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
            bool progress = false;
            for (std::size_t n = 0; n + 1 < path.size(); n++) {
                std::size_t i = (reverse ? path.size() - n - 2 : n);
                Point pos0 = i > 0 ? nodePoints[i - 1][0] + (nodePoints[i - 1][1] - nodePoints[i - 1][0]) * path[i - 1].targetNodeT : initialPoint;
                Point pos1 = nodePoints[i + 1][0] + (nodePoints[i + 1][1] - nodePoints[i + 1][0]) * path[i + 1].targetNodeT;

                // Calculate optimal T value for midpoint between 0 and 1
                double closestT = calculateClosestT(pos0, pos1, nodePoints[i]);
                double clampedT = std::max(0.0, std::min(1.0, closestT));

                // If this new T value results in a shorter path, store it.
                Point oldPos = nodePoints[i][0] + (nodePoints[i][1] - nodePoints[i][0]) * path[i].targetNodeT;
                double oldDist = calculateDistance(pos0, oldPos, lngScale) + calculateDistance(oldPos, pos1, lngScale);
                Point newPos = nodePoints[i][0] + (nodePoints[i][1] - nodePoints[i][0]) * clampedT;
                double newDist = calculateDistance(pos0, newPos, lngScale) + calculateDistance(newPos, pos1, lngScale);
                if (newDist + DIST_EPSILON < oldDist) {
                    path[i].targetNodeT = clampedT;
                    progress = true;
                }
            }

            // If we did not make any progress during this pass, we can stop.
            if (!progress) {
                break;
            }

            // Do the next pass reverse
            reverse = !reverse;
        }
    }

    boost::optional<RouteFinder::Path> RouteFinder::findOptimalPath(const Graph& graph, Graph::NodeId initialNodeId, Graph::NodeId finalNodeId, const RoutingAttributes& fastestAttributes, double lngScale, double tesselationDistance, double& bestTime) {
        struct PathNodeKey {
            Graph::NodeId nodeId;
            double nodeT;

            bool operator < (const PathNodeKey& key) const { if (nodeId != key.nodeId) return nodeId < key.nodeId; return nodeT < key.nodeT; }
        };

        struct PathElement {
            double time;
            Graph::EdgeId edgeId;
            double nodeT;
        };
        
        struct NodeRecord {
            double time;
            Graph::NodeId nodeId;
            double nodeT;

            bool operator < (const NodeRecord& rec) const { return rec.time < time; }
        };

        const Graph::Node& initialNode = graph.getNode(initialNodeId);
        const Graph::Node& finalNode = graph.getNode(finalNodeId);

        // Initialize path map for initial node
        std::map<PathNodeKey, PathElement> bestPathMap;
        bestPathMap[{ initialNodeId, 0.0 }] = { 0.0, Graph::EdgeId(-1), -1.0 };

        // Use priority queue for storing fastest estimations. Start with fastest estimation from initial node to final node.
        std::priority_queue<NodeRecord> nodeQueue;
        double bestTotalEstTime = calculateTime(fastestAttributes, false, 0.0, initialNode.points[0], finalNode.points[0], lngScale);
        nodeQueue.push({ bestTotalEstTime, initialNodeId, 0.0 });

        // Process the node queue
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
            double time = bestPathMap[{ nodeId, nodeT }].time;

            // Process each edge from the current node
            for (Graph::EdgeId edgeId : node.edgeIds) {
                const Graph::Edge& edge = graph.getEdge(edgeId);
                assert(edge.nodeIds[0] == nodeId);
                Graph::NodeId targetNodeId = edge.nodeIds[1];
                const Graph::Node& targetNode = graph.getNode(targetNodeId);

                // Tesselate all triangle edges based on tesselation distance
                int tesselationLevel = static_cast<int>(std::floor(calculateDistance(targetNode.points[0], targetNode.points[1], lngScale) / tesselationDistance)) + 1;
                for (int i = 0; i < tesselationLevel; i++) {
                    double targetNodeT = (targetNodeId == finalNodeId ? 0.0 : (i + 1.0) / (tesselationLevel + 1.0));
                    Point targetNodePos = targetNode.points[0] + (targetNode.points[1] - targetNode.points[0]) * targetNodeT;

                    // Check if we found a better path to target node compared to existing path
                    double targetTime = time + calculateTime(edge.attributes, true, 0.0, nodePos, targetNodePos, lngScale);
                    if (!std::isfinite(targetTime)) {
                        continue;
                    }
                    auto it = bestPathMap.find({ targetNodeId, targetNodeT });
                    if (it != bestPathMap.end() && it->second.time <= targetTime) {
                        continue;
                    }
                    bestPathMap[{ targetNodeId, targetNodeT }] = { targetTime, edgeId, nodeT };

                    // Calculate the fastest possible estimation from target node to the final node
                    double bestTotalEstTime = targetTime + calculateTime(fastestAttributes, false, 0.0, targetNodePos, finalNode.points[0], lngScale);
                    nodeQueue.push({ bestTotalEstTime, targetNodeId, targetNodeT });
                }
            }
        }

        auto it = bestPathMap.find({ finalNodeId, 0.0 });
        if (it == bestPathMap.end() || it->second.time >= bestTime) {
            return boost::optional<Path>();
        }
        bestTime = it->second.time;

        // Reconstruct the optimal path backwards.
        Path bestPath;
        while (it->second.edgeId != Graph::EdgeId(-1)) {
            const Graph::Edge& edge = graph.getEdge(it->second.edgeId);
            assert(edge.nodeIds[1] == it->first.nodeId);
            bestPath.push_back({ edge, it->first.nodeT });
            it = bestPathMap.find({ edge.nodeIds[0], it->second.nodeT });
            assert(it != bestPathMap.end());
        }
        std::reverse(bestPath.begin(), bestPath.end());
        return bestPath;
    }

    double RouteFinder::calculateTime(const RoutingAttributes& attrs, bool applyDelay, double turnAngle, const Point& pos0, const Point& pos1, double lngScale) {
        std::pair<double, double> dist2D = calculateDistance2D(pos0, pos1, lngScale);

        double time = 0;
        if (applyDelay) {
            time += attrs.delay;
        }
        time += (turnAngle != 0 ? std::abs(turnAngle) / attrs.turnSpeed : 0);
        time += (dist2D.first > 0 ? dist2D.first / attrs.speed : 0);
        time += (dist2D.second > 0 ? dist2D.second / attrs.zSpeed : 0);
        return time;
    }
    
    double RouteFinder::calculateDistance(const Point& pos0, const Point& pos1, double lngScale) {
        std::pair<double, double> dist2D = calculateDistance2D(pos0, pos1, lngScale);
        return std::sqrt(dist2D.first * dist2D.first + dist2D.second * dist2D.second);
    }

    std::pair<double, double> RouteFinder::calculateDistance2D(const Point& pos0, const Point& pos1, double lngScale) {
        static constexpr double EARTH_RADIUS = 6378137.0;

        cglib::vec3<double> posDelta = pos1 - pos0;
        double distXY = cglib::length(cglib::vec2<double>(posDelta(0) * lngScale, posDelta(1)));
        double distZ = std::abs(posDelta(2));
        return std::make_pair(distXY * EARTH_RADIUS * boost::math::constants::pi<double>() / 180.0, distZ);
    }
} }
