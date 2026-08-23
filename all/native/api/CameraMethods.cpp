#include "api/CameraMethods.h"
#include "api/StructCodec.h"
#include "core/MapBounds.h"
#include "core/MapPos.h"
#include "core/ScreenBounds.h"
#include "core/ScreenPos.h"
#include "ui/BaseMapView.h"

namespace massif { namespace api {

    namespace {

        /*
         * Why the camera is METHODS and not writable properties.
         *
         * Four setters animate on four clocks and visibly fight each other, which is the whole
         * reason BaseMapView::moveTo/flyTo exist. A property surface would have re-created that
         * problem in every binding, so the facade exposes the position, zoom, rotation and tilt as
         * READ-ONLY attributes and moves the camera only through these.
         *
         * Positions cross as JSON arrays, like every other struct on this surface - see
         * StructCodec and docs/internals/api-facade.md. A screen point is [x, y] too.
         */

        Result moveTo(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            auto view = static_cast<BaseMapView*>(obj);
            MapPos pos;
            double zoom = 0, rotation = 0, tilt = 0;
            if (!args.getPos(0, pos) || !args.getDouble(1, zoom) || !args.getDouble(2, rotation) ||
                !args.getDouble(3, tilt)) {
                return RESULT_BAD_SPEC;
            }
            view->moveTo(pos, static_cast<float>(zoom), static_cast<float>(rotation),
                         static_cast<float>(tilt));
            return RESULT_OK;
        }

        Result flyTo(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            auto view = static_cast<BaseMapView*>(obj);
            MapPos pos;
            double zoom = 0, rotation = 0, tilt = 0, climbHeight = 0, seconds = 0;
            if (!args.getPos(0, pos) || !args.getDouble(1, zoom) || !args.getDouble(2, rotation) ||
                !args.getDouble(3, tilt) || !args.getDouble(4, climbHeight) ||
                !args.getDouble(5, seconds)) {
                return RESULT_BAD_SPEC;
            }
            // climbHeight arches the path - highest halfway, nothing at either end, which is what
            // clears the ridge between two valleys. 0 is the straight flight.
            view->flyTo(pos, static_cast<float>(zoom), static_cast<float>(rotation),
                        static_cast<float>(tilt), static_cast<float>(climbHeight),
                        static_cast<float>(seconds));
            return RESULT_OK;
        }

        Result fitBounds(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            auto view = static_cast<BaseMapView*>(obj);
            // Bounds and a screen rectangle have no argument type of their own - they are four
            // numbers each, and adding two ARG_TYPES for two call sites would be the wrong trade.
            // They arrive as the same JSON the property channel already carries them in.
            MapBounds bounds;
            ScreenBounds screenBounds;
            bool integerZoom = false, resetRotation = false, resetTilt = false;
            double seconds = 0;
            std::string screenJson = args.get(1).toString();
            if (!args.getBounds(0, bounds) ||
                !StructCodec::decode(screenJson, screenBounds) ||
                !args.getBool(2, integerZoom) || !args.getBool(3, resetRotation) ||
                !args.getBool(4, resetTilt) || !args.getDouble(5, seconds)) {
                return RESULT_BAD_SPEC;
            }
            view->moveToFitBounds(bounds, screenBounds, integerZoom, resetRotation, resetTilt,
                                  static_cast<float>(seconds));
            return RESULT_OK;
        }

        Result screenToMap(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            auto view = static_cast<BaseMapView*>(obj);
            double x = 0, y = 0;
            if (!args.getDouble(0, x) || !args.getDouble(1, y)) {
                return RESULT_BAD_SPEC;
            }
            MapPos pos = view->screenToMap(ScreenPos(static_cast<float>(x), static_cast<float>(y)));
            result = PropertyValue::ofString(StructCodec::encode(args.toCaller(pos)));
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        Result mapToScreen(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            auto view = static_cast<BaseMapView*>(obj);
            MapPos pos;
            if (!args.getPos(0, pos)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofString(StructCodec::encode(view->mapToScreen(pos)));
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        Result stopFlight(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<BaseMapView*>(obj)->stopFlight();
            return RESULT_OK;
        }

    }

    void registerCameraMethods() {
        Methods::registerMethod("massif::BaseMapView", "moveTo", &moveTo);
        Methods::registerMethod("massif::BaseMapView", "flyTo", &flyTo);
        Methods::registerMethod("massif::BaseMapView", "fitBounds", &fitBounds);
        Methods::registerMethod("massif::BaseMapView", "screenToMap", &screenToMap);
        Methods::registerMethod("massif::BaseMapView", "mapToScreen", &mapToScreen);
        Methods::registerMethod("massif::BaseMapView", "stopFlight", &stopFlight);
    }

} }
