#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>

namespace sqlite3pp {
    class database;
}

namespace routing {

    class HTTPClient;

    /**
     * Internal helper that drives Valhalla workers (offline) or dispatches
     * requests via HTTP (online), returning raw JSON response strings.
     */
    class ValhallaRoutingProxy {
    public:
        // ----------------------------------------------------------------
        // Offline routing — drives Valhalla workers directly
        // ----------------------------------------------------------------

        /**
         * Call any Valhalla API endpoint using offline Valhalla workers.
         * @param databases   Open sqlite3pp::database handles (MBTiles).
         * @param config      Valhalla configuration JSON string.
         * @param endpoint    Endpoint name, e.g. "route", "trace_attributes".
         * @param jsonBody    Full Valhalla request JSON string (including "costing").
         * @return            Raw Valhalla JSON response string.
         */
        static std::string CallRaw(
            const std::vector<std::shared_ptr<sqlite3pp::database>>& databases,
            const std::string& config,
            const std::string& endpoint,
            const std::string& jsonBody);

        // ----------------------------------------------------------------
        // Online routing — dispatches via built-in C++ HTTP client
        // ----------------------------------------------------------------

        /**
         * Call any Valhalla endpoint via HTTP POST.
         * @param httpClient HTTP client instance.
         * @param baseURL    Base URL of the Valhalla service.
         * @param endpoint   Endpoint name, e.g. "route", "isochrone".
         * @param jsonBody   Full Valhalla request JSON string (including "costing").
         * @return           Raw JSON response string.
         */
        static std::string CallRaw(
            HTTPClient& httpClient,
            const std::string& baseURL,
            const std::string& endpoint,
            const std::string& jsonBody,
            std::map<std::string, std::string>& headers);

        // ----------------------------------------------------------------
        // Shared utilities
        // ----------------------------------------------------------------

        static void AddLocale(const std::string& key, const std::string& json);

    private:
        ValhallaRoutingProxy() = delete;
    };

} // namespace routing
