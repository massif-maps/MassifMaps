#include "api/MassifApi.h"
#include "components/Layers.h"
#include "api/Builtins.h"
#include "api/Spec.h"
#include "api/MapEventBridge.h"
#include "api/Methods.h"
#include "core/BinaryData.h"
#include "ui/MapEventListener.h"

#include <map>
#include <mutex>
#include <set>
#include <typeinfo>
#include "components/ClassRegistry.h"
#include "components/Options.h"
#include "datasources/TileDataSource.h"
#include "layers/Layer.h"
#include "components/Exceptions.h"

namespace massif { namespace api {

    int MassifApi::registerOptions(const std::string& kind, const std::string& objectId,
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
         * GetClassName returns by value.
         */
        const char* internedClassName(const std::type_info& type, const char* fallback) {
            static std::mutex mutex;
            static std::set<std::string> names;
            std::string name = ClassRegistry::GetClassName(type);
            if (name.empty()) {
                return fallback;
            }
            std::lock_guard<std::mutex> lock(mutex);
            return names.insert("massif::" + name).first->c_str();
        }
    }

    int MassifApi::registerLayer(const std::string& kind, const std::string& objectId,
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

    int MassifApi::registerLayers(const std::string& kind, const std::string& objectId,
                                  const std::shared_ptr<Layers>& layers) {
        if (!layers) {
            return NULL_HANDLE;
        }
        Handle handle = NULL_HANDLE;
        Context::GetDefault()->registerObject(kind, objectId, layers, "massif::Layers", handle);
        return handle;
    }

    int MassifApi::registerSource(const std::string& kind, const std::string& objectId,
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

    int MassifApi::create(const std::string& kind, const std::string& objectId, const std::string& json) {
        registerBuiltins();
        Handle handle = NULL_HANDLE;
        Result result = Spec::create(*Context::GetDefault(), kind, objectId, json, handle);
        if (result != RESULT_OK) {
            throw GenericException("Cannot create '" + objectId + "' of kind '" + kind +
                                   "' (result " + std::to_string(result) + "), see the log");
        }
        return static_cast<int>(handle);
    }

    std::shared_ptr<TileDataSource> MassifApi::getSource(const std::string& objectId) {
        Handle handle = Context::GetDefault()->findObject("source", objectId);
        return std::static_pointer_cast<TileDataSource>(Context::GetDefault()->getObject(handle));
    }

    std::shared_ptr<Layer> MassifApi::getLayer(const std::string& objectId) {
        Handle handle = Context::GetDefault()->findObject("layer", objectId);
        return std::static_pointer_cast<Layer>(Context::GetDefault()->getObject(handle));
    }

    // on/off/drain and the listener registry live in MassifApiEvents.cpp - they need Context and
    // nothing else, and that is what makes them host-testable.

    std::shared_ptr<MapEventListener> MassifApi::createEventBridge(
            int handle, const std::shared_ptr<MapEventListener>& chained) {
        return std::make_shared<MapEventBridge>(Context::GetDefault(), static_cast<Handle>(handle),
                                                chained);
    }

    std::shared_ptr<VectorTileEventListener> MassifApi::createVectorTileEventBridge(
            int handle, const std::shared_ptr<VectorTileEventListener>& chained) {
        return std::make_shared<VectorTileEventBridge>(Context::GetDefault(),
                                                       static_cast<Handle>(handle), chained);
    }

    std::shared_ptr<VectorElementEventListener> MassifApi::createVectorElementEventBridge(
            int handle, const std::shared_ptr<VectorElementEventListener>& chained) {
        return std::make_shared<VectorElementEventBridge>(Context::GetDefault(),
                                                          static_cast<Handle>(handle), chained);
    }

    bool MassifApi::unregisterObject(const std::string& kind, const std::string& objectId) {
        return Context::GetDefault()->unregisterObject(kind, objectId);
    }

    int MassifApi::findObject(const std::string& kind, const std::string& objectId) {
        return static_cast<int>(Context::GetDefault()->findObject(kind, objectId));
    }

    bool MassifApi::isValid(int handle) {
        return Context::GetDefault()->getObject(static_cast<Handle>(handle)) != nullptr;
    }

    int MassifApi::setFloat(int handle, const std::string& path, double value) {
        PropertyValue propertyValue = PropertyValue::ofDouble(value);
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setInt(int handle, const std::string& path, long long value) {
        PropertyValue propertyValue = PropertyValue::ofLong(value);
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setBool(int handle, const std::string& path, bool value) {
        PropertyValue propertyValue = PropertyValue::ofBool(value);
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setString(int handle, const std::string& path, const std::string& value) {
        PropertyValue propertyValue = PropertyValue::ofString(value);
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setObject(int handle, const std::string& path, int value) {
        return Context::GetDefault()->setObjectProperty(static_cast<Handle>(handle), path,
                                                        static_cast<Handle>(value));
    }

    double MassifApi::getFloat(int handle, const std::string& path, double defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.asDouble();
    }

    long long MassifApi::getInt(int handle, const std::string& path, long long defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.asLong();
    }

    bool MassifApi::getBool(int handle, const std::string& path, bool defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.asBool();
    }

    std::string MassifApi::getString(int handle, const std::string& path, const std::string& defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.stringValue;
    }

    std::string MassifApi::getPos(int handle, const std::string& path,
                                  const std::string& projection) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value,
                                               projection) != RESULT_OK) {
            return std::string();
        }
        return value.stringValue;
    }

    namespace {
        std::string describe(const std::string& method, Result result) {
            return "Cannot call '" + method + "' (result " + std::to_string(result) +
                   "), see the log";
        }
    }

    int MassifApi::call(int handle, const std::string& method, const std::string& argsJson) {
        registerBuiltins();
        Handle result = NULL_HANDLE;
        Result called = Context::GetDefault()->callHandle(static_cast<Handle>(handle), method,
                                                          argsJson, result);
        if (called != RESULT_OK) {
            throw GenericException(describe(method, called));
        }
        return static_cast<int>(result);
    }

    int MassifApi::callAsync(int handle, const std::string& method, const std::string& argsJson,
                             const std::string& event) {
        registerBuiltins();
        Call call = NULL_CALL;
        Result queued = Context::GetDefault()->callAsync(static_cast<Handle>(handle), method,
                                                         argsJson, event, &call);
        if (queued != RESULT_OK) {
            throw GenericException(describe(method, queued));
        }
        return static_cast<int>(call);
    }

    bool MassifApi::cancelCall(int call) {
        return Context::GetDefault()->cancelCall(static_cast<Call>(call));
    }

    int MassifApi::cancelCalls(int handle) {
        return Context::GetDefault()->cancelCalls(static_cast<Handle>(handle));
    }

    std::vector<double> MassifApi::getDoubles(int handle) {
        std::vector<double> values;
        Context::GetDefault()->getDoubles(static_cast<Handle>(handle), values);
        return values;
    }

    std::shared_ptr<BinaryData> MassifApi::getData(int handle, const std::string& path) {
        std::shared_ptr<BinaryData> data;
        Context::GetDefault()->getData(static_cast<Handle>(handle), path, data);
        return data;
    }

    bool MassifApi::destroy(int handle) {
        return Context::GetDefault()->destroy(static_cast<Handle>(handle));
    }

} }
