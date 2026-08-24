#include "api/GeocodingMethods.h"

#ifdef _MASSIF_GEOCODING_SUPPORT

#include "api/Methods.h"
#include "geocoding/GeocodingRequest.h"
#include "geocoding/GeocodingResult.h"
#include "geocoding/GeocodingService.h"
#include "geocoding/ReverseGeocodingRequest.h"
#include "geocoding/ReverseGeocodingService.h"

#ifdef _MASSIF_OFFLINE_SUPPORT
#include "geocoding/MultiOSMOfflineGeocodingService.h"
#include "geocoding/MultiOSMOfflineReverseGeocodingService.h"
#endif

#include <memory>
#include <string>
#include <vector>

namespace massif { namespace api {

    namespace {

        /**
         * The results as ONE GeoJSON FeatureCollection, in WGS84.
         *
         * calculateAddresses returns a vector of results, each holding a feature collection, and
         * the facade has no channel for a vector. Flattening them here is also what the caller
         * wanted: every feature carries its result's "address" and "rank", so ten results are one
         * crossing rather than one per feature plus one per address field.
         */
        std::string joinResults(const std::vector<std::shared_ptr<GeocodingResult> >& results) {
            std::string json = "{\"type\":\"FeatureCollection\",\"features\":[";
            bool first = true;
            for (const std::shared_ptr<GeocodingResult>& result : results) {
                std::string collection = result->getGeoJSON();
                // Splice the inner "features" array in rather than nesting collections: the
                // consumer wants one flat list, ordered by descending rank as the SDK returns it.
                std::size_t open = collection.find('[');
                std::size_t close = collection.rfind(']');
                if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
                    continue;
                }
                if (!first) {
                    json += ',';
                }
                first = false;
                json.append(collection, open + 1, close - open - 1);
            }
            return json + "]}";
        }

        Result calculateAddresses(Context& context, void* obj, const CallArgs& args,
                                  PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<GeocodingRequest>(
                context.getObject(requestHandle, "massif::GeocodingRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofString(
                joinResults(static_cast<GeocodingService*>(obj)->calculateAddresses(request)));
            return RESULT_OK;
        }

        Result calculateReverseAddresses(Context& context, void* obj, const CallArgs& args,
                                         PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<ReverseGeocodingRequest>(
                context.getObject(requestHandle, "massif::ReverseGeocodingRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofString(joinResults(
                static_cast<ReverseGeocodingService*>(obj)->calculateAddresses(request)));
            return RESULT_OK;
        }

#ifdef _MASSIF_OFFLINE_SUPPORT

        /** add(path) / remove(path) - one .nutigeodb per downloaded area, found by scanning. */
        template <typename Service>
        Result addDatabase(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string database;
            if (!args.getString(0, database)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<Service*>(obj)->add(database);
            return RESULT_OK;
        }

        template <typename Service>
        Result removeDatabase(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string database;
            if (!args.getString(0, database)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofBool(static_cast<Service*>(obj)->remove(database));
            return RESULT_OK;
        }

#endif

    }

    void registerGeocodingMethods() {
        Methods::registerMethod("massif::GeocodingService", "calculateAddresses",
                                &calculateAddresses);
        Methods::registerMethod("massif::ReverseGeocodingService", "calculateAddresses",
                                &calculateReverseAddresses);
#ifdef _MASSIF_OFFLINE_SUPPORT
        Methods::registerMethod("massif::MultiOSMOfflineGeocodingService", "add",
                                &addDatabase<MultiOSMOfflineGeocodingService>);
        Methods::registerMethod("massif::MultiOSMOfflineGeocodingService", "remove",
                                &removeDatabase<MultiOSMOfflineGeocodingService>);
        Methods::registerMethod("massif::MultiOSMOfflineReverseGeocodingService", "add",
                                &addDatabase<MultiOSMOfflineReverseGeocodingService>);
        Methods::registerMethod("massif::MultiOSMOfflineReverseGeocodingService", "remove",
                                &removeDatabase<MultiOSMOfflineReverseGeocodingService>);
#endif
    }

} }

#endif
