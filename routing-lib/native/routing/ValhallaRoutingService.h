#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline void append_double(char* buf, size_t buf_size, std::string& out, double value) {
    // "%.17g" = closest equivalent to default to_chars for double
    int n = std::snprintf(buf, buf_size, "%.6f", value);

    if (n > 0) {
        // clamp in case of truncation
        size_t len = static_cast<size_t>(n) < buf_size ? static_cast<size_t>(n) : buf_size;
        out.append(buf, len);
    }

    // auto res1 = std::to_chars(buf, buf + sizeof(buf), value);
    // append_double(buf, sizeof(buf), out, value);
    // if (res1.ec == std::errc()) {
    //     out.append(buf, res1.ptr);
    // } else {
    //     // fallback: snprintf with 6 fractional digits
    //     int n = std::snprintf(buf, sizeof(buf), "%.6f", value);
    //     if (n > 0) out.append(buf, static_cast<std::size_t>(n));
    // }
}

namespace routing {

    /**
     * Offline Valhalla routing service backed by MBTiles SQLite files.
     *
     * Register one or more MBTiles file paths via addMBTilesPath(). Multiple
     * files are queried so that the world can be split across several databases.
     *
     * Databases are opened lazily on the first callRaw() invocation and closed
     * automatically once all in-flight requests have completed. Concurrent
     * requests from multiple threads are supported.
     *
     * Routing results are returned as raw Valhalla JSON strings. Parsing of
     * individual fields is the responsibility of the application layer.
     *
     * Locale strings for narrative generation can be registered via addLocale().
     */
    class ValhallaRoutingService {
    public:
        /**
         * Construct with an optional initial list of MBTiles file paths.
         */
        explicit ValhallaRoutingService(std::vector<std::string> paths = {});
        virtual ~ValhallaRoutingService();

        // ----------------------------------------------------------------
        // Database path management
        // ----------------------------------------------------------------

        /**
         * Add an MBTiles database by file path.
         * If databases are currently open (a request is in progress), the new
         * path will be used starting from the next request.
         */
        void addMBTilesPath(const std::string& path);

        std::vector<std::string> getMBTilesPaths() const;

        // ----------------------------------------------------------------
        // Profile (costing model)
        //
        // The profile is injected as "costing" into every callRaw() body
        // that does not already contain a "costing" key.
        // ----------------------------------------------------------------

        std::string getProfile() const;
        void setProfile(const std::string& profile);

        // ----------------------------------------------------------------
        // Valhalla configuration (dot-delimited key paths, JSON string values)
        //
        // Example:
        //   svc.setConfigurationParameter("costing_options.auto.use_highways",
        //                                 "0.0");
        // ----------------------------------------------------------------

        /**
         * Get a configuration value by dot-delimited key path.
         * Returns the JSON-serialized value, or an empty string if not found.
         */
        std::string getConfigurationParameter(const std::string& param) const;

        /**
         * Set a configuration value by dot-delimited key path.
         * @param param     Dot-delimited key path.
         * @param valueJson JSON-encoded value (e.g. "0.5", "\"residential\"", "true").
         */
        void setConfigurationParameter(const std::string& param, const std::string& valueJson);

        // ----------------------------------------------------------------
        // Locale support for narrative generation
        // ----------------------------------------------------------------

        /**
         * Register a locale JSON blob so that Valhalla can produce navigation
         * instructions in the given language.
         * @param key  Locale identifier (e.g. "en-US", "de-DE").
         * @param json Locale JSON string (Valhalla locale format).
         */
        void addLocale(const std::string& key, const std::string& json);

        // ----------------------------------------------------------------
        // Routing API
        // ----------------------------------------------------------------

        /**
         * Call any Valhalla API endpoint directly with a pre-built JSON request.
         *
         * The service's profile is injected as "costing" if the body does not
         * already contain that key.
         *
         * @param endpoint  Endpoint name: "route", "trace_attributes",
         *                  "trace_route", "centroid". Building with
         *                  -DVALHALLA_SERVICES=ON adds "matrix", "isochrone",
         *                  "locate", "height", "expansion" and "status";
         *                  without it those throw "Unknown Valhalla endpoint".
         * @param jsonBody  Full Valhalla request JSON string.
         * @return          Raw Valhalla JSON response string.
         */
        std::string callRaw(const std::string& endpoint,
                            const std::string& jsonBody) const;

        std::string parseShape(const std::string& shape);

    private:
        static std::vector<std::string> splitKeys(const std::string& param);

        mutable std::mutex          _mutex;
        std::vector<std::string>    _paths;          // registered MBTiles file paths
        std::string                 _profile;
        std::string                 _configuration;  // Valhalla config as JSON string
    };

} // namespace routing
