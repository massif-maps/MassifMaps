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
        // Qualified name of the concrete class, interned because a slot keeps the pointer.
        // A miss is normal (facade bridges are not Swig classes): no log, fall back to the declared type.
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
        // Bound to a reference first: typeid on a smart-pointer dereference triggers an evaluated-operand warning.
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
                             const std::shared_ptr<VectorDataSource>& vectorSource) {
        if (!vectorSource) {
            return NULL_HANDLE;
        }
        const VectorDataSource& concrete = *vectorSource;
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, vectorSource,
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
        // A binding's subclass is a Swig director unknown to ClassRegistry; the base fallback is what `assets` requires.
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
        // Registered as the base: the camera methods are declared on BaseMapView.
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, objectId, view, "massif::BaseMapView",
                                                  handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        // Tells moveTo its positions are WGS84. Read once: changing Options.baseProjection later needs a re-adopt.
        Context::GetDefault()->setObjectProjection(handle,
                                                   view->getOptions()->getBaseProjection());
        return static_cast<int>(handle);
    }

    std::shared_ptr<TileDataSource> MassifInterop::getSource(const std::string& objectId) {
        Handle handle = Context::GetDefault()->findObject("source", objectId);
        return std::static_pointer_cast<TileDataSource>(Context::GetDefault()->getObject(handle));
    }

    std::shared_ptr<TileDataSource> MassifInterop::getSourceByHandle(int handle) {
        // Class is checked: a handle can name anything and the cast is from a type-erased pointer.
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
