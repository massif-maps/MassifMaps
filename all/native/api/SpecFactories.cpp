#include "api/Context.h"
#include "api/Projections.h"
#include "api/Spec.h"
#include "api/SpecBuilders.h"
#include "api/StructCodec.h"
#include "geometry/GeoJSONGeometryReader.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <memory>
#include <set>

// Only what the ADAPTIVE factories construct themselves. Everything a constructor signature
// describes is built in SpecBuilders.cpp, which is where those headers went.
#ifdef _MASSIF_SEARCH_SUPPORT
#include "layers/VectorTileLayer.h"
#include "search/VectorTileSearchService.h"
#endif

#ifdef _MASSIF_ROUTING_SUPPORT
#include "routing/RoutingRequest.h"
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
        Result buildFeature(Context& context, const Variant& spec, ObjectRef& object,
                            std::set<std::string>& consumed) {
            return buildFromConstructor(context, "feature", spec, object, consumed);
        }

        Result buildGeometry(Context& context, const Variant& spec, ObjectRef& object,
                             std::set<std::string>& consumed) {
            // Only "geojson" is adaptive; a shape with its own constructor builds from that.
            if (stringAt(spec, "type") != "geojson") {
                return buildFromConstructor(context, "geometry", spec, object, consumed);
            }
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
        registerFactory("feature", &buildFeature);
#ifdef _MASSIF_ROUTING_SUPPORT
        registerFactory("routing", &buildRouting);
#endif
#ifdef _MASSIF_SEARCH_SUPPORT
        registerFactory("search", &buildSearch);
#endif
    }

} }
