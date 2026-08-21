#include "api/MassifApi.h"
#include "api/Spec.h"
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

    int MassifApi::create(const std::string& kind, const std::string& objectId, const std::string& json) {
        // Registered here rather than at static-init time: Spec itself must not depend on the
        // factories, or a test cannot exercise create() without linking every constructor.
        static bool registered = (Spec::registerBuiltinFactories(), true);
        (void)registered;

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

    bool MassifApi::unregisterObject(const std::string& kind, const std::string& objectId) {
        return Context::GetDefault()->unregisterObject(kind, objectId);
    }

    int MassifApi::findObject(const std::string& kind, const std::string& objectId) {
        return static_cast<int>(Context::GetDefault()->findObject(kind, objectId));
    }

    int MassifApi::setFloat(int handle, const std::string& path, double value) {
        PropertyValue propertyValue;
        propertyValue.floatValue = value;
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setInt(int handle, const std::string& path, long long value) {
        PropertyValue propertyValue;
        propertyValue.intValue = value;
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setBool(int handle, const std::string& path, bool value) {
        PropertyValue propertyValue;
        propertyValue.boolValue = value;
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    int MassifApi::setString(int handle, const std::string& path, const std::string& value) {
        PropertyValue propertyValue;
        propertyValue.stringValue = value;
        return Context::GetDefault()->setProperty(static_cast<Handle>(handle), path, propertyValue);
    }

    double MassifApi::getFloat(int handle, const std::string& path, double defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.floatValue;
    }

    long long MassifApi::getInt(int handle, const std::string& path, long long defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.intValue;
    }

    bool MassifApi::getBool(int handle, const std::string& path, bool defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.boolValue;
    }

    std::string MassifApi::getString(int handle, const std::string& path, const std::string& defaultValue) {
        PropertyValue value;
        if (Context::GetDefault()->getProperty(static_cast<Handle>(handle), path, value) != RESULT_OK) {
            return defaultValue;
        }
        return value.stringValue;
    }

} }
