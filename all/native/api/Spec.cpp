#include "api/Spec.h"
#include "api/Context.h"
#include "datasources/AssetTileDataSource.h"
#include "datasources/CombinedTileDataSource.h"
#include "datasources/HTTPTileDataSource.h"
#include "datasources/MemoryCacheTileDataSource.h"
#include "datasources/MultiTileDataSource.h"
#include "datasources/OrderedTileDataSource.h"
#include "utils/Log.h"

#ifdef _MASSIF_OFFLINE_SUPPORT
#include "datasources/MBTilesTileDataSource.h"
#include "datasources/PersistentCacheTileDataSource.h"
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
                source = std::static_pointer_cast<TileDataSource>(context.getObject(handle));
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

        Result buildSource(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type");
            consumed.insert("type");

            // Zoom bounds are constructor arguments rather than properties on most sources, so
            // they are read here and not left to the property pass.
            int minZoom = intAt(spec, "minZoom", 0);
            int maxZoom = intAt(spec, "maxZoom", 24);
            consumed.insert("minZoom");
            consumed.insert("maxZoom");

            if (type == "http") {
                consumed.insert("url");
                object.obj = std::make_shared<HTTPTileDataSource>(minZoom, maxZoom, stringAt(spec, "url"));
                object.cppClass = "massif::HTTPTileDataSource";
                return RESULT_OK;
            }
            if (type == "assets") {
                consumed.insert("path");
                object.obj = std::make_shared<AssetTileDataSource>(minZoom, maxZoom, stringAt(spec, "path"));
                object.cppClass = "massif::AssetTileDataSource";
                return RESULT_OK;
            }
            if (type == "memory-cache" || type == "persistent-cache" || type == "ordered" ||
                type == "combined") {
                std::shared_ptr<TileDataSource> source;
                Result result = childSource(context, spec, "source", source);
                if (result != RESULT_OK) {
                    Log::Errorf("Spec: '%s' needs a \"source\"", type.c_str());
                    return result;
                }
                consumed.insert("source");

                if (type == "memory-cache") {
                    object.obj = std::make_shared<MemoryCacheTileDataSource>(source);
                    object.cppClass = "massif::MemoryCacheTileDataSource";
                    return RESULT_OK;
                }
#ifdef _MASSIF_OFFLINE_SUPPORT
                if (type == "persistent-cache") {
                    consumed.insert("databasePath");
                    object.obj = std::make_shared<PersistentCacheTileDataSource>(source, stringAt(spec, "databasePath"));
                    object.cppClass = "massif::PersistentCacheTileDataSource";
                    return RESULT_OK;
                }
#endif
                std::shared_ptr<TileDataSource> second;
                if (childSource(context, spec, "source2", second) != RESULT_OK) {
                    Log::Errorf("Spec: '%s' needs a second \"source2\"", type.c_str());
                    return RESULT_UNKNOWN_PROPERTY;
                }
                consumed.insert("source2");
                if (type == "ordered") {
                    object.obj = std::make_shared<OrderedTileDataSource>(source, second);
                    object.cppClass = "massif::OrderedTileDataSource";
                    return RESULT_OK;
                }
                consumed.insert("zoomLevel");
                object.obj = std::make_shared<CombinedTileDataSource>(source, second, intAt(spec, "zoomLevel", 0));
                object.cppClass = "massif::CombinedTileDataSource";
                return RESULT_OK;
            }
#ifdef _MASSIF_OFFLINE_SUPPORT
            if (type == "mbtiles") {
                consumed.insert("path");
                object.obj = std::make_shared<MBTilesTileDataSource>(minZoom, maxZoom, stringAt(spec, "path"));
                object.cppClass = "massif::MBTilesTileDataSource";
                return RESULT_OK;
            }
#endif
            if (type == "multi") {
                consumed.insert("maxOpenedPackages");
                object.obj = std::make_shared<MultiTileDataSource>(intAt(spec, "maxOpenedPackages", 4));
                object.cppClass = "massif::MultiTileDataSource";
                return RESULT_OK;
            }

            Log::Errorf("Spec: no source type '%s'", type.c_str());
            return RESULT_UNKNOWN_TYPE;
        }

    }

    Result Spec::build(Context& context, const std::string& kind, const Variant& spec,
                       ObjectRef& object, std::set<std::string>& consumed) {
        if (spec.getType() != VariantType::VARIANT_TYPE_OBJECT) {
            Log::Error("Spec: a spec has to be a JSON object");
            return RESULT_BAD_SPEC;
        }
        if (kind == "source") {
            return buildSource(context, spec, object, consumed);
        }
        Log::Errorf("Spec: no kind '%s'", kind.c_str());
        return RESULT_UNKNOWN_TYPE;
    }

} }
