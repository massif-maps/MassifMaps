#define BOOST_TEST_MODULE SGRE

#include "Rule.h"
#include "Query.h"
#include "Result.h"
#include "GraphBuilder.h"
#include "RouteFinder.h"

#include <picojson/picojson.h>

#include <boost/math/constants/constants.hpp>
#include <boost/test/included/unit_test.hpp>

using namespace carto::sgre;

static bool equal(const Point& point0, const Point& point1) {
    static constexpr double EPSILON = 1.0e-8;
    return cglib::length(point0 - point1) <= EPSILON;
}

static picojson::value parseJSON(const std::string& json) {
    picojson::value value;
    std::string err = picojson::parse(value, json);
    if (!err.empty()) {
        throw std::runtime_error(err);
    }
    return value;
}

static std::vector<Point> createChain(double dist, int n) {
    std::vector<Point> points;
    for (int i = 0; i < n; i++) {
        points.emplace_back(i * dist, 0.0, 0.0);
    }
    return points;
}

static std::vector<Point> createRing(double radius, int n) {
    std::vector<Point> points;
    for (int i = 0; i < n; i++) {
        double angle = (i + 0.5) / n * 2 * boost::math::constants::pi<double>();
        points.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0.0);
    }
    return points;
}

static std::vector<Point> shiftPoints(std::vector<Point> points, const cglib::vec3<double>& delta) {
    for (Point& point : points) {
        point += delta;
    }
    return points;
}

static std::vector<Point> createSquare(double radius) {
    std::vector<Point> points;
    points.emplace_back( radius,  radius, 0);
    points.emplace_back( radius, -radius, 0);
    points.emplace_back(-radius, -radius, 0);
    points.emplace_back(-radius,  radius, 0);
    return points;
}

// Build convex polygon regions and check that resulting routing path always contains a straight line
BOOST_AUTO_TEST_CASE(convexRouting) {
    for (int n = 3; n <= 100; n++) {
        auto ring = createRing(1.0, n);
        GraphBuilder graphBuilder = GraphBuilder(RuleList());
        graphBuilder.addPolygon({ ring }, picojson::value());
        RouteFinder routeFinder(graphBuilder.build());
        Query query(Point(-0.5, 0.0, 0.0), Point(0.5, 0.0, 0.0));
        Result result = routeFinder.find(query);
        BOOST_CHECK(result.getGeometry().size() == 2);
        if (result.getGeometry().size() == 2) {
            BOOST_CHECK(result.getGeometry()[0] == query.getPos(0));
            BOOST_CHECK(result.getGeometry()[1] == query.getPos(1));
        }
    }
}

// Build chained line and do trivial routing checks
BOOST_AUTO_TEST_CASE(chainRouting) {
    for (int n = 2; n <= 100; n++) {
        auto chain = createChain(1.0, n);
        GraphBuilder graphBuilder = GraphBuilder(RuleList());
        graphBuilder.addLineString(chain, picojson::value());
        RouteFinder routeFinder(graphBuilder.build());
        Query query(Point(0.0, 0.0, 0.0), Point(n - 1.0, 0.0, 0.0));
        Result result = routeFinder.find(query);
        BOOST_CHECK(result.getGeometry().size() == 2);
        if (result.getGeometry().size() == 2) {
            BOOST_CHECK(result.getGeometry()[0] == query.getPos(0));
            BOOST_CHECK(result.getGeometry()[1] == query.getPos(1));
        }
    }
}

// Test cases for search modes for linestring-based graphs
BOOST_AUTO_TEST_CASE(lineEdgeSearch) {
    auto buildGraph = [](const std::string& mode) -> std::shared_ptr<const StaticGraph> {
        auto chain = createChain(1.0, 3);
        auto ruleList = RuleList::parse(parseJSON("[{\"search\": \"" + mode + "\"}]"));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addLineString(chain, picojson::value());
        return graphBuilder.build();
    };

    // Basic tests for linestring search modes using exact vertex coordinates
    {
        Query query(Point(0.0, 0.0, 0.0), Point(2.0, 0.0, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("edge"));
        BOOST_CHECK(finder2.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder2.find(query).getGeometry().at(1) == query.getPos(1));

        RouteFinder finder3(buildGraph("vertex"));
        BOOST_CHECK(finder3.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder3.find(query).getGeometry().at(1) == query.getPos(1));

        RouteFinder finder4(buildGraph("firstlastvertex"));
        BOOST_CHECK(finder4.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder4.find(query).getGeometry().at(1) == query.getPos(1));
    }

    // Additional tests for linestring search modes using non-exact coordinates
    {
        Query query(Point(0.45, 0.0, 0.0), Point(1.45, 0.0, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("edge"));
        BOOST_CHECK(finder2.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder2.find(query).getGeometry().at(1) == query.getPos(1));

        RouteFinder finder3(buildGraph("vertex"));
        BOOST_CHECK(finder3.find(query).getGeometry().at(0) == Point(0, 0, 0));
        BOOST_CHECK(finder3.find(query).getGeometry().at(1) == Point(1, 0, 0));

        RouteFinder finder4(buildGraph("firstlastvertex"));
        BOOST_CHECK(finder4.find(query).getGeometry().at(0) == Point(0, 0, 0));
        BOOST_CHECK(finder4.find(query).getGeometry().at(1) == Point(2, 0, 0));
    }
}

// Test cases for search modes for polygon-based graphs
BOOST_AUTO_TEST_CASE(polygonEdgeSearch) {
    auto buildGraph = [](const std::string& mode) -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(1.0);
        auto ruleList = RuleList::parse(parseJSON("[{\"search\": \"" + mode + "\"}]"));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ square }, picojson::value());
        return graphBuilder.build();
    };

    // Basic tests for all search modes using X coordinate on polygon edge
    {
        Query query(Point(1.0, 0.1, 0.0), Point(-1.0, 0.1, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("edge"));
        BOOST_CHECK(finder2.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder2.find(query).getGeometry().at(1) == query.getPos(1));

        RouteFinder finder3(buildGraph("vertex"));
        BOOST_CHECK(finder3.find(query).getGeometry().at(0) == Point(1.0, 1.0, 0.0));
        BOOST_CHECK(finder3.find(query).getGeometry().at(1) == Point(-1.0, 1.0, 0.0));

        RouteFinder finder4(buildGraph("firstlastvertex"));
        BOOST_CHECK(finder4.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder5(buildGraph("surface"));
        BOOST_CHECK(finder5.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder5.find(query).getGeometry().at(1) == query.getPos(1));
    }

    // Additional tests for all search modes using points inside polygon
    {
        Query query(Point(0.75, 0.25, 0.0), Point(-0.75, 0.25, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("edge"));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(0), Point(1.0, 0.25, 0.0)));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(1), Point(-1.0, 0.25, 0.0)));

        RouteFinder finder3(buildGraph("vertex"));
        BOOST_CHECK(finder3.find(query).getGeometry().at(0) == Point(1.0, 1.0, 0.0));
        BOOST_CHECK(finder3.find(query).getGeometry().at(1) == Point(-1.0, 1.0, 0.0));

        RouteFinder finder4(buildGraph("surface"));
        BOOST_CHECK(finder4.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder4.find(query).getGeometry().at(1) == query.getPos(1));
    }

    // Check that edge and surface modes are properly handled for points inside the square
    {
        Query query(Point(0.25, 0.0, 0.0), Point(-0.25, 0.0, 0.0));

        RouteFinder finder1(buildGraph("edge"));
        BOOST_CHECK(equal(finder1.find(query).getGeometry().at(0), Point(1.0, 0.0, 0.0)));
        BOOST_CHECK(equal(finder1.find(query).getGeometry().at(1), Point(-1.0, 0.0, 0.0)));

        RouteFinder finder2(buildGraph("surface"));
        BOOST_CHECK(finder2.find(query).getGeometry().at(0) == query.getPos(0));
        BOOST_CHECK(finder2.find(query).getGeometry().at(1) == query.getPos(1));
    }
}

// Test cases for edge linking in hybrid graphs (polygons and linestrings)
BOOST_AUTO_TEST_CASE(edgeLinking) {
    auto buildGraph = [](const std::string& mode) -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(0.25);
        auto chain = createChain(1.0, 3);
        auto ruleList = RuleList::parse(parseJSON("[{\"link\": \"" + mode + "\"}]"));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ shiftPoints(square, { 0, 0, 0 }) }, picojson::value());
        graphBuilder.addPolygon({ shiftPoints(square, { 1, 0, 0 }) }, picojson::value());
        graphBuilder.addPolygon({ shiftPoints(square, { 2, 0, 0 }) }, picojson::value());
        graphBuilder.addLineString({ chain }, picojson::value());
        return graphBuilder.build();
    };

    // Check routing graph A-B-C with routing from A to C
    {
        Query query(Point(0.0, 0.1, 0.0), Point(2.0, 0.1, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("all"));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(0), query.getPos(0)));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(3), query.getPos(1)));

        RouteFinder finder3(buildGraph("endpoints"));
        BOOST_CHECK(equal(finder3.find(query).getGeometry().at(0), query.getPos(0)));
        BOOST_CHECK(equal(finder3.find(query).getGeometry().at(3), query.getPos(1)));
    }

    // Check routing graph A-B-C with routing from A to B
    {
        Query query(Point(0.0, 0.1, 0.0), Point(1.0, 0.1, 0.0));

        RouteFinder finder1(buildGraph("none"));
        BOOST_CHECK(finder1.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder2(buildGraph("all"));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(0), query.getPos(0)));
        BOOST_CHECK(equal(finder2.find(query).getGeometry().at(3), query.getPos(1)));

        RouteFinder finder3(buildGraph("endpoints"));
        BOOST_CHECK(finder3.find(query).getStatus() == Result::Status::FAILED);
    }
}

// Test cases for node sharing
BOOST_AUTO_TEST_CASE(nodeSharing) {
    auto buildGraph = []() -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(0.5);
        auto chain = createChain(1.0, 2);
        auto ruleList = RuleList::parse(parseJSON("[]"));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ shiftPoints(square, { 0, 0, 0 }) }, picojson::value());
        graphBuilder.addPolygon({ shiftPoints(square, { 1, 0, 0 }) }, picojson::value());
        graphBuilder.addLineString({ shiftPoints(chain, { 1, 0, 0 }) }, picojson::value());
        graphBuilder.addLineString({ shiftPoints(chain, { 2, 0, 0 }) }, picojson::value());
        return graphBuilder.build();
    };

    // Check that points and edges with identical coordinates are properly shared and a path is found
    {
        Query query(Point(0.0, 0.25, 0.0), Point(3.0, 0.0, 0.0));

        RouteFinder finder(buildGraph());
        BOOST_CHECK(equal(finder.find(query).getGeometry().at(0), query.getPos(0)));
        BOOST_CHECK(equal(finder.find(query).getGeometry().at(2), query.getPos(1)));
    }
}

// Test cases for routing attributes
BOOST_AUTO_TEST_CASE(routingAttributes) {
    auto buildGraph = [](const std::string& rules) -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(0.25);
        auto chain = createChain(1.0, 2);
        auto ruleList = RuleList::parse(parseJSON(rules));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ shiftPoints(square, { 0, 0, 0 }) }, parseJSON("{ \"type\": 0 }"));
        graphBuilder.addLineString({ chain }, parseJSON("{ \"type\": 1 }"));
        graphBuilder.addPolygon({ shiftPoints(square, { 1, 0, 0 }) }, parseJSON("{ \"type\": 2 }"));
        return graphBuilder.build();
    };

    // Check forward/backward attribute handling
    {
        Query query(Point(0.0, 0.1, 0.0), Point(1.0, 0.1, 0.0));
        Query revQuery(Point(1.0, 0.1, 0.0), Point(0.0, 0.1, 0.0));

        RouteFinder finder1(buildGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"));
        BOOST_CHECK(equal(finder1.find(query).getGeometry().at(0), query.getPos(0)));
        BOOST_CHECK(equal(finder1.find(query).getGeometry().at(3), query.getPos(1)));

        RouteFinder finder2(buildGraph(R"R([{ "filters":[{"type":0}], "speed":0.0 }])R"));
        BOOST_CHECK(finder2.find(query).getStatus() == Result::Status::FAILED);

        RouteFinder finder3(buildGraph(R"R([{ "filters":[{"type":1}], "backward_speed":0.0 }])R"));
        BOOST_CHECK(finder3.find(query).getStatus() == Result::Status::SUCCESS);
        BOOST_CHECK(finder3.find(revQuery).getStatus() == Result::Status::FAILED);

        RouteFinder finder4(buildGraph(R"R([{ "filters":[{"type":1}], "backward_speed":0.0 }, { "backward_speed":1.0 }])R"));
        BOOST_CHECK(finder4.find(query).getStatus() == Result::Status::SUCCESS);
        BOOST_CHECK(finder4.find(revQuery).getStatus() == Result::Status::SUCCESS);

        RouteFinder finder5(buildGraph(R"R([{ "filters":[{"type":1}], "backward_turnspeed":0.0 }])R"));
        BOOST_CHECK(finder5.find(query).getStatus() == Result::Status::SUCCESS);
        BOOST_CHECK(finder5.find(revQuery).getStatus() == Result::Status::FAILED);
    }
}

// Test cases for routing instructions
BOOST_AUTO_TEST_CASE(routingInstructions) {
    auto buildGraph = [](const std::string& rules) -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(0.5);
        auto chain = createChain(1.0, 2);
        auto ruleList = RuleList::parse(parseJSON(rules));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ shiftPoints(square, { -1, 0, 0 }) }, parseJSON("{ \"type\": 0 }"));
        graphBuilder.addLineString({ shiftPoints(chain, { -0.5, 0, 0 }) }, parseJSON("{ \"type\": 1 }"));
        graphBuilder.addPolygon({ shiftPoints(square, { 1, 0, 0 }) }, parseJSON("{ \"type\": 2 }"));
        return graphBuilder.build();
    };

    auto buildZGraph = [](const std::string& rules) -> std::shared_ptr<const StaticGraph> {
        auto square = createSquare(0.5);
        auto ruleList = RuleList::parse(parseJSON(rules));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addPolygon({ shiftPoints(square, { 0, 0, 1 }) }, parseJSON("{ \"type\": 0 }"));
        graphBuilder.addLineString({ Point(0, 0, 1), Point(0, 0, -1) }, parseJSON("{ \"type\": 1 }"));
        graphBuilder.addPolygon({ shiftPoints(square, { 0, 0, -1 }) }, parseJSON("{ \"type\": 2 }"));
        return graphBuilder.build();
    };

    // Check basic filters
    {
        Query query(Point(-1.0, 0.0, 0.0), Point(1.0, 0.0, 0.0));

        RouteFinder finder1(buildGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"));
        BOOST_CHECK(finder1.find(query).serialize() == parseJSON(R"R({"geometry":[[-1,0,0],[-0.5,0,0],[0.5,0,0],[1,0,0]],"instructions":[{"distance":55659.74539663679,"geomindex":0,"tag":{"type":0},"time":55659.74539663679,"type":1},{"distance":111319.49079327358,"geomindex":1,"tag":{"type":1},"time":80666.29795501483,"type":2},{"distance":55659.74539663679,"geomindex":2,"tag":{"type":2},"time":40333.148977507415,"type":2},{"distance":0,"geomindex":3,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }

    // Check delay or 'wait' handling
    {
        Query query(Point(-1.0, 0.0, 0.0), Point(1.0, 0.0, 0.0));

        RouteFinder finder1(buildGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }, { "filters":[{"type":1}], "delay":2.0 }])R"));
        BOOST_CHECK(finder1.find(query).serialize() == parseJSON(R"R({"geometry":[[-1,0,0],[-0.5,0,0],[0.5,0,0],[1,0,0]],"instructions":[{"distance":55659.74539663679,"geomindex":0,"tag":{"type":0},"time":55659.74539663679,"type":1},{"distance":0,"geomindex":1,"tag":{"type":1},"time":2,"type":7},{"distance":111319.49079327358,"geomindex":1,"tag":{"type":1},"time":80666.29795501483,"type":2},{"distance":55659.74539663679,"geomindex":2,"tag":{"type":2},"time":40333.148977507415,"type":2},{"distance":0,"geomindex":3,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }

    // Check 'go straight' handling
    {
        Query query(Point(-1.25, 0.0, 0.0), Point(1.25, 0.0, 0.0));

        RouteFinder finder1(buildGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"));
        std::string result = finder1.find(query).serialize().serialize();
        BOOST_CHECK(finder1.find(query).serialize() == parseJSON(R"R({"geometry":[[-1.25,0,0],[-0.5,0,0],[0.5,0,0],[1.25,0,0]],"instructions":[{"distance":83489.618094955178,"geomindex":0,"tag":{"type":0},"time":83489.618094955178,"type":1},{"distance":111319.49079327358,"geomindex":1,"tag":{"type":1},"time":80666.29795501483,"type":2},{"distance":83489.618094955178,"geomindex":2,"tag":{"type":2},"time":60499.723466261115,"type":2},{"distance":0,"geomindex":3,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }

    // Check 'turn left', 'turn right' handling
    {
        Query query(Point(-1.0, 0.25, 0.0), Point(1.0, -0.25, 0.0));

        RouteFinder finder1(buildGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"));
        BOOST_CHECK(finder1.find(query).serialize() == parseJSON(R"R({"geometry":[[-1,0.25,0],[-0.5,0,0],[0.5,0,0],[1,-0.25,0]],"instructions":[{"distance":62229.487158605429,"geomindex":0,"tag":{"type":0},"time":62229.487158605429,"type":1},{"distance":111319.49079327358,"geomindex":1,"tag":{"type":1},"time":80666.445538632484,"type":6},{"distance":62229.487158605429,"geomindex":2,"tag":{"type":2},"time":45093.979013784003,"type":5},{"distance":0,"geomindex":3,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }

    // Check 'zspeed' and vertical routing
    {
        Query query(Point(-1.0, 0.25, -1.0), Point(1.0, -0.25, 1.0));
        Query revQuery(Point(-1.0, 0.25, 1.0), Point(1.0, -0.25, -1.0));

        RouteFinder finder1(buildZGraph(R"R([{ "filters":[{"type":0}], "zspeed":0.0 }])R"));
        BOOST_CHECK(finder1.find(query).serialize() == parseJSON(R"R({"geometry":[[-0.49999999999999994,0.25000000000000006,-1],[0,0,-1],[0,0,1],[0.49999999999999994,-0.25000000000000006,1]],"instructions":[{"distance":62229.487158605429,"geomindex":0,"tag":{"type":2},"time":45093.831430166356,"type":1},{"distance":2,"geomindex":1,"tag":{"type":1},"time":4,"type":3},{"distance":62229.487158605429,"geomindex":2,"tag":{"type":0},"time":45093.831430173064,"type":2},{"distance":0,"geomindex":3,"tag":{"type":0},"time":0,"type":8}],"status":1})R"));

        RouteFinder finder2(buildZGraph(R"R([{ "filters":[{"type":1}], "zspeed":0.0 }])R"));
        BOOST_CHECK(finder2.find(query).serialize() == parseJSON(R"R({"status":0})R"));

        RouteFinder finder3(buildZGraph(R"R([{ "filters":[{"type":1}], "zspeed":2.0, "backward_zspeed":3.0 }])R"));
        BOOST_CHECK(finder3.find(query).serialize() == parseJSON(R"R({"geometry":[[-0.49999999999999994,0.25000000000000006,-1],[0,0,-1],[0,0,1],[0.49999999999999994,-0.25000000000000006,1]],"instructions":[{"distance":62229.487158605429,"geomindex":0,"tag":{"type":2},"time":45093.831430166356,"type":1},{"distance":2,"geomindex":1,"tag":{"type":1},"time":0.66666666666666663,"type":3},{"distance":62229.487158605429,"geomindex":2,"tag":{"type":0},"time":45093.831430173064,"type":2},{"distance":0,"geomindex":3,"tag":{"type":0},"time":0,"type":8}],"status":1})R"));
        BOOST_CHECK(finder3.find(revQuery).serialize() == parseJSON(R"R({"geometry":[[-0.49999999999999994,0.25000000000000006,1],[0,0,1],[0,0,-1],[0.49999999999999994,-0.25000000000000006,-1]],"instructions":[{"distance":62229.487158605429,"geomindex":0,"tag":{"type":0},"time":45093.831430166356,"type":1},{"distance":2,"geomindex":1,"tag":{"type":1},"time":1,"type":4},{"distance":62229.487158605429,"geomindex":2,"tag":{"type":2},"time":45093.831430173064,"type":2},{"distance":0,"geomindex":3,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }
}

// Test cases for rule parsing
BOOST_AUTO_TEST_CASE(ruleParsing) {
    auto parseRule = [](const std::string& json) -> Rule {
        return Rule::parse(parseJSON(json));
    };
    
    // Check 'link' parsing
    {
        BOOST_CHECK(parseRule(R"R({"link": "none"})R").getLinkMode() == Graph::LinkMode::NONE);
        BOOST_CHECK(parseRule(R"R({"link": "endpoints"})R").getLinkMode() == Graph::LinkMode::ENDPOINTS);
        BOOST_CHECK(parseRule(R"R({"link": "all"})R").getLinkMode() == Graph::LinkMode::ALL);
    }

    // Check 'search' parsing
    {
        BOOST_CHECK(parseRule(R"R({"search": "none"})R").getSearchCriteria() == Graph::SearchCriteria::NONE);
        BOOST_CHECK(parseRule(R"R({"search": "vertex"})R").getSearchCriteria() == Graph::SearchCriteria::VERTEX);
        BOOST_CHECK(parseRule(R"R({"search": "firstlastvertex"})R").getSearchCriteria() == Graph::SearchCriteria::FIRST_LAST_VERTEX);
        BOOST_CHECK(parseRule(R"R({"search": "edge"})R").getSearchCriteria() == Graph::SearchCriteria::EDGE);
        BOOST_CHECK(parseRule(R"R({"search": "surface"})R").getSearchCriteria() == Graph::SearchCriteria::SURFACE);
    }
}

// Test cases for zsensitivity parameter
BOOST_AUTO_TEST_CASE(zSensitivity) {
    auto buildZGraph = [](const std::string& rules) -> std::shared_ptr<const StaticGraph> {
        auto chain = createChain(1.0, 2);
        auto ruleList = RuleList::parse(parseJSON(rules));
        GraphBuilder graphBuilder = GraphBuilder(ruleList);
        graphBuilder.addLineString({ shiftPoints(chain, { 0.0, 0.0, 0.0 }) }, parseJSON("{ \"type\": 1 }"));
        graphBuilder.addLineString({ shiftPoints(chain, { 0.0, 0.1, 1.0 }) }, parseJSON("{ \"type\": 2 }"));
        return graphBuilder.build();
    };

    // Check default behaviour
    {
        Query query1(Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0));
        Query query2(Point(0.0, 0.1, 0.0), Point(1.0, 0.1, 0.0));

        RouteFinder finder1(buildZGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"));
        BOOST_CHECK(finder1.find(query1).serialize() == parseJSON(R"R({"geometry":[[0,0,0],[1,0,0]],"instructions":[{"distance":111319.49079327358,"geomindex":0,"tag":{"type":1},"time":80666.29795501483,"type":1},{"distance":0,"geomindex":1,"tag":{"type":1},"time":0,"type":8}],"status":1})R"));
        BOOST_CHECK(finder1.find(query2).serialize() == parseJSON(R"R({"geometry":[[0,0.10000000000000001,1],[1,0.10000000000000001,1]],"instructions":[{"distance":111319.32124403633,"geomindex":0,"tag":{"type":2},"time":80666.175093248283,"type":1},{"distance":0,"geomindex":1,"tag":{"type":2},"time":0,"type":8}],"status":1})R"));
    }

    // Set sensitivity to very high value
    {
        Query query1(Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0));
        Query query2(Point(0.0, 0.1, 0.0), Point(1.0, 0.1, 0.0));

        picojson::value configDef = parseJSON(R"R({ "zsensitivity": 1000000 })R");
        auto finder1 = RouteFinder::create(buildZGraph(R"R([{ "filters":[{"type":0}], "speed":1.0 }])R"), configDef);
        BOOST_CHECK(finder1->find(query1).serialize() == parseJSON(R"R({"geometry":[[0,0,0],[1,0,0]],"instructions":[{"distance":111319.49079327358,"geomindex":0,"tag":{"type":1},"time":80666.29795501483,"type":1},{"distance":0,"geomindex":1,"tag":{"type":1},"time":0,"type":8}],"status":1})R"));
        BOOST_CHECK(finder1->find(query2).serialize() == parseJSON(R"R({"geometry":[[0,0,0],[1,0,0]],"instructions":[{"distance":111319.49079327358,"geomindex":0,"tag":{"type":1},"time":80666.29795501483,"type":1},{"distance":0,"geomindex":1,"tag":{"type":1},"time":0,"type":8}],"status":1})R"));
    }
}
