#include "api/Context.h"
#include "api/ElementSpecs.h"
#include "api/Projections.h"
#include "api/Spec.h"
#include "api/SpecBuilders.h"
#include "api/StructCodec.h"
#include "core/BinaryData.h"
#include "graphics/Bitmap.h"
#include "geometry/GeoJSONGeometryReader.h"
#include "utils/URLFileLoader.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <memory>
#include <set>

// Only what the adaptive factories construct themselves; constructor-described classes build in SpecBuilders.cpp.
#ifdef _MASSIF_SEARCH_SUPPORT
#include "layers/VectorTileLayer.h"
#include "search/VectorTileSearchService.h"
#endif

#ifdef _MASSIF_ROUTING_SUPPORT
#include "routing/RouteMatchingRequest.h"
#include "routing/RoutingRequest.h"
#endif

#ifdef _MASSIF_GEOCODING_SUPPORT
#include "geocoding/GeocodingRequest.h"
#include "geocoding/ReverseGeocodingRequest.h"
#endif

namespace massif { namespace api {

    namespace {

        Result buildSource(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
            return buildFromConstructor(context, "source", spec, object, consumed);
        }

        Result buildAssets(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
            return buildFromConstructor(context, "assets", spec, object, consumed);
        }

        Result buildStyleSet(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
            return buildFromConstructor(context, "styleset", spec, object, consumed);
        }

        Result buildStyle(Context& context, const Variant& spec, ObjectRef& object,
                          std::set<std::string>& consumed) {
            return buildFromConstructor(context, "style", spec, object, consumed);
        }

        Result buildLayer(Context& context, const Variant& spec, ObjectRef& object,
                          std::set<std::string>& consumed) {
            return buildFromConstructor(context, "layer", spec, object, consumed);
        }

        /**
         * The map's option sub-objects: "terrain", "fog", "sky", "light"; build one, then set it on Options.
         * Terrain's elevation decoder comes from the source's `metaData.dem_encoding`, not the spec.
         */
        Result buildOptions(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            return buildFromConstructor(context, "options", spec, object, consumed);
        }

        /** A projection by name, from the same registry as the per-read projection argument (plugins included). */
        Result buildProjection(Context&, const Variant& spec, ObjectRef& object,
                               std::set<std::string>& consumed) {
            consumed.insert("type");
            std::string type = spec.getObjectElement("type").getString();
            std::shared_ptr<Projection> projection = Projections::find(type);
            if (!projection) {
                Log::Errorf("Spec: no projection named '%s'", type.c_str());
                return RESULT_UNKNOWN_TYPE;
            }
            object.obj = projection;
            object.cppClass = "massif::Projection";
            return RESULT_OK;
        }

        /**
         * Bytes from a file://, assets:// or http(s):// URL. Local files are allowed: a spec is written
         * by the app, not tile data. A remote URL is fetched on the calling thread; keep it off the UI thread.
         */
        Result buildData(Context&, const Variant& spec, ObjectRef& object,
                         std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type", "url");
            consumed.insert("type");
            consumed.insert("url");
            if (type != "url") {
                Log::Errorf("Spec: no data type '%s'", type.c_str());
                return RESULT_UNKNOWN_TYPE;
            }
            std::string url = stringAt(spec, "url");
            if (url.empty()) {
                Log::Error("Spec: a data spec needs a \"url\"");
                return RESULT_UNKNOWN_PROPERTY;
            }
            URLFileLoader loader;
            loader.setLocalFiles(true);
            std::shared_ptr<BinaryData> data;
            if (!loader.load(url, data) || !data) {
                Log::Errorf("Spec: could not read '%s'", url.c_str());
                return RESULT_FAILED;
            }
            object.obj = data;
            object.cppClass = "massif::BinaryData";
            return RESULT_OK;
        }

        Result buildCelestial(Context& context, const Variant& spec, ObjectRef& object,
                              std::set<std::string>& consumed) {
            return buildFromConstructor(context, "celestial", spec, object, consumed);
        }

        Result buildFeature(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            return buildFromConstructor(context, "feature", spec, object, consumed);
        }

        /** An image decoded from bytes; hand-written because Bitmap is built by a static factory, not a constructor. */
        Result buildBitmap(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
            std::shared_ptr<void> data;
            // Either an inline { "type": "url", … } or the id of a registered `data`.
            Result result = childOf(context, spec, "data", "data", "massif::BinaryData", data);
            if (result != RESULT_OK) {
                // A bare url is the common case, so accept it without the wrapper.
                Variant inlineSpec = spec;
                if (!spec.containsObjectKey("url")) {
                    Log::Error("Spec: a bitmap needs a \"url\" or a \"data\"");
                    return RESULT_UNKNOWN_PROPERTY;
                }
                ObjectRef bytes;
                std::set<std::string> dataConsumed;
                result = buildData(context, inlineSpec, bytes, dataConsumed);
                if (result != RESULT_OK) {
                    return result;
                }
                consumed.insert("url");
                data = bytes.obj;
            }
            consumed.insert("type");
            consumed.insert("data");
            std::shared_ptr<Bitmap> bitmap =
                Bitmap::CreateFromCompressed(std::static_pointer_cast<BinaryData>(data));
            if (!bitmap) {
                Log::Error("Spec: the bytes are not an image the SDK can read");
                return RESULT_BAD_SPEC;
            }
            object.obj = bitmap;
            object.cppClass = "massif::Bitmap";
            return RESULT_OK;
        }

        /** A geometry, from GeoJSON; lets a search be bounded (no geometry scans the whole world). */
        Result buildGeometry(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
            // Only "geojson" is adaptive; a shape with its own constructor builds from that.
            if (stringAt(spec, "type") != "geojson") {
                return buildFromConstructor(context, "geometry", spec, object, consumed);
            }
            consumed.insert("type");
            consumed.insert("geojson");
            consumed.insert("projection");
            // Either a JSON string or the document itself, so a binding need not escape it.
            Variant raw = spec.getObjectElement("geojson");
            std::string geoJson = raw.getType() == VariantType::VARIANT_TYPE_STRING
                                ? raw.getString() : raw.toString();
            if (!spec.containsObjectKey("geojson") || geoJson.empty() || geoJson == "null") {
                Log::Error("Spec: a geometry needs a \"geojson\"");
                return RESULT_UNKNOWN_PROPERTY;
            }
            GeoJSONGeometryReader reader;
            // GeoJSON is lon/lat; an optional target projection converts for consumers working in metres.
            if (spec.containsObjectKey("projection")) {
                std::shared_ptr<Projection> projection =
                    Projections::find(spec.getObjectElement("projection").getString());
                if (!projection) {
                    return RESULT_UNKNOWN_TYPE;
                }
                reader.setTargetProjection(projection);
            }
            std::shared_ptr<Geometry> geometry;
            try {
                geometry = reader.readGeometry(geoJson);
            } catch (const std::exception& ex) {
                Log::Errorf("Spec: unreadable geojson: %s", ex.what());
                return RESULT_BAD_SPEC;
            }
            if (!geometry) {
                return RESULT_BAD_SPEC;
            }
            object.obj = geometry;
            object.cppClass = "massif::Geometry";
            (void)context;
            return RESULT_OK;
        }

#ifdef _MASSIF_SEARCH_SUPPORT

        // Only the shortcut is hand-written: a "vectortile" search built from a layer's source and decoder.
        Result buildSearch(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
            if (stringAt(spec, "type") != "vectortile" || !spec.containsObjectKey("layer")) {
                return buildFromConstructor(context, "search", spec, object, consumed);
            }
            consumed.insert("type");
            consumed.insert("layer");
            std::shared_ptr<void> child;
            Result result = childOf(context, spec, "layer", "layer", "massif::VectorTileLayer", child);
            if (result != RESULT_OK) {
                return result;
            }
            auto layer = std::static_pointer_cast<VectorTileLayer>(child);
            object.obj = std::make_shared<VectorTileSearchService>(layer->getDataSource(),
                                                                   layer->getTileDecoder());
            object.cppClass = "massif::VectorTileSearchService";
            return RESULT_OK;
        }

#endif

#ifdef _MASSIF_ROUTING_SUPPORT

        // Only the requests are hand-written: projection by name and a points list, which no signature describes.
        Result buildRouting(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type");
            bool matching = type == "match-request";
            if (type != "request" && !matching) {
                return buildFromConstructor(context, "routing", spec, object, consumed);
            }
            consumed.insert("type");
            consumed.insert("points");
            consumed.insert("projection");
            std::shared_ptr<Projection> projection =
                Projections::find(stringAt(spec, "projection", "EPSG:4326"));
            if (!projection) {
                return RESULT_UNKNOWN_TYPE;
            }
            std::vector<MapPos> points;
            // A match request traces a track, so it needs a line too.
            if (!StructCodec::decode(spec.getObjectElement("points").toString(), points) ||
                points.size() < 2) {
                Log::Error("Spec: a routing request needs at least two \"points\"");
                return RESULT_BAD_SPEC;
            }
            if (matching) {
                consumed.insert("accuracy");
                object.obj = std::make_shared<RouteMatchingRequest>(
                    projection, points, static_cast<float>(floatAt(spec, "accuracy", 1)));
                object.cppClass = "massif::RouteMatchingRequest";
                return RESULT_OK;
            }
            object.obj = std::make_shared<RoutingRequest>(projection, points);
            object.cppClass = "massif::RoutingRequest";
            return RESULT_OK;
        }

#endif

#ifdef _MASSIF_GEOCODING_SUPPORT

        // Only the requests are hand-written, as for routing: projection by name, a reverse request carries a position.
        Result buildGeocoding(Context& context, const Variant& spec, ObjectRef& object,
                              std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type");
            bool reverse = type == "reverse-request";
            if (type != "request" && !reverse) {
                return buildFromConstructor(context, "geocoding", spec, object, consumed);
            }
            consumed.insert("type");
            consumed.insert("projection");
            std::shared_ptr<Projection> projection =
                Projections::find(stringAt(spec, "projection", "EPSG:4326"));
            if (!projection) {
                return RESULT_UNKNOWN_TYPE;
            }
            if (reverse) {
                consumed.insert("location");
                MapPos location;
                if (!StructCodec::decode(spec.getObjectElement("location").toString(), location)) {
                    Log::Error("Spec: a reverse geocoding request needs a \"location\"");
                    return RESULT_BAD_SPEC;
                }
                object.obj = std::make_shared<ReverseGeocodingRequest>(projection, location);
                object.cppClass = "massif::ReverseGeocodingRequest";
                return RESULT_OK;
            }
            consumed.insert("query");
            object.obj = std::make_shared<GeocodingRequest>(projection, stringAt(spec, "query"));
            object.cppClass = "massif::GeocodingRequest";
            return RESULT_OK;
        }

#endif

    }


    void Spec::registerBuiltinFactories() {
        registerFactory("source", &buildSource);
        registerFactory("data", &buildData);
        registerFactory("assets", &buildAssets);
        registerFactory("styleset", &buildStyleSet);
        registerFactory("style", &buildStyle);
        registerFactory("layer", &buildLayer);
        registerFactory("options", &buildOptions);
        registerFactory("projection", &buildProjection);
        registerFactory("geometry", &buildGeometry);
        registerFactory("bitmap", &buildBitmap);
        registerElementFactories();
        registerFactory("feature", &buildFeature);
        registerFactory("celestial", &buildCelestial);
#ifdef _MASSIF_ROUTING_SUPPORT
        registerFactory("routing", &buildRouting);
#endif
#ifdef _MASSIF_SEARCH_SUPPORT
        registerFactory("search", &buildSearch);
#endif
#ifdef _MASSIF_GEOCODING_SUPPORT
        registerFactory("geocoding", &buildGeocoding);
#endif

        // A !spec kind with no factory here is unreachable; report it at startup, not at first use.
        for (const char* const* kind = SPEC_KINDS; *kind; kind++) {
            if (!hasFactory(*kind)) {
                Log::Errorf("Spec: kind '%s' has generated builders but no factory - "
                            "add one in SpecFactories.cpp", *kind);
            }
        }
    }

} }
