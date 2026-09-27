#include "api/GeometryMethods.h"
#include "geometry/FeatureCollection.h"
#include "geometry/VectorTileFeatureCollection.h"

namespace massif { namespace api {

    Result objectResult(Context& context, const std::shared_ptr<void>& obj, const char* cppClass,
                        PropertyValue& result) {
        if (!obj) {
            return RESULT_FAILED;
        }
        Handle handle = NULL_HANDLE;
        Result registered = context.registerResult("result", obj, cppClass, handle);
        if (registered != RESULT_OK) {
            return registered;
        }
        result.type = PT_OBJECT;
        result.intValue = handle;
        return RESULT_OK;
    }

    namespace {

        // The facade has no array channel, so getFeature(i) walks a collection by index. A GeoJSON
        // dump would drop what only VectorTileFeature carries (layerName, distance).
        bool inRange(const CallArgs& args, int count, int& index) {
            long long argument = 0;
            if (!args.getLong(0, argument) || argument < 0 || argument >= count) {
                return false;
            }
            index = static_cast<int>(argument);
            return true;
        }

        Result getFeature(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            auto collection = static_cast<FeatureCollection*>(obj);
            int index = 0;
            if (!inRange(args, collection->getFeatureCount(), index)) {
                return RESULT_BAD_SPEC;
            }
            return objectResult(context, collection->getFeature(index), "massif::Feature", result);
        }

        /** The same, registered as the subclass - a search result's layerName and distance. */
        Result getVectorTileFeature(Context& context, void* obj, const CallArgs& args,
                                    PropertyValue& result) {
            auto collection = static_cast<VectorTileFeatureCollection*>(obj);
            int index = 0;
            if (!inRange(args, collection->getFeatureCount(), index)) {
                return RESULT_BAD_SPEC;
            }
            return objectResult(context, collection->getFeature(index), "massif::VectorTileFeature",
                                result);
        }

    }

    void registerGeometryMethods() {
        Methods::registerMethod("massif::FeatureCollection", "getFeature", &getFeature);
        Methods::registerMethod("massif::VectorTileFeatureCollection", "getFeature",
                                &getVectorTileFeature);
    }

} }
