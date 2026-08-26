#include "api/MassifInterop.h"
#include "api/Context.h"
#include "api/MapEventBridge.h"
#include "components/ClassRegistry.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "datasources/TileDataSource.h"
#include "datasources/VectorDataSource.h"
#include "layers/Layer.h"
#include "projections/Projection.h"
#include "ui/BaseMapView.h"
#include "ui/MapEventListener.h"
#include "utils/AssetPackage.h"

#include <mutex>
#include <set>
#include <typeinfo>

namespace massif { namespace api {

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<Options>& options) {
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, options, "massif::Options", handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    namespace {
        /**
         * The property table's name for an object's CONCRETE class.
         *
         * Every Swig-wrapped class registers its short name in ClassRegistry at static-init time;
         * the table keys on the qualified one. Interned because a slot keeps the pointer, and
         * FindClassName returns by value.
         *
         * A miss is normal here - the facade's own bridges are not Swig classes - so this asks
         * without logging and falls back to the declared type.
         */
        const char* internedClassName(const std::type_info& type, const char* fallback) {
            static std::mutex mutex;
            static std::set<std::string> names;
            std::string name = ClassRegistry::FindClassName(type);
            if (name.empty()) {
                return fallback;
            }
            std::lock_guard<std::mutex> lock(mutex);
            return names.insert("massif::" + name).first->c_str();
        }
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<Layer>& layer) {
        if (!layer) {
            return NULL_HANDLE;
        }
        // Bound to a reference first: typeid on a smart-pointer dereference is a call, which the
        // compiler warns is evaluated despite being a typeid operand.
        const Layer& concrete = *layer;
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, layer,
                internedClassName(typeid(concrete), "massif::Layer"), handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<Layers>& layers) {
        if (!layers) {
            return NULL_HANDLE;
        }
        Handle handle = NULL_HANDLE;
        Context::GetDefault()->registerObject(kind, objectId, layers, "massif::Layers", handle);
        return handle;
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<TileDataSource>& source) {
        if (!source) {
            return NULL_HANDLE;
        }
        const TileDataSource& concrete = *source;
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, source,
                internedClassName(typeid(concrete), "massif::TileDataSource"), handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<VectorDataSource>& source) {
        if (!source) {
            return NULL_HANDLE;
        }
        const VectorDataSource& concrete = *source;
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, source,
                internedClassName(typeid(concrete), "massif::VectorDataSource"), handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<AssetPackage>& assets) {
        if (!assets) {
            return NULL_HANDLE;
        }
        // A binding's own subclass is a Swig director, which ClassRegistry does not know - it logs
        // a miss and the fallback is the base, which is the class a spec's `assets` key requires.
        const AssetPackage& concrete = *assets;
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, assets,
                internedClassName(typeid(concrete), "massif::AssetPackage"), handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    int MassifInterop::adopt(const std::string& kind, const std::string& objectId,
                             const std::shared_ptr<BaseMapView>& view) {
        if (!view) {
            return NULL_HANDLE;
        }
        // Registered as the base, not the concrete class: the camera methods are declared on
        // BaseMapView and every platform's map view is that same class underneath.
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, view, "massif::BaseMapView",
                                                  handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        // The view declares no projection of its own, so it carries the map's - which is what
        // tells moveTo that a position handed to it is in WGS84 rather than in map coordinates.
        // Read once: an app that changes Options.baseProjection afterwards has to re-adopt.
        Context::GetDefault()->setObjectProjection(handle,
                                                   view->getOptions()->getBaseProjection());
        return static_cast<int>(handle);
    }

    std::shared_ptr<TileDataSource> MassifInterop::getSource(const std::string& objectId) {
        Handle handle = Context::GetDefault()->findObject("source", objectId);
        return std::static_pointer_cast<TileDataSource>(Context::GetDefault()->getObject(handle));
    }

    std::shared_ptr<TileDataSource> MassifInterop::getSourceByHandle(int handle) {
        // The class is CHECKED, not asserted: a handle can name anything, and the cast that
        // follows is from a type-erased pointer.
        return std::static_pointer_cast<TileDataSource>(
            Context::GetDefault()->getObject(static_cast<Handle>(handle), "massif::TileDataSource"));
    }

    std::shared_ptr<Layer> MassifInterop::getLayer(const std::string& objectId) {
        Handle handle = Context::GetDefault()->findObject("layer", objectId);
        return std::static_pointer_cast<Layer>(Context::GetDefault()->getObject(handle));
    }

    std::shared_ptr<Layer> MassifInterop::getLayerByHandle(int handle) {
        return std::static_pointer_cast<Layer>(
            Context::GetDefault()->getObject(static_cast<Handle>(handle), "massif::Layer"));
    }

    std::shared_ptr<MapEventListener> MassifInterop::createEventBridge(
            int handle, const std::shared_ptr<MapEventListener>& chained) {
        return std::make_shared<MapEventBridge>(Context::GetDefault(), static_cast<Handle>(handle),
                                                chained);
    }

    std::shared_ptr<VectorTileEventListener> MassifInterop::createVectorTileEventBridge(
            int handle, const std::shared_ptr<VectorTileEventListener>& chained) {
        return std::make_shared<VectorTileEventBridge>(Context::GetDefault(),
                                                       static_cast<Handle>(handle), chained);
    }

    std::shared_ptr<VectorElementEventListener> MassifInterop::createVectorElementEventBridge(
            int handle, const std::shared_ptr<VectorElementEventListener>& chained) {
        return std::make_shared<VectorElementEventBridge>(Context::GetDefault(),
                                                          static_cast<Handle>(handle), chained);
    }

} }
