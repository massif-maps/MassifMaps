#include "valhalla_engine.h"
#include "../../assets/ValhallaDefaultConfig.h"
#include "../routing/utils/ValhallaRoutingProxy.h"

#include <sqlite3pp.h>

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    std::mutex g_databaseMutex;
    std::map<std::string, std::weak_ptr<sqlite3pp::database> > g_databases;

    // Concurrent calls share one connection per file; the last call to finish closes it.
    std::shared_ptr<sqlite3pp::database> openDatabase(const std::string& path) {
        std::lock_guard<std::mutex> lock(g_databaseMutex);

        for (auto it = g_databases.begin(); it != g_databases.end();) {
            it = it->second.expired() ? g_databases.erase(it) : std::next(it);
        }
        auto cached = g_databases.find(path);
        if (cached != g_databases.end()) {
            if (auto database = cached->second.lock()) {
                return database;
            }
        }

        auto database = std::make_shared<sqlite3pp::database>();
        int status = database->connect_v2(path.c_str(), SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
        if (status != SQLITE_OK) {
            throw std::runtime_error("Cannot open '" + path + "': sqlite error " + std::to_string(status));
        }
        database->execute("PRAGMA journal_mode=OFF");
        database->execute("PRAGMA temp_store=MEMORY");
        database->execute("PRAGMA synchronous=OFF");
        database->execute("PRAGMA cache_size=2000");
        g_databases[path] = database;
        return database;
    }

    char* duplicate(const std::string& text) {
        char* copy = static_cast<char*>(std::malloc(text.size() + 1));
        if (copy) {
            std::memcpy(copy, text.c_str(), text.size() + 1);
        }
        return copy;
    }

    void setError(char** error, const std::string& message) {
        if (error) {
            *error = duplicate(message);
        }
    }

}

extern "C" {

int vr_abi_version(void) {
    return VR_ABI_VERSION;
}

const char* vr_default_config(void) {
    return valhalla_default_config;
}

void vr_add_locale(const char* key, const char* json) {
    if (key && json) {
        routing::ValhallaRoutingProxy::AddLocale(key, json);
    }
}

int vr_call(const char* const* paths, int path_count, const char* config_json, const char* endpoint,
            const char* body_json, char** result, char** error) {
    if (result) {
        *result = nullptr;
    }
    if (error) {
        *error = nullptr;
    }
    if (!result || !config_json || !endpoint || !body_json || path_count < 0 || (path_count > 0 && !paths)) {
        setError(error, "Invalid argument");
        return 1;
    }
    try {
        if (path_count == 0) {
            throw std::runtime_error("No MBTiles databases");
        }
        std::vector<std::shared_ptr<sqlite3pp::database> > databases;
        databases.reserve(static_cast<std::size_t>(path_count));
        for (int index = 0; index < path_count; index++) {
            databases.push_back(openDatabase(paths[index]));
        }
        *result = duplicate(routing::ValhallaRoutingProxy::CallRaw(databases, config_json, endpoint, body_json));
        if (!*result) {
            throw std::bad_alloc();
        }
        return 0;
    }
    catch (const std::exception& ex) {
        setError(error, ex.what());
    }
    catch (...) {
        setError(error, "Unknown error");
    }
    return 1;
}

void vr_free(char* ptr) {
    std::free(ptr);
}

}
