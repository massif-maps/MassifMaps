#include "api/MassifApi.h"
#include "components/Options.h"

namespace massif { namespace api {

    int MassifApi::registerOptions(const std::string& kind, const std::string& id,
                                   const std::shared_ptr<Options>& options) {
        Handle handle = NULL_HANDLE;
        if (Context::GetDefault()->registerObject(kind, id, options, "massif::Options", handle) != RESULT_OK) {
            return NULL_HANDLE;
        }
        return static_cast<int>(handle);
    }

    bool MassifApi::unregisterObject(const std::string& kind, const std::string& id) {
        return Context::GetDefault()->unregisterObject(kind, id);
    }

    int MassifApi::findObject(const std::string& kind, const std::string& id) {
        return static_cast<int>(Context::GetDefault()->findObject(kind, id));
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
