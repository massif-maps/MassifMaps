#include "ClassRegistry.h"
#include "utils/Log.h"

namespace massif {

    ClassRegistry::Entry::Entry(const std::type_info& typeInfo, const char* name) {
        ClassRegistry& registry = ClassRegistry::GetInstance();

        std::lock_guard<std::mutex> lock(registry._mutex);
        registry._classNames[std::type_index(typeInfo)] = name;
        // Log::Infof("ClassRegistry: Registered class %s as %s", typeInfo.name(), name);
    }
    
    std::string ClassRegistry::GetClassName(const std::type_info& typeInfo) {
        std::string name = FindClassName(typeInfo);
        if (name.empty()) {
            Log::Errorf("ClassRegistry: Could not find class: %s", typeInfo.name());
        }
        return name;
    }

    std::string ClassRegistry::FindClassName(const std::type_info& typeInfo) {
        ClassRegistry& registry = GetInstance();

        std::lock_guard<std::mutex> lock(registry._mutex);
        auto it = registry._classNames.find(std::type_index(typeInfo));
        if (it != registry._classNames.end()) {
            return it->second;
        }
        return std::string();
    }

    ClassRegistry::ClassRegistry() :
        _classNames(),
        _mutex()
    {
    }

    ClassRegistry::~ClassRegistry() {
    }

    ClassRegistry& ClassRegistry::GetInstance() {
        static ClassRegistry registry;
        return registry;
    }

}
