#include "api/Projections.h"
#include "api/Spec.h"
#include "api/StructCodec.h"
#include "api/Context.h"
#include "datasources/AssetTileDataSource.h"
#include "datasources/CombinedTileDataSource.h"
#include "datasources/HTTPTileDataSource.h"
#include "datasources/MemoryCacheTileDataSource.h"
#include "datasources/MultiTileDataSource.h"
#include "datasources/OrderedTileDataSource.h"
#include "geometry/GeoJSONGeometryReader.h"
#include "projections/Projection.h"
#include "layers/CompositeVectorTileLayer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "layers/RasterTileLayer.h"
#include "layers/SolidLayer.h"
#include "layers/VectorTileLayer.h"
#include "styles/CartoCSSStyleSet.h"
#include "styles/CompiledStyleSet.h"
#include "utils/DirAssetPackage.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "graphics/Color.h"
#include "utils/Log.h"

#include <map>

#ifdef _MASSIF_OFFLINE_SUPPORT
#include "datasources/MBTilesTileDataSource.h"
#include "datasources/PersistentCacheTileDataSource.h"
#endif

#ifdef _MASSIF_SEARCH_SUPPORT
#include "search/SearchRequest.h"
#include "search/VectorTileSearchService.h"
#endif

#ifdef _MASSIF_ROUTING_SUPPORT
#include "routing/RoutingRequest.h"
#include "routing/ValhallaOnlineRoutingService.h"
#endif
#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)
#include "routing/ValhallaOfflineRoutingService.h"
#endif

#include <memory>

namespace massif { namespace api {

    namespace {

        std::string stringAt(const Variant& spec, const char* key, const std::string& fallback = std::string()) {
            return spec.containsObjectKey(key) ? spec.getObjectElement(key).getString() : fallback;
        }

        int intAt(const Variant& spec, const char* key, int fallback) {
            return spec.containsObjectKey(key) ? static_cast<int>(spec.getObjectElement(key).getLong()) : fallback;
        }

        double floatAt(const Variant& spec, const char* key, double fallback) {
            return spec.containsObjectKey(key) ? spec.getObjectElement(key).getDouble() : fallback;
        }

        bool boolAt(const Variant& spec, const char* key, bool fallback) {
            return spec.containsObjectKey(key) ? spec.getObjectElement(key).getBool() : fallback;
        }

        Variant variantAt(const Variant& spec, const char* key) {
            return spec.containsObjectKey(key) ? spec.getObjectElement(key) : Variant();
        }

        /**
         * Resolves a nested source: an object is an anonymous child built here and now, a string
         * names something already in the registry.
         */
        Result childSource(Context& context, const Variant& spec, const char* key,
                           std::shared_ptr<TileDataSource>& source) {
            if (!spec.containsObjectKey(key)) {
                return RESULT_UNKNOWN_PROPERTY;
            }
            Variant child = spec.getObjectElement(key);
            if (child.getType() == VariantType::VARIANT_TYPE_STRING) {
                Handle handle = context.findObject("source", child.getString());
                source = std::static_pointer_cast<TileDataSource>(
                    context.getObject(handle, "massif::TileDataSource"));
                return source ? RESULT_OK : RESULT_BAD_HANDLE;
            }
            ObjectRef object;
            std::set<std::string> consumed;
            Result result = Spec::build(context, "source", child, object, consumed);
            if (result != RESULT_OK) {
                return result;
            }
            source = std::static_pointer_cast<TileDataSource>(object.obj);
            return RESULT_OK;
        }

        /**
         * Resolves a reference that is either a registry id or an inline spec of another kind.
         *
         * requiredClass is what the caller is about to cast to: a "style" id naming a source has
         * to be refused here, not cast and read as the wrong class.
         */
        Result childOf(Context& context, const Variant& spec, const char* key, const char* kind,
                       const char* requiredClass, std::shared_ptr<void>& out) {
            if (!spec.containsObjectKey(key)) {
                return RESULT_UNKNOWN_PROPERTY;
            }
            Variant child = spec.getObjectElement(key);
            if (child.getType() == VariantType::VARIANT_TYPE_STRING) {
                out = context.getObject(context.findObject(kind, child.getString()), requiredClass);
                return out ? RESULT_OK : RESULT_BAD_HANDLE;
            }
            ObjectRef object;
            std::set<std::string> consumed;
            Result result = Spec::build(context, kind, child, object, consumed);
            if (result != RESULT_OK) {
                return result;
            }
            if (!isSubclassOf(object.cppClass, requiredClass)) {
                return RESULT_UNKNOWN_CLASS;
            }
            out = object.obj;
            return RESULT_OK;
        }

        // Every class that declares a !spec in its .i, built from its own constructor signature.
        // Here because it calls childOf and the value helpers above.
        #include "api/SpecConstructors.inc"

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
         * A projection, by its well-known name.
         *
         * Needed to write one into a property - Options.baseProjection is the obvious case, and
         * without a way to BUILD a projection there is nothing to point it at. Uses the same name
         * registry the per-read projection argument does, so a plugin's projection is buildable
         * the moment it registers.
         */
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
         * A geometry, from GeoJSON.
         *
         * One factory rather than one per shape: the SDK already reads every type from GeoJSON, and
         * a binding that has coordinates at all has them in that form. This is what lets a search
         * be bounded - a request with no geometry scans the whole world at its zoom.
         */
        Result buildGeometry(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
            consumed.insert("type");
            consumed.insert("geojson");
            consumed.insert("projection");
            // Either a JSON string or the document itself - nesting one inside the other is not
            // something a binding should have to escape by hand.
            Variant raw = spec.getObjectElement("geojson");
            std::string geoJson = raw.getType() == VariantType::VARIANT_TYPE_STRING
                                ? raw.getString() : raw.toString();
            if (!spec.containsObjectKey("geojson") || geoJson.empty() || geoJson == "null") {
                Log::Error("Spec: a geometry needs a \"geojson\"");
                return RESULT_UNKNOWN_PROPERTY;
            }
            GeoJSONGeometryReader reader;
            // The coordinates are lon/lat by definition; a target projection says what to leave
            // them in, for a consumer that works in metres.
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

        /**
         * A search request and the service that runs it.
         *
         * Every filter on a request - the expression, the geometry, the radius - is already a
         * property, so the request needs nothing here beyond being constructible. The service is
         * the part with a constructor: it takes a source and a decoder, or the layer that has both,
         * which is how the app this API is measured against builds it.
         */
        /**
         * Only the shortcut is hand-written: a "vectortile" search built FROM A LAYER, which is
         * what the app this is measured against does - the source and the decoder both come from
         * the layer it is already showing, and no constructor signature says that.
         */
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

        /**
         * Only the request is hand-written: its projection is a NAME rather than a registry object
         * and its via points are a list of positions, neither of which a signature describes. The
         * services are plain constructors.
         */
        Result buildRouting(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            if (stringAt(spec, "type") != "request") {
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
            if (!StructCodec::decode(spec.getObjectElement("points").toString(), points) ||
                points.size() < 2) {
                Log::Error("Spec: a routing request needs at least two \"points\"");
                return RESULT_BAD_SPEC;
            }
            object.obj = std::make_shared<RoutingRequest>(projection, points);
            object.cppClass = "massif::RoutingRequest";
            return RESULT_OK;
        }

#endif

    }


    void Spec::registerBuiltinFactories() {
        registerFactory("source", &buildSource);
        registerFactory("assets", &buildAssets);
        registerFactory("styleset", &buildStyleSet);
        registerFactory("style", &buildStyle);
        registerFactory("layer", &buildLayer);
        registerFactory("projection", &buildProjection);
        registerFactory("geometry", &buildGeometry);
#ifdef _MASSIF_ROUTING_SUPPORT
        registerFactory("routing", &buildRouting);
#endif
#ifdef _MASSIF_SEARCH_SUPPORT
        registerFactory("search", &buildSearch);
#endif
    }

} }
