#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT)

#include "ValhallaEngine.h"
#include "components/Exceptions.h"

#include "../../../../routing-lib/native/engine/valhalla_engine.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace massif {

    namespace {

        struct EngineApi {
            const char* (*defaultConfig)(void);
            void (*addLocale)(const char*, const char*);
            int (*call)(const char* const*, int, const char*, const char*, const char*, char**, char**);
            void (*free)(char*);
        };

        EngineApi LoadApi() {
#if !defined(__ANDROID__)
            return EngineApi { vr_default_config, vr_add_locale, vr_call, vr_free };
#else
            void* library = dlopen("libvalhalla_routing.so", RTLD_NOW | RTLD_LOCAL);
            if (!library) {
                const char* reason = dlerror();
                throw GenericException("Valhalla engine not found, ship libvalhalla_routing.so with the app", reason ? reason : "");
            }
            auto version = reinterpret_cast<int (*)(void)>(dlsym(library, "vr_abi_version"));
            if (!version || version() != VR_ABI_VERSION) {
                throw GenericException("Valhalla engine has an incompatible ABI version");
            }
            EngineApi api;
            api.defaultConfig = reinterpret_cast<decltype(api.defaultConfig)>(dlsym(library, "vr_default_config"));
            api.addLocale = reinterpret_cast<decltype(api.addLocale)>(dlsym(library, "vr_add_locale"));
            api.call = reinterpret_cast<decltype(api.call)>(dlsym(library, "vr_call"));
            api.free = reinterpret_cast<decltype(api.free)>(dlsym(library, "vr_free"));
            if (!api.defaultConfig || !api.addLocale || !api.call || !api.free) {
                throw GenericException("Valhalla engine is missing functions");
            }
            return api;
#endif
        }

        const EngineApi& GetApi() {
            // A throwing initializer is retried on the next call, so an engine that appears later is found.
            static const EngineApi api = LoadApi();
            return api;
        }

    }

    ValhallaEngine::ValhallaEngine() {
    }

    const ValhallaEngine& ValhallaEngine::Get() {
        GetApi();
        static const ValhallaEngine engine;
        return engine;
    }

    std::string ValhallaEngine::getDefaultConfiguration() const {
        return GetApi().defaultConfig();
    }

    void ValhallaEngine::addLocale(const std::string& key, const std::string& json) const {
        GetApi().addLocale(key.c_str(), json.c_str());
    }

    std::string ValhallaEngine::call(const std::vector<std::string>& paths, const std::string& configuration, const std::string& endpoint, const std::string& body) const {
        const EngineApi& api = GetApi();
        std::vector<const char*> pathPointers;
        pathPointers.reserve(paths.size());
        for (const std::string& path : paths) {
            pathPointers.push_back(path.c_str());
        }
        char* result = nullptr;
        char* error = nullptr;
        int status = api.call(pathPointers.data(), static_cast<int>(pathPointers.size()), configuration.c_str(), endpoint.c_str(), body.c_str(), &result, &error);
        std::string resultString = result ? result : "";
        std::string errorString = error ? error : "";
        api.free(result);
        api.free(error);
        if (status != 0) {
            throw GenericException("Exception in Valhalla engine", errorString);
        }
        return resultString;
    }

}

#endif
