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
#include "layers/Layer.h"
#include "layers/TileLayer.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "vectortiles/VectorTileDecoder.h"
#include "utils/Log.h"

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
         * getElevations([[x, y], ...]) -> a handle onto a flat array of metres.
         *
         * An object result rather than a value: a profile over a track is thousands of numbers,
         * and neither a JSON array nor a per-element proxy is an acceptable way to move them.
         * Read it with getDoubles.
         */
        Result getElevations(Context& context, void* obj, const CallArgs& args,
                             PropertyValue& result) {
            std::vector<MapPos> positions;
            if (!args.getPositions(0, positions)) {
                return RESULT_BAD_SPEC;
            }
            auto elevations = std::make_shared<std::vector<double> >(
                static_cast<HillshadeRasterTileLayer*>(obj)->getElevations(positions));

            Handle handle = NULL_HANDLE;
            Result registered = context.registerResult("result", elevations,
                                                       Context::DOUBLE_VECTOR_CLASS, handle);
            if (registered != RESULT_OK) {
                return registered;
            }
            result.type = PT_OBJECT;
            result.intValue = handle;
            return RESULT_OK;
        }

        /*
         * The style parameters of a CartoCSS decoder - the most-used call in the app this API is
         * measured against, and how a live theme switch is done without re-decoding from scratch.
         *
         * Registered on the DECLARED class and downcast here, because traversal records the class
         * the property declares rather than the concrete one - see the design doc's gaps.
         */
        MBVectorTileDecoder* asMBDecoder(void* obj) {
            return dynamic_cast<MBVectorTileDecoder*>(static_cast<VectorTileDecoder*>(obj));
        }

        Result setStyleParameter(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string name, value;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            // The SDK takes the value as text whatever it means, so a number is spelled out here
            // rather than refused.
            if (!args.getString(1, value)) {
                Variant raw = args.get(1);
                if (raw.getType() == VariantType::VARIANT_TYPE_NULL) {
                    return RESULT_BAD_SPEC;
                }
                value = raw.toString();
            }
            MBVectorTileDecoder* decoder = asMBDecoder(obj);
            if (!decoder) {
                return RESULT_UNSUPPORTED_TYPE;
            }
            result = PropertyValue::ofBool(decoder->setStyleParameter(name, value));
            return RESULT_OK;
        }

        Result getStyleParameter(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string name;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            MBVectorTileDecoder* decoder = asMBDecoder(obj);
            if (!decoder) {
                return RESULT_UNSUPPORTED_TYPE;
            }
            result = PropertyValue::ofString(decoder->getStyleParameter(name));
            return RESULT_OK;
        }

        /** Every parameter the style declares, as a JSON array - for a UI that builds itself. */
        Result getStyleParameters(Context&, void* obj, const CallArgs&, PropertyValue& result) {
            MBVectorTileDecoder* decoder = asMBDecoder(obj);
            if (!decoder) {
                return RESULT_UNSUPPORTED_TYPE;
            }
            std::vector<Variant> names;
            for (const std::string& name : decoder->getStyleParameters()) {
                names.push_back(Variant(name));
            }
            result = PropertyValue::ofString(Variant(names).toString());
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        /** clearTileCaches(all) - true also drops the persistent cache, not just the in-memory one. */
        Result clearTileCaches(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            bool all = false;
            args.getBool(0, all);
            static_cast<TileLayer*>(obj)->clearTileCaches(all);
            return RESULT_OK;
        }

        Result refresh(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<Layer*>(obj)->refresh();
            return RESULT_OK;
        }

    }

    void Methods::registerBuiltins() {
        registerMethod("massif::TileDataSource", "loadTile", &loadTile);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevation", &getElevation);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevations", &getElevations);
        registerMethod("massif::VectorTileDecoder", "setStyleParameter", &setStyleParameter);
        registerMethod("massif::VectorTileDecoder", "getStyleParameter", &getStyleParameter);
        registerMethod("massif::VectorTileDecoder", "getStyleParameters", &getStyleParameters);
        registerMethod("massif::TileLayer", "clearTileCaches", &clearTileCaches);
        registerMethod("massif::Layer", "refresh", &refresh);
    }

} }
