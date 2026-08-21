/*
 * The SDK's own methods, in their own translation unit.
 *
 * Kept out of Methods.cpp for the same reason SpecFactories is kept out of Spec.cpp: these pull in
 * every class they call, and a test should be able to link the registry without the whole SDK.
 */

#include "api/GeometryMethods.h"
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

#ifdef _MASSIF_SEARCH_SUPPORT
#include "search/FeatureCollectionSearchService.h"
#include "search/SearchRequest.h"
#include "search/VectorTileSearchService.h"
#endif

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
            return objectResult(context, data, "massif::TileData", result);
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
            return objectResult(context, elevations, Context::DOUBLE_VECTOR_CLASS, result);
        }

        /*
         * The style parameters of a CartoCSS decoder - the most-used call in the app this API is
         * measured against, and how a live theme switch is done without re-decoding from scratch.
         *
         * Registered on MBVectorTileDecoder, which traversal now reports: a `tileDecoder` reached
         * through a path used to arrive as its DECLARED class and these needed a dynamic_cast.
         */
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
            result = PropertyValue::ofBool(
                static_cast<MBVectorTileDecoder*>(obj)->setStyleParameter(name, value));
            return RESULT_OK;
        }

        Result getStyleParameter(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string name;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofString(
                static_cast<MBVectorTileDecoder*>(obj)->getStyleParameter(name));
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

#ifdef _MASSIF_SEARCH_SUPPORT

        /**
         * findFeatures([requestHandle]) -> a feature collection handle.
         *
         * The request is an object rather than an inline spec because every one of its filters is
         * already a property: create a "search"/"request", set them, pass the handle. Nothing about
         * a search filter had to be taught to the facade.
         */
        Result findVectorTileFeatures(Context& context, void* obj, const CallArgs& args,
                                      PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<SearchRequest>(
                context.getObject(requestHandle, "massif::SearchRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            return objectResult(context,
                                static_cast<VectorTileSearchService*>(obj)->findFeatures(request),
                                "massif::VectorTileFeatureCollection", result);
        }

        Result findCollectionFeatures(Context& context, void* obj, const CallArgs& args,
                                      PropertyValue& result) {
            Handle requestHandle = NULL_HANDLE;
            if (!args.getHandle(0, requestHandle)) {
                return RESULT_BAD_SPEC;
            }
            auto request = std::static_pointer_cast<SearchRequest>(
                context.getObject(requestHandle, "massif::SearchRequest"));
            if (!request) {
                return RESULT_BAD_HANDLE;
            }
            return objectResult(context,
                                static_cast<FeatureCollectionSearchService*>(obj)->findFeatures(request),
                                "massif::FeatureCollection", result);
        }

#endif

    }

    void Methods::registerBuiltins() {
        registerMethod("massif::TileDataSource", "loadTile", &loadTile);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevation", &getElevation);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevations", &getElevations);
        registerMethod("massif::MBVectorTileDecoder", "setStyleParameter", &setStyleParameter);
        registerMethod("massif::MBVectorTileDecoder", "getStyleParameter", &getStyleParameter);
        registerMethod("massif::TileLayer", "clearTileCaches", &clearTileCaches);
        registerMethod("massif::Layer", "refresh", &refresh);
        registerGeometryMethods();
#ifdef _MASSIF_SEARCH_SUPPORT
        registerMethod("massif::VectorTileSearchService", "findFeatures", &findVectorTileFeatures);
        registerMethod("massif::FeatureCollectionSearchService", "findFeatures",
                       &findCollectionFeatures);
#endif
    }

} }
