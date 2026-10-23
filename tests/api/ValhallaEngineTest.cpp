// The client of the Valhalla engine (routing/utils/ValhallaEngine.cpp): what it hands over the C ABI,
// what it takes back, and that an engine failure surfaces as an exception.
// A stub engine is defined below and found through the weak-import path, which is the Apple branch;
// the dlopen branch, a missing engine and an ABI version mismatch are not reachable here and need
// an Android run.

#include "routing/utils/ValhallaEngine.h"

#include "components/Exceptions.h"

#include "TestCheck.h"

#include "../../routing-lib/native/engine/valhalla_engine.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace massif;

namespace {
    std::vector<std::string> stubPaths;
    std::string stubConfig;
    std::string stubEndpoint;
    std::string stubBody;
    std::string stubLocaleKey;
    std::string stubLocaleJson;
    bool stubFail = false;
    int stubFreed = 0;

    char* duplicateString(const std::string& text) {
        char* copy = static_cast<char*>(std::malloc(text.size() + 1));
        std::memcpy(copy, text.c_str(), text.size() + 1);
        return copy;
    }
}

extern "C" {
    int vr_abi_version(void) {
        return VR_ABI_VERSION;
    }

    const char* vr_default_config(void) {
        return "{\"default\":true}";
    }

    void vr_add_locale(const char* key, const char* json) {
        stubLocaleKey = key;
        stubLocaleJson = json;
    }

    int vr_call(const char* const* paths, int path_count, const char* config_json, const char* endpoint, const char* body_json, char** result, char** error) {
        stubPaths.assign(paths, paths + path_count);
        stubConfig = config_json;
        stubEndpoint = endpoint;
        stubBody = body_json;
        if (stubFail) {
            *error = duplicateString("graph tile missing");
            return 1;
        }
        *result = duplicateString("{\"trip\":{}}");
        return 0;
    }

    void vr_free(char* ptr) {
        if (ptr) {
            stubFreed++;
        }
        std::free(ptr);
    }
}

void testValhallaEngine() {
    const ValhallaEngine& engine = ValhallaEngine::Get();

    TEST_CHECK(engine.getDefaultConfiguration() == "{\"default\":true}", "the engine's own default configuration is returned");

    engine.addLocale("fr-FR", "{\"fr\":1}");
    TEST_CHECK(stubLocaleKey == "fr-FR" && stubLocaleJson == "{\"fr\":1}", "a locale reaches the engine with its key");

    stubFreed = 0;
    std::string result = engine.call({"/a.mbtiles", "/b.mbtiles"}, "{\"cfg\":1}", "trace_attributes", "{\"shape\":[]}");
    TEST_CHECK(result == "{\"trip\":{}}", "the engine's JSON comes back unchanged");
    TEST_CHECK(stubPaths.size() == 2 && stubPaths[0] == "/a.mbtiles" && stubPaths[1] == "/b.mbtiles", "every database path is passed, in order");
    TEST_CHECK(stubConfig == "{\"cfg\":1}" && stubEndpoint == "trace_attributes" && stubBody == "{\"shape\":[]}", "configuration, endpoint and body are not swapped");
    TEST_CHECK(stubFreed == 1, "the engine's result buffer is released through vr_free");

    stubFreed = 0;
    stubFail = true;
    std::string message;
    try {
        engine.call({"/a.mbtiles"}, "{}", "route", "{}");
    }
    catch (const GenericException& ex) {
        message = ex.what();
    }
    stubFail = false;
    TEST_CHECK(message.find("graph tile missing") != std::string::npos, "an engine failure throws with the engine's message");
    TEST_CHECK(stubFreed == 1, "the engine's error buffer is released through vr_free");

    engine.call({}, "{}", "route", "{}");
    TEST_CHECK(stubPaths.empty(), "an empty path list is forwarded as it is, the engine decides");
}
