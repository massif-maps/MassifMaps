/*
 * The SDK's own methods, in their own translation unit.
 *
 * Kept out of Methods.cpp for the same reason SpecFactories is kept out of Spec.cpp: these pull in
 * every class they call, and a test should be able to link the registry without the whole SDK.
 */

#include "api/CameraMethods.h"
#include "api/DownloadMethods.h"
#include "api/GeocodingMethods.h"
#include "api/GeometryMethods.h"
#include "api/RoutingMethods.h"
#include "api/Methods.h"
#include "api/StructCodec.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "components/Layers.h"
#include "datasources/GeoJSONVectorTileDataSource.h"
#include "datasources/LocalVectorDataSource.h"
#include "datasources/MultiTileDataSource.h"
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

        /**
         * getMetaDataElement(key) -> the value, or null.
         *
         * Beside the whole-map 'metaData' property: a per-key read needs no round trip through
         * JSON for the entries the caller does not want.
         */
        Result getMetaDataElement(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string key;
            if (!args.getString(0, key)) {
                return RESULT_BAD_SPEC;
            }
            Variant value = static_cast<TileDataSource*>(obj)->getMetaDataElement(key);
            result = PropertyValue::ofString(StructCodec::encode(value));
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        /** setMetaDataElement(key, value) - one entry, without rewriting the whole map. */
        Result setMetaDataElement(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string key;
            if (!args.getString(0, key) || args.count() < 2) {
                return RESULT_BAD_SPEC;
            }
            static_cast<TileDataSource*>(obj)->setMetaDataElement(key, args.get(1));
            return RESULT_OK;
        }

        /** The same read on a loaded tile: behind a wrapper source, only the tile knows. */
        Result getTileMetaDataElement(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string key;
            if (!args.getString(0, key)) {
                return RESULT_BAD_SPEC;
            }
            Variant value = static_cast<TileData*>(obj)->getMetaDataElement(key);
            result = PropertyValue::ofString(StructCodec::encode(value));
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        /** WGS84: HillshadeRasterTileLayer reprojects to the data source itself. */
        Result getElevation(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            MapPos pos;
            if (!args.getPosWgs84(0, pos)) {
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
         *
         * WGS84, like getElevation: the layer reprojects to the data source itself.
         */
        Result getElevations(Context& context, void* obj, const CallArgs& args,
                             PropertyValue& result) {
            std::vector<MapPos> positions;
            if (!args.getPositionsWgs84(0, positions)) {
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

        /**
         * setStyleParameters({name: value, …}) - several at once, in one crossing.
         *
         * An app switching theme writes half a dozen together, and each one re-runs the style's
         * repaintability check; the SDK's own JSON form does that once for the whole set.
         */
        Result setStyleParameters(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            Variant params = args.get(0);
            if (params.getType() != VariantType::VARIANT_TYPE_OBJECT) {
                return RESULT_BAD_SPEC;
            }
            static_cast<MBVectorTileDecoder*>(obj)->setJSONStyleParameters(params.toString());
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

        /**
         * add(elementHandle) / remove(elementHandle) on a local source.
         *
         * The one thing a spec cannot express: a spec builds an object, it does not put it
         * anywhere. This is how a marker reaches the map.
         */
        Result addElement(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto element = std::static_pointer_cast<VectorElement>(
                context.getObject(handle, "massif::VectorElement"));
            if (!element) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<LocalVectorDataSource*>(obj)->add(element);
            return RESULT_OK;
        }

        Result removeElement(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto element = std::static_pointer_cast<VectorElement>(
                context.getObject(handle, "massif::VectorElement"));
            if (!element) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofBool(
                static_cast<LocalVectorDataSource*>(obj)->remove(element));
            return RESULT_OK;
        }

        /** add(layerHandle) / remove(layerHandle) - how a layer built from a spec reaches the map. */
        Result addLayer(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto layer = std::static_pointer_cast<Layer>(
                context.getObject(handle, "massif::Layer"));
            if (!layer) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<Layers*>(obj)->add(layer);
            return RESULT_OK;
        }

        Result removeLayer(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto layer = std::static_pointer_cast<Layer>(
                context.getObject(handle, "massif::Layer"));
            if (!layer) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofBool(static_cast<Layers*>(obj)->remove(layer));
            return RESULT_OK;
        }

        /**
         * insert(index, layerHandle) / set(index, layerHandle) / get(index) / clear().
         *
         * An app whose stack has an order - base, then overlays, then its own markers - places a
         * layer at an index rather than only on top, and swaps one in place when its decoder is
         * rebuilt. add() alone made both of those a remove-and-re-add of everything above.
         */
        Result insertLayer(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            long long index = 0;
            Handle handle = NULL_HANDLE;
            if (!args.getLong(0, index) || !args.getHandle(1, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto layer = std::static_pointer_cast<Layer>(context.getObject(handle, "massif::Layer"));
            if (!layer) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<Layers*>(obj)->insert(static_cast<int>(index), layer);
            return RESULT_OK;
        }

        Result setLayer(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            long long index = 0;
            Handle handle = NULL_HANDLE;
            if (!args.getLong(0, index) || !args.getHandle(1, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto layer = std::static_pointer_cast<Layer>(context.getObject(handle, "massif::Layer"));
            if (!layer) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<Layers*>(obj)->set(static_cast<int>(index), layer);
            return RESULT_OK;
        }

        Result getLayer(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            long long index = 0;
            auto layers = static_cast<Layers*>(obj);
            if (!args.getLong(0, index) || index < 0 || index >= layers->count()) {
                return RESULT_BAD_SPEC;
            }
            return objectResult(context, layers->get(static_cast<int>(index)),
                                "massif::Layer", result);
        }

        Result clearLayers(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<Layers*>(obj)->clear();
            return RESULT_OK;
        }

        /**
         * add(sourceHandle, tileMask) / remove(sourceHandle) on a MultiTileDataSource.
         *
         * One package per downloaded area, discovered at run time, so the list is filled after
         * construction. An empty tileMask means "read it off the package", which is what an
         * MBTiles or a merged source carries.
         */
        Result addSubSource(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            Handle handle = NULL_HANDLE;
            std::string tileMask;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            args.getString(1, tileMask);
            auto source = std::static_pointer_cast<TileDataSource>(
                context.getObject(handle, "massif::TileDataSource"));
            if (!source) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<MultiTileDataSource*>(obj)->add(source, tileMask);
            return RESULT_OK;
        }

        Result removeSubSource(Context& context, void* obj, const CallArgs& args,
                               PropertyValue& result) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto source = std::static_pointer_cast<TileDataSource>(
                context.getObject(handle, "massif::TileDataSource"));
            if (!source) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofBool(static_cast<MultiTileDataSource*>(obj)->remove(source));
            return RESULT_OK;
        }

        Result clearElements(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<LocalVectorDataSource*>(obj)->clear();
            return RESULT_OK;
        }

        /**
         * The GeoJSON source's layers: createLayer(name) -> index, then setLayerGeoJSON(index,
         * geojson) with the document as a string.
         *
         * A layer index rather than a name because that is what the SDK's own API takes, and
         * a string rather than a Variant because a binding has the document as text already -
         * parsing it into a Variant only to serialise it again is a round trip for nothing.
         */
        Result createGeoJSONLayer(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string name;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            try {
                result = PropertyValue::ofLong(
                    static_cast<GeoJSONVectorTileDataSource*>(obj)->createLayer(name));
            } catch (const std::exception& ex) {
                Log::Errorf("api createLayer: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result setGeoJSONLayer(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            long long index = 0;
            if (!args.getLong(0, index)) {
                return RESULT_BAD_SPEC;
            }
            // The document may arrive as a quoted string or as the JSON itself, which is what a
            // binding that built it with its own writer has.
            Variant raw = args.get(1);
            if (raw.getType() == VariantType::VARIANT_TYPE_NULL) {
                return RESULT_BAD_SPEC;
            }
            std::string geoJson = raw.getType() == VariantType::VARIANT_TYPE_STRING
                                ? raw.getString() : raw.toString();
            try {
                static_cast<GeoJSONVectorTileDataSource*>(obj)->setLayerGeoJSONString(
                    static_cast<int>(index), geoJson);
            } catch (const std::exception& ex) {
                Log::Errorf("api setLayerGeoJSON: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        /**
         * addFeature / updateFeature / removeFeature - one feature at a time.
         *
         * An app that edits a saved item otherwise re-encodes and re-tiles its whole document for
         * every change, which on a few hundred routes is the difference between instant and not.
         * `update` matches on the feature's own `id`, `remove` takes that id.
         */
        Result editGeoJSONFeature(void* obj, const CallArgs& args, bool update) {
            long long index = 0;
            if (!args.getLong(0, index)) {
                return RESULT_BAD_SPEC;
            }
            Variant raw = args.get(1);
            if (raw.getType() == VariantType::VARIANT_TYPE_NULL) {
                return RESULT_BAD_SPEC;
            }
            std::string geoJson = raw.getType() == VariantType::VARIANT_TYPE_STRING
                                ? raw.getString() : raw.toString();
            auto source = static_cast<GeoJSONVectorTileDataSource*>(obj);
            try {
                if (update) {
                    source->updateGeoJSONStringFeature(static_cast<int>(index), geoJson);
                } else {
                    source->addGeoJSONStringFeature(static_cast<int>(index), geoJson);
                }
            } catch (const std::exception& ex) {
                Log::Errorf("api %s: %s", update ? "updateFeature" : "addFeature", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result addGeoJSONFeature(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            return editGeoJSONFeature(obj, args, false);
        }

        Result updateGeoJSONFeature(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            return editGeoJSONFeature(obj, args, true);
        }

        Result removeGeoJSONFeature(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            long long index = 0;
            if (!args.getLong(0, index) || args.count() < 2) {
                return RESULT_BAD_SPEC;
            }
            try {
                static_cast<GeoJSONVectorTileDataSource*>(obj)->removeGeoJSONFeature(
                    static_cast<int>(index), args.get(1));
            } catch (const std::exception& ex) {
                Log::Errorf("api removeFeature: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result deleteGeoJSONLayer(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            long long index = 0;
            if (!args.getLong(0, index)) {
                return RESULT_BAD_SPEC;
            }
            try {
                static_cast<GeoJSONVectorTileDataSource*>(obj)->deleteLayer(
                    static_cast<int>(index));
            } catch (const std::exception& ex) {
                Log::Errorf("api deleteLayer: %s", ex.what());
                return RESULT_FAILED;
            }
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
        registerMethod("massif::TileDataSource", "getMetaDataElement", &getMetaDataElement);
        registerMethod("massif::TileDataSource", "setMetaDataElement", &setMetaDataElement);
        registerMethod("massif::TileData", "getMetaDataElement", &getTileMetaDataElement);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevation", &getElevation);
        registerMethod("massif::HillshadeRasterTileLayer", "getElevations", &getElevations);
        registerMethod("massif::MBVectorTileDecoder", "setStyleParameter", &setStyleParameter);
        registerMethod("massif::MBVectorTileDecoder", "setStyleParameters", &setStyleParameters);
        registerMethod("massif::MBVectorTileDecoder", "getStyleParameter", &getStyleParameter);
        registerMethod("massif::TileLayer", "clearTileCaches", &clearTileCaches);
        registerMethod("massif::Layer", "refresh", &refresh);
        registerMethod("massif::Layers", "add", &addLayer);
        registerMethod("massif::Layers", "remove", &removeLayer);
        registerMethod("massif::Layers", "insert", &insertLayer);
        registerMethod("massif::Layers", "set", &setLayer);
        registerMethod("massif::Layers", "get", &getLayer);
        registerMethod("massif::Layers", "clear", &clearLayers);
        registerMethod("massif::MultiTileDataSource", "add", &addSubSource);
        registerMethod("massif::MultiTileDataSource", "remove", &removeSubSource);
        registerMethod("massif::LocalVectorDataSource", "add", &addElement);
        registerMethod("massif::LocalVectorDataSource", "remove", &removeElement);
        registerMethod("massif::LocalVectorDataSource", "clear", &clearElements);
        registerMethod("massif::GeoJSONVectorTileDataSource", "createLayer", &createGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "setLayerGeoJSON", &setGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "deleteLayer", &deleteGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "addFeature", &addGeoJSONFeature);
        registerMethod("massif::GeoJSONVectorTileDataSource", "updateFeature", &updateGeoJSONFeature);
        registerMethod("massif::GeoJSONVectorTileDataSource", "removeFeature", &removeGeoJSONFeature);
        registerGeometryMethods();
        registerCameraMethods();
#ifdef _MASSIF_SEARCH_SUPPORT
        registerMethod("massif::VectorTileSearchService", "findFeatures", &findVectorTileFeatures);
        registerMethod("massif::FeatureCollectionSearchService", "findFeatures",
                       &findCollectionFeatures);
#endif
#ifdef _MASSIF_ROUTING_SUPPORT
        registerRoutingMethods();
#endif
#ifdef _MASSIF_GEOCODING_SUPPORT
        registerGeocodingMethods();
#endif
#ifdef _MASSIF_OFFLINE_SUPPORT
        registerDownloadMethods();
#endif
        // Everything registered above has to be declared in a .i too, or no binding can complete
        // it and no generated reference lists it.
        checkDeclarations();
    }

} }
