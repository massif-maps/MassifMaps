#include "api/MassifApi.h"
#include "api/Builtins.h"
#include "api/Spec.h"
#include "api/Methods.h"
#include "components/Exceptions.h"
#include "core/BinaryData.h"

#include <map>

namespace massif { namespace api {

    int MassifApi::create(const std::string& kind, const std::string& objectId, const std::string& json) {
        registerBuiltins();
        Handle handle = NULL_HANDLE;
        Result result = Spec::create(*Context::GetDefault(), kind, objectId, json, handle);
        if (result != RESULT_OK) {
            throw GenericException("Cannot create '" + objectId + "' of kind '" + kind + "': " +
                                   resultName(result));
        }
        return static_cast<int>(handle);
    }

    // on/off/drain and the listener registry live in MassifApiEvents.cpp - they need Context and
    // nothing else, and that is what makes them host-testable. adopt, getSource, getLayer and the
    // event bridges live in MassifInterop.cpp - they name SDK types, which this class must not.

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

    int MassifApi::getObject(int handle, const std::string& path) {
        return static_cast<int>(
            Context::GetDefault()->getObjectProperty(static_cast<Handle>(handle), path));
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
            return "Cannot call '" + method + "': " + resultName(result);
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

    std::vector<unsigned char> MassifApi::getData(int handle, const std::string& path) {
        std::shared_ptr<BinaryData> data;
        Context::GetDefault()->getData(static_cast<Handle>(handle), path, data);
        if (!data) {
            return std::vector<unsigned char>();
        }
        // Copied rather than handed over: BinaryData is an SDK type and this signature must not
        // name one. A tile is tens of kilobytes and this is not a per-frame path.
        return std::vector<unsigned char>(data->data(), data->data() + data->size());
    }

    bool MassifApi::destroy(int handle) {
        return Context::GetDefault()->destroy(static_cast<Handle>(handle));
    }

} }
