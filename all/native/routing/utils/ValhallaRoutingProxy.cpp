#ifdef _MASSIF_ROUTING_SUPPORT

#include "ValhallaRoutingProxy.h"
#include "assets/ValhallaDefaultConfig.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "components/Exceptions.h"
#include "projections/Projection.h"
#include "routing/RoutingRequest.h"
#include "routing/RoutingResult.h"
#include "routing/RouteMatchingRequest.h"
#include "routing/RouteMatchingResult.h"
#include "routing/RouteMatchingPoint.h"
#include "routing/RouteMatchingEdge.h"
#include "routing/utils/RoutingResultBuilder.h"
#include "network/HTTPClient.h"
#include "utils/NetworkUtils.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <stdexcept>
#include <vector>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <utility>

#include <boost/property_tree/ptree.hpp>
#include <boost/lexical_cast.hpp>

#include <picojson/picojson.h>

#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
#include <valhalla/meili/map_matcher.h>
#include <valhalla/meili/map_matcher_factory.h>
#include <valhalla/thor/worker.h>
#include <valhalla/meili/measurement.h>
#include <valhalla/meili/match_result.h>
#include <valhalla/meili/map_matcher_factory.h>
#include <valhalla/meili/candidate_search.h>
#include <valhalla/meili/map_matcher_factory.h>
#include <valhalla/midgard/logging.h>
#include <valhalla/midgard/constants.h>
#include <valhalla/midgard/encoded.h>
#include <valhalla/midgard/pointll.h>
#include <valhalla/baldr/json.h>
#include <valhalla/baldr/directededge.h>
#include <valhalla/baldr/datetime.h>
#include <valhalla/baldr/rapidjson_utils.h>
#include <valhalla/loki/search.h>
#include <valhalla/loki/worker.h>
#include <valhalla/sif/autocost.h>
#include <valhalla/sif/costfactory.h>
#include <valhalla/sif/bicyclecost.h>
#include <valhalla/sif/pedestriancost.h>
#include <valhalla/meili/measurement.h>
#include <valhalla/meili/map_matcher.h>
#include <valhalla/meili/map_matcher_factory.h>
#include <valhalla/meili/match_result.h>
#include <valhalla/odin/worker.h>
#include <valhalla/thor/worker.h>
#include <valhalla/tyr/serializers.h>
#include <valhalla/odin/util.h>
#include <valhalla/odin/directionsbuilder.h>

#else

namespace valhalla { namespace midgard {

    typedef std::pair<float, float> PointLL;

    template<typename Point>
    class Shape5Decoder {
    public:
        Shape5Decoder(const char* begin, const size_t size) : begin(begin), end(begin + size) {
        }
        Point pop() noexcept(false) {
            lat = next(lat);
            lon = next(lon);
            return Point(typename Point::first_type(double(lon) * 1e-6),
                         typename Point::second_type(double(lat) * 1e-6));
        }
        bool empty() const {
            return begin == end;
        }

    private:
        const char* begin;
        const char* end;
        int32_t lat = 0;
        int32_t lon = 0;

        int32_t next(const int32_t previous) noexcept(false) {
            //grab each 5 bits and mask it in where it belongs using the shift
            int byte, shift = 0, result = 0;
              do {
                if(empty()) throw std::runtime_error("Bad encoded polyline");
                //take the least significant 5 bits shifted into place
                byte = int32_t(*begin++) - 63;
                result |= (byte & 0x1f) << shift;
                shift += 5;
                //if the most significant bit is set there is more to this number
            } while (byte >= 0x20);
            //handle the bit flipping and add to previous since its an offset
            return previous + (result & 1 ? ~(result >> 1) : (result >> 1));
        }
    };

    // specialized implemetation for std::vector with reserve
    template<class container_t,
             class ShapeDecoder = Shape5Decoder<typename container_t::value_type>>
    container_t decode(const std::string& encoded) {
        ShapeDecoder shape(encoded.c_str(), encoded.size());
        container_t c;
        c.reserve(encoded.size() / 4);
        while (!shape.empty()) {
            c.emplace_back(shape.pop());
        }
        return c;
    }

    template<class container_t>
    std::string encode(const container_t& points) {
        //a place to keep the output
        std::string output;
        //unless the shape is very course you should probably only need about 3 bytes
        //per coord, which is 6 bytes with 2 coords, so we overshoot to 8 just in case
        output.reserve(points.size() * 8);

        //handy lambda to turn an integer into an encoded string
        auto serialize = [&output](int number) {
            //move the bits left 1 position and flip all the bits if it was a negative number
            number = number < 0 ? ~(number << 1) : (number << 1);
            //write 5 bit chunks of the number
            while (number >= 0x20) {
                int nextValue = (0x20 | (number & 0x1f)) + 63;
                output.push_back(static_cast<char>(nextValue));
                number >>= 5;
            }
            //write the last chunk
            number += 63;
            output.push_back(static_cast<char>(number));
        };

        //this is an offset encoding so we remember the last point we saw
        int last_lon = 0, last_lat = 0;
        //for each point
        for (const auto& p : points) {
            //shift the decimal point 5 places to the right and truncate
            int lon = static_cast<int>(floor(static_cast<double>(p.first) * 1e6));
            int lat = static_cast<int>(floor(static_cast<double>(p.second) * 1e6));
            //encode each coordinate, lat first for some reason
            serialize(lat - last_lat);
            serialize(lon - last_lon);
            //remember the last one we encountered
            last_lon = lon;
            last_lat = lat;
        }
        return output;
    }

} }
#endif

namespace massif {

    std::shared_ptr<RouteMatchingResult> ValhallaRoutingProxy::MatchRoute(HTTPClient& httpClient, const std::string& baseURL, const std::string& profile, const std::shared_ptr<RouteMatchingRequest>& request, std::map<std::string, std::string>& headers) {
        std::map<std::string, std::string> params;
        params["json"] = SerializeRouteMatchingRequest(profile, request);
        std::string finalURL = NetworkUtils::BuildURLFromParameters(baseURL, params);
        Log::Debugf("ValhallaRoutingProxy::MatchRoute: Loading %s", finalURL.c_str());

        std::string responseString = MakeHTTPRequest(httpClient, finalURL, headers);
        return ParseRouteMatchingResult(request->getProjection(), responseString);
    }

    std::shared_ptr<RoutingResult> ValhallaRoutingProxy::CalculateRoute(HTTPClient& httpClient, const std::string& baseURL, const std::string& profile, const std::shared_ptr<RoutingRequest>& request, std::map<std::string, std::string>& headers) {
        std::map<std::string, std::string> params;
        params["json"] = SerializeRoutingRequest(profile, request);
        std::string finalURL = NetworkUtils::BuildURLFromParameters(baseURL, params);
        Log::Debugf("ValhallaRoutingProxy::CalculateRoute: Loading %s", finalURL.c_str());

        std::string responseString = MakeHTTPRequest(httpClient, finalURL, headers);
        return ParseRoutingResult(request->getProjection(), responseString);
    }


#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
    std::unordered_map<std::string, std::string> valhalla_locales;
    void ValhallaRoutingProxy::AddLocale(const std::string& key, const std::string& json) {

        if (valhalla_locales.find(key) == valhalla_locales.end()) {
            valhalla_locales.insert(std::make_pair(key, json));
        }
    }

    std::shared_ptr<RouteMatchingResult> ValhallaRoutingProxy::MatchRoute(const std::vector<std::shared_ptr<sqlite3pp::database> >& databases, const std::string& profile, const Variant& config, const std::shared_ptr<RouteMatchingRequest>& request) {
        std::string resultString;
        try {
            std::stringstream ss;
            ss << config.toPicoJSON().serialize();
            boost::property_tree::ptree configTree;
            rapidjson::read_json(ss, configTree);
            auto reader = std::make_shared<valhalla::baldr::GraphReader>(databases);

            valhalla::Api api;
            valhalla::ParseApi(SerializeRouteMatchingRequest(profile, request), valhalla::Options::trace_attributes, api, valhalla_locales);

            valhalla::loki::loki_worker_t lokiworker(configTree, reader);
            lokiworker.trace(api);
            valhalla::thor::thor_worker_t thorworker(configTree, reader);
            resultString = thorworker.trace_attributes(api);
            lokiworker.cleanup();
        }
        catch (const std::exception& ex) {
            throw GenericException("Exception while matching route", ex.what());
        }
        return ParseRouteMatchingResult(request->getProjection(), resultString);
    }

    std::shared_ptr<RoutingResult> ValhallaRoutingProxy::CalculateRoute(const std::vector<std::shared_ptr<sqlite3pp::database> >& databases, const std::string& profile, const Variant& config, const std::shared_ptr<RoutingRequest>& request) {
        std::string resultString;
        try {
            std::stringstream ss;
            ss << config.toPicoJSON().serialize();
            boost::property_tree::ptree configTree;
            rapidjson::read_json(ss, configTree);
            auto reader = std::make_shared<valhalla::baldr::GraphReader>(databases);

            valhalla::Api api;
            valhalla::ParseApi(SerializeRoutingRequest(profile, request), valhalla::Options::route, api, valhalla_locales);

            valhalla::loki::loki_worker_t lokiworker(configTree, reader);
            lokiworker.route(api);
            valhalla::thor::thor_worker_t thorworker(configTree, reader);
            thorworker.route(api);
            valhalla::odin::odin_worker_t odinworker(configTree);
            odinworker.narrate(api);
            resultString = valhalla::tyr::serializeDirections(api);
            lokiworker.cleanup();
            thorworker.cleanup();
            odinworker.cleanup();
        }
        catch (const std::exception& ex) {
            throw GenericException("Exception while calculating route", ex.what());
        }
        return ParseRoutingResult(request->getProjection(), resultString);
    }

    namespace {
        const int LOOK_AHEAD_MAX_JUNCTIONS = 16;
        const int LOOK_AHEAD_MAX_STEPS = 256;
        const double LOOK_AHEAD_PROBE_METERS = 30.0;

        void PointAhead(double lat, double lon, double bearing, double meters, double& aheadLat, double& aheadLon) {
            const double radius = 6371000.0;
            const double toRadians = M_PI / 180.0;
            double angular = meters / radius;
            double theta = bearing * toRadians;
            double phi = lat * toRadians;
            double phiAhead = std::asin(std::sin(phi) * std::cos(angular) + std::cos(phi) * std::sin(angular) * std::cos(theta));
            double lambdaAhead = lon * toRadians + std::atan2(std::sin(theta) * std::sin(angular) * std::cos(phi), std::cos(angular) - std::sin(phi) * std::sin(phiAhead));
            aheadLat = phiAhead / toRadians;
            aheadLon = lambdaAhead / toRadians;
        }

        uint32_t AccessMaskForProfile(const std::string& profile) {
            if (profile == "pedestrian") {
                return valhalla::baldr::kPedestrianAccess;
            } else if (profile == "bicycle") {
                return valhalla::baldr::kBicycleAccess;
            } else if (profile == "motorcycle") {
                return valhalla::baldr::kMotorcycleAccess;
            } else if (profile == "truck") {
                return valhalla::baldr::kTruckAccess;
            } else if (profile == "bus") {
                return valhalla::baldr::kBusAccess;
            }
            return valhalla::baldr::kAutoAccess;
        }

        double AngleDelta(double first, double second) {
            double delta = std::fabs(std::fmod(first - second, 360.0));
            return delta > 180.0 ? 360.0 - delta : delta;
        }

        std::vector<valhalla::midgard::PointLL> DirectedShape(const valhalla::baldr::graph_tile_ptr& tile, const valhalla::baldr::DirectedEdge* edge) {
            std::vector<valhalla::midgard::PointLL> shape = tile->edgeinfo(edge).shape();
            if (!edge->forward()) {
                std::reverse(shape.begin(), shape.end());
            }
            return shape;
        }

        struct LookAheadExit {
            valhalla::baldr::GraphId id;
            const valhalla::baldr::DirectedEdge* edge;
            double heading;
            bool enterable;
        };
    }

    // Starts on the edge the position matches in the direction of `bearing`, then follows the enterable exit that
    // is closest to straight on, listing every node that has a real choice (>= 2 exits besides the way back).
    std::string ValhallaRoutingProxy::LookAhead(const std::vector<std::shared_ptr<sqlite3pp::database> >& databases, const std::string& profile, const Variant& config, double lat, double lon, double bearing, double lengthMeters) {
        using namespace valhalla::baldr;
        picojson::object result;
        picojson::array junctions;
        try {
            std::stringstream ss;
            ss << config.toPicoJSON().serialize();
            boost::property_tree::ptree configTree;
            rapidjson::read_json(ss, configTree);
            auto reader = std::make_shared<GraphReader>(databases);

            // locate is not built (VALHALLA_SERVICES off): map-match two points along the bearing instead, which also
            // honours the profile's access, so the edge found is one the user may travel in that direction
            double aheadLat = lat;
            double aheadLon = lon;
            PointAhead(lat, lon, bearing, LOOK_AHEAD_PROBE_METERS, aheadLat, aheadLon);
            picojson::array shape;
            for (const std::pair<double, double>& point : {std::make_pair(lat, lon), std::make_pair(aheadLat, aheadLon)}) {
                picojson::object shapePoint;
                shapePoint["lat"] = picojson::value(point.first);
                shapePoint["lon"] = picojson::value(point.second);
                shape.push_back(picojson::value(shapePoint));
            }
            picojson::array attributes;
            for (const char* attribute : {"edge.id", "matched.distance_along_edge"}) {
                attributes.push_back(picojson::value(attribute));
            }
            picojson::object filters;
            filters["attributes"] = picojson::value(attributes);
            filters["action"] = picojson::value("include");
            picojson::object request;
            request["shape"] = picojson::value(shape);
            request["costing"] = picojson::value(profile);
            request["shape_match"] = picojson::value("map_snap");
            request["filters"] = picojson::value(filters);

            std::string matched;
            try {
                valhalla::Api api;
                valhalla::ParseApi(picojson::value(request).serialize(), valhalla::Options::trace_attributes, api, valhalla_locales);
                valhalla::loki::loki_worker_t lokiworker(configTree, reader);
                lokiworker.trace(api);
                valhalla::thor::thor_worker_t thorworker(configTree, reader);
                matched = thorworker.trace_attributes(api);
                lokiworker.cleanup();
            } catch (const std::exception&) {
                // no road of this profile at the position, in that direction
                return picojson::value(result).serialize();
            }

            picojson::value parsed;
            std::string error = picojson::parse(parsed, matched);
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            if (!parsed.is<picojson::object>() || !parsed.contains("edges") || !parsed.get("edges").is<picojson::array>() || parsed.get("edges").get<picojson::array>().empty()) {
                return picojson::value(result).serialize();
            }
            const picojson::value& firstEdge = parsed.get("edges").get<picojson::array>().front();
            if (!firstEdge.is<picojson::object>() || !firstEdge.contains("id")) {
                return picojson::value(result).serialize();
            }
            double percentAlong = 0.0;
            if (parsed.contains("matched_points") && parsed.get("matched_points").is<picojson::array>() && !parsed.get("matched_points").get<picojson::array>().empty()) {
                const picojson::value& firstPoint = parsed.get("matched_points").get<picojson::array>().front();
                if (firstPoint.is<picojson::object>() && firstPoint.contains("distance_along_edge")) {
                    percentAlong = firstPoint.get("distance_along_edge").get<double>();
                }
            }

            const uint32_t accessMask = AccessMaskForProfile(profile);
            GraphId edgeId(static_cast<uint64_t>(firstEdge.get("id").get<double>()));
            graph_tile_ptr tile = reader->GetGraphTile(edgeId);
            if (!tile) {
                return picojson::value(result).serialize();
            }
            const DirectedEdge* edge = tile->directededge(edgeId);
            double travelled = (1.0 - percentAlong) * edge->length();

            for (int step = 0; step < LOOK_AHEAD_MAX_STEPS && travelled < lengthMeters && static_cast<int>(junctions.size()) < LOOK_AHEAD_MAX_JUNCTIONS; step++) {
                GraphId nodeId = edge->endnode();
                graph_tile_ptr nodeTile = reader->GetGraphTile(nodeId);
                if (!nodeTile) {
                    break;
                }
                const NodeInfo* node = nodeTile->node(nodeId);
                valhalla::midgard::PointLL nodeLL = nodeTile->get_node_ll(nodeId);
                GraphId opposingId = reader->GetOpposingEdgeId(edgeId);

                std::vector<valhalla::midgard::PointLL> arrivalShape = DirectedShape(tile, edge);
                double arrivalHeading = arrivalShape.size() >= 2 ? arrivalShape[arrivalShape.size() - 2].Heading(arrivalShape.back()) : bearing;

                std::vector<LookAheadExit> exits;
                for (uint32_t i = 0; i < node->edge_count(); i++) {
                    GraphId outId(nodeId.tileid(), nodeId.level(), node->edge_index() + i);
                    const DirectedEdge* out = nodeTile->directededge(outId);
                    if (outId == opposingId || out->is_shortcut() || ((out->forwardaccess() | out->reverseaccess()) & accessMask) == 0) {
                        continue;
                    }
                    std::vector<valhalla::midgard::PointLL> outShape = DirectedShape(nodeTile, out);
                    double heading = outShape.size() >= 2 ? outShape[0].Heading(outShape[1]) : arrivalHeading;
                    exits.push_back({outId, out, heading, (out->forwardaccess() & accessMask) != 0});
                }

                const LookAheadExit* straight = nullptr;
                for (const LookAheadExit& exit : exits) {
                    if (exit.enterable && (!straight || AngleDelta(exit.heading, arrivalHeading) < AngleDelta(straight->heading, arrivalHeading))) {
                        straight = &exit;
                    }
                }

                if (exits.size() >= 2) {
                    picojson::object junction;
                    junction["distance"] = picojson::value(travelled);
                    junction["lat"] = picojson::value(static_cast<double>(nodeLL.lat()));
                    junction["lon"] = picojson::value(static_cast<double>(nodeLL.lng()));
                    junction["arrival"] = picojson::value(arrivalHeading);
                    picojson::array exitsJson;
                    for (const LookAheadExit& exit : exits) {
                        picojson::object exitJson;
                        exitJson["heading"] = picojson::value(static_cast<double>(exit.heading));
                        exitJson["enterable"] = picojson::value(exit.enterable);
                        exitsJson.push_back(picojson::value(exitJson));
                    }
                    junction["exits"] = picojson::value(exitsJson);
                    junctions.push_back(picojson::value(junction));
                }

                if (!straight) {
                    picojson::object ahead;
                    ahead["kind"] = picojson::value(exits.empty() ? "end" : "noEntry");
                    ahead["distance"] = picojson::value(travelled);
                    result["ahead"] = picojson::value(ahead);
                    break;
                }

                edge = straight->edge;
                edgeId = straight->id;
                tile = nodeTile;
                travelled += edge->length();
            }
            result["junctions"] = picojson::value(junctions);
            result["heading"] = picojson::value(bearing);
        }
        catch (const std::exception& ex) {
            throw GenericException("Exception while looking ahead", ex.what());
        }
        return picojson::value(result).serialize();
    }
#endif

    Variant ValhallaRoutingProxy::GetDefaultConfiguration() {
        return Variant::FromString(valhalla_default_config);
    }

    bool ValhallaRoutingProxy::TranslateManeuverType(int maneuverType, RoutingAction::RoutingAction& action) {
        enum {
            TripDirections_Maneuver_Type_kNone = 0,
            TripDirections_Maneuver_Type_kStart = 1,
            TripDirections_Maneuver_Type_kStartRight = 2,
            TripDirections_Maneuver_Type_kStartLeft = 3,
            TripDirections_Maneuver_Type_kDestination = 4,
            TripDirections_Maneuver_Type_kDestinationRight = 5,
            TripDirections_Maneuver_Type_kDestinationLeft = 6,
            TripDirections_Maneuver_Type_kBecomes = 7,
            TripDirections_Maneuver_Type_kContinue = 8,
            TripDirections_Maneuver_Type_kSlightRight = 9,
            TripDirections_Maneuver_Type_kRight = 10,
            TripDirections_Maneuver_Type_kSharpRight = 11,
            TripDirections_Maneuver_Type_kUturnRight = 12,
            TripDirections_Maneuver_Type_kUturnLeft = 13,
            TripDirections_Maneuver_Type_kSharpLeft = 14,
            TripDirections_Maneuver_Type_kLeft = 15,
            TripDirections_Maneuver_Type_kSlightLeft = 16,
            TripDirections_Maneuver_Type_kRampStraight = 17,
            TripDirections_Maneuver_Type_kRampRight = 18,
            TripDirections_Maneuver_Type_kRampLeft = 19,
            TripDirections_Maneuver_Type_kExitRight = 20,
            TripDirections_Maneuver_Type_kExitLeft = 21,
            TripDirections_Maneuver_Type_kStayStraight = 22,
            TripDirections_Maneuver_Type_kStayRight = 23,
            TripDirections_Maneuver_Type_kStayLeft = 24,
            TripDirections_Maneuver_Type_kMerge = 25,
            TripDirections_Maneuver_Type_kRoundaboutEnter = 26,
            TripDirections_Maneuver_Type_kRoundaboutExit = 27,
            TripDirections_Maneuver_Type_kFerryEnter = 28,
            TripDirections_Maneuver_Type_kFerryExit = 29,
            TripDirections_Maneuver_Type_kTransit = 30,
            TripDirections_Maneuver_Type_kTransitTransfer = 31,
            TripDirections_Maneuver_Type_kTransitRemainOn = 32,
            TripDirections_Maneuver_Type_kTransitConnectionStart = 33,
            TripDirections_Maneuver_Type_kTransitConnectionTransfer = 34,
            TripDirections_Maneuver_Type_kTransitConnectionDestination = 35,
            TripDirections_Maneuver_Type_kPostTransitConnectionDestination = 36,
            TripDirections_Maneuver_Type_kMergeRight = 37,
            TripDirections_Maneuver_Type_kMergeLeft = 38
        };

        static const std::unordered_map<int, RoutingAction::RoutingAction> maneuverTypes = {
            { TripDirections_Maneuver_Type_kNone,             massif::RoutingAction::ROUTING_ACTION_NO_TURN },//NoTurn = 0,
            { TripDirections_Maneuver_Type_kContinue,         massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kBecomes,          massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kRampStraight,     massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kStayStraight,     massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kMerge,            massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kMergeLeft,        massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kMergeRight,       massif::RoutingAction::ROUTING_ACTION_GO_STRAIGHT },//GoStraight,
            { TripDirections_Maneuver_Type_kFerryEnter,       massif::RoutingAction::ROUTING_ACTION_ENTER_FERRY },//EnterFerry,
            { TripDirections_Maneuver_Type_kFerryExit,        massif::RoutingAction::ROUTING_ACTION_LEAVE_FERRY },//LeaveFerry,
            { TripDirections_Maneuver_Type_kSlightRight,      massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnSlightRight,
            { TripDirections_Maneuver_Type_kRight,            massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnRight,
            { TripDirections_Maneuver_Type_kRampRight,        massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnRight,
            { TripDirections_Maneuver_Type_kExitRight,        massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnRight,
            { TripDirections_Maneuver_Type_kStayRight,        massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnRight,
            { TripDirections_Maneuver_Type_kSharpRight,       massif::RoutingAction::ROUTING_ACTION_TURN_RIGHT },//TurnSharpRight,
            { TripDirections_Maneuver_Type_kUturnLeft,        massif::RoutingAction::ROUTING_ACTION_UTURN },//UTurn,
            { TripDirections_Maneuver_Type_kUturnRight,       massif::RoutingAction::ROUTING_ACTION_UTURN },//UTurn,
            { TripDirections_Maneuver_Type_kSharpLeft,        massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnSharpLeft,
            { TripDirections_Maneuver_Type_kLeft,             massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnLeft,
            { TripDirections_Maneuver_Type_kRampLeft,         massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnLeft,
            { TripDirections_Maneuver_Type_kExitLeft,         massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnLeft,
            { TripDirections_Maneuver_Type_kStayLeft,         massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnLeft,
            { TripDirections_Maneuver_Type_kSlightLeft,       massif::RoutingAction::ROUTING_ACTION_TURN_LEFT },//TurnSlightLeft,
            { TripDirections_Maneuver_Type_kRoundaboutEnter,  massif::RoutingAction::ROUTING_ACTION_ENTER_ROUNDABOUT },//EnterRoundAbout,
            { TripDirections_Maneuver_Type_kRoundaboutExit,   massif::RoutingAction::ROUTING_ACTION_LEAVE_ROUNDABOUT },//LeaveRoundAbout,
            { TripDirections_Maneuver_Type_kStart,            massif::RoutingAction::ROUTING_ACTION_HEAD_ON },//StartAtEndOfStreet,
            { TripDirections_Maneuver_Type_kStartRight,       massif::RoutingAction::ROUTING_ACTION_HEAD_ON },//StartAtEndOfStreet,
            { TripDirections_Maneuver_Type_kStartLeft,        massif::RoutingAction::ROUTING_ACTION_HEAD_ON },//StartAtEndOfStreet,
            { TripDirections_Maneuver_Type_kDestination,      massif::RoutingAction::ROUTING_ACTION_FINISH },//ReachedYourDestination,
            { TripDirections_Maneuver_Type_kDestinationRight, massif::RoutingAction::ROUTING_ACTION_FINISH },//ReachedYourDestination,
            { TripDirections_Maneuver_Type_kDestinationLeft,  massif::RoutingAction::ROUTING_ACTION_FINISH },//ReachedYourDestination,
            // NOTE: transit maneuvers are missing from this table
        };

        auto it = maneuverTypes.find(maneuverType);
        if (it != maneuverTypes.end()) {
            action = it->second;
            return true;
        }

        Log::Infof("ValhallaRoutingProxy::TranslateManeuverType: ignoring maneuver %d", maneuverType);
        return false;
    }

    std::string ValhallaRoutingProxy::SerializeRouteMatchingRequest(const std::string& profile, const std::shared_ptr<RouteMatchingRequest>& request) {
        std::shared_ptr<Projection> proj = request->getProjection();

        picojson::array locations;
        std::vector<MapPos> points = request->getPoints();
        for (std::size_t i = 0; i < points.size(); i++) {
            picojson::object location;
            picojson::value pointParams = request->getPointParameters(static_cast<int>(i)).toPicoJSON();
            if (pointParams.is<picojson::object>()) {
                location = pointParams.get<picojson::object>();
            }
            MapPos posWgs84 = proj->toWgs84(points[i]);
            location["lon"] = picojson::value(posWgs84.getX());
            location["lat"] = picojson::value(posWgs84.getY());
            locations.emplace_back(location);
        }


        picojson::object json;
        json["shape"] = picojson::value(locations);
        json["shape_match"] = picojson::value("map_snap");
        json["costing"] = picojson::value(profile);
        json["units"] = picojson::value("kilometers");
        if (request->getAccuracy() > 0) {
            json["gps_accuracy"] = picojson::value(request->getAccuracy());
        }
        picojson::value customParams = request->getCustomParameters().toPicoJSON();
        if (customParams.is<picojson::object>()) {
            const picojson::object& customParamsObj = customParams.get<picojson::value::object>();
            for (auto it = customParamsObj.begin(); it != customParamsObj.end(); it++) {
                    json[it->first] = it->second;
            }
        }
        return picojson::value(json).serialize();
    }

    std::string ValhallaRoutingProxy::SerializeRoutingRequest(const std::string& profile, const std::shared_ptr<RoutingRequest>& request) {
        std::shared_ptr<Projection> proj = request->getProjection();

        picojson::array locations;
        std::vector<MapPos> points = request->getPoints();
        for (std::size_t i = 0; i < points.size(); i++) {
            picojson::object location;
            picojson::value pointParams = request->getPointParameters(static_cast<int>(i)).toPicoJSON();
            if (pointParams.is<picojson::object>()) {
                location = pointParams.get<picojson::object>();
            }
            MapPos posWgs84 = proj->toWgs84(points[i]);
            location["lon"] = picojson::value(posWgs84.getX());
            location["lat"] = picojson::value(posWgs84.getY());
            locations.emplace_back(location);
        }

        picojson::object json;
        json["locations"] = picojson::value(locations);
        json["costing"] = picojson::value(profile);
        json["units"] = picojson::value("kilometers");

        picojson::value customParams = request->getCustomParameters().toPicoJSON();
        if (customParams.is<picojson::object>()) {
            const picojson::object& customParamsObj = customParams.get<picojson::value::object>();
            for (auto it = customParamsObj.begin(); it != customParamsObj.end(); it++) {
                json[it->first] = it->second;
            }
        }
        return picojson::value(json).serialize();
    }

    std::shared_ptr<RouteMatchingResult> ValhallaRoutingProxy::ParseRouteMatchingResult(const std::shared_ptr<Projection>& proj, const std::string& resultString) {
        picojson::value result;
        std::string err = picojson::parse(result, resultString);
        if (!err.empty()) {
            throw GenericException("Failed to parse result", err);
        }

        std::vector<RouteMatchingPoint> matchingPoints;
        std::vector<RouteMatchingEdge> matchingEdges;
        try {
            if (result.get("matched_points").is<picojson::array>()) {
                for (const picojson::value& matchedPointInfo : result.get("matched_points").get<picojson::array>()) {
                    RouteMatchingPointType::RouteMatchingPointType type = RouteMatchingPointType::ROUTE_MATCHING_POINT_UNMATCHED;
                    if (matchedPointInfo.get("type").get<std::string>() == "matched") {
                        type = RouteMatchingPointType::ROUTE_MATCHING_POINT_MATCHED;
                    } else if (matchedPointInfo.get("type").get<std::string>() == "interpolated") {
                        type = RouteMatchingPointType::ROUTE_MATCHING_POINT_INTERPOLATED;
                    }

                    double lat = matchedPointInfo.get("lat").get<double>();
                    double lon = matchedPointInfo.get("lon").get<double>();
                    int edgeIndex = static_cast<int>(matchedPointInfo.get("edge_index").get<std::int64_t>());

                    matchingPoints.emplace_back(proj->fromLatLong(lat, lon), type, edgeIndex);
                }
            }
            if (result.get("edges").is<picojson::array>()) {
                for (const picojson::value &edgeInfo: result.get("edges").get<picojson::array>()) {
                    std::map<std::string, Variant> attributes;
                    if (edgeInfo.is<picojson::object>()) {
                        const picojson::object &edgeInfoObject = edgeInfo.get<picojson::object>();
                        for (auto it = edgeInfoObject.begin(); it != edgeInfoObject.end(); it++) {
                            attributes[it->first] = Variant::FromPicoJSON(it->second);
                        }
                    }

                    matchingEdges.emplace_back(attributes);
                }
            }
        }
        catch (const std::exception& ex) {
            throw GenericException("Exception while translating route", ex.what());
        }
        return std::make_shared<RouteMatchingResult>(proj, std::move(matchingPoints), std::move(matchingEdges), resultString);
    }

    std::shared_ptr<RoutingResult> ValhallaRoutingProxy::ParseRoutingResult(const std::shared_ptr<Projection>& proj, const std::string& resultString) {
        picojson::value result;
        std::string err = picojson::parse(result, resultString);
        if (!err.empty()) {
            throw GenericException("Failed to parse result", err);
        }
        if (!result.get("trip").is<picojson::object>()) {
            throw GenericException("No trip info in the result");
        }

        RoutingResultBuilder resultBuilder(proj, resultString);
        try {
            std::size_t shapeIndex= 0;
            for (const picojson::value& legInfo : result.get("trip").get("legs").get<picojson::array>()) {
                std::vector<valhalla::midgard::PointLL> shape = valhalla::midgard::decode<std::vector<valhalla::midgard::PointLL> >(legInfo.get("shape").get<std::string>());
                std::vector<MapPos> points;
                points.reserve(shape.size());
                for (std::size_t i = 0; i < shape.size(); i++) {
                    const valhalla::midgard::PointLL& point = shape.at(i);
                    points.push_back(proj->fromLatLong(point.second, point.first));
                }
                resultBuilder.addPoints(points);

                const picojson::array& maneuvers = legInfo.get("maneuvers").get<picojson::array>();
                for (std::size_t i = 0; i < maneuvers.size(); i++) {
                    const picojson::value& maneuver = maneuvers[i];

                    std::size_t maneuverIndex0 = static_cast<std::size_t>(maneuver.get("begin_shape_index").get<std::int64_t>());
                    std::size_t maneuverIndex1 = static_cast<std::size_t>(maneuver.get("end_shape_index").get<std::int64_t>());

                    RoutingAction::RoutingAction action = RoutingAction::ROUTING_ACTION_NO_TURN;
                    TranslateManeuverType(static_cast<int>(maneuver.get("type").get<std::int64_t>()), action);
                    if (action == RoutingAction::ROUTING_ACTION_FINISH && i + 1 < maneuvers.size()) {
                        action = RoutingAction::ROUTING_ACTION_REACH_VIA_LOCATION;
                    }

                    std::string streetName;
                    if (maneuver.get("street_names").is<picojson::array>()) {
                        for (const picojson::value& name : maneuver.get("street_names").get<picojson::array>()) {
                            streetName += (streetName.empty() ? "" : "/") + name.get<std::string>();
                        }
                    }

                    std::string instruction = maneuver.get("instruction").get<std::string>();
                    double time = maneuver.get("time").get<double>();
                    double distance = maneuver.get("length").get<double>() * 1000.0;

                    std::size_t shapeBeginIndex = maneuver.get("begin_shape_index").get<std::int64_t>();
                    int pointIndex = shapeIndex + shapeBeginIndex;

                    RoutingInstructionBuilder& instrBuilder = resultBuilder.addInstruction(action, pointIndex);
                    instrBuilder.setStreetName(streetName);
                    instrBuilder.setTime(time);
                    instrBuilder.setDistance(distance);
                    instrBuilder.setInstruction(instruction);
                }
                shapeIndex += maneuvers[maneuvers.size()-1].get("begin_shape_index").get<std::int64_t>();
            }
        }
        catch (const std::exception& ex) {
            throw GenericException("Exception while translating route", ex.what());
        }
        return resultBuilder.buildRoutingResult();
    }

    std::string ValhallaRoutingProxy::MakeHTTPRequest(HTTPClient& httpClient, const std::string& url, std::map<std::string, std::string>& headers) {
        std::map<std::string, std::string> requestHeaders;
        requestHeaders["Connection"] = "close";
        requestHeaders.merge(headers);
        std::map<std::string, std::string> responseHeaders;
        std::shared_ptr<BinaryData> responseData;
        int code = httpClient.get(url, requestHeaders, responseHeaders, responseData);
        std::string responseString;
        if (responseData) {
            const char* responseDataPtr = reinterpret_cast<const char*>(responseData->data());
            responseString = std::string(responseDataPtr, responseDataPtr + responseData->size());
        }

        if (code != 0) {
            if (responseString.empty()) {
                throw NetworkException("Failed to fetch response");
            }
            Log::Debugf("ValhallaRoutingProxy::MakeHTTPRequest: Failed response %s", responseString.c_str());

            picojson::value result;
            std::string err = picojson::parse(result, responseString);
            if (!err.empty()) {
                throw GenericException("Failed to parse routing/matching result", err);
            }
            std::string details;
            if (result.get("error").is<std::string>()) {
                details = result.get("error").get<std::string>();
            }
            throw GenericException("Routing/matching failed", details);
        }
        return responseString;
    }


    ValhallaRoutingProxy::ValhallaRoutingProxy() {
    }

}

#endif
