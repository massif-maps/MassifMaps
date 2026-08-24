#include "api/RoutingMethods.h"

#ifdef _MASSIF_ROUTING_SUPPORT

#include "api/GeometryMethods.h"
#include "api/Methods.h"
#include "core/MapPos.h"
#include "routing/RouteMatchingRequest.h"
#include "routing/RouteMatchingResult.h"
#include "routing/RoutingInstruction.h"
#include "routing/RoutingRequest.h"
#include "routing/RoutingResult.h"
#include "routing/RoutingService.h"

#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
#include "routing/MultiValhallaOfflineRoutingService.h"
#endif

#include <memory>
#include <vector>

namespace massif { namespace api {

    namespace {

        /**
         * calculateRoute([requestHandle]) -> a RoutingResult handle.
         *
         * Blocking, and for the online service that means an HTTP round trip on the calling thread,
         * so it belongs on callAsync exactly like findFeatures.
         */
        Result calculateRoute(Context& context, void* obj, const CallArgs& args,
                              PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<RoutingRequest>(
                context.getObject(requestHandle, "massif::RoutingRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            return objectResult(context,
                                static_cast<RoutingService*>(obj)->calculateRoute(request),
                                "massif::RoutingResult", result);
        }

        /**
         * matchRoute([requestHandle]) -> a RouteMatchingResult handle.
         *
         * Map matching: what turns a recorded track into edges carrying surface, grade and road
         * class. Blocking like calculateRoute, so it belongs on callAsync.
         */
        Result matchRoute(Context& context, void* obj, const CallArgs& args,
                          PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<RouteMatchingRequest>(
                context.getObject(requestHandle, "massif::RouteMatchingRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            return objectResult(context,
                                static_cast<RoutingService*>(obj)->matchRoute(request),
                                "massif::RouteMatchingResult", result);
        }

        /**
         * getInstruction(i) -> a handle onto a COPY of the maneuver.
         *
         * A RoutingInstruction is a value type, not a shared_ptr one, so the element is copied onto
         * the heap to have a handle at all. It is nine scalars; the alternative is a per-field
         * method. instructionCount is a property, so the loop reads the same as a feature loop.
         */
        Result getInstruction(Context& context, void* obj, const CallArgs& args,
                              PropertyValue& result) {
            long long index = 0;
            auto route = static_cast<RoutingResult*>(obj);
            if (!args.getLong(0, index) || index < 0 || index >= route->getInstructionCount()) {
                return RESULT_BAD_SPEC;
            }
            auto instruction = std::make_shared<RoutingInstruction>(
                route->getInstructions()[static_cast<std::size_t>(index)]);
            return objectResult(context, instruction, "massif::RoutingInstruction", result);
        }

        /**
         * getPoints() -> the path, flat, as x0,y0,x1,y1,… through the bulk numeric channel.
         *
         * A route is thousands of positions and `points` is a struct property with no codec on
         * purpose: JSON is what this channel exists to avoid. Read it with getDoubles.
         */
        Result getRoutePoints(Context& context, void* obj, const CallArgs&, PropertyValue& result) {
            auto flat = std::make_shared<std::vector<double> >();
            const std::vector<MapPos>& points = static_cast<RoutingResult*>(obj)->getPoints();
            flat->reserve(points.size() * 2);
            for (const MapPos& pos : points) {
                flat->push_back(pos.getX());
                flat->push_back(pos.getY());
            }
            return objectResult(context, flat, Context::DOUBLE_VECTOR_CLASS, result);
        }

        /** setCustomParameter(name, value) - the costing options a Valhalla request carries. */
        Result setCustomParameter(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string name;
            if (!args.getString(0, name) || args.count() < 2) {
                return RESULT_BAD_SPEC;
            }
            static_cast<RoutingRequest*>(obj)->setCustomParameter(name, args.get(1));
            return RESULT_OK;
        }

        /** The same on a match request - shape_match and the attribute filters go through it. */
        Result setMatchingCustomParameter(Context&, void* obj, const CallArgs& args,
                                          PropertyValue&) {
            std::string name;
            if (!args.getString(0, name) || args.count() < 2) {
                return RESULT_BAD_SPEC;
            }
            static_cast<RouteMatchingRequest*>(obj)->setCustomParameter(name, args.get(1));
            return RESULT_OK;
        }

#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT

        /**
         * add(path) / remove(path) on the multi-database offline service.
         *
         * One .vtiles per downloaded area, found by scanning at run time, so the databases cannot
         * be constructor arguments.
         */
        Result addRoutingDatabase(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string database;
            if (!args.getString(0, database)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<MultiValhallaOfflineRoutingService*>(obj)->add(database);
            return RESULT_OK;
        }

        Result removeRoutingDatabase(Context&, void* obj, const CallArgs& args,
                                     PropertyValue& result) {
            std::string database;
            if (!args.getString(0, database)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofBool(
                static_cast<MultiValhallaOfflineRoutingService*>(obj)->remove(database));
            return RESULT_OK;
        }

        /** addLocale(key, json) - the maneuver wording for a language the SDK does not bundle. */
        Result addRoutingLocale(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string key;
            std::string json;
            if (!args.getString(0, key) || !args.getString(1, json)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<MultiValhallaOfflineRoutingService*>(obj)->addLocale(key, json);
            return RESULT_OK;
        }

        /** setConfigurationParameter(param, value) - valhalla's own limits, e.g. max distances. */
        Result setRoutingConfiguration(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string param;
            if (!args.getString(0, param) || args.count() < 2) {
                return RESULT_BAD_SPEC;
            }
            static_cast<MultiValhallaOfflineRoutingService*>(obj)
                ->setConfigurationParameter(param, args.get(1));
            return RESULT_OK;
        }

#endif

    }

    void registerRoutingMethods() {
        Methods::registerMethod("massif::RoutingService", "calculateRoute", &calculateRoute);
        Methods::registerMethod("massif::RoutingService", "matchRoute", &matchRoute);
        Methods::registerMethod("massif::RoutingResult", "getInstruction", &getInstruction);
        Methods::registerMethod("massif::RoutingResult", "getPoints", &getRoutePoints);
        Methods::registerMethod("massif::RoutingRequest", "setCustomParameter", &setCustomParameter);
        Methods::registerMethod("massif::RouteMatchingRequest", "setCustomParameter",
                                &setMatchingCustomParameter);
#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
        Methods::registerMethod("massif::MultiValhallaOfflineRoutingService", "add",
                                &addRoutingDatabase);
        Methods::registerMethod("massif::MultiValhallaOfflineRoutingService", "remove",
                                &removeRoutingDatabase);
        Methods::registerMethod("massif::MultiValhallaOfflineRoutingService", "addLocale",
                                &addRoutingLocale);
        Methods::registerMethod("massif::MultiValhallaOfflineRoutingService",
                                "setConfigurationParameter", &setRoutingConfiguration);
#endif
    }

} }

#endif
