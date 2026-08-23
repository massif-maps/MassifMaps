#include "api/RoutingMethods.h"

#ifdef _MASSIF_ROUTING_SUPPORT

#include "api/GeometryMethods.h"
#include "api/Methods.h"
#include "core/MapPos.h"
#include "routing/RoutingInstruction.h"
#include "routing/RoutingRequest.h"
#include "routing/RoutingResult.h"
#include "routing/RoutingService.h"

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

    }

    void registerRoutingMethods() {
        Methods::registerMethod("massif::RoutingService", "calculateRoute", &calculateRoute);
        Methods::registerMethod("massif::RoutingResult", "getInstruction", &getInstruction);
        Methods::registerMethod("massif::RoutingResult", "getPoints", &getRoutePoints);
        Methods::registerMethod("massif::RoutingRequest", "setCustomParameter", &setCustomParameter);
    }

} }

#endif
