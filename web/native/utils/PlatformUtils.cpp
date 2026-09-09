#include "utils/PlatformUtils.h"

#include <cstdlib>

#include <emscripten/em_asm.h>

namespace massif {

    namespace {
        // Both of these read WorkerGlobalScope properties, so they answer the same on the render
        // thread and on a tile worker. stringToNewUTF8 comes from the runtime - see the
        // EXPORTED_RUNTIME_METHODS list in scripts/build/CMakeLists.txt.
        std::string TakeString(char* ptr) {
            if (!ptr) {
                return std::string();
            }
            std::string result(ptr);
            std::free(ptr);
            return result;
        }
    }

    PlatformType::PlatformType PlatformUtils::GetPlatformType() {
        return PlatformType::PLATFORM_TYPE_WEB;
    }

    std::string PlatformUtils::GetDeviceId() {
        // A browser has no stable device identifier, and fingerprinting for one is not something
        // the SDK should do. Empty rather than a fabricated value.
        return std::string();
    }

    std::string PlatformUtils::GetDeviceType() {
        return "web";
    }

    std::string PlatformUtils::GetDeviceOS() {
        return TakeString(reinterpret_cast<char*>(EM_ASM_PTR({
            return stringToNewUTF8(globalThis.navigator ? globalThis.navigator.userAgent : "");
        })));
    }

    std::string PlatformUtils::GetAppIdentifier() {
        // Feeds the Referer header in NetworkUtils, where the origin is what a tile server checks.
        return TakeString(reinterpret_cast<char*>(EM_ASM_PTR({
            return stringToNewUTF8(globalThis.location ? globalThis.location.origin : "");
        })));
    }

    std::string PlatformUtils::GetAppDeviceId() {
        return GetAppIdentifier();
    }

    bool PlatformUtils::ExcludeFolderFromBackup(const std::string& folder) {
        // Nothing backs the virtual filesystem up.
        return false;
    }

    PlatformUtils::PlatformUtils() {
    }

}
