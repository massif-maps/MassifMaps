// The SDK's own methods, kept out of Methods.cpp so a test can link the registry without the whole SDK.

#include "api/CameraMethods.h"
#include "api/DownloadMethods.h"
#include "api/GeocodingMethods.h"
#include "api/GeometryMethods.h"
#include "api/RoutingMethods.h"
#include "api/Methods.h"
#include "api/PropertyTable.h"
#include "api/StructCodec.h"
#include "core/BinaryData.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "components/Layers.h"
#include "components/LightOptions.h"
#include "components/TerrainOptions.h"
#include "celestial/CelestialArc.h"
#include "celestial/CelestialImage.h"
#include "celestial/CelestialLabel.h"
#include "celestial/CelestialObject.h"
#include "layers/CelestialLayer.h"
#include "terrain/ElevationManager.h"
#include "datasources/GeoJSONVectorTileDataSource.h"
#include "datasources/LocalVectorDataSource.h"
#include "datasources/MultiTileDataSource.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "layers/CompositeVectorTileLayer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "layers/Layer.h"
#include "layers/TileLayer.h"
#include "renderers/PostProcessEffect.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "vectortiles/VectorTileDecoder.h"
#include "utils/Log.h"

#include <typeinfo>

#ifdef _MASSIF_SEARCH_SUPPORT
#include "search/FeatureCollectionSearchService.h"
#include "search/SearchRequest.h"
#include "search/VectorTileSearchService.h"
#endif


namespace massif { namespace api {

    namespace {

        /** loadTile([x, y, zoom]) -> TileData handle or null. Fetches synchronously: use callAsync. */
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

        /** getMetaDataElement(key) -> the value, or null. */
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
         * getElevations([[x, y], ...]) WGS84 -> handle onto a flat array of metres; read with getDoubles.
         * A handle, not a value: a track profile is thousands of numbers.
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

        // CartoCSS style parameters: a live theme switch without re-decoding.
        Result setStyleParameter(Context&, void* obj, const CallArgs& args, PropertyValue& result) {
            std::string name, value;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            // The SDK takes the value as text, so a number is stringified rather than refused.
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

        /** setStyleParameters({name: value, …}): the repaintability check runs once for the set, not per key. */
        Result setStyleParameters(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            Variant params = args.get(0);
            if (params.getType() != VariantType::VARIANT_TYPE_OBJECT) {
                return RESULT_BAD_SPEC;
            }
            static_cast<MBVectorTileDecoder*>(obj)->setJSONStyleParameters(params.toString());
            return RESULT_OK;
        }

        /** addFallbackFont(dataHandle): a spec cannot carry font bytes, so it arrives as a `data` handle. */
        Result addFallbackFont(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto font = std::static_pointer_cast<BinaryData>(
                context.getObject(handle, "massif::BinaryData"));
            if (!font) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<MBVectorTileDecoder*>(obj)->addFallbackFont(font);
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

        /** setSunPositionFromTime(year, month, day, hour, minute, latitude, longitude); pass the map centre. */
        Result setSunPositionFromTime(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            long long year = 0, month = 0, day = 0, hour = 0, minute = 0;
            double latitude = 0, longitude = 0;
            if (!args.getLong(0, year) || !args.getLong(1, month) || !args.getLong(2, day) ||
                !args.getLong(3, hour) || !args.getLong(4, minute) ||
                !args.getDouble(5, latitude) || !args.getDouble(6, longitude)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<LightOptions*>(obj)->setSunPositionFromTime(
                static_cast<int>(year), static_cast<int>(month), static_cast<int>(day),
                static_cast<int>(hour), static_cast<int>(minute), latitude, longitude);
            return RESULT_OK;
        }

        /** clearTileCaches(all) - true also drops the persistent cache, not just the in-memory one. */
        Result clearTileCaches(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            bool all = false;
            args.getBool(0, all);
            static_cast<TileLayer*>(obj)->clearTileCaches(all);
            return RESULT_OK;
        }

        /** add(elementHandle) / remove(elementHandle) on a local source: a spec builds, it does not place. */
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

        /** insert(index, layerHandle) / set(index, layerHandle) / get(index) / clear(). */
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

        /** add(sourceHandle, tileMask) / remove(sourceHandle) on a MultiTileDataSource; empty tileMask = read off the package. */
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

        // A JSON array of numbers, as an argument - a direction list or a set of azimuths.
        bool getNumbers(const CallArgs& args, int index, std::vector<double>& numbers) {
            Variant list = args.get(index);
            if (list.getType() != VariantType::VARIANT_TYPE_ARRAY) {
                return false;
            }
            numbers.clear();
            for (int i = 0; i < list.getArraySize(); i++) {
                Variant number = list.getArrayElement(i);
                if (number.getType() == VariantType::VARIANT_TYPE_INTEGER) {
                    numbers.push_back(static_cast<double>(number.getLong()));
                } else if (number.getType() == VariantType::VARIANT_TYPE_DOUBLE) {
                    numbers.push_back(number.getDouble());
                } else {
                    return false;
                }
            }
            return true;
        }

        /** add(objectHandle) / remove(objectHandle) / clear() on a sky layer. */
        Result addCelestialObject(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto object = std::static_pointer_cast<CelestialObject>(context.getObject(handle, "massif::CelestialObject"));
            if (!object) {
                return RESULT_BAD_HANDLE;
            }
            static_cast<CelestialLayer*>(obj)->add(object);
            return RESULT_OK;
        }

        Result removeCelestialObject(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            Handle handle = NULL_HANDLE;
            if (!args.getHandle(0, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto object = std::static_pointer_cast<CelestialObject>(context.getObject(handle, "massif::CelestialObject"));
            if (!object) {
                return RESULT_BAD_HANDLE;
            }
            result = PropertyValue::ofBool(static_cast<CelestialLayer*>(obj)->remove(object));
            return RESULT_OK;
        }

        Result clearCelestialObjects(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<CelestialLayer*>(obj)->clear();
            return RESULT_OK;
        }

        /** setDirection(azimuth, altitude, distance) - distance 0 is infinitely far. */
        Result setCelestialDirection(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            double azimuth = 0, altitude = 0, distance = 0;
            if (!args.getDouble(0, azimuth) || !args.getDouble(1, altitude) || !args.getDouble(2, distance)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialObject*>(obj)->setDirection(static_cast<float>(azimuth), static_cast<float>(altitude), distance);
            return RESULT_OK;
        }

        /** setDirections([az0, alt0, az1, alt1, ...]) - a path through the sky. */
        Result setArcDirections(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::vector<double> directions;
            if (!getNumbers(args, 0, directions) || directions.size() % 2 != 0) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialArc*>(obj)->setDirections(directions);
            return RESULT_OK;
        }

        /** setSegments([...]) - the same list read as disjoint pairs of directions. */
        Result setArcSegments(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::vector<double> directions;
            if (!getNumbers(args, 0, directions) || directions.size() % 4 != 0) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialArc*>(obj)->setSegments(directions);
            return RESULT_OK;
        }

        /** setCircle(axisAzimuth, axisAltitude, radius) - a body's daily path is one. */
        Result setArcCircle(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            double axisAzimuth = 0, axisAltitude = 0, radius = 0;
            if (!args.getDouble(0, axisAzimuth) || !args.getDouble(1, axisAltitude) || !args.getDouble(2, radius)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialArc*>(obj)->setCircle(static_cast<float>(axisAzimuth), static_cast<float>(axisAltitude), static_cast<float>(radius));
            return RESULT_OK;
        }

        /** setAnchors([u, v, az, alt] x 3) - three points of a sky image pinned to three directions. */
        Result setImageAnchors(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::vector<double> anchors;
            if (!getNumbers(args, 0, anchors) || anchors.size() != 12) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialImage*>(obj)->setAnchors(anchors);
            return RESULT_OK;
        }

        /** setAnchorPoint(x, y) - which point of a sky label sits on its direction, -1..1 each. */
        Result setLabelAnchorPoint(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            double x = 0, y = 0;
            if (!args.getDouble(0, x) || !args.getDouble(1, y)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialLabel*>(obj)->setAnchorPoint(static_cast<float>(x), static_cast<float>(y));
            return RESULT_OK;
        }

        /** setOffset(x, y) - a sky label moved on screen from its anchor, dp, y up. */
        Result setLabelOffset(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            double x = 0, y = 0;
            if (!args.getDouble(0, x) || !args.getDouble(1, y)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<CelestialLabel*>(obj)->setOffset(static_cast<float>(x), static_cast<float>(y));
            return RESULT_OK;
        }

        /** setSurfaceParameter(name, value) - a float uniform of the terrain's surface shader. */
        Result setSurfaceParameter(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string name;
            double value = 0;
            if (!args.getString(0, name) || !args.getDouble(1, value)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<TerrainOptions*>(obj)->setSurfaceParameter(name, static_cast<float>(value));
            return RESULT_OK;
        }

        /** setFloatParameter(name, value) - a float uniform of a post-process effect. */
        Result setEffectFloatParameter(Context&, void* obj, const CallArgs& args, PropertyValue&) {
            std::string name;
            double value = 0;
            if (!args.getString(0, name) || !args.getDouble(1, value)) {
                return RESULT_BAD_SPEC;
            }
            static_cast<PostProcessEffect*>(obj)->setFloatParameter(name, static_cast<float>(value));
            return RESULT_OK;
        }

        /**
         * calculateHorizon(pos, eyeHeight, [azimuths], maxDistance) -> a handle onto one apparent
         * altitude per azimuth (ElevationManager::calculateHorizon). WGS84, like getElevation.
         */
        Result calculateHorizon(Context& context, void* obj, const CallArgs& args, PropertyValue& result) {
            MapPos pos;
            double eyeHeight = 0, maxDistance = 0;
            std::vector<double> azimuths;
            if (!args.getPosWgs84(0, pos) || !args.getDouble(1, eyeHeight) || !getNumbers(args, 2, azimuths) || !args.getDouble(3, maxDistance)) {
                return RESULT_BAD_SPEC;
            }
            std::shared_ptr<ElevationManager> elevationManager = static_cast<TerrainOptions*>(obj)->getElevationManager();
            if (!elevationManager) {
                return RESULT_FAILED;
            }
            auto horizon = std::make_shared<std::vector<double> >(elevationManager->calculateHorizon(pos, eyeHeight, azimuths, maxDistance));
            return objectResult(context, horizon, Context::DOUBLE_VECTOR_CLASS, result);
        }

        /**
         * createLayer(name) -> index, then setLayerGeoJSON(index, geojson). The document stays a string:
         * a binding already has it as text, so a Variant would be a pointless parse/serialise round trip.
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
            // Accepts a quoted string or the JSON itself.
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
         * addFeature / updateFeature / removeFeature: one feature without re-tiling the whole document.
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

        // Methods, not spec keys: a slot's source is usually shared (e.g. the DEM) and changes at run time.
        Result addExternalDataSource(Context& context, void* obj, const CallArgs& args,
                                     PropertyValue&) {
            std::string name;
            Handle handle = NULL_HANDLE;
            long long type = 0;
            if (!args.getString(0, name) || !args.getHandle(1, handle) || !args.getLong(2, type)) {
                return RESULT_BAD_SPEC;
            }
            auto source = std::static_pointer_cast<TileDataSource>(
                context.getObject(handle, "massif::TileDataSource"));
            if (!source) {
                return RESULT_BAD_HANDLE;
            }
            try {
                static_cast<CompositeVectorTileLayer*>(obj)->addExternalDataSource(
                    name, source,
                    static_cast<CompositeSourceType::CompositeSourceType>(type));
            } catch (const std::exception& ex) {
                Log::Errorf("api addExternalDataSource: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result addVectorDataSource(Context& context, void* obj, const CallArgs& args,
                                   PropertyValue&) {
            std::string name;
            Handle handle = NULL_HANDLE;
            if (!args.getString(0, name) || !args.getHandle(1, handle)) {
                return RESULT_BAD_SPEC;
            }
            auto source = std::static_pointer_cast<TileDataSource>(
                context.getObject(handle, "massif::TileDataSource"));
            if (!source) {
                return RESULT_BAD_HANDLE;
            }
            try {
                static_cast<CompositeVectorTileLayer*>(obj)->addVectorDataSource(name, source);
            } catch (const std::exception& ex) {
                Log::Errorf("api addVectorDataSource: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result removeExternalDataSource(Context&, void* obj, const CallArgs& args,
                                        PropertyValue& result) {
            std::string name;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            result = PropertyValue::ofBool(
                static_cast<CompositeVectorTileLayer*>(obj)->removeExternalDataSource(name));
            return RESULT_OK;
        }

        /** getExternalDataSourceNames() -> the registered slot names, as a JSON array. */
        Result getExternalDataSourceNames(Context&, void* obj, const CallArgs&,
                                          PropertyValue& result) {
            std::vector<Variant> names;
            for (const std::string& name :
                 static_cast<CompositeVectorTileLayer*>(obj)->getExternalDataSourceNames()) {
                names.push_back(Variant(name));
            }
            result = PropertyValue::ofString(StructCodec::encode(Variant(names)));
            result.type = PT_VARIANT;
            return RESULT_OK;
        }

        Result setExternalDataSourceZoomLevelBias(Context&, void* obj, const CallArgs& args,
                                                  PropertyValue&) {
            std::string name;
            double bias = 0.0;
            if (!args.getString(0, name) || !args.getDouble(1, bias)) {
                return RESULT_BAD_SPEC;
            }
            try {
                static_cast<CompositeVectorTileLayer*>(obj)->setExternalDataSourceZoomLevelBias(
                    name, static_cast<float>(bias));
            } catch (const std::exception& ex) {
                Log::Errorf("api setExternalDataSourceZoomLevelBias: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result setExternalDataSourceMaxOverzoomLevel(Context&, void* obj, const CallArgs& args,
                                                     PropertyValue&) {
            std::string name;
            long long level = 0;
            if (!args.getString(0, name) || !args.getLong(1, level)) {
                return RESULT_BAD_SPEC;
            }
            try {
                static_cast<CompositeVectorTileLayer*>(obj)->setExternalDataSourceMaxOverzoomLevel(
                    name, static_cast<int>(level));
            } catch (const std::exception& ex) {
                Log::Errorf("api setExternalDataSourceMaxOverzoomLevel: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        /**
         * getExternalChildLayer(name) -> the layer drawing that slot, or RESULT_FAILED.
         * Returned under its concrete class so the child's own (e.g. hillshade) properties resolve.
         */
        Result getExternalChildLayer(Context& context, void* obj, const CallArgs& args,
                                     PropertyValue& result) {
            std::string name;
            if (!args.getString(0, name)) {
                return RESULT_BAD_SPEC;
            }
            std::shared_ptr<Layer> child =
                static_cast<CompositeVectorTileLayer*>(obj)->getExternalChildLayer(name);
            if (!child) {
                return RESULT_FAILED;
            }
            return objectResult(context, child, concreteClass(typeid(*child), "massif::Layer"),
                                result);
        }

#ifdef _MASSIF_SEARCH_SUPPORT

        /** findFeatures([requestHandle]) -> a feature collection handle; the request is a "search"/"request" object. */
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
        registerMethod("massif::MBVectorTileDecoder", "addFallbackFont", &addFallbackFont);
        registerMethod("massif::LightOptions", "setSunPositionFromTime", &setSunPositionFromTime);
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
        registerMethod("massif::CelestialLayer", "add", &addCelestialObject);
        registerMethod("massif::CelestialLayer", "remove", &removeCelestialObject);
        registerMethod("massif::CelestialLayer", "clear", &clearCelestialObjects);
        registerMethod("massif::CelestialObject", "setDirection", &setCelestialDirection);
        registerMethod("massif::CelestialArc", "setDirections", &setArcDirections);
        registerMethod("massif::CelestialArc", "setSegments", &setArcSegments);
        registerMethod("massif::CelestialArc", "setCircle", &setArcCircle);
        registerMethod("massif::CelestialImage", "setAnchors", &setImageAnchors);
        registerMethod("massif::CelestialLabel", "setAnchorPoint", &setLabelAnchorPoint);
        registerMethod("massif::CelestialLabel", "setOffset", &setLabelOffset);
        registerMethod("massif::TerrainOptions", "calculateHorizon", &calculateHorizon);
        registerMethod("massif::TerrainOptions", "setSurfaceParameter", &setSurfaceParameter);
        registerMethod("massif::PostProcessEffect", "setFloatParameter", &setEffectFloatParameter);
        registerMethod("massif::GeoJSONVectorTileDataSource", "createLayer", &createGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "setLayerGeoJSON", &setGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "deleteLayer", &deleteGeoJSONLayer);
        registerMethod("massif::GeoJSONVectorTileDataSource", "addFeature", &addGeoJSONFeature);
        registerMethod("massif::GeoJSONVectorTileDataSource", "updateFeature", &updateGeoJSONFeature);
        registerMethod("massif::GeoJSONVectorTileDataSource", "removeFeature", &removeGeoJSONFeature);
        registerMethod("massif::CompositeVectorTileLayer", "addExternalDataSource",
                       &addExternalDataSource);
        registerMethod("massif::CompositeVectorTileLayer", "addVectorDataSource",
                       &addVectorDataSource);
        registerMethod("massif::CompositeVectorTileLayer", "removeExternalDataSource",
                       &removeExternalDataSource);
        registerMethod("massif::CompositeVectorTileLayer", "getExternalDataSourceNames",
                       &getExternalDataSourceNames);
        registerMethod("massif::CompositeVectorTileLayer", "setExternalDataSourceZoomLevelBias",
                       &setExternalDataSourceZoomLevelBias);
        registerMethod("massif::CompositeVectorTileLayer", "setExternalDataSourceMaxOverzoomLevel",
                       &setExternalDataSourceMaxOverzoomLevel);
        registerMethod("massif::CompositeVectorTileLayer", "getExternalChildLayer",
                       &getExternalChildLayer);
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
        // Every method registered above must also be declared in a .i, or no binding or reference sees it.
        checkDeclarations();
    }

} }
