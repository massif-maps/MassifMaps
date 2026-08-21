#include "api/Projections.h"
#include "api/Spec.h"
#include "api/Context.h"
#include "datasources/AssetTileDataSource.h"
#include "datasources/CombinedTileDataSource.h"
#include "datasources/HTTPTileDataSource.h"
#include "datasources/MemoryCacheTileDataSource.h"
#include "datasources/MultiTileDataSource.h"
#include "datasources/OrderedTileDataSource.h"
#include "projections/Projection.h"
#include "layers/CompositeVectorTileLayer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "layers/RasterTileLayer.h"
#include "layers/SolidLayer.h"
#include "layers/VectorTileLayer.h"
#include "styles/CartoCSSStyleSet.h"
#include "utils/DirAssetPackage.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "graphics/Color.h"
#include "utils/Log.h"

#include <map>

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

        /** A vector tile decoder and the CartoCSS behind it. */
        Result buildStyle(Context& context, const Variant& spec, ObjectRef& object,
                          std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type", "cartocss");
            consumed.insert("type");
            if (type != "cartocss") {
                Log::Errorf("Spec: no style type '%s'", type.c_str());
                return RESULT_UNKNOWN_TYPE;
            }

            consumed.insert("css");
            consumed.insert("assets");
            std::string css = stringAt(spec, "css");
            std::string assets = stringAt(spec, "assets");

            std::shared_ptr<CartoCSSStyleSet> styleSet;
            if (assets.rfind("dir://", 0) == 0) {
                auto package = std::make_shared<DirAssetPackage>(assets.substr(6));
                styleSet = std::make_shared<CartoCSSStyleSet>(css, package);
            } else {
                if (!assets.empty()) {
                    // zip:// needs the archive read into BinaryData, which is platform work.
                    Log::Warnf("Spec: only dir:// asset packages are supported, ignoring '%s'", assets.c_str());
                }
                styleSet = std::make_shared<CartoCSSStyleSet>(css);
            }
            object.obj = std::make_shared<MBVectorTileDecoder>(styleSet);
            object.cppClass = "massif::MBVectorTileDecoder";
            return RESULT_OK;
        }

        /**
         * Resolves a reference that is either a registry id or an inline spec of another kind.
         */
        Result childOf(Context& context, const Variant& spec, const char* key, const char* kind,
                       std::shared_ptr<void>& out) {
            if (!spec.containsObjectKey(key)) {
                return RESULT_UNKNOWN_PROPERTY;
            }
            Variant child = spec.getObjectElement(key);
            if (child.getType() == VariantType::VARIANT_TYPE_STRING) {
                out = context.getObject(context.findObject(kind, child.getString()));
                return out ? RESULT_OK : RESULT_BAD_HANDLE;
            }
            ObjectRef object;
            std::set<std::string> consumed;
            Result result = Spec::build(context, kind, child, object, consumed);
            out = object.obj;
            return result;
        }

        Result buildLayer(Context& context, const Variant& spec, ObjectRef& object,
                          std::set<std::string>& consumed) {
            std::string type = stringAt(spec, "type");
            consumed.insert("type");

            if (type == "solid") {
                consumed.insert("color");
                object.obj = std::make_shared<SolidLayer>(
                    Color(static_cast<int>(spec.containsObjectKey("color")
                                           ? spec.getObjectElement("color").getLong() : 0)));
                object.cppClass = "massif::SolidLayer";
                return RESULT_OK;
            }

            std::shared_ptr<void> source;
            Result result = childOf(context, spec, "source", "source", source);
            if (result != RESULT_OK) {
                Log::Errorf("Spec: layer '%s' needs a \"source\"", type.c_str());
                return result;
            }
            consumed.insert("source");
            auto dataSource = std::static_pointer_cast<TileDataSource>(source);

            if (type == "raster") {
                object.obj = std::make_shared<RasterTileLayer>(dataSource);
                object.cppClass = "massif::RasterTileLayer";
                return RESULT_OK;
            }
            if (type == "hillshade") {
                object.obj = std::make_shared<HillshadeRasterTileLayer>(dataSource);
                object.cppClass = "massif::HillshadeRasterTileLayer";
                return RESULT_OK;
            }
            if (type == "vector" || type == "composite-vector") {
                std::shared_ptr<void> style;
                if (childOf(context, spec, "style", "style", style) != RESULT_OK) {
                    Log::Errorf("Spec: layer '%s' needs a \"style\"", type.c_str());
                    return RESULT_UNKNOWN_PROPERTY;
                }
                consumed.insert("style");
                auto decoder = std::static_pointer_cast<VectorTileDecoder>(style);
                if (type == "vector") {
                    object.obj = std::make_shared<VectorTileLayer>(dataSource, decoder);
                    object.cppClass = "massif::VectorTileLayer";
                } else {
                    object.obj = std::make_shared<CompositeVectorTileLayer>(dataSource, decoder);
                    object.cppClass = "massif::CompositeVectorTileLayer";
                }
                return RESULT_OK;
            }

            Log::Errorf("Spec: no layer type '%s'", type.c_str());
            return RESULT_UNKNOWN_TYPE;
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

    }


    void Spec::registerBuiltinFactories() {
        registerFactory("source", &buildSource);
        registerFactory("style", &buildStyle);
        registerFactory("layer", &buildLayer);
        registerFactory("projection", &buildProjection);
    }

} }
