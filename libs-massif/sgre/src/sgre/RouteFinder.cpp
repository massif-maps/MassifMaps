#include "RouteFinder.h"

#include <cassert>
#include <algorithm>
#include <queue>
#include <set>
#include <map>

#include <boost/math/constants/constants.hpp>

namespace carto { namespace sgre {
    Result RouteFinder::find(const Query& query) const {
        struct EndPoint {
            Point point = Point();
            Graph::TriangleId triangleId = Graph::TriangleId(-1);
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
        
        // Link all endpoint combinations.
        auto graph = std::make_shared<DynamicGraph>(_graph);
        double lngScale = calculateAvgLngScale(endPoints[0][0].point, endPoints[1][0].point);
        
        std::vector<Graph::NodeId> initialNodeIds;
        std::vector<Graph::NodeId> finalNodeIds;
        for (std::size_t i0 = 0; i0 < endPoints[0].size(); i0++) {
            Graph::NodeId initialNodeId = createNode(*graph, endPoints[0][i0].point);
            linkNodeToEdges(*graph, endPoints[0][i0].edgeIds, initialNodeId, 0);
            initialNodeIds.push_back(initialNodeId);
        }
        for (std::size_t i1 = 0; i1 < endPoints[1].size(); i1++) {
            Graph::NodeId finalNodeId = createNode(*graph, endPoints[1][i1].point);
            linkNodeToEdges(*graph, endPoints[1][i1].edgeIds, finalNodeId, 1);
            finalNodeIds.push_back(finalNodeId);
        }
        for (std::size_t i0 = 0; i0 < endPoints[0].size(); i0++) {
            for (std::size_t i1 = 0; i1 < endPoints[1].size(); i1++) {
                linkNodesToCommonEdges(*graph, endPoints[0][i0].edgeIds, endPoints[1][i1].edgeIds, initialNodeIds[i0], finalNodeIds[i1]);
            }
        }

        // Try to find the fastest route
        boost::optional<Route> route = findFastestRoute(*graph, initialNodeIds, finalNodeIds, _fastestAttributes, lngScale, _routeOptions.tesselationDistance);
        if (!route) {
            return Result();
        }

        // Do optional path straightening. This is very important for polygon/hybrid graphs.
        if (_routeOptions.pathStraightening) {
            route = straightenRoute(*graph, *route, lngScale);
        }

        // Build final result (remove duplicate nodes, create instructions)
        return buildResult(*graph, *route, lngScale, _routeOptions.minTurnAngle, _routeOptions.minUpDownAngle);
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

    bool RouteFinder::isNodeVisible(const Graph& graph, Graph::NodeId nodeId0, double t0, Graph::NodeId nodeId1, double t1, double lngScale, std::set<Graph::NodeId> visitedNodeIds) {
        if (nodeId0 == nodeId1) {
            return true;
        }
        if (visitedNodeIds.count(nodeId0) > 0) {
            return false;
        }
        visitedNodeIds.insert(nodeId0);

        const Graph::Node& node0 = graph.getNode(nodeId0);
        const Graph::Node& node1 = graph.getNode(nodeId1);
        Point pos0 = node0.points[0] + (node0.points[1] - node0.points[0]) * t0;
        Point pos1 = node1.points[0] + (node1.points[1] - node1.points[0]) * t1;
        cglib::vec3<double> posDelta = pos1 - pos0;

        for (Graph::EdgeId edgeId : node0.edgeIds) {
            Graph::NodeId targetNodeId = graph.getEdge(edgeId).nodeIds[1];
            const Graph::Node& targetNode = graph.getNode(targetNodeId);
            cglib::vec3<double> pointsDelta = targetNode.points[1] - targetNode.points[0];

            // Calculate distances along 2 axes for intersection point
            double s = 0;
            if (cglib::norm(posDelta) != 0) {
                cglib::vec3<double> normal = cglib::vector_product(cglib::vector_product(pointsDelta, posDelta), pointsDelta);
                double dot = cglib::dot_product(posDelta, normal);
                if (dot != 0) {
                    s = std::min(1.0, std::max(0.0, cglib::dot_product(targetNode.points[0] - pos0, normal) / dot));
                } else {
                    s = std::min(1.0, std::max(0.0, cglib::dot_product(targetNode.points[0] - pos0, posDelta) / cglib::norm(posDelta)));
                }
            }

            double t = 0;
            if (cglib::norm(pointsDelta) != 0) {
                t = std::min(1.0, std::max(0.0, cglib::dot_product(pos0 + posDelta * s - targetNode.points[0], pointsDelta) / cglib::norm(pointsDelta)));
            }

            // If the resulting points are close, try moving along the edge recursively
            double dist = calculateDistance(pos0 + posDelta * s, targetNode.points[0] + pointsDelta * t, lngScale);
            if (dist < DIST_EPSILON) {
                if (isNodeVisible(graph, targetNodeId, t, nodeId1, t1, lngScale, visitedNodeIds)) {
                    return true;
                }
            }
        }
        return false;
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

    Result RouteFinder::buildResult(const Graph& graph, const Route& route, double lngScale, double minTurnAngle, double minUpDownAngle) {
        // Build both route geometry and instructions in one pass
        std::vector<Point> points;
        std::vector<Instruction> instructions;
        Graph::FeatureId lastFeatureId = Graph::FeatureId(-1);
        for (std::size_t i = 0; i < route.size(); i++) {
            const RouteNode& routeNode = route[i];
            
            // Add route point
            const Graph::Node& targetNode = graph.getNode(routeNode.targetNodeId);
            points.push_back(targetNode.points[0] + (targetNode.points[1] - targetNode.points[0]) * routeNode.targetNodeT);
            if (i == 0) {
                continue;
            }

            // Read node feature
            const Graph::Feature& feature = graph.getFeature(routeNode.featureId);

            // Add optional waiting instruction.
            if (routeNode.attributes.delay > 0) {
                Instruction waitInstruction(Instruction::Type::WAIT, feature, 0, routeNode.attributes.delay, points.size() - 2);
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
            double time = calculateTime(routeNode.attributes, false, turnAngle, pos0, pos1, lngScale);
            if (!std::isfinite(time)) {
                return Result();
            }

            // Store the instruction. But first check if we can merge this instruction with the last one based on type
            std::size_t geometryIndex = points.size() - 2;
            if (instructions.size() > 0 && routeNode.featureId == lastFeatureId && type == Instruction::Type::GO_STRAIGHT) {
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
            if (i + 1 == route.size()) {
                Instruction finalInstruction(Instruction::Type::REACHED_YOUR_DESTINATION, feature, 0, 0, points.size() - 1);
                instructions.push_back(std::move(finalInstruction));
            }

            lastFeatureId = routeNode.featureId;
        }

        return Result(std::move(instructions), std::move(points));
    }

    RouteFinder::Route RouteFinder::straightenRoute(const Graph& graph, const Route& route, double lngScale) {
        Route optimizedRoute;
        if (!route.empty()) {
            optimizedRoute.push_back(route.front());
        }
        for (std::size_t i = 1; i < route.size(); ) {
            const RouteNode& routeNode0 = optimizedRoute.back();

            // Find first node that is not 'visible' from current node
            std::size_t j = i + 1;
            for (; j < route.size(); j++) {
                // Stop if feature id has changed
                if (route[j - 1].featureId != route[j].featureId) {
                    break;
                }
                
                const RouteNode& routeNode1 = route[j];

                std::set<Graph::NodeId> visitedNodeIds;
                if (!isNodeVisible(graph, routeNode0.targetNodeId, routeNode0.targetNodeT, routeNode1.targetNodeId, routeNode1.targetNodeT, lngScale, visitedNodeIds)) {
                    break;
                }
            }

            // Store previous node of the found node
            optimizedRoute.push_back(route[j - 1]);

            i = j;
        }
        return optimizedRoute;
    }

    boost::optional<RouteFinder::Route> RouteFinder::findFastestRoute(const Graph& graph, const std::vector<Graph::NodeId>& initialNodeIds, const std::vector<Graph::NodeId>& finalNodeIds, const RoutingAttributes& fastestAttributes, double lngScale, double tesselationDistance) {
        struct NodeRecord {
            double time; // best estimated time to final some from some initial node. Estimated time must not exceed actual time.
            Graph::NodeId nodeId;
            std::size_t routeEdgeIndex; // outgoing route edge index

            bool operator < (const NodeRecord& rec) const { return rec.time < time; }
        };

        struct RouteEdge {
            double time; // time spent up to this point
            Graph::EdgeId edgeId;
            std::size_t routeEdgeIndex; // incoming route edge index
            double nodeT; // outgoing edge node T value
        };

        // Initialize route map and node queue
        std::unordered_map<Graph::NodeId, std::vector<RouteEdge>> bestRouteMap;
        std::priority_queue<NodeRecord> nodeQueue;
        for (Graph::NodeId initialNodeId : initialNodeIds) {
            const Graph::Node& initialNode = graph.getNode(initialNodeId);

            // Store initial node of the path
            bestRouteMap[initialNodeId] = std::vector<RouteEdge> {{ 0.0, Graph::EdgeId(-1), 0, 0.0 }};

            // Calculate best possible time from the node to the closest final node
            double bestTotalEstTime = std::numeric_limits<double>::infinity();
            for (Graph::NodeId finalNodeId : finalNodeIds) {
                const Graph::Node& finalNode = graph.getNode(finalNodeId);
                double totalEstTime = calculateTime(fastestAttributes, false, 0.0, initialNode.points[0], finalNode.points[0], lngScale);
                bestTotalEstTime = std::min(bestTotalEstTime, totalEstTime);
            }
            nodeQueue.push({ bestTotalEstTime, initialNodeId, 0 });
        }

        // Process the node queue
        Graph::NodeId finalNodeId = Graph::NodeId(-1);
        while (!nodeQueue.empty()) {
            NodeRecord rec = nodeQueue.top();
            nodeQueue.pop();

            // Check if we have reached a final node
            auto nodeIt = std::find(finalNodeIds.begin(), finalNodeIds.end(), rec.nodeId);
            if (nodeIt != finalNodeIds.end()) {
                finalNodeId = *nodeIt;
                break;
            }

            Graph::NodeId nodeId = rec.nodeId;
            const Graph::Node& node = graph.getNode(nodeId);
            const RouteEdge& routeEdge = bestRouteMap[nodeId][rec.routeEdgeIndex];
            Point nodePos = node.points[0] + (node.points[1] - node.points[0]) * routeEdge.nodeT;
            double time = routeEdge.time;

            // Process each edge from the current node
            for (Graph::EdgeId edgeId : node.edgeIds) {
                const Graph::Edge& edge = graph.getEdge(edgeId);
                assert(edge.nodeIds[0] == nodeId);
                Graph::NodeId targetNodeId = edge.nodeIds[1];
                const Graph::Node& targetNode = graph.getNode(targetNodeId);

                // Read/initialize edges to target node
                std::vector<RouteEdge>& targetRouteEdges = bestRouteMap[targetNodeId];
                if (targetRouteEdges.empty()) {
                    std::size_t tesselationLevel = static_cast<std::size_t>(std::ceil(calculateDistance(targetNode.points[0], targetNode.points[1], lngScale) / tesselationDistance));
                    targetRouteEdges.resize(tesselationLevel + 1, { std::numeric_limits<double>::infinity(), Graph::EdgeId(-1), 0, 0.0 });
                }
                
                // Tesselate all triangle edges based on tesselation distance
                for (std::size_t i = 0; i < targetRouteEdges.size(); i++) {
                    // Fast preliminary check - if we already have result that we can not improve upon, skip further calculations
                    if (targetRouteEdges[i].time <= time) {
                        continue;
                    }
                    
                    // Check if we found a better route to target node compared to existing route
                    double targetNodeT = static_cast<double>(i) / std::max(targetRouteEdges.size() - 1, std::size_t(1));
                    Point targetNodePos = targetNode.points[0] + (targetNode.points[1] - targetNode.points[0]) * targetNodeT;
                    double targetTime = time + calculateTime(edge.attributes, true, 0.0, nodePos, targetNodePos, lngScale);
                    if (targetRouteEdges[i].time <= targetTime || !std::isfinite(targetTime)) {
                        continue;
                    }

                    // We have a better route edge. Store it.
                    targetRouteEdges[i] = { targetTime, edgeId, rec.routeEdgeIndex, targetNodeT };

                    // Calculate the fastest possible estimation from target node to the closest final node and push it to queue
                    double bestTotalEstTime = std::numeric_limits<double>::infinity();
                    for (Graph::NodeId finalNodeId : finalNodeIds) {
                        const Graph::Node& finalNode = graph.getNode(finalNodeId);
                        double totalEstTime = targetTime + calculateTime(fastestAttributes, false, 0.0, targetNodePos, finalNode.points[0], lngScale);
                        bestTotalEstTime = std::min(bestTotalEstTime, totalEstTime);
                    }
                    nodeQueue.push({ bestTotalEstTime, targetNodeId, i });
                }
            }
        }

        // Success?
        if (finalNodeId == Graph::NodeId(-1)) {
            return boost::optional<Route>();
        }

        // Reconstruct the fastest route backwards.
        Route bestRoute;
        Graph::NodeId lastNodeId = finalNodeId;
        std::size_t lastRouteEdgeIndex = 0;
        while (true) {
            const RouteEdge& routeEdge = bestRouteMap[lastNodeId].at(lastRouteEdgeIndex);
            if (routeEdge.edgeId == Graph::EdgeId(-1)) {
                break;
            }
            
            const Graph::Edge& edge = graph.getEdge(routeEdge.edgeId);
            RouteNode routeNode;
            routeNode.featureId = edge.featureId;
            routeNode.attributes = edge.attributes;
            routeNode.targetNodeId = edge.nodeIds[1];
            routeNode.targetNodeT = routeEdge.nodeT;
            bestRoute.push_back(routeNode);

            lastNodeId = edge.nodeIds[0];
            lastRouteEdgeIndex = routeEdge.routeEdgeIndex;
        }
        RouteNode initialRouteNode;
        initialRouteNode.targetNodeId = lastNodeId;
        bestRoute.push_back(initialRouteNode);
        
        // Done, reverse the generated route
        std::reverse(bestRoute.begin(), bestRoute.end());
        return bestRoute;
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
