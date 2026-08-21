/*
 * The SDK's own methods, in their own translation unit.
 *
 * Kept out of Methods.cpp for the same reason SpecFactories is kept out of Spec.cpp: these pull in
 * every class they call, and a test should be able to link the registry without the whole SDK.
 */

#include "api/Methods.h"
#include "api/StructCodec.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "utils/Log.h"

#include <sstream>

namespace massif { namespace api {

    namespace {

        /**
         * loadTile([x, y, zoom]) -> a TileData handle, or null when the source has no such tile.
         *
         * Synchronously fetches, so it is the first method that belongs on callAsync rather than
         * call - an HTTP source blocks the calling thread otherwise.
         */
        Result loadTile(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            MapTile tile;
            if (!args.getTile(0, tile)) {
                return RESULT_BAD_SPEC;
            }
            std::shared_ptr<TileData> data;
            try {
                data = static_cast<TileDataSource*>(obj)->loadTile(tile);
            } catch (const std::exception& ex) {
                Log::Errorf("api loadTile: %s", ex.what());
                return RESULT_FAILED;
            }
            if (!data) {
                return RESULT_FAILED;
            }
            Handle handle = NULL_HANDLE;
            Result registered = context.registerResult("result", data, "massif::TileData", handle);
            if (registered != RESULT_OK) {
                return registered;
            }
            result.type = PT_OBJECT;
            result.intValue = handle;
            return RESULT_OK;
        }

        Result getElevation(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            MapPos pos;
            if (!args.getPos(0, pos)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofDouble(
                static_cast<HillshadeRasterTileLayer*>(obj)->getElevation(pos));
            return RESULT_OK;
        }

        /**
         * getElevations([[x, y], ...]) -> a JSON array of metres.
         *
         * JSON, not a typed array: the facade has no bulk numeric channel yet, and one profile
         * of a few hundred points is small enough that it has not been worth inventing one. See
         * the design doc's known gaps before using this for a whole track.
         */
        Result getElevations(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::vector<MapPos> positions;
            if (!args.getPositions(0, positions)) {
                return RESULT_BAD_SPEC;
            }
            std::vector<double> elevations =
                static_cast<HillshadeRasterTileLayer*>(obj)->getElevations(positions);

            std::ostringstream json;
            json.precision(17);
            json << "[";
            for (std::size_t index = 0; index < elevations.size(); index++) {
                json << (index ? "," : "") << elevations[index];
            }
            json << "]";
            result = PropertyValue::ofString(json.str());
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

    }

    void Methods::registerBuiltins() {
        registerMethod("massif::TileDataSource", "loadTile", &loadTile);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevation", &getElevation);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevations", &getElevations);
    }

} }
